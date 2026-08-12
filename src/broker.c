#define _POSIX_C_SOURCE 200809L

#include "broker_protocol.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#define MAX_SESSIONS 64
#define MAX_ASSIGNS  64
#define MAX_VARS     128
#define MAX_NAME     64
#define MAX_VALUE    4096
#define MAX_LIST_RESULT AMIGA_BROKER_MAX_PAYLOAD
#define DEFAULT_FAIL_LEVEL 10
#define DEFAULT_PROMPT "%N.%S> "

struct variable_entry {
    char name[MAX_NAME];
    char value[MAX_VALUE];
    uint32_t type;
};

struct assign_entry {
    char name[MAX_NAME];
    char root[PATH_MAX];
};

struct broker_session {
    bool in_use;
    char id[128];
    char cwd[PATH_MAX];
    struct assign_entry assigns[MAX_ASSIGNS];
    struct variable_entry local_vars[MAX_VARS];
    int32_t return_code;
    int32_t result2;
    int32_t fail_level;
    char prompt[MAX_VALUE];
};

static struct broker_session sessions[MAX_SESSIONS];
static struct variable_entry global_vars[MAX_VARS];
static int server_fd = -1;
static const char *socket_path = "/tmp/ace-broker.sock";

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

static struct broker_session *get_session(const char *id)
{
    struct broker_session *free_slot = NULL;

    for (size_t i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].in_use && strcmp(sessions[i].id, id) == 0)
            return &sessions[i];
        if (!sessions[i].in_use && !free_slot)
            free_slot = &sessions[i];
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
    return free_slot;
}

static struct broker_session *find_session(const char *id)
{
    for (size_t i = 0; i < MAX_SESSIONS; i++)
        if (sessions[i].in_use && strcmp(sessions[i].id, id) == 0)
            return &sessions[i];
    return NULL;
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

static struct assign_entry *find_assign(struct broker_session *session,
                                         const char *name)
{
    for (size_t i = 0; i < MAX_ASSIGNS; i++)
        if (session->assigns[i].name[0] &&
            strcasecmp(session->assigns[i].name, name) == 0)
            return &session->assigns[i];
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

static int resolve_path(struct broker_session *session, const char *input,
                        char *result, size_t result_size)
{
    const char *colon = strchr(input, ':');
    char base[PATH_MAX];
    const char *relative = input;

    if (colon && colon != input) {
        char assign_name[MAX_NAME];
        size_t name_length = (size_t)(colon - input);
        if (name_length < sizeof(assign_name)) {
            memcpy(assign_name, input, name_length);
            assign_name[name_length] = '\0';
            struct assign_entry *assign = find_assign(session, assign_name);
            if (assign) {
                strcpy(base, assign->root);
                relative = colon + 1;
                while (*relative == '/')
                    relative++;
                return normalize_path(base, relative, result, result_size);
            }
        }
    }
    return normalize_path(session->cwd, relative, result, result_size);
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

static void handle_client(int fd)
{
    struct amiga_broker_request request;
    char *session_id = NULL;
    char *path = NULL;
    char *value = NULL;
    struct broker_session *session;
    char result[MAX_LIST_RESULT];
    int status = 0;

    if (read_all(fd, &request, sizeof(request)) != 0 ||
        request.magic != AMIGA_BROKER_MAGIC ||
        request.session_length > 4096 || request.path_length > PATH_MAX ||
        request.value_length > PATH_MAX) {
        send_response(fd, EPROTO, "invalid request");
        return;
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
        goto done;
    }

    switch (request.operation) {
    case AMIGA_BROKER_RESOLVE:
        if (resolve_path(session, path, result, sizeof(result)) != 0)
            status = errno;
        break;

    case AMIGA_BROKER_GETCWD:
        strcpy(result, session->cwd);
        break;

    case AMIGA_BROKER_SETCWD: {
        struct stat information;
        if (resolve_path(session, path, result, sizeof(result)) != 0 ||
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
        if (assign_length && path[assign_length - 1] == ':')
            assign_length--;
        struct assign_entry *assign = NULL;

        if (assign_length < sizeof(assign_name)) {
            memcpy(assign_name, path, assign_length);
            assign_name[assign_length] = '\0';
            assign = find_assign(session, assign_name);
        }
        if (!assign) {
            for (size_t i = 0; i < MAX_ASSIGNS; i++) {
                if (!session->assigns[i].name[0]) {
                    assign = &session->assigns[i];
                    break;
                }
            }
        }
        if (!assign || !assign_name[0] || assign_length >= MAX_NAME ||
            resolve_path(session, value, result, sizeof(result)) != 0 ||
            stat(result, &information) != 0 || !S_ISDIR(information.st_mode)) {
            status = errno ? errno : ENOSPC;
        } else {
            strcpy(assign->name, assign_name);
            strcpy(assign->root, result);
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
        send_response(fd, status, strerror(status));
    } else if (request.operation == AMIGA_BROKER_RESOLVE ||
               request.operation == AMIGA_BROKER_GETCWD ||
               request.operation == AMIGA_BROKER_GETVAR ||
               request.operation == AMIGA_BROKER_LISTVARS ||
               request.operation == AMIGA_BROKER_GETCLI ||
               request.operation == AMIGA_BROKER_GETRESULT) {
        send_response(fd, 0, result);
    } else {
        send_response(fd, 0, NULL);
    }

done:
    free(session_id);
    free(path);
    free(value);
}

static void stop_server(int signal_number)
{
    (void)signal_number;
    if (server_fd >= 0)
        close(server_fd);
    unlink(socket_path);
    _exit(0);
}

int main(int argc, char **argv)
{
    struct sockaddr_un address;

    if (argc > 2 || (argc == 2 && argv[1][0] == '\0')) {
        fprintf(stderr, "usage: %s [socket-path]\n", argv[0]);
        return 2;
    }
    if (argc == 2)
        socket_path = argv[1];

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
        int client = accept(server_fd, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR)
                continue;
            perror("accept");
            return 1;
        }
        handle_client(client);
        close(client);
    }
}
