#define _POSIX_C_SOURCE 200809L

#include "dos_devices.h"

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
    int mount_method;
};

static struct ace_dos_device devices[MAX_DOS_DEVICES];

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
        if (character < 0x21 || character == ':' || character == '/' ||
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
        char *fields[6] = {0};
        char *cursor = line;
        char *save = NULL;
        unsigned int major_number;
        unsigned int minor_number;
        char mountpoint[PATH_MAX];
        int field_count = 0;

        while (field_count < 6 &&
               (fields[field_count] = strtok_r(cursor, " ", &save))) {
            cursor = NULL;
            field_count++;
        }
        if (field_count < 5 ||
            sscanf(fields[2], "%u:%u", &major_number, &minor_number) != 2 ||
            makedev(major_number, minor_number) != device_id)
            continue;
        if (decode_mount_field(fields[4], mountpoint, sizeof(mountpoint)) != 0)
            continue;
        if (snprintf(result, result_size, "%s", mountpoint) >=
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
    if (!directory)
        return;

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
            if (!probe)
                continue;
            if (blkid_do_safeprobe(probe) != 0 ||
                blkid_probe_lookup_value(probe, "TYPE", &type,
                                         &type_length) != 0 ||
                !type || type_length == 0) {
                blkid_free_probe(probe);
                continue;
            }
            copy_probe_value(filesystem_type, sizeof(filesystem_type), type,
                             type_length);
            if (blkid_probe_lookup_value(probe, "UUID", &probe_uuid,
                                         &uuid_length) == 0)
                copy_probe_value(uuid, sizeof(uuid), probe_uuid, uuid_length);
            if (blkid_probe_lookup_value(probe, "LABEL", &probe_label,
                                         &label_length) == 0)
                copy_probe_value(label, sizeof(label), probe_label,
                                 label_length);
            blkid_free_probe(probe);
        }
        if (!filesystem_type[0] || is_non_filesystem_type(filesystem_type))
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
    if (ensure_device_mount(match) != 0)
        return -1;
    if (snprintf(result, result_size, "%s", match->mount_path) >=
        (int)result_size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

void ace_dos_devices_shutdown(void)
{
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
