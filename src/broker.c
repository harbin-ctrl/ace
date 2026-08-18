#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "broker_protocol.h"
#include "clipboard_bridge.h"
#include "dos_devices.h"

#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define MAX_SESSIONS 64
#define MAX_ASSIGNS  64
#define MAX_ASSIGN_TARGETS 16
#define MAX_COMMAND_PATHS 32
#define MAX_VARS     128
#define MAX_NAME     64
#define MAX_VALUE    4096
#define MAX_LIST_RESULT AMIGA_BROKER_MAX_PAYLOAD
#define MAX_TASKS 256
#define DEFAULT_FAIL_LEVEL 10
#define DEFAULT_PROMPT "%N.%S> "
#define AMIGA_COMPONENT_LIMIT 107
/* '~' is legal in a stored name, but AROS pattern syntax makes it a
 * negation operator. '^' remains an ordinary filename character there and
 * is still visually conspicuous on Linux. */
#define MAPPED_MARKER '^'
#define MAPPED_SUFFIX_DIGITS 8
#define ASSIGN_DIRECTORY 1
#define ASSIGN_LATE 3
#define ASSIGN_NONBINDING 4

#ifndef NAME_MAX
#define NAME_MAX 255
#endif

struct variable_entry {
    char name[MAX_NAME];
    char value[MAX_VALUE];
    uint32_t type;
};

struct assign_entry {
    char name[MAX_NAME];
    char root[PATH_MAX];
    char targets[MAX_ASSIGN_TARGETS][PATH_MAX];
    size_t target_count;
    int type;
};

/* A Linux directory entry that cannot be carried safely through an AROS
 * pathname gets a short, visible spelling for the life of this broker. The
 * parent is part of the key: the same spelling may be used independently in
 * different directories. */
struct component_mapping {
    char parent[PATH_MAX];
    char host_name[NAME_MAX + 1];
    char amiga_name[AMIGA_COMPONENT_LIMIT + 1];
    struct component_mapping *next;
};

struct broker_session {
    bool in_use;
    /* Ordinal of the last request that touched this session, for reclaiming
     * the coldest one when every slot is taken. See get_session(). */
    uint64_t last_used;
    /* Connections that have claimed this session with ATTACH -- normally the
     * one shell the session belongs to, briefly two while a replacement
     * connection re-attaches. While this is non-zero the session has a live
     * owner: it is never reclaimed, and when it falls back to zero the
     * session is freed on the spot. See release_session(). */
    uint32_t anchors;
    char id[128];
    char cwd[PATH_MAX];
    char command_paths[MAX_COMMAND_PATHS][PATH_MAX];
    size_t command_path_count;
    struct assign_entry assigns[MAX_ASSIGNS];
    struct variable_entry local_vars[MAX_VARS];
    int32_t return_code;
    int32_t result2;
    int32_t fail_level;
    char prompt[MAX_VALUE];
    pid_t foreground_pid;
    uint64_t foreground_task;
    uint32_t pending_foreground_signals;
};

static struct broker_session sessions[MAX_SESSIONS];

struct broker_task {
    uint64_t id;
    int fd;
    int session;
    pid_t pid;
    char name[MAX_NAME];
};

static struct broker_task tasks[MAX_TASKS];
static uint64_t next_task_id = 1;
static struct variable_entry global_vars[MAX_VARS];
static struct component_mapping *component_mappings;
static uint32_t mapping_state;
static int server_fd = -1;
static time_t broker_started;
/* Set from amiga_broker_socket_path(), or argv[1], as main() starts, before
 * anything (including the signal handlers) can read it. */
static const char *socket_path;

static int write_all(int fd, const void *buffer, size_t length)
{
    const char *bytes = buffer;
    while (length) {
        ssize_t written = write(fd, bytes, length);
        if (written < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (!written)
            return -1;
        bytes += written;
        length -= (size_t)written;
    }
    return 0;
}

static int read_all(int fd, void *buffer, size_t length)
{
    char *bytes = buffer;
    while (length) {
        ssize_t received = read(fd, bytes, length);
        if (received < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (!received)
            return -1;
        bytes += received;
        length -= (size_t)received;
    }
    return 0;
}

/*
 * Reads a whole message, distinguishing a peer that closed cleanly between
 * requests from one that failed mid-message. With one request per connection
 * that distinction did not exist -- every end of input was the end of the
 * exchange -- but a held connection ends with exactly this, and a normal
 * disconnect must not be reported as an error.
 *
 * Returns 1 for a complete message, 0 for a clean close, -1 for a failure.
 */
static int read_message(int fd, void *buffer, size_t length)
{
    char *bytes = buffer;
    size_t received = 0;

    while (received < length) {
        ssize_t chunk = read(fd, bytes + received, length - received);

        if (chunk < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (!chunk)
            return received ? -1 : 0;
        received += (size_t)chunk;
    }
    return 1;
}

static uint64_t session_clock;

static struct broker_session *touch_session(struct broker_session *session)
{
    if (session)
        session->last_used = ++session_clock;
    return session;
}

/* Gives a session's slot back. Called when the last connection that claimed
   the session closes, whether its shell exited, was killed, or crashed. */
static void release_session(struct broker_session *session)
{
    memset(session, 0, sizeof(*session));
}

/*
 * Finds a session by name, creating it if it is new.
 *
 * A session that a shell has claimed with ATTACH has a lifetime the broker
 * knows exactly, and is freed the moment its last connection closes, so it
 * is never a candidate here.
 *
 * That leaves sessions nobody claimed -- created by a standalone command or
 * by brokerctl, which come and go as separate processes and expect their
 * session to outlive each of them. Nothing can say when those are finished
 * with, so they keep the older rule: a full table gives up its coldest
 * unclaimed slot rather than refusing, since the alternative was that the
 * sixty-fifth distinct session got ENOSPC forever after and every ACE window
 * opened from then on died on the spot. What has changed is that this can no
 * longer take a live shell's session away from underneath it.
 */
/*
 * SYS: -- the boot volume. On a real machine dos.library locks the volume it
 * was booted from; here the equivalent question is which installation of ACE
 * this broker belongs to. The compiled-in answer is the install prefix, the
 * environment can override it for a test run, and a broker that finds
 * neither falls back to its own directory, which in a build tree is where
 * the commands are.
 */
static char system_root[PATH_MAX];

static struct assign_entry *allocate_assign(struct broker_session *session,
                                            const char *name);

static bool is_directory(const char *path)
{
    struct stat information;

    return path && *path && stat(path, &information) == 0 &&
           S_ISDIR(information.st_mode);
}

static bool is_regular_file(const char *path)
{
    struct stat information;

    return path && *path && stat(path, &information) == 0 &&
           S_ISREG(information.st_mode);
}

/* mkdir -p for a path ACE owns. */
static bool make_directory_path(const char *path)
{
    char work[PATH_MAX];
    size_t length = strlen(path);

    if (length >= sizeof(work))
        return false;
    strcpy(work, path);
    for (char *cursor = work + 1; *cursor; cursor++) {
        if (*cursor != '/')
            continue;
        *cursor = '\0';
        if (mkdir(work, 0700) != 0 && errno != EEXIST)
            return false;
        *cursor = '/';
    }
    return (mkdir(work, 0700) == 0 || errno == EEXIST) && is_directory(work);
}

static void resolve_system_root(void)
{
    /* Shared with every client through broker_protocol.h: the socket this
       broker listens on is named after this root, so a client that resolved
       it differently would look for its broker somewhere else entirely. */
    const char *root = amiga_broker_system_root();

    if (strlen(root) < sizeof(system_root))
        strcpy(system_root, root);
    else
        strcpy(system_root, "/");
}

/*
 * The volatile half of the boot assigns. AROS puts ENV: and T: in RAM:,
 * which it can name because the RAM disk is a fixed part of the system. ACE
 * names its tmpfs mounts RAM:, RAM1: ... in host mount order, so which one
 * is "the" RAM disk is an accident of how this machine happens to be
 * mounted, and no script could portably name it. The host's own per-user
 * runtime directory is the same thing said properly: tmpfs, private, and
 * emptied between boots.
 */
static void volatile_root(char *result, size_t result_size)
{
    const char *runtime = getenv("XDG_RUNTIME_DIR");

    if (runtime && *runtime &&
        (size_t)snprintf(result, result_size, "%s/ace", runtime) < result_size)
        return;
    snprintf(result, result_size, "/tmp/ace-%lu", (unsigned long)getuid());
}

static void set_directory_assign(struct broker_session *session,
                                 const char *name, const char *path)
{
    struct assign_entry *assign;

    if (!is_directory(path))
        return;
    assign = allocate_assign(session, name);
    if (!assign || strlen(path) >= sizeof(assign->root))
        return;
    memset(assign->targets, 0, sizeof(assign->targets));
    strcpy(assign->targets[0], path);
    assign->target_count = 1;
    strcpy(assign->root, path);
    assign->type = ASSIGN_DIRECTORY;
}

/*
 * AddBootAssign() in rom/dos/cliinit.c: the drawer if it is there, and SYS:
 * itself if it is not. A system with nothing to put in L: still answers for
 * L:, which is why an unmodified program can open "LIBS:foo" on any Amiga
 * and get a missing file rather than a missing device.
 */
static void add_boot_assign(struct broker_session *session, const char *name,
                            const char *drawer)
{
    char path[PATH_MAX];

    if ((size_t)snprintf(path, sizeof(path), "%s/%s", system_root, drawer) <
        sizeof(path) && is_directory(path))
        set_directory_assign(session, name, path);
    else
        set_directory_assign(session, name, system_root);
}

/*
 * What dos.library establishes before the first shell runs. It has to happen
 * here rather than in a script, for the same reason it is C code in AROS:
 * the shell cannot find the script that would make these assigns, or the
 * Assign command that would make them, until they exist.
 */
static void seed_boot_assigns(struct broker_session *session)
{
    char volatile_path[PATH_MAX];
    char path[PATH_MAX];
    char clips_path[PATH_MAX];

    set_directory_assign(session, "SYS", system_root);
    add_boot_assign(session, "C", "C");
    add_boot_assign(session, "LIBS", "Libs");
    add_boot_assign(session, "DEVS", "Devs");
    add_boot_assign(session, "L", "L");
    add_boot_assign(session, "S", "S");
    add_boot_assign(session, "FONTS", "Fonts");

    /* ENVARC: is the saved half of the environment and lives with the
       installation; ENV: is the live half and does not survive a reboot. */
    if ((size_t)snprintf(path, sizeof(path), "%s/Prefs/Env-Archive",
                         system_root) < sizeof(path)) {
        (void)make_directory_path(path);
        set_directory_assign(session, "ENVARC", path);
    }
    volatile_root(volatile_path, sizeof(volatile_path));
    if ((size_t)snprintf(path, sizeof(path), "%s/env", volatile_path) <
        sizeof(path)) {
        (void)make_directory_path(path);
        set_directory_assign(session, "ENV", path);
    }
    if ((size_t)snprintf(path, sizeof(path), "%s/t", volatile_path) <
        sizeof(path)) {
        (void)make_directory_path(path);
        set_directory_assign(session, "T", path);
    }
    if (ace_clipboard_store_prepare() == 0 &&
        ace_clipboard_store_root(clips_path, sizeof(clips_path)) == 0)
        set_directory_assign(session, "CLIPS", clips_path);
}

/*
 * The Startup-Sequence's `Copy ENVARC: ENV: ALL`, done here because ACE has
 * no Copy command to do it with yet, and because a broker is the closest
 * thing ACE has to a boot: this runs once, when the broker starts, and the
 * live environment it produces is then shared by every session.
 */
static void restore_environment_archive(void)
{
    char archive[PATH_MAX];
    char live[PATH_MAX];
    char volatile_path[PATH_MAX];
    struct dirent *entry;
    DIR *stream;

    if ((size_t)snprintf(archive, sizeof(archive), "%s/Prefs/Env-Archive",
                         system_root) >= sizeof(archive))
        return;
    volatile_root(volatile_path, sizeof(volatile_path));
    if ((size_t)snprintf(live, sizeof(live), "%s/env", volatile_path) >=
        sizeof(live) || !make_directory_path(live))
        return;
    stream = opendir(archive);
    if (!stream)
        return;
    while ((entry = readdir(stream))) {
        char from[PATH_MAX];
        char to[PATH_MAX];
        char buffer[4096];
        ssize_t count;
        int source;
        int target;

        if (entry->d_name[0] == '.')
            continue;
        if ((size_t)snprintf(from, sizeof(from), "%s/%s", archive,
                             entry->d_name) >= sizeof(from) ||
            (size_t)snprintf(to, sizeof(to), "%s/%s", live, entry->d_name) >=
            sizeof(to))
            continue;
        if (!is_regular_file(from))
            continue;
        source = open(from, O_RDONLY);
        if (source < 0)
            continue;
        target = open(to, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (target < 0) {
            close(source);
            continue;
        }
        while ((count = read(source, buffer, sizeof(buffer))) > 0) {
            ssize_t written = 0;

            while (written < count) {
                ssize_t step = write(target, buffer + written,
                                     (size_t)(count - written));

                if (step <= 0)
                    break;
                written += step;
            }
            if (written < count)
                break;
        }
        close(source);
        close(target);
    }
    closedir(stream);
}

static struct broker_session *get_session(const char *id)
{
    struct broker_session *free_slot = NULL;
    struct broker_session *coldest = NULL;

    for (size_t i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].in_use && strcmp(sessions[i].id, id) == 0)
            return touch_session(&sessions[i]);
        if (!sessions[i].in_use) {
            if (!free_slot)
                free_slot = &sessions[i];
        } else if (sessions[i].anchors == 0 &&
                   (!coldest || sessions[i].last_used < coldest->last_used)) {
            coldest = &sessions[i];
        }
    }
    if (!free_slot) {
        free_slot = coldest;
        if (free_slot)
            fprintf(stderr, "ace-broker: session table full, reclaiming the "
                            "least recently used session '%s' for '%s'\n",
                    free_slot->id, id);
    }
    if (!free_slot || strlen(id) >= sizeof(free_slot->id))
        return NULL;

    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->in_use = true;
    strcpy(free_slot->id, id);
    if (!getcwd(free_slot->cwd, sizeof(free_slot->cwd)))
        strcpy(free_slot->cwd, "/");
    free_slot->fail_level = DEFAULT_FAIL_LEVEL;
    strcpy(free_slot->prompt, DEFAULT_PROMPT);
    seed_boot_assigns(free_slot);
    return touch_session(free_slot);
}

static struct broker_session *find_session(const char *id)
{
    for (size_t i = 0; i < MAX_SESSIONS; i++)
        if (sessions[i].in_use && strcmp(sessions[i].id, id) == 0)
            return touch_session(&sessions[i]);
    return NULL;
}

static bool component_needs_mapping(const char *name)
{
    size_t length = strlen(name);

    if (length > AMIGA_COMPONENT_LIMIT)
        return true;
    for (size_t index = 0; index < length; index++) {
        unsigned char character = (unsigned char)name[index];

        /* Keep the exposed form deliberately boring. Besides ':' being a
         * path separator, AROS pattern syntax gives several other legal
         * Linux bytes structural meaning. Control and non-ASCII bytes are
         * also poor terminal display material. */
        if (!((character >= 'a' && character <= 'z') ||
              (character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9') ||
              character == '.' || character == '_' || character == '-') )
            return true;
    }
    return false;
}

static uint32_t next_mapping_value(void)
{
    if (!mapping_state) {
        struct timespec now;

        clock_gettime(CLOCK_MONOTONIC, &now);
        mapping_state = (uint32_t)now.tv_nsec ^ (uint32_t)now.tv_sec ^
                        (uint32_t)getpid();
        if (!mapping_state)
            mapping_state = 0x9e3779b9u;
    }

    /* xorshift32 is sufficient here: this is a collision-resistant display
     * suffix, not a security token. */
    mapping_state ^= mapping_state << 13;
    mapping_state ^= mapping_state >> 17;
    mapping_state ^= mapping_state << 5;
    return mapping_state;
}

static struct component_mapping *find_mapping_by_host(const char *parent,
                                                       const char *host_name)
{
    for (struct component_mapping *mapping = component_mappings;
         mapping; mapping = mapping->next)
        if (strcmp(mapping->parent, parent) == 0 &&
            strcmp(mapping->host_name, host_name) == 0)
            return mapping;
    return NULL;
}

static struct component_mapping *find_mapping_by_amiga(const char *parent,
                                                        const char *amiga_name)
{
    for (struct component_mapping *mapping = component_mappings;
         mapping; mapping = mapping->next)
        if (strcmp(mapping->parent, parent) == 0 &&
            strcasecmp(mapping->amiga_name, amiga_name) == 0)
            return mapping;
    return NULL;
}

static int host_component_exists(const char *parent, const char *name)
{
    char path[PATH_MAX];

    if (snprintf(path, sizeof(path), "%s%s%s", parent,
                 strcmp(parent, "/") == 0 ? "" : "/", name) >=
        (int)sizeof(path))
        return 1;
    return lstat(path, &(struct stat){0}) == 0;
}

/* Linux permits several spellings which AmigaDOS would regard as the same
 * component.  Keep the lexical first one as the ordinary Amiga spelling and
 * make every other one go through the visible ^ escape mapping. */
static bool host_case_variant_needs_mapping(const char *parent,
                                            const char *host_name)
{
    DIR *stream;
    struct dirent *entry;
    char first[NAME_MAX + 1];
    bool collision = false;
    bool found = false;

    stream = opendir(parent);
    if (!stream)
        return false;
    while ((entry = readdir(stream)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0 ||
            strcasecmp(entry->d_name, host_name) != 0)
            continue;
        if (!found || strcmp(entry->d_name, first) < 0) {
            strcpy(first, entry->d_name);
            found = true;
        }
        if (strcmp(entry->d_name, host_name) != 0)
            collision = true;
    }
    closedir(stream);
    return collision && strcmp(host_name, first) != 0;
}

/*
 * AmigaDOS treats these as ordinary component names, while Linux reserves
 * them for its own traversal. Colon cannot occur in an AmigaDOS component,
 * but is an ordinary Linux filename byte, so the two spellings are a stable,
 * collision-free bridge. This is deliberately before the broker-lifetime
 * escape mapping: ':' and '::' have these meanings on every ACE volume, not
 * a synthetic spelling that varies with the broker session.
 */
static const char *amiga_component_for_host(const char *host_name)
{
    if (strcmp(host_name, ":") == 0)
        return ".";
    if (strcmp(host_name, "::") == 0)
        return "..";
    return NULL;
}

static const char *host_component_for_amiga(const char *amiga_name)
{
    if (strcmp(amiga_name, ".") == 0)
        return ":";
    if (strcmp(amiga_name, "..") == 0)
        return "::";
    return NULL;
}

static int map_component(const char *parent, const char *host_name,
                         char *result, size_t result_size)
{
    struct component_mapping *mapping;
    const char *fixed_name;
    char prefix[AMIGA_COMPONENT_LIMIT + 1];
    size_t prefix_length = 0;

    fixed_name = amiga_component_for_host(host_name);
    if (fixed_name) {
        if (strlen(fixed_name) >= result_size) {
            errno = ENAMETOOLONG;
            return -1;
        }
        strcpy(result, fixed_name);
        return 0;
    }
    if (!component_needs_mapping(host_name) &&
        !host_case_variant_needs_mapping(parent, host_name)) {
        if (strlen(host_name) >= result_size) {
            errno = ENAMETOOLONG;
            return -1;
        }
        strcpy(result, host_name);
        return 0;
    }
    mapping = find_mapping_by_host(parent, host_name);
    if (mapping) {
        if (strlen(mapping->amiga_name) >= result_size) {
            errno = ENAMETOOLONG;
            return -1;
        }
        strcpy(result, mapping->amiga_name);
        return 0;
    }

    for (size_t index = 0; host_name[index] &&
         prefix_length < sizeof(prefix) - 1; index++) {
        unsigned char character = (unsigned char)host_name[index];

        if ((character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') ||
            character == '.' || character == '_' || character == '-')
            prefix[prefix_length++] = (char)character;
        else
            prefix[prefix_length++] = '-';
    }
    if (!prefix_length)
        memcpy(prefix, "file", 5), prefix_length = 4;
    prefix[prefix_length] = '\0';

    /* The suffix is intentionally fixed-width and random-looking. It makes
     * the synthetic nature obvious while giving us enough room for a useful
     * readable prefix even at the 107-byte AROS component limit. */
    for (int attempt = 0; attempt < 1024; attempt++) {
        char candidate[AMIGA_COMPONENT_LIMIT + 1];
        unsigned int suffix = next_mapping_value();
        size_t suffix_length = 1 + MAPPED_SUFFIX_DIGITS;
        size_t available = AMIGA_COMPONENT_LIMIT - suffix_length;
        size_t candidate_prefix_length = prefix_length < available ?
                                         prefix_length : available;

        if (snprintf(candidate, sizeof(candidate), "%.*s%c%08X",
                     (int)candidate_prefix_length, prefix, MAPPED_MARKER,
                     suffix) >=
            (int)sizeof(candidate))
            continue;
        if (find_mapping_by_amiga(parent, candidate) ||
            host_component_exists(parent, candidate))
            continue;
        mapping = calloc(1, sizeof(*mapping));
        if (!mapping) {
            errno = ENOMEM;
            return -1;
        }
        if (strlen(parent) >= sizeof(mapping->parent) ||
            strlen(host_name) >= sizeof(mapping->host_name)) {
            free(mapping);
            errno = ENAMETOOLONG;
            return -1;
        }
        strcpy(mapping->parent, parent);
        strcpy(mapping->host_name, host_name);
        strcpy(mapping->amiga_name, candidate);
        mapping->next = component_mappings;
        component_mappings = mapping;
        if (strlen(candidate) >= result_size) {
            errno = ENAMETOOLONG;
            return -1;
        }
        strcpy(result, candidate);
        return 0;
    }
    errno = EEXIST;
    return -1;
}

/*
 * An Amiga filesystem matches a name without regard to case: `dir`, `Dir` and
 * `DIR` name the same thing, which is why an Amiga user types commands in
 * whatever case they please. Linux does not, so a name that does not exist
 * is looked for among the entries that are there.
 *
 * A name being created -- which exists nowhere yet -- keeps the spelling it
 * was given. Where a directory holds several entries differing only in case,
 * something an Amiga filesystem could not have contained in the first place,
 * the lexical first is its ordinary spelling.  The other spellings are
 * available through the explicit ^ mappings made by map_component().
 */
static void match_existing_case(const char *parent, char *name,
                                size_t name_size)
{
    char best[NAME_MAX + 1];
    struct dirent *entry;
    DIR *stream;
    int found = 0;

    stream = opendir(parent);
    if (!stream)
        return;
    while ((entry = readdir(stream))) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        if (strcasecmp(entry->d_name, name) != 0)
            continue;
        if (strlen(entry->d_name) >= sizeof(best))
            continue;
        if (!found || strcmp(entry->d_name, best) < 0) {
            strcpy(best, entry->d_name);
            found = 1;
        }
    }
    closedir(stream);
    if (found && strlen(best) < name_size)
        strcpy(name, best);
}

static int unmap_component(const char *parent, const char *amiga_name,
                           char *result, size_t result_size)
{
    struct component_mapping *mapping;
    const char *fixed_name = host_component_for_amiga(amiga_name);

    if (fixed_name) {
        if (strlen(fixed_name) >= result_size) {
            errno = ENAMETOOLONG;
            return -1;
        }
        strcpy(result, fixed_name);
        return 0;
    }
    mapping = find_mapping_by_amiga(parent, amiga_name);

    if (!mapping) {
        if (strlen(amiga_name) >= result_size) {
            errno = ENAMETOOLONG;
            return -1;
        }
        strcpy(result, amiga_name);
        match_existing_case(parent, result, result_size);
        return 0;
    }
    if (strlen(mapping->host_name) >= result_size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(result, mapping->host_name);
    return 0;
}

static int append_path_text(char *result, size_t result_size,
                            size_t *used, const char *text)
{
    int written = snprintf(result + *used, result_size - *used, "%s%s",
                           *used && result[*used - 1] != ':' ? "/" : "",
                           text);

    if (written < 0 || (size_t)written >= result_size - *used) {
        errno = ENAMETOOLONG;
        return -1;
    }
    *used += (size_t)written;
    return 0;
}

static int normalize_path(const char *base, const char *path,
                          char *result, size_t result_size);

/* Convert the device layer's ordinary volume spelling into the AROS-facing
 * spelling, mapping each unsafe host component against its host parent. The
 * host and Amiga suffixes normally have the same component structure; the
 * host parent is what makes each mapping directory-specific. */
static int name_from_host_with_mappings(const char *path, char *result,
                                        size_t result_size)
{
    char raw[PATH_MAX];
    char canonical[PATH_MAX];
    char volume_root[PATH_MAX];
    char host_parent[PATH_MAX];
    const char *raw_cursor;
    const char *host_cursor;
    char *colon;
    size_t used;

    if (ace_dos_devices_name_from_path(path, raw, sizeof(raw)) != 0)
        return -1;
    if (normalize_path("/", path, canonical, sizeof(canonical)) != 0 ||
        ace_dos_devices_volume_root_for_path(canonical, volume_root,
                                             sizeof(volume_root)) != 0)
        return -1;
    colon = strchr(raw, ':');
    if (!colon) {
        errno = EINVAL;
        return -1;
    }
    used = (size_t)(colon - raw) + 1;
    if (used >= result_size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(result, raw, used);
    result[used] = '\0';

    if (strlen(volume_root) >= sizeof(host_parent)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(host_parent, volume_root);
    host_cursor = canonical + strlen(volume_root);
    while (*host_cursor == '/')
        host_cursor++;
    raw_cursor = colon + 1;
    while (*raw_cursor == '/')
        raw_cursor++;

    while (*raw_cursor) {
        char raw_component[PATH_MAX];
        char host_component[NAME_MAX + 1];
        char mapped_component[AMIGA_COMPONENT_LIMIT + 1];
        const char *raw_slash = strchr(raw_cursor, '/');
        const char *host_slash = strchr(host_cursor, '/');
        size_t raw_length = raw_slash ? (size_t)(raw_slash - raw_cursor) :
                                        strlen(raw_cursor);
        size_t host_length = host_slash ? (size_t)(host_slash - host_cursor) :
                                          strlen(host_cursor);

        if (!raw_length || raw_length >= sizeof(raw_component)) {
            errno = ENAMETOOLONG;
            return -1;
        }
        memcpy(raw_component, raw_cursor, raw_length);
        raw_component[raw_length] = '\0';
        if (!host_length || host_length >= sizeof(host_component)) {
            /* This can only happen for a filesystem root alias whose
             * spelling contains a synthetic component not present in the
             * host mount path. Preserve it literally. */
            strcpy(host_component, raw_component);
        } else {
            memcpy(host_component, host_cursor, host_length);
            host_component[host_length] = '\0';
        }
        if (map_component(host_parent, host_component, mapped_component,
                          sizeof(mapped_component)) != 0)
            return -1;
        if (append_path_text(result, result_size, &used, mapped_component) != 0)
            return -1;
        if (strlen(host_parent) + strlen(host_component) + 2 >=
            sizeof(host_parent)) {
            errno = ENAMETOOLONG;
            return -1;
        }
        if (strcmp(host_parent, "/") != 0)
            strcat(host_parent, "/");
        strcat(host_parent, host_component);
        raw_cursor = raw_slash ? raw_slash + 1 : raw_cursor + raw_length;
        host_cursor = host_slash ? host_slash + 1 : host_cursor + host_length;
        while (*raw_cursor == '/')
            raw_cursor++;
        while (*host_cursor == '/')
            host_cursor++;
    }
    return 0;
}

static int normalize_path(const char *base, const char *path,
                          char *result, size_t result_size)
{
    char combined[PATH_MAX * 2];
    char *parts[PATH_MAX / 2];
    size_t part_count = 0;
    char *cursor;
    int written;

    if (path[0] == '/')
        written = snprintf(combined, sizeof(combined), "%s", path);
    else
        written = snprintf(combined, sizeof(combined), "%s/%s", base, path);
    if (written < 0 || (size_t)written >= sizeof(combined)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    cursor = combined;
    while (*cursor) {
        char *slash = strchr(cursor, '/');
        if (slash)
            *slash = '\0';
        if (*cursor && strcmp(cursor, ".") != 0) {
            if (strcmp(cursor, "..") == 0) {
                if (part_count)
                    part_count--;
            } else if (part_count < sizeof(parts) / sizeof(parts[0])) {
                parts[part_count++] = cursor;
            } else {
                errno = ENAMETOOLONG;
                return -1;
            }
        }
        if (!slash)
            break;
        cursor = slash + 1;
    }

    if (result_size < 2) {
        errno = ENAMETOOLONG;
        return -1;
    }
    result[0] = '/';
    result[1] = '\0';
    for (size_t i = 0; i < part_count; i++) {
        size_t current = strlen(result);
        written = snprintf(result + current, result_size - current,
                           "%s%s", current > 1 ? "/" : "", parts[i]);
        if (written < 0 || (size_t)written >= result_size - current) {
            errno = ENAMETOOLONG;
            return -1;
        }
    }
    return 0;
}

static void pop_host_component(char *path)
{
    char *slash;

    if (strcmp(path, "/") == 0)
        return;
    slash = strrchr(path, '/');
    if (!slash || slash == path)
        strcpy(path, "/");
    else
        *slash = '\0';
}

/* Like normalize_path(), but resolves broker-created component spellings as
 * the path is walked. The parent host path is therefore available for every
 * reverse-map lookup; a component token is never interpreted globally. */
static int normalize_mapped_path(const char *base, const char *path,
                                 char *result, size_t result_size)
{
    char combined[PATH_MAX * 2];
    char *cursor;
    int written;

    written = snprintf(combined, sizeof(combined), "%s", path);
    if (written < 0 || (size_t)written >= sizeof(combined)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    if (path[0] == '/')
        strcpy(result, "/");
    else {
        if (strlen(base) >= result_size) {
            errno = ENAMETOOLONG;
            return -1;
        }
        strcpy(result, base);
    }

    cursor = combined;
    while (*cursor) {
        char *slash = strchr(cursor, '/');
        char decoded[PATH_MAX];

        if (slash)
            *slash = '\0';
        if (*cursor) {
            if (unmap_component(result, cursor, decoded,
                                sizeof(decoded)) != 0)
                return -1;
            size_t current = strlen(result);

            written = snprintf(result + current, result_size - current,
                               "%s%s", current > 1 ? "/" : "", decoded);
            if (written < 0 || (size_t)written >= result_size - current) {
                errno = ENAMETOOLONG;
                return -1;
            }
        }
        if (!slash)
            break;
        cursor = slash + 1;
    }
    return 0;
}

/*
 * An Amiga volume is one filesystem. Linux will happily mount a second
 * filesystem partway down a first one, but on this side that mount is a
 * separate volume with a name of its own, so resolution must stop at the
 * boundary rather than walk through it: "sda2:proc" is procfs reached
 * through the wrong volume, and PROC: is how procfs is addressed. Nothing
 * becomes unreachable, because every mount the broker knows about is
 * published under its own name in the DOS device list.
 *
 * The comparison is against the nearest ancestor that exists, because the
 * caller may be naming an object it is about to create -- MakeDir() and
 * Open() for write both resolve a path before there is anything there.
 *
 * The mount point's own directory is refused along with everything under
 * it. That directory really does belong to this volume, and its contents as
 * this volume sees them are whatever the mount obscures, but an unprivileged
 * process cannot read underneath a mount: the kernel locks that view
 * deliberately. Refusing is the honest answer until ACE can see through it.
 */
static int within_base_filesystem(const char *base, const char *path)
{
    struct stat base_info;
    struct stat probe_info;
    char probe[PATH_MAX];

    if (stat(base, &base_info) != 0)
        return -1;
    if (strlen(path) >= sizeof(probe)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(probe, path);
    while (stat(probe, &probe_info) != 0) {
        char *slash;

        if (errno != ENOENT && errno != ENOTDIR)
            return -1;
        slash = strrchr(probe, '/');
        if (!slash || slash == probe)
            return 0;
        *slash = '\0';
        /* Trimmed back to the volume root: same filesystem by definition. */
        if (strcmp(probe, base) == 0)
            return 0;
    }
    if (probe_info.st_dev != base_info.st_dev) {
        /* From this volume's point of view the object is simply not here,
           which is what ERROR_OBJECT_NOT_FOUND says. Reporting a crossing
           would describe a Linux arrangement AmigaDOS has no word for. */
        errno = ENOENT;
        return -1;
    }
    return 0;
}

static int normalize_mapped_path_beneath(const char *base, const char *path,
                                         char *result, size_t result_size)
{
    size_t base_length;

    if (normalize_mapped_path(base, path, result, result_size) != 0)
        return -1;
    if (strcmp(base, "/") != 0) {
        base_length = strlen(base);
        if (strcmp(result, base) != 0 &&
            (strncmp(result, base, base_length) != 0 ||
             result[base_length] != '/')) {
            errno = EACCES;
            return -1;
        }
    }
    return within_base_filesystem(base, result);
}

static struct assign_entry *find_assign(struct broker_session *session,
                                         const char *name)
{
    for (size_t i = 0; i < MAX_ASSIGNS; i++)
        if (session->assigns[i].name[0] &&
            strcasecmp(session->assigns[i].name, name) == 0)
            return &session->assigns[i];
    return NULL;
}

static struct assign_entry *allocate_assign(struct broker_session *session,
                                             const char *name)
{
    struct assign_entry *entry = find_assign(session, name);

    if (entry)
        return entry;
    for (size_t i = 0; i < MAX_ASSIGNS; i++) {
        if (!session->assigns[i].name[0]) {
            if (strlen(name) >= sizeof(session->assigns[i].name))
                return NULL;
            memset(&session->assigns[i], 0, sizeof(session->assigns[i]));
            strcpy(session->assigns[i].name, name);
            return &session->assigns[i];
        }
    }
    return NULL;
}

static struct variable_entry *find_variable(struct variable_entry *variables,
                                            const char *name, uint32_t type)
{
    for (size_t i = 0; i < MAX_VARS; i++)
        if (variables[i].name[0] && strcasecmp(variables[i].name, name) == 0)
            if (type == 0 || variables[i].type == type)
                return &variables[i];
    return NULL;
}

static struct variable_entry *allocate_variable(struct variable_entry *variables,
                                                const char *name, uint32_t type)
{
    struct variable_entry *entry = find_variable(variables, name, type);

    if (entry)
        return entry;
    for (size_t i = 0; i < MAX_VARS; i++) {
        if (!variables[i].name[0]) {
            if (strlen(name) >= sizeof(variables[i].name))
                return NULL;
            strcpy(variables[i].name, name);
            variables[i].type = type;
            return &variables[i];
        }
    }
    return NULL;
}

static int variable_scope(uint32_t flags, bool *local, bool *global)
{
    *local = (flags & AMIGA_BROKER_VAR_LOCAL) != 0;
    *global = (flags & AMIGA_BROKER_VAR_GLOBAL) != 0;
    if (!*local && !*global)
        *local = *global = true;
    return *local && *global &&
           (flags & (AMIGA_BROKER_VAR_LOCAL | AMIGA_BROKER_VAR_GLOBAL)) != 0;
}

static struct variable_entry *lookup_variable(struct broker_session *session,
                                              const char *name, uint32_t flags)
{
    bool local, global;
    struct variable_entry *entry;
    uint32_t type = (flags & AMIGA_BROKER_VAR_ALIAS) ?
                    AMIGA_BROKER_VAR_ALIAS : AMIGA_BROKER_VAR_VARIABLE;

    if (variable_scope(flags, &local, &global))
        return NULL;
    if (local) {
        entry = find_variable(session->local_vars, name, type);
        if (entry)
            return entry;
    }
    return global ? find_variable(global_vars, name, type) : NULL;
}

static int append_variable_list(char *result, size_t result_size, size_t *used,
                                const struct variable_entry *variables,
                                uint32_t flags)
{
    bool aliases = (flags & AMIGA_BROKER_VAR_ALIAS) != 0;
    bool any = (flags & AMIGA_BROKER_VAR_ANY) != 0;

    for (size_t i = 0; i < MAX_VARS; i++) {
        int written;
        if (!variables[i].name[0])
            continue;
        if (!any && ((variables[i].type == AMIGA_BROKER_VAR_ALIAS) != aliases))
            continue;
        written = snprintf(result + *used, result_size - *used,
                           "%u\t%s\n", variables[i].type == AMIGA_BROKER_VAR_ALIAS,
                           variables[i].name);
        if (written < 0 || (size_t)written >= result_size - *used)
            return -1;
        *used += (size_t)written;
    }
    return 0;
}

static int normalize_amiga_path(struct broker_session *session,
                                 const char *path, char *result,
                                 size_t result_size)
{
    char floor[PATH_MAX];
    const char *cursor = path;
    size_t slashes = 0;

    while (*cursor == '/') {
        slashes++;
        cursor++;
    }
    if (strlen(session->cwd) >= result_size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(result, session->cwd);
    /* AmigaDOS uses each leading slash as a parent traversal: / is the
       parent and // is the grandparent. An empty relative path is the
       current directory, rather than inventing a POSIX-style dot component. */
    for (size_t index = 0; index < slashes; index++)
        pop_host_component(result);
    while (*cursor) {
        const char *end = strchr(cursor, '/');
        size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
        char component[PATH_MAX];

        if (length != 0) {
            if (length >= sizeof(component)) {
                errno = ENAMETOOLONG;
                return -1;
            }
            memcpy(component, cursor, length);
            component[length] = '\0';
            if (normalize_mapped_path(result, component, result,
                                      result_size) != 0)
                return -1;
        }
        if (!end)
            break;
        cursor = end;
        slashes = 0;
        while (*cursor == '/') {
            slashes++;
            cursor++;
        }
        /* The first slash separates components. Any remaining slashes walk
           back up from the component just resolved. */
        for (size_t index = 1; index < slashes; index++)
            pop_host_component(result);
    }
    if (ace_dos_devices_volume_root_for_path(session->cwd, floor,
                                              sizeof(floor)) == 0 &&
        normalize_mapped_path_beneath(floor, result, result, result_size) != 0)
        return -1;
    return 0;
}

static int resolve_path(struct broker_session *session, const char *input,
                        char *result, size_t result_size, bool host_path);

static int resolve_assign_target(struct broker_session *session,
                                 struct assign_entry *assign,
                                 char *result, size_t result_size)
{
    char candidate[PATH_MAX];
    size_t count = assign->target_count ? assign->target_count : 1;

    for (size_t index = 0; index < count; index++) {
        const char *target = assign->target_count ?
                             assign->targets[index] : assign->root;

        if (assign->type == ASSIGN_LATE ||
            assign->type == ASSIGN_NONBINDING) {
            if (resolve_path(session, target, candidate, sizeof(candidate),
                             false) != 0)
                continue;
        } else if (snprintf(candidate, sizeof(candidate), "%s", target) >=
                   (int)sizeof(candidate)) {
            errno = ENAMETOOLONG;
            return -1;
        }
        if (access(candidate, F_OK) != 0)
            continue;
        if (snprintf(result, result_size, "%s", candidate) >=
            (int)result_size) {
            errno = ENAMETOOLONG;
            return -1;
        }
        if (assign->type == ASSIGN_LATE) {
            strcpy(assign->root, candidate);
            assign->type = ASSIGN_DIRECTORY;
        }
        return 0;
    }
    errno = ENOENT;
    return -1;
}

static int resolve_path(struct broker_session *session, const char *input,
                        char *result, size_t result_size, bool host_path)
{
    const char *colon = strchr(input, ':');
    char base[PATH_MAX];
    const char *relative = input;

    if (host_path)
        return normalize_path(session->cwd, input, result, result_size);

    /* AROS treats a lone leading colon as the current volume, not as a
       relative filename.  GetDeviceProc(":") starts with the current
       directory's volume root; the text after the colon is then resolved
       beneath that root.  Keeping this here, at the volume-dispatch seam,
       makes the rule apply consistently to Lock(), Open(), MakeDir(), and
       every other DOS operation that accepts a path. */
    if (colon == input) {
        if (ace_dos_devices_volume_root_for_path(session->cwd, base,
                                                 sizeof(base)) != 0)
            return -1;
        relative = colon + 1;
        while (*relative == '/')
            relative++;
                return normalize_mapped_path_beneath(base, relative, result,
                                                     result_size);
    }

    if (colon && colon != input) {
        char assign_name[MAX_NAME];
        size_t name_length = (size_t)(colon - input);
        if (name_length < sizeof(assign_name)) {
            memcpy(assign_name, input, name_length);
            assign_name[name_length] = '\0';
            struct assign_entry *assign = find_assign(session, assign_name);
            /* CLIPS: is a boot-time virtual assign backed by the shared
             * clipboard store.  A session can outlive the broker revision
             * that created it, so repair this one special assign lazily when
             * an older session has lost it.  This keeps DeleteFile("CLIPS:n")
             * and clipboard.device pointed at the same unit instead of
             * silently resolving to a host filename named "CLIPS:n". */
            if (!assign && strcasecmp(assign_name, "CLIPS") == 0) {
                char clips_path[PATH_MAX];

                if (ace_clipboard_store_prepare() == 0 &&
                    ace_clipboard_store_root(clips_path,
                                             sizeof(clips_path)) == 0)
                    set_directory_assign(session, "CLIPS", clips_path);
                assign = find_assign(session, assign_name);
            }
            if (assign) {
                char clips_component[32];

                relative = colon + 1;
                while (*relative == '/')
                    relative++;
                if (resolve_assign_target(session, assign, base,
                                          sizeof(base)) != 0)
                    return -1;
                /* The real CLIPS: handler presents unit names as 0..255,
                 * while ACE keeps the backing files visibly separate from
                 * its lock files as clip0..clip255. Keep that spelling
                 * translation at the DOS handler seam so ordinary clients
                 * continue to use CLIPS:7. */
                if (strcasecmp(assign_name, "CLIPS") == 0 && *relative) {
                    char *end;
                    unsigned long unit;

                    errno = 0;
                    unit = strtoul(relative, &end, 10);
                    if (!errno && *end == '\0' && unit < 256) {
                        snprintf(clips_component, sizeof(clips_component),
                                 "clip%lu", unit);
                        relative = clips_component;
                    }
                }
                return normalize_mapped_path_beneath(base, relative, result,
                                                     result_size);
            }
            switch (ace_dos_devices_lookup(assign_name)) {
            case 1:
                if (ace_dos_devices_root(assign_name, base, sizeof(base)) != 0)
                    return -1;
                relative = colon + 1;
                while (*relative == '/')
                    relative++;
                return normalize_mapped_path_beneath(base, relative, result,
                                                     result_size);
            case -1:
                errno = EEXIST;
                return -1;
            default:
                break;
            }
        }
    }
    return normalize_amiga_path(session, relative, result, result_size);
}

static int canonical_command_path(struct broker_session *session,
                                  const char *input, char *result,
                                  size_t result_size)
{
    char resolved[PATH_MAX];
    struct stat information;
    char canonical[PATH_MAX];

    if (!input || !*input ||
        resolve_path(session, input, resolved, sizeof(resolved), false) != 0)
        return -1;
    if (stat(resolved, &information) != 0)
        return -1;
    if (!S_ISDIR(information.st_mode)) {
        errno = ENOTDIR;
        return -1;
    }
    if (realpath(resolved, canonical)) {
        if (strlen(canonical) >= sizeof(resolved)) {
            errno = ENAMETOOLONG;
            return -1;
        }
        strcpy(resolved, canonical);
    }
    if (strlen(resolved) >= result_size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(result, resolved);
    return 0;
}

static size_t find_command_path(struct broker_session *session,
                                const char *path)
{
    for (size_t index = 0; index < session->command_path_count; index++)
        if (strcmp(session->command_paths[index], path) == 0)
            return index;
    return session->command_path_count;
}

static int send_response(int fd, int status, const char *payload)
{
    struct amiga_broker_response response;
    size_t length = payload ? strlen(payload) : 0;

    if (length > UINT32_MAX)
        return -1;
    response.magic = AMIGA_BROKER_MAGIC;
    response.status = status;
    response.payload_length = (uint32_t)length;
    if (write_all(fd, &response, sizeof(response)) != 0)
        return -1;
    return length ? write_all(fd, payload, length) : 0;
}

/*
 * One connection. anchor is the index of the session this connection has
 * claimed with ATTACH, or -1 for a connection that is only using a session
 * it does not own.
 */
struct broker_connection {
    int fd;
    int anchor;
    uint64_t task_id;
};

static struct broker_task *find_task_id(uint64_t id)
{
    for (size_t index = 0; index < MAX_TASKS; index++)
        if (tasks[index].id == id)
            return &tasks[index];
    return NULL;
}

static void drop_task_connection(struct broker_connection *connection)
{
    struct broker_task *task;

    if (!connection->task_id)
        return;
    task = find_task_id(connection->task_id);
    if (task)
        memset(task, 0, sizeof(*task));
    connection->task_id = 0;
}

static int attach_task(struct broker_connection *connection,
                       struct broker_session *session, const char *name,
                       const char *pid_text, char *result, size_t result_size)
{
    char *end;
    long pid = strtol(pid_text, &end, 10);

    if (connection->task_id || !name[0] || strlen(name) >= MAX_NAME ||
        !pid_text[0] || *end || pid <= 0) {
        errno = EINVAL;
        return -1;
    }
    for (size_t index = 0; index < MAX_TASKS; index++) {
        struct broker_task *task = &tasks[index];

        if (task->id)
            continue;
        task->id = next_task_id++;
        if (!next_task_id)
            next_task_id = 1;
        task->fd = connection->fd;
        task->session = (int)(session - sessions);
        task->pid = (pid_t)pid;
        strcpy(task->name, name);
        connection->task_id = task->id;
        if (session->foreground_pid == task->pid)
            session->foreground_task = task->id;
        snprintf(result, result_size, "%llu",
                 (unsigned long long)task->id);
        return 0;
    }
    errno = ENOSPC;
    return -1;
}

/*
 * Serves one request on one connection.
 *
 * Returns 0 to keep the connection, -1 to drop it. A protocol error drops
 * it: the stream carries length-prefixed messages back to back, so once a
 * header has been misread there is no way to find where the next one starts.
 */
static int handle_client(struct broker_connection *connection)
{
    int fd = connection->fd;
    struct amiga_broker_request request;
    char *session_id = NULL;
    char *path = NULL;
    char *value = NULL;
    struct broker_session *session;
    char result[MAX_LIST_RESULT];
    int status = 0;
    int outcome = -1;

    if (read_message(fd, &request, sizeof(request)) != 1)
        return -1;
    if (request.magic != AMIGA_BROKER_MAGIC) {
        char message[160];

        /* The magic carries the peer's protocol version, so say which one
           turned up rather than only that the request was rejected.  The
           reply's own magic lets the client name this broker in turn. */
        snprintf(message, sizeof(message),
                 "broker protocol 0x%08x, client sent 0x%08x",
                 (unsigned)AMIGA_BROKER_PROTOCOL_VERSION,
                 (unsigned)amiga_broker_version_from_magic(request.magic));
        send_response(fd, EPROTO, message);
        return -1;
    }
    if (request.session_length > 4096 || request.path_length > PATH_MAX ||
        request.value_length > PATH_MAX) {
        send_response(fd, EPROTO, "invalid request");
        return -1;
    }

    session_id = calloc(request.session_length + 1, 1);
    path = calloc(request.path_length + 1, 1);
    value = calloc(request.value_length + 1, 1);
    if (!session_id || !path || !value ||
        read_all(fd, session_id, request.session_length) != 0 ||
        read_all(fd, path, request.path_length) != 0 ||
        read_all(fd, value, request.value_length) != 0) {
        send_response(fd, ENOMEM, "request allocation failed");
        goto done;
    }

    session = get_session(session_id);
    if (!session) {
        send_response(fd, ENOSPC, "too many sessions");
        outcome = 0;
        goto done;
    }
    outcome = 0;
    result[0] = '\0';

    switch (request.operation) {
    case AMIGA_BROKER_RESOLVE:
        if (resolve_path(session, path, result, sizeof(result), false) != 0)
            status = errno;
        break;

    case AMIGA_BROKER_RESOLVE_BENEATH:
        if (normalize_mapped_path_beneath(path, value, result,
                                          sizeof(result)) != 0)
            status = errno;
        break;

    case AMIGA_BROKER_NAMEFROMHOST:
        if (name_from_host_with_mappings(path, result, sizeof(result)) != 0)
            status = errno;
        break;

    case AMIGA_BROKER_GETCWD:
        strcpy(result, session->cwd);
        break;

    case AMIGA_BROKER_SETCWD: {
        struct stat information;
        if (resolve_path(session, path, result, sizeof(result),
                         (request.flags & AMIGA_BROKER_PATH_HOST) != 0) != 0 ||
            stat(result, &information) != 0 || !S_ISDIR(information.st_mode)) {
            status = errno ? errno : ENOTDIR;
        } else {
            strcpy(session->cwd, result);
        }
        break;
    }

    case AMIGA_BROKER_ASSIGN: {
        struct stat information;
        char assign_name[MAX_NAME];
        size_t assign_length = strlen(path);
        struct assign_entry *assign;

        if (assign_length && path[assign_length - 1] == ':')
            assign_length--;

        if (assign_length == 0 || assign_length >= sizeof(assign_name)) {
            status = EINVAL;
            break;
        }
        memcpy(assign_name, path, assign_length);
        assign_name[assign_length] = '\0';
        assign = find_assign(session, assign_name);

        if (request.flags & AMIGA_BROKER_ASSIGN_REMOVE) {
            if (assign)
                memset(assign, 0, sizeof(*assign));
            break;
        }
        if (request.flags & AMIGA_BROKER_ASSIGN_REMOVE_ITEM) {
            char target[PATH_MAX];

            if (!assign || resolve_path(session, value, target,
                                        sizeof(target), false) != 0) {
                status = errno ? errno : ENOENT;
                break;
            }
            for (size_t index = 0; index < assign->target_count; index++) {
                if (strcmp(assign->targets[index], target) == 0) {
                    memmove(&assign->targets[index],
                            &assign->targets[index + 1],
                            (assign->target_count - index - 1) *
                            sizeof(assign->targets[0]));
                    assign->target_count--;
                    break;
                }
            }
            if (assign->target_count == 0)
                memset(assign, 0, sizeof(*assign));
            break;
        }

        if (request.flags & (AMIGA_BROKER_ASSIGN_ADD |
                             AMIGA_BROKER_ASSIGN_PREPEND)) {
            char target[PATH_MAX];

            if (!assign || assign->target_count >= MAX_ASSIGN_TARGETS ||
                resolve_path(session, value, target, sizeof(target), false) != 0 ||
                stat(target, &information) != 0 ||
                !S_ISDIR(information.st_mode)) {
                status = errno ? errno : ENOSPC;
                break;
            }
            if (request.flags & AMIGA_BROKER_ASSIGN_PREPEND) {
                memmove(&assign->targets[1], &assign->targets[0],
                        assign->target_count * sizeof(assign->targets[0]));
                strcpy(assign->targets[0], target);
            } else {
                strcpy(assign->targets[assign->target_count], target);
            }
            assign->target_count++;
            strcpy(assign->root, assign->targets[0]);
            assign->type = ASSIGN_DIRECTORY;
            break;
        }

        assign = allocate_assign(session, assign_name);
        if (!assign) {
            status = ENOSPC;
            break;
        }
        if (request.flags & (AMIGA_BROKER_ASSIGN_PATH |
                             AMIGA_BROKER_ASSIGN_DEFER)) {
            if (strlen(value) >= sizeof(assign->root)) {
                status = ENAMETOOLONG;
                break;
            }
            memset(assign->targets, 0, sizeof(assign->targets));
            assign->target_count = 0;
            strcpy(assign->root, value);
            assign->type = (request.flags & AMIGA_BROKER_ASSIGN_PATH) ?
                           ASSIGN_NONBINDING : ASSIGN_LATE;
            break;
        }
        if (resolve_path(session, value, result, sizeof(result), false) != 0 ||
            stat(result, &information) != 0 || !S_ISDIR(information.st_mode)) {
            status = errno ? errno : ENOENT;
            break;
        }
        memset(assign->targets, 0, sizeof(assign->targets));
        assign->target_count = 1;
        strcpy(assign->targets[0], result);
        strcpy(assign->root, result);
        assign->type = ASSIGN_DIRECTORY;
        break;
    }

    case AMIGA_BROKER_LISTASSIGNS: {
        size_t used = 0;

        result[0] = '\0';
        for (size_t index = 0; index < MAX_ASSIGNS; index++) {
            struct assign_entry *assign = &session->assigns[index];
            int written;

            if (!assign->name[0])
                continue;
            written = snprintf(result + used, sizeof(result) - used,
                               "%s\t%d\t%s\n", assign->name, assign->type,
                               assign->root);
            if (written < 0 || (size_t)written >= sizeof(result) - used) {
                status = ENOSPC;
                break;
            }
            used += (size_t)written;
            for (size_t target = 1; target < assign->target_count; target++) {
                written = snprintf(result + used, sizeof(result) - used,
                                   "%s\t%d\t%s\n", assign->name,
                                   ASSIGN_DIRECTORY,
                                   assign->targets[target]);
                if (written < 0 ||
                    (size_t)written >= sizeof(result) - used) {
                    status = ENOSPC;
                    break;
                }
                used += (size_t)written;
            }
        }
        break;
    }

    case AMIGA_BROKER_LISTDOS:
        if (ace_dos_devices_list(result, sizeof(result)) != 0)
            status = errno;
        break;

    case AMIGA_BROKER_RELABEL:
        if (ace_dos_devices_relabel(path, value) != 0)
            status = errno;
        break;

    case AMIGA_BROKER_LISTPATH: {
        size_t used = 0;

        result[0] = '\0';
        for (size_t index = 0; index < session->command_path_count; index++) {
            int written = snprintf(result + used, sizeof(result) - used,
                                   "%s\n", session->command_paths[index]);

            if (written < 0 || (size_t)written >= sizeof(result) - used) {
                status = ENOSPC;
                break;
            }
            used += (size_t)written;
        }
        break;
    }

    case AMIGA_BROKER_PATH: {
        char canonical[PATH_MAX];

        if (request.flags & AMIGA_BROKER_PATH_RESET)
            session->command_path_count = 0;
        if (!path[0])
            break;
        if (canonical_command_path(session, path, canonical,
                                   sizeof(canonical)) != 0) {
            status = errno;
            break;
        }
        if (request.flags & AMIGA_BROKER_PATH_REMOVE) {
            size_t index = find_command_path(session, canonical);

            if (index == session->command_path_count) {
                status = ENOENT;
                break;
            }
            memmove(&session->command_paths[index],
                    &session->command_paths[index + 1],
                    (session->command_path_count - index - 1) *
                    sizeof(session->command_paths[0]));
            session->command_path_count--;
        } else if (find_command_path(session, canonical) ==
                   session->command_path_count) {
            if (session->command_path_count >= MAX_COMMAND_PATHS) {
                status = ENOSPC;
                break;
            }
            if (request.flags & AMIGA_BROKER_PATH_PREPEND) {
                memmove(&session->command_paths[1],
                        &session->command_paths[0],
                        session->command_path_count *
                        sizeof(session->command_paths[0]));
                strcpy(session->command_paths[0], canonical);
            } else {
                strcpy(session->command_paths[session->command_path_count],
                       canonical);
            }
            session->command_path_count++;
        }
        break;
    }

    /*
     * The shell claiming its session. From here the session's lifetime is
     * this connection's: it cannot be reclaimed while the connection is
     * open, and it is freed when the connection closes.
     *
     * A second ATTACH on the same connection is refused rather than counted,
     * so a connection can never hold more than the one reference that
     * closing it will give back.
     */
    case AMIGA_BROKER_ATTACH:
        if (connection->anchor >= 0) {
            status = EALREADY;
        } else {
            session->anchors++;
            connection->anchor = (int)(session - sessions);
        }
        break;

    case AMIGA_BROKER_TASK_ATTACH:
        if (attach_task(connection, session, path, value, result, sizeof(result)) != 0)
            status = errno;
        break;

    case AMIGA_BROKER_TASK_FIND:
        for (size_t index = 0; index < MAX_TASKS; index++) {
            if (tasks[index].id && strcmp(tasks[index].name, path) == 0) {
                snprintf(result, sizeof(result), "%llu",
                         (unsigned long long)tasks[index].id);
                break;
            }
        }
        if (!result[0])
            status = ESRCH;
        break;

    case AMIGA_BROKER_TASK_SIGNAL: {
        char *end;
        unsigned long long id = strtoull(path, &end, 10);
        unsigned long mask;
        struct broker_task *task;
        struct amiga_broker_task_signal signal;

        if (!path[0] || *end || id == 0) {
            status = EINVAL;
            break;
        }
        mask = strtoul(value, &end, 10);
        if (!value[0] || *end || mask > UINT32_MAX) {
            status = EINVAL;
            break;
        }
        task = find_task_id((uint64_t)id);
        if (!task) {
            status = ESRCH;
            break;
        }
        signal.magic = AMIGA_BROKER_MAGIC;
        signal.operation = AMIGA_BROKER_TASK_SIGNAL;
        signal.task_id = task->id;
        signal.signals = (uint32_t)mask;
        if (write_all(task->fd, &signal, sizeof(signal)) != 0)
            status = ESRCH;
        break;
    }

    case AMIGA_BROKER_TASK_SET_FOREGROUND: {
        char *end;
        long pid = strtol(path, &end, 10);

        if (!path[0] || *end || pid < 0) {
            status = EINVAL;
            break;
        }
        session->foreground_pid = (pid_t)pid;
        session->foreground_task = 0;
        if (pid) {
            for (size_t index = 0; index < MAX_TASKS; index++)
                if (tasks[index].id && tasks[index].pid == pid) {
                    session->foreground_task = tasks[index].id;
                    break;
                }
        } else {
            session->pending_foreground_signals = 0;
        }
        break;
    }

    case AMIGA_BROKER_TASK_BREAK_FOREGROUND: {
        char *end;
        unsigned long mask = strtoul(path, &end, 10);
        struct broker_task *task = find_task_id(session->foreground_task);
        struct amiga_broker_task_signal signal;

        if (!path[0] || *end || mask > UINT32_MAX) {
            status = EINVAL;
            break;
        }
        if (!task) {
            /* A foreground child need not use ACE's native DOS layer (for
               example, an Exec-only command can call SetSignal directly).
               Its process has no broker task socket, so hand its break to
               the host signal bridge instead of leaving it queued forever.
               Registered tasks still take the broker route below. */
            if (!session->foreground_pid) {
                status = ESRCH;
                break;
            }
            if ((mask & (1U << 12)) &&
                kill(session->foreground_pid, SIGUSR1) != 0)
                status = errno;
            if (!status && (mask & (1U << 13)) &&
                kill(session->foreground_pid, SIGUSR2) != 0)
                status = errno;
            if (!status && (mask & (1U << 14)) &&
                kill(session->foreground_pid, SIGRTMIN) != 0)
                status = errno;
            if (!status && (mask & (1U << 15)) &&
                kill(session->foreground_pid, SIGRTMIN + 1) != 0)
                status = errno;
            if (status == ESRCH)
                session->foreground_pid = 0;
            break;
        }
        signal.magic = AMIGA_BROKER_MAGIC;
        signal.operation = AMIGA_BROKER_TASK_SIGNAL;
        signal.task_id = task->id;
        signal.signals = (uint32_t)mask;
        if (write_all(task->fd, &signal, sizeof(signal)) != 0)
            status = ESRCH;
        break;
    }

    case AMIGA_BROKER_STATUS: {
        char executable[PATH_MAX];
        ssize_t length;
        size_t used = 0;
        size_t live_sessions = 0;
        size_t live_tasks = 0;
        int written;

        length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
        if (length > 0)
            executable[length] = '\0';
        else
            strcpy(executable, "(unknown)");

        for (size_t i = 0; i < MAX_SESSIONS; i++)
            if (sessions[i].in_use)
                live_sessions++;
        for (size_t i = 0; i < MAX_TASKS; i++)
            if (tasks[i].id)
                live_tasks++;

        written = snprintf(result, sizeof(result),
                           "protocol\t0x%08x\n"
                           "executable\t%s\n"
                           "pid\t%ld\n"
                           "uptime\t%lld\n"
                           "socket\t%s\n"
                           "sys\t%s\n"
                           "sessions\t%zu\n"
                           "tasks\t%zu\n",
                           (unsigned)AMIGA_BROKER_PROTOCOL_VERSION,
                           executable, (long)getpid(),
                           (long long)(time(NULL) - broker_started),
                           socket_path, system_root,
                           live_sessions, live_tasks);
        if (written < 0 || (size_t)written >= sizeof(result)) {
            status = ENAMETOOLONG;
            break;
        }
        used = (size_t)written;

        for (size_t i = 0; i < MAX_SESSIONS; i++) {
            if (!sessions[i].in_use)
                continue;
            written = snprintf(result + used, sizeof(result) - used,
                               "session\t%s\t%u\t%s\n", sessions[i].id,
                               sessions[i].anchors, sessions[i].cwd);
            if (written < 0 || (size_t)written >= sizeof(result) - used)
                break; /* report what fits; this is a diagnostic, not a feed */
            used += (size_t)written;
        }
        (void)used;
        break;
    }

    case AMIGA_BROKER_TASK_LIST: {
        size_t used = 0;

        for (size_t index = 0; index < MAX_TASKS; index++) {
            int written;

            if (!tasks[index].id)
                continue;
            written = snprintf(result + used, sizeof(result) - used,
                               "%llu\t%ld\t%s\n",
                               (unsigned long long)tasks[index].id,
                               (long)tasks[index].pid, tasks[index].name);
            if (written < 0 || (size_t)written >= sizeof(result) - used) {
                status = EOVERFLOW;
                break;
            }
            used += (size_t)written;
        }
        break;
    }

    case AMIGA_BROKER_GETVAR: {
        struct variable_entry *variable = lookup_variable(session, path,
                                                           request.flags);
        if (variable)
            strcpy(result, variable->value);
        else
            status = ENOENT;
        break;
    }

    case AMIGA_BROKER_SETVAR: {
        struct variable_entry *variable;
        bool local = (request.flags & AMIGA_BROKER_VAR_LOCAL) != 0;
        bool global = (request.flags & AMIGA_BROKER_VAR_GLOBAL) != 0;

        if (request.flags & AMIGA_BROKER_VAR_SAVE)
            status = ENOTSUP;
        else if (local && global)
            status = EINVAL;
        else {
            if (!local && !global)
                local = true;
            if (strlen(path) >= MAX_NAME || strlen(value) >= MAX_VALUE)
                status = EOVERFLOW;
        }
        if (!status) {
            variable = allocate_variable(global ? global_vars : session->local_vars,
                                         path,
                                         (request.flags & AMIGA_BROKER_VAR_ALIAS) ?
                                         AMIGA_BROKER_VAR_ALIAS :
                                         AMIGA_BROKER_VAR_VARIABLE);
            if (!variable)
                status = ENOSPC;
            else
                strcpy(variable->value, value);
        }
        break;
    }

    case AMIGA_BROKER_DELVAR: {
        struct variable_entry *variable = lookup_variable(session, path,
                                                           request.flags);
        if (request.flags & AMIGA_BROKER_VAR_SAVE)
            status = ENOTSUP;
        else if (!variable)
            status = ENOENT;
        else
            memset(variable, 0, sizeof(*variable));
        break;
    }

    case AMIGA_BROKER_LISTVARS: {
        size_t used = 0;
        bool local = (request.flags & AMIGA_BROKER_VAR_LOCAL) != 0;
        bool global = (request.flags & AMIGA_BROKER_VAR_GLOBAL) != 0;

        if (!local && !global)
            local = true;
        if (local && append_variable_list(result, sizeof(result), &used,
                                          session->local_vars,
                                          request.flags) != 0)
            status = EOVERFLOW;
        if (!status && global &&
            append_variable_list(result, sizeof(result), &used, global_vars,
                                 request.flags) != 0)
            status = EOVERFLOW;
        break;
    }

    case AMIGA_BROKER_GETRESULT:
        snprintf(result, sizeof(result), "%ld,%ld",
                 (long)session->return_code, (long)session->result2);
        break;

    case AMIGA_BROKER_GETCLI:
        snprintf(result, sizeof(result), "%ld\n%ld\n%ld\n%s",
                 (long)session->return_code, (long)session->result2,
                 (long)session->fail_level, session->prompt);
        break;

    case AMIGA_BROKER_SETFAILLEVEL: {
        char *end;
        long fail_level = strtol(path, &end, 10);
        if (!path[0] || *end || fail_level < INT32_MIN || fail_level > INT32_MAX)
            status = EINVAL;
        else
            session->fail_level = (int32_t)fail_level;
        break;
    }

    case AMIGA_BROKER_SETPROMPT:
        if (strlen(path) >= sizeof(session->prompt))
            status = EOVERFLOW;
        else
            strcpy(session->prompt, path);
        break;

    case AMIGA_BROKER_CLONESESSION: {
        struct broker_session *child;
        if (!path[0] || strlen(path) >= sizeof(session->id))
            status = EINVAL;
        else if (strcmp(path, session->id) == 0)
            status = EINVAL;
        else {
            if (find_session(path)) {
                status = EEXIST;
                break;
            }
            child = get_session(path);
            if (!child)
                status = ENOSPC;
            else {
                strcpy(child->cwd, session->cwd);
                memcpy(child->command_paths, session->command_paths,
                       sizeof(child->command_paths));
                child->command_path_count = session->command_path_count;
                memcpy(child->assigns, session->assigns, sizeof(child->assigns));
                memcpy(child->local_vars, session->local_vars,
                       sizeof(child->local_vars));
                child->return_code = session->return_code;
                child->result2 = session->result2;
                child->fail_level = session->fail_level;
                strcpy(child->prompt, session->prompt);
            }
        }
        break;
    }

    case AMIGA_BROKER_SETRESULT: {
        char *end;
        long return_code = strtol(path, &end, 10);
        long result2;
        if (!path[0] || *end || !value[0]) {
            status = EINVAL;
            break;
        }
        result2 = strtol(value, &end, 10);
        if (*end || return_code < INT32_MIN || return_code > INT32_MAX ||
            result2 < INT32_MIN || result2 > INT32_MAX) {
            status = EINVAL;
        } else {
            session->return_code = (int32_t)return_code;
            session->result2 = (int32_t)result2;
        }
        break;
    }

    default:
        status = EINVAL;
        break;
    }
    if (status) {
        if (send_response(fd, status, strerror(status)) != 0)
            outcome = -1;
    } else if (request.operation == AMIGA_BROKER_RESOLVE ||
               request.operation == AMIGA_BROKER_RESOLVE_BENEATH ||
               request.operation == AMIGA_BROKER_NAMEFROMHOST ||
               request.operation == AMIGA_BROKER_GETCWD ||
               request.operation == AMIGA_BROKER_GETVAR ||
               request.operation == AMIGA_BROKER_LISTVARS ||
               request.operation == AMIGA_BROKER_GETCLI ||
               request.operation == AMIGA_BROKER_GETRESULT ||
               request.operation == AMIGA_BROKER_LISTDOS ||
               request.operation == AMIGA_BROKER_LISTASSIGNS ||
               request.operation == AMIGA_BROKER_LISTPATH ||
               request.operation == AMIGA_BROKER_TASK_ATTACH ||
               request.operation == AMIGA_BROKER_TASK_FIND ||
               request.operation == AMIGA_BROKER_TASK_LIST ||
               request.operation == AMIGA_BROKER_STATUS) {
        if (send_response(fd, 0, result) != 0)
            outcome = -1;
    } else {
        if (send_response(fd, 0, NULL) != 0)
            outcome = -1;
    }
    if (outcome == 0 && request.operation == AMIGA_BROKER_TASK_ATTACH &&
        session->foreground_task == connection->task_id &&
        session->pending_foreground_signals) {
        struct amiga_broker_task_signal signal;

        signal.magic = AMIGA_BROKER_MAGIC;
        signal.operation = AMIGA_BROKER_TASK_SIGNAL;
        signal.task_id = connection->task_id;
        signal.signals = session->pending_foreground_signals;
        session->pending_foreground_signals = 0;
        if (write_all(fd, &signal, sizeof(signal)) != 0)
            outcome = -1;
    }

done:
    free(session_id);
    free(path);
    free(value);
    return outcome;
}

/*
 * Held connections, and the one place a session's lifetime is decided.
 *
 * The cap is a backstop, not a working limit: a connection lasts as long as
 * the process behind it, so this is one per live shell plus one per command
 * currently running, against a session table of sixty-four.
 */
#define MAX_CONNECTIONS 256

static struct broker_connection connections[MAX_CONNECTIONS];
static struct pollfd poll_fds[MAX_CONNECTIONS + 1];
static size_t connection_count;

/*
 * Closes a connection and, if it was the last one holding the session it
 * claimed, frees the session. This is the whole point of holding the
 * connection open: the kernel reports the close whether the shell exited
 * normally, was killed, or crashed, so a session's current directory,
 * assigns and variables are released exactly when their owner is gone
 * instead of being guessed at.
 */
static void drop_connection(size_t index)
{
    struct broker_connection *connection = &connections[index];

    drop_task_connection(connection);

    if (connection->anchor >= 0) {
        struct broker_session *session = &sessions[connection->anchor];

        if (session->anchors && !--session->anchors)
            release_session(session);
    }
    close(connection->fd);
    connections[index] = connections[--connection_count];
}

static void accept_connection(void)
{
    int fd = accept(server_fd, NULL, NULL);

    if (fd < 0)
        return;
    /* The broker execs mount(8) and udisksctl to bring volumes up
     * (dos_devices.c); without this every one of them would inherit every
     * client connection the broker is holding. */
    (void)fcntl(fd, F_SETFD, FD_CLOEXEC);
    if (connection_count == MAX_CONNECTIONS) {
        fprintf(stderr, "ace-broker: connection table full, refusing\n");
        close(fd);
        return;
    }
    connections[connection_count].fd = fd;
    connections[connection_count].anchor = -1;
    connections[connection_count].task_id = 0;
    connection_count++;
}

static int lock_fd = -1;
static bool already_running;

/*
 * Takes the exclusive lock that marks this process as the broker for this
 * socket path. The lock lives in a file beside the socket and is released by
 * the kernel when the process exits, however it exits, so a broker that
 * crashes does not leave the path permanently claimed.
 *
 * The client's own start lock (<socket>.start.lock, in broker_client.c) is a
 * different file: that one serialises "should I spawn a broker?" and is held
 * only across the spawn, and a broker taking it would deadlock against the
 * client waiting for that broker to come up.
 */
static int acquire_socket_lock(void)
{
    char lock_path[PATH_MAX];

    if (snprintf(lock_path, sizeof(lock_path), "%s.lock", socket_path) >=
        (int)sizeof(lock_path)) {
        fprintf(stderr, "broker socket path is too long\n");
        return -1;
    }
    lock_fd = open(lock_path, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (lock_fd < 0) {
        perror("broker lock");
        return -1;
    }
    if (flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
        if (errno == EWOULDBLOCK) {
            already_running = true;
            fprintf(stderr, "ace-broker: already running for %s\n",
                    socket_path);
        } else {
            perror("broker lock");
        }
        close(lock_fd);
        lock_fd = -1;
        return -1;
    }

    /* Record who holds it, so broker-stop can find this process without
     * guessing from command lines. The lock, not the contents, is what makes
     * this authoritative: a stale pid cannot be read out of an unlocked file
     * by anything that checks the lock first. */
    {
        char line[32];
        int length = snprintf(line, sizeof(line), "%ld\n", (long)getpid());

        if (ftruncate(lock_fd, 0) == 0 && length > 0)
            (void)!write(lock_fd, line, (size_t)length);
    }
    return 0;
}

static void stop_server(int signal_number)
{
    (void)signal_number;
    ace_dos_devices_shutdown();
    if (server_fd >= 0)
        close(server_fd);
    unlink(socket_path);
    _exit(0);
}

int main(int argc, char **argv)
{
    struct sockaddr_un address;

    if (argc == 2 && strcmp(argv[1], "--print-socket") == 0) {
        /* So the start/stop scripts do not have to reimplement the naming
           rule in shell and drift away from it. */
        printf("%s\n", amiga_broker_socket_path());
        return 0;
    }
    if (argc > 2 || (argc == 2 && argv[1][0] == '\0')) {
        fprintf(stderr, "usage: %s [socket-path | --print-socket]\n", argv[0]);
        return 2;
    }
    socket_path = amiga_broker_socket_path();
    if (argc == 2)
        socket_path = argv[1];

    /*
     * One broker per socket, enforced by the kernel rather than by
     * convention. The lock is held for the broker's whole life, so a second
     * broker started on the same path finds it taken and leaves; without
     * this, the unlink() below silently stole the path from the live broker
     * and stranded it -- still listening, on a socket nobody could reach any
     * more, together with every session it was holding.
     *
     * Reaching this point means the lock is ours, so any socket still on
     * disk belongs to a broker that is gone, and clearing it is safe.
     */
    if (acquire_socket_lock() != 0)
        return already_running ? 0 : 1;

    broker_started = time(NULL);
    ace_dos_devices_discover();
    resolve_system_root();
    restore_environment_archive();

    signal(SIGINT, stop_server);
    signal(SIGTERM, stop_server);
    signal(SIGPIPE, SIG_IGN);

    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }
    unlink(socket_path);
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    if (strlen(socket_path) >= sizeof(address.sun_path)) {
        fprintf(stderr, "broker socket path is too long\n");
        return 1;
    }
    strcpy(address.sun_path, socket_path);
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        chmod(socket_path, 0600) != 0 || listen(server_fd, 16) != 0) {
        perror("broker setup");
        return 1;
    }

    for (;;) {
        nfds_t watched = 1;

        poll_fds[0].fd = server_fd;
        poll_fds[0].events = POLLIN;
        poll_fds[0].revents = 0;
        for (size_t i = 0; i < connection_count; i++) {
            poll_fds[watched].fd = connections[i].fd;
            poll_fds[watched].events = POLLIN;
            poll_fds[watched].revents = 0;
            watched++;
        }

        if (poll(poll_fds, watched, -1) < 0) {
            if (errno == EINTR)
                continue;
            perror("poll");
            return 1;
        }

        /*
         * Connections first, and backwards, so that dropping one can fill
         * its slot from the end of the table without disturbing an index
         * still to be visited.
         */
        for (size_t i = connection_count; i-- > 0;) {
            short events = poll_fds[i + 1].revents;

            if (!events)
                continue;
            if ((events & POLLIN) && handle_client(&connections[i]) == 0)
                continue;
            drop_connection(i);
        }

        if (poll_fds[0].revents & POLLIN)
            accept_connection();
    }
}
