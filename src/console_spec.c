#define _POSIX_C_SOURCE 200809L

#include "console_spec.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int parse_number(const char *text, int *value)
{
    char *end;
    long parsed;

    if (!text || !*text)
        return 1;
    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' ||
        parsed < 0 || parsed > INT_MAX)
        return -1;
    *value = (int)parsed;
    return 0;
}

int ace_console_spec_parse(const char *text, struct ace_console_spec *spec)
{
    char fields[6][256];
    const char *cursor;
    size_t field_count = 0;
    size_t length;
    size_t copied;
    int result;

    if (!text || !spec || strncasecmp(text, "CON:", 4) != 0)
        return -1;
    memset(spec, 0, sizeof(*spec));
    cursor = text + 4;
    while (*cursor != '\0' && field_count < 6) {
        const char *slash = strchr(cursor, '/');

        length = slash ? (size_t)(slash - cursor) : strlen(cursor);
        if (length >= sizeof(fields[0]))
            return -1;
        memcpy(fields[field_count], cursor, length);
        fields[field_count][length] = '\0';
        field_count++;
        if (!slash)
            break;
        cursor = slash + 1;
    }
    if (field_count == 0)
        return 0;

    result = parse_number(fields[0], &spec->x);
    if (result < 0)
        return -1;
    result = parse_number(field_count > 1 ? fields[1] : NULL, &spec->y);
    if (result < 0)
        return -1;
    if (field_count >= 2 && fields[0][0] != '\0' && fields[1][0] != '\0')
        spec->has_position = 1;

    result = parse_number(field_count > 2 ? fields[2] : NULL, &spec->width);
    if (result < 0)
        return -1;
    result = parse_number(field_count > 3 ? fields[3] : NULL, &spec->height);
    if (result < 0)
        return -1;
    if (field_count >= 4 && spec->width > 0 && spec->height > 0)
        spec->has_size = 1;

    if (field_count >= 5) {
        copied = strlen(fields[4]);
        if (copied >= sizeof(spec->title))
            return -1;
        strcpy(spec->title, fields[4]);
    }
    return 0;
}
