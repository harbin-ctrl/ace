#define _POSIX_C_SOURCE 200809L

#include "broker_client.h"
#include "broker_protocol.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

static const char *broker_socket_path(void)
{
    const char *path = getenv("ACE_BROKER_SOCKET");
    return path && *path ? path : "/tmp/ace-broker.sock";
}

static const char *broker_session(void)
{
    const char *session = getenv("ACE_SESSION");
    return session && *session ? session : "default";
}

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
        if (written == 0)
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
        if (received == 0)
            return -1;
        bytes += received;
        length -= (size_t)received;
    }
    return 0;
}

static int connect_broker(void)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un address;

    if (fd < 0)
        return -1;

    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    if (strlen(broker_socket_path()) >= sizeof(address.sun_path)) {
        close(fd);
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(address.sun_path, broker_socket_path());

    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        int error = errno;
        close(fd);
        errno = error;
        return -1;
    }
    return fd;
}

static int broker_is_reachable(void)
{
    int fd = connect_broker();

    if (fd < 0)
        return -1;
    close(fd);
    return 0;
}

static int broker_executable(char *result, size_t result_size)
{
    const char *configured = getenv("ACE_BROKER_BINARY");
    char executable[PATH_MAX];
    char *slash;
    ssize_t length;

    if (configured && *configured) {
        if (strlen(configured) >= result_size)
            return -1;
        strcpy(result, configured);
        return 0;
    }
    length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    if (length < 0 || (size_t)length >= sizeof(executable) - 1)
        return -1;
    executable[length] = '\0';
    slash = strrchr(executable, '/');
    if (!slash)
        return -1;
    *slash = '\0';
    if (snprintf(result, result_size, "%s/ace-broker", executable) >=
        (int)result_size)
        return -1;
    return 0;
}

static void broker_sleep(void)
{
    struct timespec delay = {0, 20000000L};

    while (nanosleep(&delay, &delay) != 0 && errno == EINTR)
        ;
}

static int broker_wait_until_reachable(void)
{
    for (int attempt = 0; attempt < 100; attempt++) {
        if (broker_is_reachable() == 0)
            return 0;
        broker_sleep();
    }
    return -1;
}

int native_broker_ensure(void)
{
    char lock_path[PATH_MAX];
    char executable[PATH_MAX];
    int lock_fd;
    pid_t child;

    if (broker_is_reachable() == 0)
        return 0;
    if (snprintf(lock_path, sizeof(lock_path), "%s.start.lock",
                 broker_socket_path()) >= (int)sizeof(lock_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    lock_fd = open(lock_path, O_CREAT | O_RDWR, 0600);
    if (lock_fd < 0)
        return -1;
    if (flock(lock_fd, LOCK_EX) != 0) {
        close(lock_fd);
        return -1;
    }
    if (broker_is_reachable() == 0) {
        flock(lock_fd, LOCK_UN);
        close(lock_fd);
        return 0;
    }
    if (broker_executable(executable, sizeof(executable)) != 0) {
        flock(lock_fd, LOCK_UN);
        close(lock_fd);
        errno = ENOENT;
        return -1;
    }

    child = fork();
    if (child == 0) {
        int null_fd;

        if (setsid() < 0)
            _exit(127);
        null_fd = open("/dev/null", O_RDWR);
        if (null_fd >= 0) {
            (void)dup2(null_fd, STDIN_FILENO);
            (void)dup2(null_fd, STDOUT_FILENO);
            (void)dup2(null_fd, STDERR_FILENO);
            if (null_fd > STDERR_FILENO)
                close(null_fd);
        }
        execl(executable, executable, broker_socket_path(), (char *)NULL);
        _exit(127);
    }
    if (child < 0 || broker_wait_until_reachable() != 0) {
        if (child > 0)
            (void)kill(child, SIGTERM);
        flock(lock_fd, LOCK_UN);
        close(lock_fd);
        errno = ECONNREFUSED;
        return -1;
    }
    flock(lock_fd, LOCK_UN);
    close(lock_fd);
    return 0;
}

static int broker_request(uint32_t operation, const char *path,
                          const char *value, uint32_t flags,
                          char *result, size_t result_size)
{
    const char *session = broker_session();
    size_t session_length = strlen(session);
    size_t path_length = path ? strlen(path) : 0;
    size_t value_length = value ? strlen(value) : 0;
    struct amiga_broker_request request;
    struct amiga_broker_response response;
    int fd;

    if (session_length > UINT32_MAX || path_length > UINT32_MAX ||
        value_length > UINT32_MAX) {
        errno = EOVERFLOW;
        return -1;
    }

    fd = connect_broker();
    if (fd < 0)
        return -1;

    request.magic = AMIGA_BROKER_MAGIC;
    request.operation = operation;
    request.session_length = (uint32_t)session_length;
    request.path_length = (uint32_t)path_length;
    request.value_length = (uint32_t)value_length;
    request.flags = flags;

    if (write_all(fd, &request, sizeof(request)) != 0 ||
        write_all(fd, session, session_length) != 0 ||
        write_all(fd, path, path_length) != 0 ||
        write_all(fd, value, value_length) != 0 ||
        read_all(fd, &response, sizeof(response)) != 0) {
        close(fd);
        return -1;
    }

    if (response.magic != AMIGA_BROKER_MAGIC ||
        response.payload_length > AMIGA_BROKER_MAX_PAYLOAD) {
        close(fd);
        errno = EPROTO;
        return -1;
    }

    if (response.status != 0) {
        int error = response.status;
        char ignored[AMIGA_BROKER_MAX_PAYLOAD];
        if (response.payload_length &&
            read_all(fd, ignored, response.payload_length) != 0) {
            close(fd);
            return -1;
        }
        close(fd);
        errno = error;
        return -1;
    }

    if (response.payload_length >= result_size) {
        char ignored[AMIGA_BROKER_MAX_PAYLOAD];
        read_all(fd, ignored, response.payload_length);
        close(fd);
        errno = ENAMETOOLONG;
        return -1;
    }
    if (response.payload_length && read_all(fd, result, response.payload_length) != 0) {
        close(fd);
        return -1;
    }
    if (result)
        result[response.payload_length] = '\0';
    close(fd);
    return 0;
}

int native_broker_resolve_path(const char *path, char *result, size_t result_size)
{
    return broker_request(AMIGA_BROKER_RESOLVE, path, NULL, 0, result, result_size);
}

int native_broker_getcwd(char *result, size_t result_size)
{
    return broker_request(AMIGA_BROKER_GETCWD, NULL, NULL, 0, result, result_size);
}

int native_broker_setcwd(const char *path)
{
    char ignored[1];
    return broker_request(AMIGA_BROKER_SETCWD, path, NULL, 0, ignored, sizeof(ignored));
}

int native_broker_assign(const char *name, const char *path)
{
    char ignored[1];
    return broker_request(AMIGA_BROKER_ASSIGN, name, path, 0, ignored, sizeof(ignored));
}

int native_broker_getvar(const char *name, uint32_t flags,
                         char *result, size_t result_size)
{
    return broker_request(AMIGA_BROKER_GETVAR, name, NULL, flags,
                          result, result_size);
}

int native_broker_setvar(const char *name, const char *value, uint32_t flags)
{
    char ignored[1];
    return broker_request(AMIGA_BROKER_SETVAR, name, value, flags,
                          ignored, sizeof(ignored));
}

int native_broker_deletevar(const char *name, uint32_t flags)
{
    char ignored[1];
    return broker_request(AMIGA_BROKER_DELVAR, name, NULL, flags,
                          ignored, sizeof(ignored));
}

int native_broker_listvars(uint32_t flags, char *result, size_t result_size)
{
    return broker_request(AMIGA_BROKER_LISTVARS, NULL, NULL, flags,
                          result, result_size);
}

int native_broker_getcli(char *result, size_t result_size)
{
    return broker_request(AMIGA_BROKER_GETCLI, NULL, NULL, 0,
                          result, result_size);
}

int native_broker_setfaillevel(int32_t fail_level)
{
    char value[32];
    char ignored[1];

    snprintf(value, sizeof(value), "%ld", (long)fail_level);
    return broker_request(AMIGA_BROKER_SETFAILLEVEL, value, NULL, 0,
                          ignored, sizeof(ignored));
}

int native_broker_setprompt(const char *prompt)
{
    char ignored[1];
    return broker_request(AMIGA_BROKER_SETPROMPT, prompt, NULL, 0,
                          ignored, sizeof(ignored));
}

int native_broker_clone_session(const char *child_session)
{
    char ignored[1];
    return broker_request(AMIGA_BROKER_CLONESESSION, child_session, NULL, 0,
                          ignored, sizeof(ignored));
}

int native_broker_getresult(char *result, size_t result_size)
{
    return broker_request(AMIGA_BROKER_GETRESULT, NULL, NULL, 0,
                          result, result_size);
}

int native_broker_setresult(int32_t return_code, int32_t result2)
{
    char return_text[32];
    char result_text[32];
    char ignored[1];

    snprintf(return_text, sizeof(return_text), "%ld", (long)return_code);
    snprintf(result_text, sizeof(result_text), "%ld", (long)result2);
    return broker_request(AMIGA_BROKER_SETRESULT, return_text, result_text, 0,
                          ignored, sizeof(ignored));
}
