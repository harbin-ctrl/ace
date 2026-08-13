#define _POSIX_C_SOURCE 200809L

#include "dos_devices.h"

#include <blkid/blkid.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>

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
        if (!(isalnum(character) || character == '_' || character == '-' ||
              character == '.'))
            return 0;
    }
    return 1;
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
