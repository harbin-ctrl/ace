#define _POSIX_C_SOURCE 200809L

#include "assign_compat.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>

#include <dos/dos.h>
#include <dos/dosextens.h>

#include "broker_client.h"
#include "broker_protocol.h"
#include "native_host.h"

#define ASSIGN_LIST_MAX 256

static struct DosList dos_entries[ASSIGN_LIST_MAX];
static size_t dos_entry_count;
static char *dos_entry_names[ASSIGN_LIST_MAX];
static char *dos_entry_assign_names[ASSIGN_LIST_MAX];
static int dos_list_locked;

static BSTR make_bstr(const char *value)
{
    size_t length = strlen(value);
    BSTR result;

    if (length > 255)
        return NULL;
    result = malloc(length + 2);
    if (!result)
        return NULL;
    result[0] = (char)length;
    memcpy(result + 1, value, length + 1);
    return result;
}

static const char *bstr_text(BSTR value)
{
    return value ? value + 1 : "";
}

static void clear_dos_entries(void)
{
    for (size_t index = 0; index < dos_entry_count; index++) {
        if (dos_entries[index].dol_Lock)
            UnLock(dos_entries[index].dol_Lock);
        free(dos_entry_names[index]);
        free(dos_entry_assign_names[index]);
    }
    memset(dos_entries, 0, sizeof(dos_entries));
    memset(dos_entry_names, 0, sizeof(dos_entry_names));
    memset(dos_entry_assign_names, 0, sizeof(dos_entry_assign_names));
    dos_entry_count = 0;
}

static struct DosList *append_dos_entry(const char *name, LONG type)
{
    struct DosList *entry;

    if (dos_entry_count >= ASSIGN_LIST_MAX)
        return NULL;
    entry = &dos_entries[dos_entry_count];
    memset(entry, 0, sizeof(*entry));
    dos_entry_names[dos_entry_count] = make_bstr(name);
    if (!dos_entry_names[dos_entry_count])
        return NULL;
    entry->dol_Name = dos_entry_names[dos_entry_count];
    entry->dol_Type = type;
    if (dos_entry_count > 0)
        dos_entries[dos_entry_count - 1].dol_Next = entry;
    dos_entry_count++;
    return entry;
}

static void load_devices(ULONG flags)
{
    char serialized[AMIGA_BROKER_MAX_PAYLOAD];
    char *line;
    char *save_line = NULL;

    if (!(flags & (LDF_DEVICES | LDF_VOLUMES)) ||
        native_broker_listdos(serialized, sizeof(serialized)) != 0)
        return;
    for (line = strtok_r(serialized, "\n", &save_line);
         line; line = strtok_r(NULL, "\n", &save_line)) {
        char *fields[5] = {0};
        char *save_field = NULL;
        int field_count = 0;
        char *cursor = line;

        while (field_count < 5 &&
               (fields[field_count] = strtok_r(cursor, "\t", &save_field))) {
            cursor = NULL;
            field_count++;
        }
        if (field_count < 1)
            continue;
        if (flags & LDF_VOLUMES) {
            struct DosList *volume = append_dos_entry(fields[0], DLT_VOLUME);
            if (volume)
                volume->dol_Lock = Lock(fields[0], SHARED_LOCK);
        }
        if (flags & LDF_DEVICES)
            (void)append_dos_entry(fields[0], DLT_DEVICE);
    }
}

static void load_assigns(ULONG flags)
{
    char serialized[AMIGA_BROKER_MAX_PAYLOAD];
    char *line;
    char *save_line = NULL;

    if (!(flags & LDF_ASSIGNS) ||
        native_broker_listassigns(serialized, sizeof(serialized)) != 0)
        return;
    for (line = strtok_r(serialized, "\n", &save_line);
         line; line = strtok_r(NULL, "\n", &save_line)) {
        char *name = strtok(line, "\t");
        char *type_text = strtok(NULL, "\t");
        char *root = strtok(NULL, "");
        LONG type;
        struct DosList *entry;

        if (!name || !type_text || !root)
            continue;
        type = (LONG)strtol(type_text, NULL, 10);
        entry = append_dos_entry(name, type);
        if (!entry)
            continue;
        if (type == DLT_LATE || type == DLT_NONBINDING) {
            dos_entry_assign_names[dos_entry_count - 1] = strdup(root);
            entry->dol_misc.dol_assign.dol_AssignName =
                dos_entry_assign_names[dos_entry_count - 1];
        } else {
            entry->dol_Lock = native_lock_host_path(root);
        }
    }
}

struct DosList *LockDosList(ULONG flags)
{
    if (dos_list_locked)
        clear_dos_entries();
    dos_list_locked = 1;
    load_devices(flags);
    load_assigns(flags);
    return NULL;
}

struct DosList *NextDosEntry(struct DosList *entry, ULONG flags)
{
    size_t index = 0;

    if (entry) {
        if (entry < dos_entries || entry >= dos_entries + dos_entry_count)
            return NULL;
        index = (size_t)(entry - dos_entries) + 1;
    }
    for (; index < dos_entry_count; index++) {
        LONG type = dos_entries[index].dol_Type;
        BOOL match = (type == DLT_DEVICE && (flags & LDF_DEVICES)) ||
                     (type == DLT_VOLUME && (flags & LDF_VOLUMES)) ||
                     ((type == DLT_DIRECTORY || type == DLT_LATE ||
                       type == DLT_NONBINDING) && (flags & LDF_ASSIGNS));
        if (match)
            return &dos_entries[index];
    }
    return NULL;
}

struct DosList *FindDosEntry(struct DosList *list, CONST_STRPTR name,
                             ULONG flags)
{
    struct DosList *entry = NULL;

    while ((entry = NextDosEntry(entry, flags)) != NULL)
        if (strcasecmp(bstr_text(entry->dol_Name), name ? name : "") == 0)
            return entry;
    (void)list;
    return NULL;
}

void UnLockDosList(ULONG flags)
{
    (void)flags;
    clear_dos_entries();
    dos_list_locked = 0;
}

BOOL AddDosEntry(struct DosList *entry)
{
    (void)entry;
    SetIoErr(ERROR_NOT_IMPLEMENTED);
    return DOSFALSE;
}

BOOL RemDosEntry(struct DosList *entry)
{
    (void)entry;
    SetIoErr(ERROR_NOT_IMPLEMENTED);
    return DOSFALSE;
}

struct DosList *MakeDosEntry(CONST_STRPTR name, LONG type)
{
    (void)name;
    (void)type;
    SetIoErr(ERROR_NOT_IMPLEMENTED);
    return NULL;
}

void FreeDosEntry(struct DosList *entry)
{
    (void)entry;
}

static int assign_lock_path(BPTR lock, char *result, size_t result_size)
{
    if (!lock || !NameFromLock(lock, result, result_size))
        return -1;
    return 0;
}

BOOL AssignLock(CONST_STRPTR name, BPTR lock)
{
    char path[PATH_MAX];
    uint32_t flags = lock ? 0 : AMIGA_BROKER_ASSIGN_REMOVE;

    if (lock && assign_lock_path(lock, path, sizeof(path)) != 0)
        return DOSFALSE;
    if (native_broker_assign_ex(name, lock ? path : "", flags) != 0) {
        SetIoErr(errno == ENOENT ? ERROR_OBJECT_NOT_FOUND : errno);
        return DOSFALSE;
    }
    SetIoErr(0);
    return DOSTRUE;
}

BOOL AssignAdd(CONST_STRPTR name, BPTR lock)
{
    char path[PATH_MAX];

    if (assign_lock_path(lock, path, sizeof(path)) != 0 ||
        native_broker_assign_ex(name, path, AMIGA_BROKER_ASSIGN_ADD) != 0)
        return DOSFALSE;
    return DOSTRUE;
}

BOOL AssignAddToList(CONST_STRPTR name, BPTR lock, LONG flags)
{
    char path[PATH_MAX];

    (void)flags;
    if (assign_lock_path(lock, path, sizeof(path)) != 0 ||
        native_broker_assign_ex(name, path,
                                AMIGA_BROKER_ASSIGN_PREPEND) != 0)
        return DOSFALSE;
    return DOSTRUE;
}

BOOL AssignPath(CONST_STRPTR name, CONST_STRPTR path)
{
    return native_broker_assign_ex(name, path, AMIGA_BROKER_ASSIGN_PATH) == 0;
}

BOOL AssignLate(CONST_STRPTR name, CONST_STRPTR path)
{
    return native_broker_assign_ex(name, path, AMIGA_BROKER_ASSIGN_DEFER) == 0;
}

LONG RemAssignList(CONST_STRPTR name, BPTR lock)
{
    char path[PATH_MAX];

    if (assign_lock_path(lock, path, sizeof(path)) != 0 ||
        native_broker_assign_ex(name, path,
                                AMIGA_BROKER_ASSIGN_REMOVE_ITEM) != 0)
        return DOSFALSE;
    return DOSTRUE;
}

static void emit_text(void (*put_character)(void), APTR data,
                      const char *text, size_t length)
{
    void (*put)(UBYTE, APTR) = (void (*)(UBYTE, APTR))put_character;

    for (size_t index = 0; index < length; index++)
        put((UBYTE)text[index], data);
}

void RawDoFmt(CONST_STRPTR format, va_list arguments,
              void (*put_character)(void), APTR data)
{
    while (*format) {
        char conversion;
        int long_value = 0;

        if (*format != '%') {
            emit_text(put_character, data, format++, 1);
            continue;
        }
        format++;
        if (*format == '-')
            format++;
        while (isdigit((unsigned char)*format))
            format++;
        if (*format == '.') {
            format++;
            while (isdigit((unsigned char)*format))
                format++;
        }
        if (*format == 'l') {
            long_value = 1;
            format++;
        }
        conversion = *format ? *format++ : '\0';
        if (conversion == '%') {
            emit_text(put_character, data, "%", 1);
        } else if (conversion == 'b') {
            BSTR value = (BSTR)(uintptr_t)va_arg(arguments, IPTR);
            emit_text(put_character, data, bstr_text(value),
                      value ? (unsigned char)value[0] : 0);
        } else if (conversion == 's') {
            const char *value = (const char *)(uintptr_t)va_arg(arguments, IPTR);
            if (value)
                emit_text(put_character, data, value, strlen(value));
        } else if (conversion == 'c') {
            IPTR value = va_arg(arguments, IPTR);
            char character = (char)value;
            (void)long_value;
            emit_text(put_character, data, &character, 1);
        } else {
            char buffer[64];
            IPTR value = va_arg(arguments, IPTR);
            int written = snprintf(buffer, sizeof(buffer),
                                   conversion == 'd' || conversion == 'D' ?
                                   "%ld" : "%lx", (long)value);
            if (written > 0)
                emit_text(put_character, data, buffer, (size_t)written);
        }
    }
}

LONG Write(BPTR file, CONST_APTR buffer, LONG length)
{
    const unsigned char *bytes = buffer;

    for (LONG index = 0; index < length; index++)
        if (FPutC(file, bytes[index]) < 0)
            return index;
    return length;
}
