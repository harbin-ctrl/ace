#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "dos_devices.h"
#include "ace_fmm_client.h"
#include "ace_modes.h"

#include <blkid/blkid.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_DOS_DEVICES 128
#define DEVICE_NAME_MAX 128
#define DEVICE_TYPE_MAX 64
#define DEVICE_VALUE_MAX 256

struct ace_dos_device {
    bool in_use;
    char kernel_name[DEVICE_NAME_MAX];
    char device_path[PATH_MAX];
    char filesystem_type[DEVICE_TYPE_MAX];
    char uuid[DEVICE_VALUE_MAX];
    char label[DEVICE_VALUE_MAX];
    dev_t device_id;
    char mount_path[PATH_MAX];
    char mount_root[PATH_MAX];
    char view_path[PATH_MAX];
    int mount_method;
};

struct mount_record {
    dev_t device_id;
    char root[PATH_MAX];
    char mount_path[PATH_MAX];
    char filesystem_type[DEVICE_TYPE_MAX];
    char source[PATH_MAX];
};

static struct ace_dos_device devices[MAX_DOS_DEVICES];
static int device_view;
static char device_view_root[PATH_MAX];

static int valid_alias(const char *name)
{
    size_t length;

    if (!name || !*name)
        return 0;
    length = strlen(name);
    if (length >= DEVICE_VALUE_MAX)
        return 0;
    for (size_t index = 0; index < length; index++) {
        unsigned char character = (unsigned char)name[index];
        /* A volume label may contain spaces.  The DOS separator and host
         * path separators may not be part of the alias itself. */
        if (character < 0x20 || character == ':' || character == '/' ||
            character == '\\')
            return 0;
    }
    return 1;
}

static int supported_filesystem_type(const char *type)
{
    return strcasecmp(type, "vfat") == 0 || strcasecmp(type, "ext2") == 0 ||
           strcasecmp(type, "ext3") == 0 || strcasecmp(type, "ext4") == 0;
}

int ace_dos_devices_is_full_root(const char *path)
{
    if (!device_view || !path)
        return 0;
    for (size_t index = 0; index < MAX_DOS_DEVICES; index++)
        if (devices[index].in_use && devices[index].view_path[0] &&
            strcmp(devices[index].view_path, path) == 0)
            return 1;
    return 0;
}

static int decode_mount_field(const char *source, char *result,
                              size_t result_size)
{
    size_t used = 0;

    while (*source) {
        unsigned int value;
        int digits;

        if (source[0] == '\\' && source[1] && source[2] && source[3]) {
            digits = sscanf(source + 1, "%3o", &value);
            if (digits == 1) {
                if (used + 1 >= result_size)
                    return -1;
                result[used++] = (char)value;
                source += 4;
                continue;
            }
        }
        if (used + 1 >= result_size)
            return -1;
        result[used++] = *source++;
    }
    result[used] = '\0';
    return 0;
}

/* Parse one Linux mountinfo record. The first six fields are fixed; the
 * filesystem type and source follow the literal " - " separator. */
static int parse_mount_record(char *line, struct mount_record *record)
{
    char *fields[6] = {0};
    char *cursor = line;
    char *save = NULL;
    char *separator = strstr(line, " - ");
    char *tail;
    char *tail_save = NULL;
    char *filesystem_type;
    char *source;
    unsigned int major_number;
    unsigned int minor_number;
    int field_count = 0;

    if (!separator)
        return -1;
    while (field_count < 6 &&
           (fields[field_count] = strtok_r(cursor, " ", &save))) {
        cursor = NULL;
        field_count++;
    }
    if (field_count < 6 ||
        sscanf(fields[2], "%u:%u", &major_number, &minor_number) != 2 ||
        decode_mount_field(fields[3], record->root, sizeof(record->root)) !=
            0 ||
        decode_mount_field(fields[4], record->mount_path,
                           sizeof(record->mount_path)) != 0)
        return -1;

    /* strtok() replaced the separator's first space with NUL, so use the
     * original offset to find the post-separator fields. */
    tail = line + (separator - line) + 3;
    filesystem_type = strtok_r(tail, " ", &tail_save);
    source = strtok_r(NULL, " ", &tail_save);
    if (!filesystem_type || !source ||
        snprintf(record->filesystem_type, sizeof(record->filesystem_type),
                 "%s", filesystem_type) >=
            (int)sizeof(record->filesystem_type) ||
        decode_mount_field(source, record->source, sizeof(record->source)) !=
            0)
        return -1;
    record->device_id = makedev(major_number, minor_number);
    return 0;
}

/* Find a pre-existing mount of this filesystem.  This is important for the
 * common case where the Linux desktop already mounted the volume: ACE still
 * enters it through the DOS volume root, never by interpreting the host
 * mountpoint as part of the DOS name. */
static int find_existing_mount(dev_t device_id, char *result, size_t result_size)
{
    FILE *stream = fopen("/proc/self/mountinfo", "r");
    char *line = NULL;
    size_t line_size = 0;
    int found = -1;

    if (!stream)
        return -1;
    while (getline(&line, &line_size, stream) >= 0) {
        struct mount_record record;

        if (parse_mount_record(line, &record) != 0 ||
            record.device_id != device_id)
            continue;
        if (snprintf(result, result_size, "%s", record.mount_path) >=
            (int)result_size)
            continue;
        found = 0;
        break;
    }
    free(line);
    fclose(stream);
    return found;
}

static int run_quiet(const char *program, const char *const arguments[])
{
    pid_t child = fork();
    int status;

    if (child < 0)
        return -1;
    if (child == 0) {
        int null_fd = open("/dev/null", O_RDWR);
        if (null_fd >= 0) {
            (void)dup2(null_fd, STDOUT_FILENO);
            (void)dup2(null_fd, STDERR_FILENO);
            if (null_fd > STDERR_FILENO)
                close(null_fd);
        }
        execv(program, (char *const *)arguments);
        _exit(127);
    }
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR)
            return -1;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}

static int mount_root(const struct ace_dos_device *device, char *result,
                      size_t result_size)
{
    const char *runtime_root = getenv("ACE_MOUNT_ROOT");
    const char *xdg_runtime = getenv("XDG_RUNTIME_DIR");
    char private_root[PATH_MAX];
    char parent[PATH_MAX];
    char mountpoint[PATH_MAX];
    struct stat information;
    const char *mount_arguments[6];

    if (!runtime_root || !*runtime_root) {
        if (xdg_runtime && *xdg_runtime) {
            runtime_root = xdg_runtime;
            if (snprintf(private_root, sizeof(private_root), "%s/ace",
                         runtime_root) >= (int)sizeof(private_root)) {
                errno = ENAMETOOLONG;
                return -1;
            }
        } else if (snprintf(private_root, sizeof(private_root), "/tmp/ace-%lu",
                            (unsigned long)getuid()) >=
                   (int)sizeof(private_root)) {
            errno = ENAMETOOLONG;
            return -1;
        }
    } else if (snprintf(private_root, sizeof(private_root), "%s/ace",
                        runtime_root) >= (int)sizeof(private_root)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    if (snprintf(parent, sizeof(parent), "%s/volumes", private_root) >=
            (int)sizeof(parent) ||
        (mkdir(parent, 0700) != 0 && errno != EEXIST)) {
        if ((mkdir(private_root, 0700) != 0 && errno != EEXIST) ||
            (mkdir(parent, 0700) != 0 && errno != EEXIST))
            return -1;
    }
    if (snprintf(mountpoint, sizeof(mountpoint), "%s/%s", parent,
                 device->kernel_name) >= (int)sizeof(mountpoint)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    if (stat(mountpoint, &information) == 0) {
        if (!S_ISDIR(information.st_mode) || rmdir(mountpoint) != 0) {
            errno = EEXIST;
            return -1;
        }
    } else if (errno != ENOENT) {
        return -1;
    }
    if (mkdir(mountpoint, 0700) != 0)
        return -1;

    mount_arguments[0] = "/usr/bin/mount";
    mount_arguments[1] = "-t";
    mount_arguments[2] = device->filesystem_type;
    mount_arguments[3] = device->device_path;
    mount_arguments[4] = mountpoint;
    mount_arguments[5] = NULL;
    if (run_quiet(mount_arguments[0], mount_arguments) == 0) {
        snprintf(result, result_size, "%s", mountpoint);
        return 1;
    }

    /* On a desktop Linux system, udisks is the normal unprivileged mount
     * authority.  It chooses the host mountpoint; that path remains an
     * implementation detail and is never exposed in DOS path resolution. */
    {
        const char *udisks_arguments[] = {
            "/usr/bin/udisksctl", "mount", "-b", device->device_path,
            "--no-user-interaction", NULL
        };
        if (run_quiet(udisks_arguments[0], udisks_arguments) == 0 &&
            find_existing_mount(device->device_id, result, result_size) == 0)
            return 2;
    }
    (void)rmdir(mountpoint);
    return -1;
}

static int ensure_device_mount(struct ace_dos_device *device)
{
    if (device->mount_path[0])
        return 0;
    if (!supported_filesystem_type(device->filesystem_type)) {
        errno = ENOTSUP;
        return -1;
    }
    if (find_existing_mount(device->device_id, device->mount_path,
                            sizeof(device->mount_path)) == 0)
        return 0;
    device->mount_method = mount_root(device, device->mount_path,
                                      sizeof(device->mount_path));
    if (device->mount_method < 0) {
        device->mount_path[0] = '\0';
        errno = EACCES;
        return -1;
    }
    return 0;
}

static int is_non_filesystem_type(const char *type)
{
    static const char *const excluded[] = {
        "swap", "crypto_LUKS", "LVM2_member", "linux_raid_member",
        "zfs_member", "bcache", "ddf_raid_member", "isw_raid_member"
    };

    for (size_t index = 0; index < sizeof(excluded) / sizeof(excluded[0]);
         index++)
        if (strcasecmp(type, excluded[index]) == 0)
            return 1;
    return 0;
}

static int is_partition_device(const char *kernel_name)
{
    char path[PATH_MAX];

    if (snprintf(path, sizeof(path), "/sys/class/block/%s/partition",
                 kernel_name) >= (int)sizeof(path))
        return 0;
    return access(path, F_OK) == 0;
}

static void copy_probe_value(char *destination, size_t destination_size,
                             const char *value, size_t value_length)
{
    if (!value || destination_size == 0) {
        if (destination_size)
            destination[0] = '\0';
        return;
    }
    if (value_length >= destination_size)
        value_length = destination_size - 1;
    memcpy(destination, value, value_length);
    destination[value_length] = '\0';
}

static int device_alias_matches(const struct ace_dos_device *device,
                                const char *name)
{
    return strcasecmp(device->kernel_name, name) == 0 ||
           (device->uuid[0] && strcasecmp(device->uuid, name) == 0) ||
           (device->label[0] && strcasecmp(device->label, name) == 0);
}

static struct ace_dos_device *device_for_id(dev_t device_id)
{
    for (size_t index = 0; index < MAX_DOS_DEVICES; index++)
        if (devices[index].in_use && devices[index].device_id == device_id)
            return &devices[index];
    return NULL;
}

static int device_alias_in_use(const char *name)
{
    for (size_t index = 0; index < MAX_DOS_DEVICES; index++)
        if (devices[index].in_use && device_alias_matches(&devices[index], name))
            return 1;
    return 0;
}

static void synthetic_alias(const char *filesystem_type, char *result,
                            size_t result_size)
{
    size_t used = 0;
    const char *prefix = strcasecmp(filesystem_type, "tmpfs") == 0 ?
                         "RAM" : filesystem_type;

    for (const unsigned char *source = (const unsigned char *)prefix;
         *source && used + 1 < result_size; source++) {
        if (isalnum(*source))
            result[used++] = (char)toupper(*source);
        else if (used == 0 || result[used - 1] != '_')
            result[used++] = '_';
    }
    while (used && result[used - 1] == '_')
        used--;
    if (!used)
        snprintf(result, result_size, "VOLUME");
    else
        result[used] = '\0';
}

static int add_mount_device(const struct mount_record *record)
{
    struct ace_dos_device *device = device_for_id(record->device_id);

    if (!device) {
        char alias[DEVICE_NAME_MAX];
        char base[DEVICE_NAME_MAX];
        size_t suffix = 0;

        for (size_t index = 0; index < MAX_DOS_DEVICES; index++) {
            if (!devices[index].in_use) {
                device = &devices[index];
                break;
            }
        }
        if (!device)
            return -1;
        synthetic_alias(record->filesystem_type, base, sizeof(base));
        snprintf(alias, sizeof(alias), "%s", base);
        while (device_alias_in_use(alias)) {
            suffix++;
            if (snprintf(alias, sizeof(alias), "%s%zu", base, suffix) >=
                (int)sizeof(alias))
                return -1;
        }
        memset(device, 0, sizeof(*device));
        device->in_use = true;
        device->device_id = record->device_id;
        snprintf(device->kernel_name, sizeof(device->kernel_name), "%s", alias);
        snprintf(device->device_path, sizeof(device->device_path), "%s",
                 record->source);
        snprintf(device->filesystem_type, sizeof(device->filesystem_type),
                 "%s", record->filesystem_type);
    }
    if (!device->mount_path[0] ||
        strlen(record->mount_path) < strlen(device->mount_path)) {
        snprintf(device->mount_path, sizeof(device->mount_path), "%s",
                 record->mount_path);
        snprintf(device->mount_root, sizeof(device->mount_root), "%s",
                 record->root);
    }
    return 0;
}

/* Refresh the mount side of the catalog. Block devices are discovered first;
 * this pass then attaches their existing mounts and adds synthetic volumes
 * for filesystems Linux has no /dev block node for, notably tmpfs (RAM:). */
static void discover_mounts(void)
{
    FILE *stream = fopen("/proc/self/mountinfo", "r");
    char *line = NULL;
    size_t line_size = 0;

    if (!stream)
        return;
    while (getline(&line, &line_size, stream) >= 0) {
        struct mount_record record;

        if (parse_mount_record(line, &record) == 0)
            (void)add_mount_device(&record);
    }
    free(line);
    fclose(stream);
}

static int hex_digit(int character)
{
    if (character >= '0' && character <= '9')
        return character - '0';
    if (character >= 'a' && character <= 'f')
        return character - 'a' + 10;
    if (character >= 'A' && character <= 'F')
        return character - 'A' + 10;
    return -1;
}

static int read_udev_value(const char *data_path, const char *key,
                           char *result, size_t result_size)
{
    FILE *stream = fopen(data_path, "r");
    char line[1024];
    char prefix[128];

    if (!stream || snprintf(prefix, sizeof(prefix), "E:%s=", key) >=
                       (int)sizeof(prefix)) {
        if (stream)
            fclose(stream);
        return -1;
    }
    while (fgets(line, sizeof(line), stream)) {
        const char *source;
        size_t used = 0;

        if (strncmp(line, prefix, strlen(prefix)) != 0)
            continue;
        source = line + strlen(prefix);
        while (*source && *source != '\n' && used + 1 < result_size) {
            if (source[0] == '\\' && source[1] == 'x' &&
                source[2] && source[3]) {
                int high = hex_digit(source[2]);
                int low = hex_digit(source[3]);
                if (high >= 0 && low >= 0) {
                    result[used++] = (char)((high << 4) | low);
                    source += 4;
                    continue;
                }
            }
            result[used++] = *source++;
        }
        result[used] = '\0';
        fclose(stream);
        return 0;
    }
    fclose(stream);
    return -1;
}

void ace_dos_devices_discover(void)
{
    DIR *directory;
    struct dirent *entry;

    memset(devices, 0, sizeof(devices));
    directory = opendir("/sys/class/block");
    if (!directory) {
        discover_mounts();
        return;
    }

    while ((entry = readdir(directory)) != NULL) {
        char device_path[PATH_MAX];
        char udev_data_path[PATH_MAX];
        struct stat information;
        blkid_probe probe;
        char filesystem_type[DEVICE_TYPE_MAX] = {0};
        char uuid[DEVICE_VALUE_MAX] = {0};
        char label[DEVICE_VALUE_MAX] = {0};
        const char *type;
        const char *probe_uuid;
        const char *probe_label;
        size_t type_length = 0;
        size_t uuid_length = 0;
        size_t label_length = 0;
        struct ace_dos_device *device = NULL;
        int have_type;

        if (entry->d_name[0] == '.')
            continue;
        if (strlen(entry->d_name) >= DEVICE_NAME_MAX)
            continue;
        if (snprintf(device_path, sizeof(device_path), "/dev/%s",
                     entry->d_name) >= (int)sizeof(device_path) ||
            stat(device_path, &information) != 0 ||
            !S_ISBLK(information.st_mode))
            continue;

        if (snprintf(udev_data_path, sizeof(udev_data_path),
                     "/run/udev/data/b%u:%u", major(information.st_rdev),
                     minor(information.st_rdev)) >= (int)sizeof(udev_data_path))
            continue;
        have_type = read_udev_value(udev_data_path, "ID_FS_TYPE",
                                    filesystem_type,
                                    sizeof(filesystem_type)) == 0;
        if (have_type) {
            (void)read_udev_value(udev_data_path, "ID_FS_UUID", uuid,
                                  sizeof(uuid));
            (void)read_udev_value(udev_data_path, "ID_FS_LABEL", label,
                                  sizeof(label));
        } else {
            probe = blkid_new_probe_from_filename(device_path);
            if (probe) {
                if (blkid_do_safeprobe(probe) == 0 &&
                    blkid_probe_lookup_value(probe, "TYPE", &type,
                                             &type_length) == 0 &&
                    type && type_length != 0) {
                    copy_probe_value(filesystem_type,
                                     sizeof(filesystem_type), type,
                                     type_length);
                    if (blkid_probe_lookup_value(probe, "UUID", &probe_uuid,
                                                  &uuid_length) == 0)
                        copy_probe_value(uuid, sizeof(uuid), probe_uuid,
                                         uuid_length);
                    if (blkid_probe_lookup_value(probe, "LABEL", &probe_label,
                                                  &label_length) == 0)
                        copy_probe_value(label, sizeof(label), probe_label,
                                         label_length);
                }
                blkid_free_probe(probe);
            }
        }
        if ((!filesystem_type[0] && !is_partition_device(entry->d_name)) ||
            (filesystem_type[0] && is_non_filesystem_type(filesystem_type)))
            continue;
        for (size_t index = 0; index < MAX_DOS_DEVICES; index++) {
            if (!devices[index].in_use) {
                device = &devices[index];
                break;
            }
        }
        if (!device) {
            break;
        }
        memset(device, 0, sizeof(*device));
        device->in_use = true;
        device->device_id = information.st_rdev;
        snprintf(device->kernel_name, sizeof(device->kernel_name), "%s",
                 entry->d_name);
        snprintf(device->device_path, sizeof(device->device_path), "%s",
                 device_path);
        snprintf(device->filesystem_type, sizeof(device->filesystem_type),
                 "%s", filesystem_type);
        snprintf(device->uuid, sizeof(device->uuid), "%s", uuid);
        snprintf(device->label, sizeof(device->label), "%s", label);
        if (!valid_alias(device->kernel_name))
            device->in_use = false;
        if (device->label[0] && !valid_alias(device->label))
            device->label[0] = '\0';
    }
    closedir(directory);
    discover_mounts();
}




/*
 * Build the device view by asking, rather than by mounting.
 *
 * Everything privileged here now happens in the fmm, in its own mount
 * namespace, and this function's whole job is to say which devices were found
 * and to record where they were put.  The broker never learns how to mount
 * anything and never needed to.
 *
 * Note what is not passed: a mountpoint.  The broker names a kernel device
 * and what it believes the filesystem to be; the fmm derives the device
 * path, checks that it is really a block device, checks the type against its
 * own list, and chooses where it goes.  There is no parameter through which a
 * confused broker could ask for a mount somewhere of its choosing.
 */
int ace_dos_devices_prepare_device_view(struct ace_privilege_connection *fmm)
{
    if (!fmm) {
        /* Without a fmm there is no privilege anywhere in this session,
           so there is no device view to build.  Reportable, not fatal. */
        errno = EACCES;
        return -1;
    }
    if (ace_fmm_prepare_view(fmm, device_view_root,
                                  sizeof(device_view_root)) != 0)
        return -1;
    device_view = 1;
    for (size_t index = 0; index < MAX_DOS_DEVICES; index++) {
        struct ace_dos_device *device = &devices[index];
        char view_path[PATH_MAX];

        if (!device->in_use || !supported_filesystem_type(
                device->filesystem_type))
            continue;
        if (ace_fmm_mount(fmm, device->kernel_name,
                               device->filesystem_type, view_path,
                               sizeof(view_path)) != 0) {
            /* A filesystem this build will not mount is not a failure of the
               view -- it is a device that does not appear in it.  Anything
               else is a real failure and stops here. */
            if (errno == ENOTSUP)
                continue;
            return -1;
        }
        if (strlen(view_path) >= sizeof(device->view_path)) {
            errno = ENAMETOOLONG;
            return -1;
        }
        strcpy(device->view_path, view_path);
    }
    return 0;
}

/* Where the fmm put the device roots, or "" when there is no device
   view.  The broker needs this to recognise a path that only the access
   worker can open: such a path fails locally with ENOENT, and ENOENT must
   never be what triggers a privileged request. */
const char *ace_dos_devices_view_root(void)
{
    return device_view_root;
}

int ace_dos_devices_lookup(const char *name)
{
    int matches = 0;

    if (!valid_alias(name))
        return 0;
    for (size_t index = 0; index < MAX_DOS_DEVICES; index++) {
        if (devices[index].in_use && device_alias_matches(&devices[index], name))
            matches++;
    }
    return matches > 1 ? -1 : matches;
}

int ace_dos_devices_root(const char *name, char *result, size_t result_size)
{
    struct ace_dos_device *match = NULL;
    const char *selected_root;
    struct stat device_info;

    if (!name || !result || result_size == 0) {
        errno = EINVAL;
        return -1;
    }
    if (!valid_alias(name)) {
        errno = ENOENT;
        return -1;
    }
    for (size_t index = 0; index < MAX_DOS_DEVICES; index++) {
        if (!devices[index].in_use ||
            !device_alias_matches(&devices[index], name))
            continue;
        if (match) {
            errno = EEXIST;
            return -1;
        }
        match = &devices[index];
    }
    if (!match) {
        errno = ENOENT;
        return -1;
    }
    if (device_view) {
        if (stat(match->device_path, &device_info) == 0 &&
            S_ISBLK(device_info.st_mode)) {
            if (!supported_filesystem_type(match->filesystem_type) ||
                !match->view_path[0]) {
                errno = ENOTSUP;
                return -1;
            }
            selected_root = match->view_path;
        } else {
            /* Device view changes only block-backed filesystems for now.
             * RAM: (one device per tmpfs) and the other synthetic devices
             * retain their existing mount-tree roots. */
            if (!match->mount_path[0]) {
                errno = ENOENT;
                return -1;
            }
            selected_root = match->mount_path;
        }
    } else if (ensure_device_mount(match) != 0) {
        return -1;
    } else {
        selected_root = match->mount_path;
    }
    if (snprintf(result, result_size, "%s", selected_root) >=
        (int)result_size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

static int path_is_beneath(const char *path, const char *mount_path)
{
    size_t mount_length = strlen(mount_path);

    if (mount_length == 1 && mount_path[0] == '/')
        return path[0] == '/';
    return strncmp(path, mount_path, mount_length) == 0 &&
           (path[mount_length] == '\0' || path[mount_length] == '/');
}

/* A softlink records its target text, not the path obtained after following
 * it.  Mount selection must therefore work for a dangling target and must
 * not let an intermediate host symlink rewrite the target it reports. */
static int normalize_lexical_absolute_path(const char *path, char *result,
                                           size_t result_size)
{
    char combined[PATH_MAX];
    char *parts[PATH_MAX / 2];
    char *cursor;
    size_t count = 0;
    size_t used = 0;

    if (!path || path[0] != '/' || strlen(path) >= sizeof(combined)) {
        errno = EINVAL;
        return -1;
    }
    strcpy(combined, path);
    cursor = combined;
    while (*cursor) {
        char *slash;

        while (*cursor == '/')
            cursor++;
        if (!*cursor)
            break;
        slash = strchr(cursor, '/');
        if (slash)
            *slash = '\0';
        if (strcmp(cursor, ".") == 0) {
            /* Nothing. */
        } else if (strcmp(cursor, "..") == 0) {
            if (count)
                count--;
        } else if (count < sizeof(parts) / sizeof(parts[0])) {
            parts[count++] = cursor;
        } else {
            errno = ENAMETOOLONG;
            return -1;
        }
        if (!slash)
            break;
        cursor = slash + 1;
    }
    if (result_size < 2) {
        errno = ENAMETOOLONG;
        return -1;
    }
    result[used++] = '/';
    result[used] = '\0';
    for (size_t index = 0; index < count; index++) {
        size_t length = strlen(parts[index]);

        if (used + (used > 1) + length >= result_size) {
            errno = ENAMETOOLONG;
            return -1;
        }
        if (used > 1)
            result[used++] = '/';
        memcpy(result + used, parts[index], length);
        used += length;
        result[used] = '\0';
    }
    return 0;
}

static int find_mount_for_path(const char *path, char *canonical,
                               size_t canonical_size,
                               struct mount_record *best,
                               struct ace_dos_device **device)
{
    FILE *stream;
    char *line = NULL;
    size_t line_size = 0;
    size_t best_length = 0;

    if (normalize_lexical_absolute_path(path, canonical, canonical_size) != 0)
        return -1;
    stream = fopen("/proc/self/mountinfo", "r");
    if (!stream)
        return -1;
    *device = NULL;
    while (getline(&line, &line_size, stream) >= 0) {
        struct mount_record record;
        size_t mount_length;
        struct ace_dos_device *candidate;

        if (parse_mount_record(line, &record) == 0 &&
            path_is_beneath(canonical, record.mount_path)) {
            candidate = device_for_id(record.device_id);
            if (!candidate)
                continue;
            mount_length = strlen(record.mount_path);
            if (mount_length >= best_length) {
                *best = record;
                best_length = mount_length;
                *device = candidate;
            }
        }
    }
    free(line);
    fclose(stream);
    if (!*device) {
        errno = ENODEV;
        return -1;
    }
    return 0;
}

static int append_relative_path(const struct mount_record *mount,
                                const char *host_path, char *result,
                                size_t result_size)
{
    const char *suffix = host_path + strlen(mount->mount_path);
    const char *root = mount->root;
    size_t used;

    while (*suffix == '/')
        suffix++;
    if (strcmp(root, "/") == 0)
        root++;
    else
        while (*root == '/')
            root++;

    used = 0;
    if (snprintf(result + used, result_size - used, "%s%s%s",
                 root, *root && *suffix ? "/" : "", suffix) >=
        (int)(result_size - used)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

int ace_dos_devices_name_from_path(const char *path, char *result,
                                   size_t result_size)
{
    char canonical[PATH_MAX];
    struct mount_record best = {0};
    struct ace_dos_device *device = NULL;
    char alias[DEVICE_VALUE_MAX];

    if (!path || !result || result_size == 0) {
        errno = EINVAL;
        return -1;
    }
    if (find_mount_for_path(path, canonical, sizeof(canonical), &best,
                            &device) != 0)
        return -1;

    if (device->label[0] && ace_dos_devices_lookup(device->label) == 1)
        snprintf(alias, sizeof(alias), "%s", device->label);
    else if (ace_dos_devices_lookup(device->kernel_name) == 1)
        snprintf(alias, sizeof(alias), "%s", device->kernel_name);
    else if (device->uuid[0] && ace_dos_devices_lookup(device->uuid) == 1)
        snprintf(alias, sizeof(alias), "%s", device->uuid);
    else {
        errno = EEXIST;
        return -1;
    }

    if (snprintf(result, result_size, "%s:", alias) >= (int)result_size)
        return -1;
    if (append_relative_path(&best, canonical, result + strlen(result),
                             result_size - strlen(result)) != 0)
        return -1;
    return 0;
}

int ace_dos_devices_volume_root_for_path(const char *path, char *result,
                                         size_t result_size)
{
    char canonical[PATH_MAX];
    struct mount_record mount;
    struct ace_dos_device *device;

    if (!result || result_size == 0) {
        errno = EINVAL;
        return -1;
    }
    if (find_mount_for_path(path, canonical, sizeof(canonical), &mount,
                            &device) != 0)
        return -1;
    (void)device;
    if (snprintf(result, result_size, "%s", mount.mount_path) >=
        (int)result_size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

void ace_dos_devices_shutdown(void)
{
    /* The device-view mounts are not this process's to take down: they live
     * in the fmm's namespace and go when it does, whether that is an
     * orderly shutdown or the kernel discarding the namespace after the last
     * process in it exits.  Only the mounts this broker made itself, through
     * the ordinary unprivileged path below, are its own to undo. */
    for (size_t index = MAX_DOS_DEVICES; index-- > 0;)
        devices[index].view_path[0] = '\0';
    for (size_t index = 0; index < MAX_DOS_DEVICES; index++) {
        struct ace_dos_device *device = &devices[index];

        if (!device->in_use || device->mount_method == 0)
            continue;
        if (device->mount_method == 1) {
            const char *arguments[] = {
                "/usr/bin/umount", device->mount_path, NULL
            };
            (void)run_quiet(arguments[0], arguments);
        } else {
            const char *arguments[] = {
                "/usr/bin/udisksctl", "unmount", "-b", device->device_path,
                "--no-user-interaction", NULL
            };
            (void)run_quiet(arguments[0], arguments);
        }
        device->mount_path[0] = '\0';
        device->mount_method = 0;
    }
}

int ace_dos_devices_list(char *result, size_t result_size)
{
    size_t used = 0;

    if (!result || result_size == 0) {
        errno = EINVAL;
        return -1;
    }
    result[0] = '\0';
    for (size_t index = 0; index < MAX_DOS_DEVICES; index++) {
        const struct ace_dos_device *device = &devices[index];
        int written;

        if (!device->in_use)
            continue;
        written = snprintf(result + used, result_size - used,
                           "%s\t%s\t%s\t%s\t%s\n", device->kernel_name,
                           device->filesystem_type, device->uuid,
                           device->label, device->device_path);
        if (written < 0 || (size_t)written >= result_size - used) {
            errno = ENOSPC;
            return -1;
        }
        used += (size_t)written;
    }
    return 0;
}

static const char *label_program(const char *filesystem_type)
{
    if (strcasecmp(filesystem_type, "ext2") == 0 ||
        strcasecmp(filesystem_type, "ext3") == 0 ||
        strcasecmp(filesystem_type, "ext4") == 0)
        return "/usr/sbin/e2label";
    if (strcasecmp(filesystem_type, "vfat") == 0 ||
        strcasecmp(filesystem_type, "fat") == 0 ||
        strcasecmp(filesystem_type, "msdos") == 0)
        return "/usr/sbin/fatlabel";
    return NULL;
}

int ace_dos_devices_relabel(const char *name, const char *label)
{
    char alias[DEVICE_VALUE_MAX];
    struct ace_dos_device *match = NULL;
    const char *program;
    size_t length;
    const char *arguments[4];

    if (!name || !label) {
        errno = EINVAL;
        return -1;
    }
    length = strlen(name);
    if (length && name[length - 1] == ':')
        length--;
    if (!length || length >= sizeof(alias)) {
        errno = ENOENT;
        return -1;
    }
    memcpy(alias, name, length);
    alias[length] = '\0';
    if (!valid_alias(alias) || !valid_alias(label)) {
        errno = EINVAL;
        return -1;
    }
    for (size_t index = 0; index < MAX_DOS_DEVICES; index++) {
        if (!devices[index].in_use ||
            !device_alias_matches(&devices[index], alias))
            continue;
        if (match) {
            errno = EEXIST;
            return -1;
        }
        match = &devices[index];
    }
    if (!match) {
        errno = ENOENT;
        return -1;
    }
    program = label_program(match->filesystem_type);
    if (strcasecmp(match->filesystem_type, "tmpfs") == 0) {
        /* tmpfs has no on-disk volume label. Keep the AmigaDOS operation
         * useful for ACE's synthetic RAM: volumes by changing their live
         * broker alias for the lifetime of this broker. */
        snprintf(match->label, sizeof(match->label), "%s", label);
        return 0;
    }
    if (!program || !match->device_path[0]) {
        errno = ENOTSUP;
        return -1;
    }
    arguments[0] = program;
    arguments[1] = match->device_path;
    arguments[2] = label;
    arguments[3] = NULL;
    if (run_quiet(program, arguments) != 0) {
        errno = EIO;
        return -1;
    }
    snprintf(match->label, sizeof(match->label), "%s", label);
    return 0;
}
