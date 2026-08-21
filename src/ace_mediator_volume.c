/*
 * The mediator's mount personality.
 *
 * Everything here runs as root, so the order of operations in each function
 * is part of the design rather than a matter of taste: a name is validated
 * before it is used to build a path, a path is built by this process rather
 * than accepted from the far side, and a device is confirmed to be a block
 * device before anything is asked of the kernel about it.
 *
 * The mountinfo parsing below deliberately does not share code with
 * dos_devices.c.  That file is full of Amiga alias and name-translation
 * semantics; this one runs with the power to mount filesystems.  The
 * privileged parser should be small enough to read in one sitting and should
 * not change because a naming rule changed.
 */

#define _GNU_SOURCE

#include "ace_mediator_volume.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>

#define VOLUME_MAX_DEVICES 128
#define VOLUME_NAME_MAX 64
#define VOLUME_TYPE_MAX 32

struct volume_entry {
    int in_use;
    char kernel_name[VOLUME_NAME_MAX];
    char view_path[PATH_MAX];
    dev_t device_id;
};

static struct volume_entry volumes[VOLUME_MAX_DEVICES];
static char view_root[PATH_MAX];
static int namespace_ready;

/*
 * The filesystems ACE will mount, and the ones it will not.
 *
 * This list lives here rather than being taken from the request, because a
 * filesystem type is a choice of which kernel driver parses untrusted bytes
 * from a disk.  The broker may say what it found; it may not say what the
 * mediator is willing to hand to the kernel.
 *
 * btrfs is absent on purpose and not by omission: a btrfs mount whose root is
 * a subvolume cannot presently be turned back into the true device root, and
 * a device view that quietly showed a subvolume as a volume would be showing
 * something that is not the disk.
 */
static int supported_filesystem(const char *type)
{
    return strcasecmp(type, "vfat") == 0 || strcasecmp(type, "ext2") == 0 ||
           strcasecmp(type, "ext3") == 0 || strcasecmp(type, "ext4") == 0;
}

/*
 * Filesystems with no owners of their own.
 *
 * FAT and its relatives do not record a uid anywhere; the kernel invents one
 * at mount time from the uid= option.  Mounting them as the ACE user is what
 * makes the Amiga model exactly true rather than approximately true -- an
 * ownerless filesystem presented to a single person who owns all of it is
 * precisely what a floppy was, and it means a Copy onto a stick never
 * produces a root-owned file on the user's own media.
 *
 * ext2/3/4 do record ownership, and there ACE shows what is really there.
 */
static int ownerless_filesystem(const char *type)
{
    return strcasecmp(type, "vfat") == 0;
}

/*
 * A kernel device name, and nothing else.
 *
 * Letters, digits, dash and underscore covers every real one -- sda1,
 * nvme0n1p2, mmcblk0p1, dm-0.  A dot is refused outright rather than filtered
 * for ".." specifically, because the only reason a dot would appear in this
 * field is an attempt to make it mean something other than a device name.
 */
static int valid_kernel_name(const char *name)
{
    size_t length;

    if (!name || !*name)
        return 0;
    length = strlen(name);
    if (length >= VOLUME_NAME_MAX)
        return 0;
    for (size_t index = 0; index < length; index++) {
        unsigned char character = (unsigned char)name[index];

        if (!isalnum(character) && character != '-' && character != '_')
            return 0;
    }
    return 1;
}

static int valid_type_name(const char *type)
{
    size_t length;

    if (!type || !*type)
        return 0;
    length = strlen(type);
    if (length >= VOLUME_TYPE_MAX)
        return 0;
    for (size_t index = 0; index < length; index++)
        if (!isalnum((unsigned char)type[index]))
            return 0;
    return 1;
}

/*
 * Split "name\0type\0" out of a request payload.
 *
 * Bounded by the length the header declared, not by looking for a NUL and
 * hoping there is one: a payload without its terminators must fail here, not
 * in whatever reads past the end of it.
 */
static int split_payload(const void *payload, size_t length,
                         const char **name, const char **type)
{
    const char *bytes = payload;
    size_t first;

    if (!bytes || length < 2 || bytes[length - 1] != '\0')
        return -1;
    first = strnlen(bytes, length);
    if (first == length)
        return -1;
    *name = bytes;
    *type = bytes + first + 1;
    if (*(*type) == '\0' && first + 1 >= length - 1)
        return -1;
    return 0;
}

static int make_directory_path(const char *path)
{
    char work[PATH_MAX];

    if (!path || strlen(path) >= sizeof(work)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(work, path);
    for (char *cursor = work + 1; *cursor; cursor++) {
        struct stat information;

        if (*cursor != '/')
            continue;
        *cursor = '\0';
        if (mkdir(work, 0700) != 0 && errno != EEXIST)
            return -1;
        if (stat(work, &information) != 0 || !S_ISDIR(information.st_mode)) {
            errno = ENOTDIR;
            return -1;
        }
        *cursor = '/';
    }
    if (mkdir(work, 0700) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

/* One mountinfo record, reduced to the four fields this side cares about. */
struct mount_line {
    dev_t device_id;
    char root[PATH_MAX];
    char mount_path[PATH_MAX];
};

static int decode_mount_field(const char *source, char *result,
                              size_t result_size)
{
    size_t out = 0;

    while (*source && out + 1 < result_size) {
        if (source[0] == '\\' && isdigit((unsigned char)source[1]) &&
            isdigit((unsigned char)source[2]) &&
            isdigit((unsigned char)source[3])) {
            result[out++] = (char)((source[1] - '0') * 64 +
                                   (source[2] - '0') * 8 + (source[3] - '0'));
            source += 4;
            continue;
        }
        result[out++] = *source++;
    }
    if (*source)
        return -1;
    result[out] = '\0';
    return 0;
}

static int parse_mount_line(char *line, struct mount_line *record)
{
    char *fields[6];
    unsigned major_number;
    unsigned minor_number;
    char *cursor = line;

    for (size_t index = 0; index < 6; index++) {
        fields[index] = strsep(&cursor, " ");
        if (!fields[index])
            return -1;
    }
    if (sscanf(fields[2], "%u:%u", &major_number, &minor_number) != 2)
        return -1;
    if (decode_mount_field(fields[3], record->root, sizeof(record->root)) != 0 ||
        decode_mount_field(fields[4], record->mount_path,
                           sizeof(record->mount_path)) != 0)
        return -1;
    record->device_id = makedev(major_number, minor_number);
    return 0;
}

/* Find where this device is already mounted with its true root, if anywhere.
   A mount whose root is not "/" is a subroot -- a btrfs subvolume, typically
   -- and is not the device, so it does not count as one. */
static int existing_device_root(dev_t device_id, char *result,
                                size_t result_size)
{
    FILE *stream = fopen("/proc/self/mountinfo", "r");
    char *line = NULL;
    size_t line_size = 0;
    int found = -1;

    if (!stream)
        return -1;
    while (getline(&line, &line_size, stream) >= 0) {
        struct mount_line record;
        char *newline = strchr(line, '\n');

        if (newline)
            *newline = '\0';
        if (parse_mount_line(line, &record) != 0 ||
            record.device_id != device_id || strcmp(record.root, "/") != 0)
            continue;
        if (snprintf(result, result_size, "%s", record.mount_path) <
            (int)result_size)
            found = 0;
        break;
    }
    free(line);
    fclose(stream);
    return found;
}

static int path_is_mountpoint(const char *path)
{
    FILE *stream = fopen("/proc/self/mountinfo", "r");
    char *line = NULL;
    size_t line_size = 0;
    int found = 0;

    if (!stream)
        return 0;
    while (getline(&line, &line_size, stream) >= 0) {
        struct mount_line record;
        char *newline = strchr(line, '\n');

        if (newline)
            *newline = '\0';
        if (parse_mount_line(line, &record) == 0 &&
            strcmp(record.mount_path, path) == 0) {
            found = 1;
            break;
        }
    }
    free(line);
    fclose(stream);
    return found;
}

/*
 * Where the device roots hang.
 *
 * Inside the mediator's own namespace, so the name is not shared with
 * anything and does not have to avoid colliding with the rest of the system.
 * It is reported back to the broker rather than agreed in advance, because
 * the side that created it is the side that knows.
 */
static int ensure_view_root(uid_t served_uid)
{
    /*
     * ACE_MOUNT_ROOT relocates the user's own mount tree, which is a choice
     * about tidiness rather than about privilege: the mediator still derives
     * every device path itself, still validates the device, and still names
     * the leaf.  pkexec scrubs the environment, so a production mediator
     * never sees this and always uses /run -- it is reachable only by a
     * mediator launched directly, which is to say by a test.
     */
    const char *configured = getenv("ACE_MOUNT_ROOT");

    if (view_root[0])
        return 0;
    if (configured && *configured) {
        if (snprintf(view_root, sizeof(view_root), "%s/device-roots",
                     configured) >= (int)sizeof(view_root)) {
            view_root[0] = '\0';
            errno = ENAMETOOLONG;
            return -1;
        }
        if (make_directory_path(view_root) != 0) {
            view_root[0] = '\0';
            return -1;
        }
        return 0;
    }
    if (snprintf(view_root, sizeof(view_root), "/run/ace-%lu/device-roots",
                 (unsigned long)served_uid) >= (int)sizeof(view_root)) {
        view_root[0] = '\0';
        errno = ENAMETOOLONG;
        return -1;
    }
    if (make_directory_path(view_root) != 0) {
        view_root[0] = '\0';
        return -1;
    }
    return 0;
}

const char *ace_mediator_volume_view_root(void)
{
    return view_root;
}

int namespace_is_ready(void)
{
    return namespace_ready;
}

static struct volume_entry *entry_for(const char *kernel_name)
{
    for (size_t index = 0; index < VOLUME_MAX_DEVICES; index++)
        if (volumes[index].in_use &&
            strcmp(volumes[index].kernel_name, kernel_name) == 0)
            return &volumes[index];
    return NULL;
}

static struct volume_entry *free_entry(void)
{
    for (size_t index = 0; index < VOLUME_MAX_DEVICES; index++)
        if (!volumes[index].in_use)
            return &volumes[index];
    return NULL;
}

/*
 * Create the private mount namespace.
 *
 * MS_REC|MS_PRIVATE on / is what stops anything mounted here propagating back
 * out to the rest of the system: ACE's view of the disks is ACE's, and a
 * device the user asked ACE to show should not appear under the desktop's
 * file manager as a side effect.
 */
static int init_namespace(int *host_errno)
{
    if (namespace_ready)
        return ACE_MEDIATOR_OK;
    if (unshare(CLONE_NEWNS) != 0 ||
        mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) != 0) {
        *host_errno = errno;
        return ACE_MEDIATOR_HOST_ERROR;
    }
    namespace_ready = 1;
    return ACE_MEDIATOR_OK;
}

static int do_mount(const struct ace_mediator_request *request,
                    const void *payload, uid_t served_uid, char *reply,
                    size_t reply_size, size_t *reply_length, int *host_errno)
{
    const char *kernel_name;
    const char *filesystem_type;
    struct volume_entry *entry;
    struct stat information;
    char device_path[PATH_MAX];
    char target[PATH_MAX];
    char source_mount[PATH_MAX];
    char options[64];
    int written;

    if (split_payload(payload, request->payload_length, &kernel_name,
                      &filesystem_type) != 0)
        return ACE_MEDIATOR_PROTOCOL_ERROR;
    /* Name first, before it is used to build anything. */
    if (!valid_kernel_name(kernel_name) || !valid_type_name(filesystem_type))
        return ACE_MEDIATOR_ESCAPED;
    if (!supported_filesystem(filesystem_type))
        return ACE_MEDIATOR_UNSUPPORTED;
    if (!namespace_ready)
        return ACE_MEDIATOR_REFUSED;

    /* The device path is derived here and never accepted from the far side.
       That is the whole reason a malformed request cannot ask this process to
       mount something of its choosing. */
    if (snprintf(device_path, sizeof(device_path), "/dev/%s", kernel_name) >=
        (int)sizeof(device_path))
        return ACE_MEDIATOR_ESCAPED;
    if (stat(device_path, &information) != 0) {
        *host_errno = errno;
        return ACE_MEDIATOR_HOST_ERROR;
    }
    if (!S_ISBLK(information.st_mode)) {
        *host_errno = ENOTBLK;
        return ACE_MEDIATOR_ESCAPED;
    }

    if (ensure_view_root(served_uid) != 0) {
        *host_errno = errno;
        return ACE_MEDIATOR_HOST_ERROR;
    }
    if (snprintf(target, sizeof(target), "%s/%s", view_root, kernel_name) >=
        (int)sizeof(target))
        return ACE_MEDIATOR_ESCAPED;

    entry = entry_for(kernel_name);
    if (entry && entry->view_path[0]) {
        /* Already ours.  Idempotent, so that a broker retrying after a lost
           reply does not stack a second mount on the same directory. */
        written = snprintf(reply, reply_size, "%s", entry->view_path);
        if (written < 0 || (size_t)written >= reply_size)
            return ACE_MEDIATOR_PROTOCOL_ERROR;
        *reply_length = (size_t)written + 1;
        return ACE_MEDIATOR_OK;
    }
    if (make_directory_path(target) != 0) {
        *host_errno = errno;
        return ACE_MEDIATOR_HOST_ERROR;
    }
    if (path_is_mountpoint(target)) {
        *host_errno = EBUSY;
        return ACE_MEDIATOR_HOST_ERROR;
    }

    options[0] = '\0';
    if (ownerless_filesystem(filesystem_type)) {
        struct passwd *account = getpwuid(served_uid);

        /* uid= and gid= are what make an ownerless filesystem the user's.
           If the account cannot be looked up, uid alone still does the job
           that matters. */
        if (account)
            snprintf(options, sizeof(options), "uid=%lu,gid=%lu",
                     (unsigned long)served_uid,
                     (unsigned long)account->pw_gid);
        else
            snprintf(options, sizeof(options), "uid=%lu",
                     (unsigned long)served_uid);
    }

    if (existing_device_root(information.st_rdev, source_mount,
                             sizeof(source_mount)) == 0) {
        /*
         * The filesystem is already mounted somewhere the system chose.
         * Bind it rather than mounting it twice.
         *
         * Deliberately not MS_REC: leaving the source's child mounts behind
         * is exactly what reveals the directories they were obscuring, which
         * is the point of a device view.
         */
        if (mount(source_mount, target, NULL, MS_BIND, NULL) != 0) {
            *host_errno = errno;
            return ACE_MEDIATOR_HOST_ERROR;
        }
    } else if (mount(device_path, target, filesystem_type, 0,
                     options[0] ? options : NULL) != 0) {
        *host_errno = errno;
        return ACE_MEDIATOR_HOST_ERROR;
    }

    entry = entry ? entry : free_entry();
    if (!entry) {
        (void)umount2(target, MNT_DETACH);
        *host_errno = ENOSPC;
        return ACE_MEDIATOR_HOST_ERROR;
    }
    entry->in_use = 1;
    entry->device_id = information.st_rdev;
    snprintf(entry->kernel_name, sizeof(entry->kernel_name), "%s", kernel_name);
    snprintf(entry->view_path, sizeof(entry->view_path), "%s", target);

    written = snprintf(reply, reply_size, "%s", entry->view_path);
    if (written < 0 || (size_t)written >= reply_size)
        return ACE_MEDIATOR_PROTOCOL_ERROR;
    *reply_length = (size_t)written + 1;
    return ACE_MEDIATOR_OK;
}

static int do_unmount(const struct ace_mediator_request *request,
                      const void *payload, int *host_errno)
{
    const char *bytes = payload;
    struct volume_entry *entry;

    if (!bytes || request->payload_length == 0 ||
        bytes[request->payload_length - 1] != '\0')
        return ACE_MEDIATOR_PROTOCOL_ERROR;
    if (!valid_kernel_name(bytes))
        return ACE_MEDIATOR_ESCAPED;
    /* Named by the device this worker already knows about, never by a path.
       A path would have to be resolved here a second time, and a check and a
       use that resolve separately are a check and a use that can disagree. */
    entry = entry_for(bytes);
    if (!entry || !entry->view_path[0]) {
        *host_errno = ENOENT;
        return ACE_MEDIATOR_HOST_ERROR;
    }
    if (umount2(entry->view_path, MNT_DETACH) != 0) {
        *host_errno = errno;
        return ACE_MEDIATOR_HOST_ERROR;
    }
    entry->view_path[0] = '\0';
    entry->in_use = 0;
    return ACE_MEDIATOR_OK;
}

static int do_list(char *reply, size_t reply_size, size_t *reply_length)
{
    size_t used = 0;

    for (size_t index = 0; index < VOLUME_MAX_DEVICES; index++) {
        struct volume_entry *entry = &volumes[index];
        int written;

        if (!entry->in_use || !entry->view_path[0])
            continue;
        written = snprintf(reply + used, reply_size - used, "%s\t%s\n",
                           entry->kernel_name, entry->view_path);
        if (written < 0 || (size_t)written >= reply_size - used)
            return ACE_MEDIATOR_PROTOCOL_ERROR;
        used += (size_t)written;
    }
    *reply_length = used;
    return ACE_MEDIATOR_OK;
}

int ace_mediator_volume_dispatch(const struct ace_mediator_request *request,
                                 const void *payload, uid_t served_uid,
                                 char *reply, size_t reply_size,
                                 size_t *reply_length, int *host_errno)
{
    *reply_length = 0;
    *host_errno = 0;

    switch (request->operation) {
    case ACE_MEDIATOR_VOLUME_INIT_NAMESPACE:
        return init_namespace(host_errno);
    case ACE_MEDIATOR_VOLUME_PREPARE_VIEW:
        /* The namespace and the view root, without mounting anything.  The
           broker drives the individual mounts, one typed request each, so
           that every device that becomes visible did so because it was named
           -- not because a bulk operation swept something in. */
        if (init_namespace(host_errno) != ACE_MEDIATOR_OK)
            return ACE_MEDIATOR_HOST_ERROR;
        if (ensure_view_root(served_uid) != 0) {
            *host_errno = errno;
            return ACE_MEDIATOR_HOST_ERROR;
        }
        *reply_length = strlen(view_root) + 1;
        if (*reply_length > reply_size) {
            *reply_length = 0;
            return ACE_MEDIATOR_PROTOCOL_ERROR;
        }
        memcpy(reply, view_root, *reply_length);
        return ACE_MEDIATOR_OK;
    case ACE_MEDIATOR_VOLUME_MOUNT:
        return do_mount(request, payload, served_uid, reply, reply_size,
                        reply_length, host_errno);
    case ACE_MEDIATOR_VOLUME_UNMOUNT:
        return do_unmount(request, payload, host_errno);
    case ACE_MEDIATOR_VOLUME_LIST:
        return do_list(reply, reply_size, reply_length);
    default:
        break;
    }
    return ACE_MEDIATOR_REFUSED;
}

void ace_mediator_volume_shutdown(void)
{
    /*
     * Reverse order, so a mount that sits under another comes off first.
     *
     * MNT_DETACH because something may still be standing in one of these
     * directories -- a shell that joined the namespace and has not noticed
     * yet -- and a lazy unmount lets the kernel finish when they leave.  If
     * this process is killed outright the kernel discards the whole namespace
     * when its last member exits, which is the same outcome by a shorter
     * route.
     */
    for (size_t index = VOLUME_MAX_DEVICES; index-- > 0;) {
        struct volume_entry *entry = &volumes[index];

        if (!entry->in_use || !entry->view_path[0])
            continue;
        (void)umount2(entry->view_path, MNT_DETACH);
        entry->view_path[0] = '\0';
        entry->in_use = 0;
    }
}
