#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <dos/dos.h>

#include "broker_client.h"
#include "broker_protocol.h"

enum {
    ARG_DISKS,
    ARG_VOLUMES,
    ARG_ALL,
    ARG_BLOCKS,
    ARG_DEVICES,
    ARG_COUNT
};

struct device_record {
    char kernel[128];
    char filesystem[64];
    char uuid[256];
    char label[256];
    char host_path[4096];
};

static int copy_field(char *destination, size_t destination_size,
                      const char *source)
{
    if (!source || snprintf(destination, destination_size, "%s", source) >=
                    (int)destination_size)
        return -1;
    return 0;
}

static int parse_record(char *line, struct device_record *record)
{
    char *fields[5] = {0};
    char *save = NULL;
    char *field;
    int count = 0;

    for (field = strtok_r(line, "\t", &save);
         field && count < 5;
         field = strtok_r(NULL, "\t", &save))
        fields[count++] = field;
    if (count < 5)
        return -1;
    memset(record, 0, sizeof(*record));
    return copy_field(record->kernel, sizeof(record->kernel), fields[0]) ||
           copy_field(record->filesystem, sizeof(record->filesystem),
                      fields[1]) ||
           copy_field(record->uuid, sizeof(record->uuid), fields[2]) ||
           copy_field(record->label, sizeof(record->label), fields[3]) ||
           copy_field(record->host_path, sizeof(record->host_path), fields[4])
           ? -1 : 0;
}

static int same_name(const char *left, const char *right)
{
    size_t left_length = strlen(left);
    size_t right_length = strlen(right);

    if (left_length && left[left_length - 1] == ':')
        left_length--;
    if (right_length && right[right_length - 1] == ':')
        right_length--;
    return left_length == right_length &&
           strncasecmp(left, right, left_length) == 0;
}

static int selected(const struct device_record *record, char **filters)
{
    if (!filters || !*filters)
        return 1;
    for (; *filters; filters++) {
        if (same_name(*filters, record->kernel) ||
            (record->label[0] && same_name(*filters, record->label)) ||
            (record->uuid[0] && same_name(*filters, record->uuid)))
            return 1;
    }
    return 0;
}

static void display_size(uint64_t bytes, char *result, size_t result_size)
{
    const char *suffix = "B";
    double value = (double)bytes;

    if (value >= 1024.0) {
        value /= 1024.0;
        suffix = "K";
    }
    if (value >= 1024.0) {
        value /= 1024.0;
        suffix = "M";
    }
    if (value >= 1024.0) {
        value /= 1024.0;
        suffix = "G";
    }
    if (value >= 1024.0) {
        value /= 1024.0;
        suffix = "T";
    }
    if (suffix[0] == 'B')
        snprintf(result, result_size, "%.0f%s", value, suffix);
    else
        snprintf(result, result_size, "%.1f%s", value, suffix);
}

static uint64_t blocks_to_bytes(uint64_t blocks, uint64_t block_size)
{
    return blocks > UINT64_MAX / block_size ? UINT64_MAX : blocks * block_size;
}

static int print_record(const struct device_record *record, int blocks)
{
    char volume[sizeof(record->label) + 2];
    char size[32];
    char used[32];
    char free_space[32];
    BPTR lock;
    struct InfoData64 info;
    uint64_t block_size;
    uint64_t total_bytes;
    uint64_t used_bytes;
    uint64_t free_bytes;
    uint64_t full = 0;

    snprintf(volume, sizeof(volume), "%s:",
             record->label[0] ? record->label : record->kernel);
    lock = Lock(volume, SHARED_LOCK);
    if (!lock || Info64(lock, &info) != DOSTRUE) {
        if (lock)
            UnLock(lock);
        return -1;
    }
    block_size = info.id_BytesPerBlock > 0 ?
                 (uint64_t)info.id_BytesPerBlock : 1;
    total_bytes = blocks_to_bytes(info.id_NumBlocks, block_size);
    used_bytes = info.id_NumBlocksUsed > info.id_NumBlocks ?
                 total_bytes : blocks_to_bytes(info.id_NumBlocksUsed,
                                               block_size);
    free_bytes = total_bytes >= used_bytes ? total_bytes - used_bytes : 0;
    if (info.id_NumBlocks)
        full = info.id_NumBlocksUsed >= info.id_NumBlocks ? 100 :
               (uint64_t)(((__uint128_t)info.id_NumBlocksUsed * 100) /
                          info.id_NumBlocks);
    display_size(total_bytes, size, sizeof(size));
    display_size(used_bytes, used, sizeof(used));
    display_size(free_bytes, free_space, sizeof(free_space));
    printf("%-12s %8s %8s %8s %3" PRIu64 "%% %4ld %-11s %-8s %s\n",
           record->kernel, size, used, free_space, full,
           (long)info.id_NumSoftErrors,
           info.id_DiskState == ID_WRITE_PROTECTED ? "read only" :
           info.id_DiskState == ID_VALIDATING ? "validating" : "read/write",
           record->filesystem[0] ? record->filesystem : "unknown",
           record->label[0] ? record->label : record->kernel);
    if (blocks)
        printf("  Total blocks: %" PRIu64 "  Blocks used: %" PRIu64
               "  Blocks free: %" PRIu64 "  Blocksize: %ld\n",
               info.id_NumBlocks, info.id_NumBlocksUsed,
               info.id_NumBlocks - info.id_NumBlocksUsed,
               (long)info.id_BytesPerBlock);
    UnLock(lock);
    return 0;
}

int main(void)
{
    IPTR args[ARG_COUNT] = {0};
    struct RDArgs *rdargs;
    char serialized[AMIGA_BROKER_MAX_PAYLOAD];
    char volume_serialized[AMIGA_BROKER_MAX_PAYLOAD];
    char *line;
    char *save = NULL;
    char **filters;
    int show_disks;
    int show_volumes;
    int blocks;
    int printed = 0;

    rdargs = ReadArgs("DISKS/S,VOLS=VOLUMES/S,ALL/S,BLOCKS/S,DEVICES/M",
                      args, NULL);
    if (!rdargs) {
        PrintFault(IoErr(), "Info");
        return RETURN_FAIL;
    }
    show_disks = args[ARG_DISKS] != 0;
    show_volumes = args[ARG_VOLUMES] != 0;
    blocks = args[ARG_BLOCKS] != 0;
    filters = (char **)args[ARG_DEVICES];
    if (!show_disks && !show_volumes)
        show_disks = show_volumes = 1;
    if (native_broker_listdos(serialized, sizeof(serialized)) != 0) {
        PrintFault(IoErr(), "Info");
        FreeArgs(rdargs);
        return RETURN_FAIL;
    }
    memcpy(volume_serialized, serialized, sizeof(volume_serialized));

    if (show_disks)
        printf("Unit         Size     Used     Free Full Errs State       Type     Name\n");
    for (line = strtok_r(serialized, "\n", &save);
         line;
         line = strtok_r(NULL, "\n", &save)) {
        struct device_record record;

        if (parse_record(line, &record) != 0 || !selected(&record, filters))
            continue;
        if (show_disks && print_record(&record, blocks) == 0)
            printed = 1;
    }

    if (show_volumes) {
        save = NULL;
        printf("\nVolumes available:\n");
        for (line = strtok_r(volume_serialized, "\n", &save);
             line;
             line = strtok_r(NULL, "\n", &save)) {
            struct device_record record;
            const char *name;

            if (parse_record(line, &record) != 0 ||
                !selected(&record, filters))
                continue;
            name = record.label[0] ? record.label : record.kernel;
            printf("%s [Mounted]\n", name);
            printed = 1;
        }
    }
    FreeArgs(rdargs);
    return printed || (!show_disks && !show_volumes) ?
           RETURN_OK : RETURN_WARN;
}
