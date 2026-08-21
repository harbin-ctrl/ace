/*
 * The broker's end of the mediator channel.
 *
 * The shape of this file is decided by one fact about pkexec: it rewrites the
 * environment and does not carry arbitrary descriptors across the exec, so
 * the mediator cannot simply be handed one end of a socketpair.  The broker
 * therefore listens on a private socket and the mediator connects back to it.
 *
 * That inverts the usual arrangement and it is worth being explicit about
 * what still holds.  The socket lives in a directory only this user can enter,
 * under a name containing sixteen random bytes, so it is not a rendezvous
 * anybody can guess their way to.  But the path is not the security: the
 * broker checks SO_PEERCRED and refuses a peer that is not root, and the
 * mediator checks that the broker's HELLO carries the nonce it was launched
 * with and that its peer is the user it was started for.  A path can be read;
 * credentials cannot be claimed.
 */

#define _GNU_SOURCE

#include "ace_mediator_client.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__linux__)
#include <sys/random.h>
#endif

/*
 * How long to wait for the mediator to appear.
 *
 * Generous because what happens in this window is a human being reading an
 * authentication dialog and typing a password, and a timeout tuned to machine
 * speeds would cancel their authorisation out from under them.  It exists at
 * all only so that a pkexec that never returns cannot wedge the broker
 * forever.
 */
#define MEDIATOR_ACCEPT_TIMEOUT_MS 120000
/* Once the channel is up, both peers are programs.  A mediator that has not
   answered a control request in this long is not thinking. */
#define MEDIATOR_REPLY_TIMEOUT_MS 30000
/* After SHUTDOWN, how long to let the mediator finish unmounting before
   giving up on reaping it.  Not a deadline for the mediator's own cleanup --
   it exits when it is done -- only for how long the broker waits to watch. */
#define MEDIATOR_EXIT_TIMEOUT_MS 5000

struct ace_mediator {
    int fd;
    pid_t launched_pid;
    pid_t peer_pid;
    uint32_t capabilities;
    uint32_t authorisation_seconds;
    uint64_t next_request_id;
    /* Set once the far end is known to be gone, so that a caller which keeps
       going after a failure gets a clean refusal rather than a second attempt
       on a dead descriptor. */
    int closed;
};

static int fill_random(void *buffer, size_t length)
{
#if defined(__linux__)
    ssize_t got = getrandom(buffer, length, 0);

    if (got == (ssize_t)length)
        return 0;
#endif
    {
        int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
        ssize_t got;

        if (fd < 0)
            return -1;
        got = read(fd, buffer, length);
        close(fd);
        return got == (ssize_t)length ? 0 : -1;
    }
}

/* Where a private, per-user, session-lifetime socket belongs.  Same rule the
   broker's own socket follows, and for the same reason: XDG_RUNTIME_DIR is
   cleared when the user's last session ends, so nothing outlives the login it
   belonged to. */
static int runtime_directory(char *result, size_t result_size)
{
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    int written;

    if (runtime && *runtime)
        written = snprintf(result, result_size, "%s/ace", runtime);
    else
        written = snprintf(result, result_size, "/tmp/ace-%lu",
                           (unsigned long)getuid());
    if (written < 0 || (size_t)written >= result_size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    if (mkdir(result, 0700) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

/* The mediator lives beside whoever launched it.  ACE installs as a set that
   stays together, so "beside me" is the only answer that cannot pick up a
   different build from elsewhere on PATH. */
static int mediator_program(char *result, size_t result_size)
{
    char self[PATH_MAX];
    ssize_t length = readlink("/proc/self/exe", self, sizeof(self) - 1);
    char *slash;

    if (length <= 0)
        return -1;
    self[length] = '\0';
    slash = strrchr(self, '/');
    if (!slash)
        return -1;
    *slash = '\0';
    if (snprintf(result, result_size, "%s/ace-mediator", self) >=
        (int)result_size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

/*
 * How to become root, and why there is a choice.
 *
 * pkexec is the right answer on a desktop: it is polkit's, it explains itself
 * to the user, and it can be given an ACE-specific action later without any
 * of this changing.  But a machine configured for noninteractive sudo can
 * elevate without a human in the loop, and that is what makes the elevated
 * path testable at all -- an elevated channel that only ever runs when
 * somebody types a password is an elevated channel that never runs in a test
 * suite.
 *
 * ace_modes.c already prefers a sudo probe that executes true over `sudo -n
 * -v`, because -v fails under some otherwise working NOPASSWD configurations.
 * Same probe here, for the same reason.
 *
 * Neither is given -E.  The environment does not cross into the mediator, by
 * both tools' default and by preference: a root process that inherits its
 * idea of the world from an unprivileged one has a user-supplied idea of its
 * own purpose.
 */
enum elevation { ELEVATE_NONE, ELEVATE_SUDO, ELEVATE_PKEXEC };

#define ACE_POLKIT_ACTION "org.ace.Ace.mediator"

static int probe_succeeds(char *const arguments[])
{
    pid_t child = fork();
    int status;

    if (child < 0)
        return 0;
    if (child == 0) {
        int null_fd = open("/dev/null", O_RDWR);

        if (null_fd >= 0) {
            dup2(null_fd, STDIN_FILENO);
            dup2(null_fd, STDOUT_FILENO);
            dup2(null_fd, STDERR_FILENO);
            if (null_fd > STDERR_FILENO)
                close(null_fd);
        }
        execv(arguments[0], arguments);
        _exit(127);
    }
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR)
            return 0;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static enum elevation choose_elevation(void)
{
    char *const sudo_probe[] = {
        (char *)"/usr/bin/sudo", (char *)"-n", (char *)"/usr/bin/true", NULL
    };

    if (access("/usr/bin/sudo", X_OK) == 0 && probe_succeeds(sudo_probe))
        return ELEVATE_SUDO;
    if (access("/usr/bin/pkexec", X_OK) == 0)
        return ELEVATE_PKEXEC;
    return ELEVATE_NONE;
}

/*
 * Authorise the broker's process for ACE's action before asking pkexec to
 * start the mediator.  pkexec's built-in action is intentionally generic;
 * this explicit check gives desktop policy a stable ACE-specific action and
 * lets installations grant or audit ACE without broadening the policy for
 * arbitrary pkexec commands.  sudo remains the first choice when its
 * noninteractive probe succeeds, so a configured NOPASSWD setup never nags.
 */
static int polkit_authorise(pid_t broker_pid)
{
    char process[32];
    char *const arguments[] = {
        (char *)"/usr/bin/pkcheck", (char *)"--action-id",
        (char *)ACE_POLKIT_ACTION, (char *)"--process", process,
        (char *)"--allow-user-interaction", NULL
    };

    if (snprintf(process, sizeof(process), "%ld", (long)broker_pid) >=
        (int)sizeof(process)) {
        errno = EOVERFLOW;
        return -1;
    }
    return probe_succeeds(arguments) ? 0 : -1;
}

static int polkit_action_available(void)
{
    char *const arguments[] = {
        (char *)"/usr/bin/pkaction", (char *)"--action-id",
        (char *)ACE_POLKIT_ACTION, NULL
    };

    return probe_succeeds(arguments);
}

static int send_record(int fd, const void *header, size_t header_size,
                       const void *payload, size_t payload_length)
{
    struct iovec io[2];
    struct msghdr message;
    ssize_t sent;

    memset(&message, 0, sizeof(message));
    io[0].iov_base = (void *)header;
    io[0].iov_len = header_size;
    io[1].iov_base = (void *)payload;
    io[1].iov_len = payload_length;
    message.msg_iov = io;
    message.msg_iovlen = payload && payload_length ? 2 : 1;
    do {
        sent = sendmsg(fd, &message, MSG_NOSIGNAL);
    } while (sent < 0 && errno == EINTR);
    if (sent < 0)
        return -1;
    /* SOCK_SEQPACKET preserves record boundaries, so a short send is not a
       thing to loop over: it means the record did not fit and the peer would
       see a truncated one. */
    if ((size_t)sent != header_size + (payload ? payload_length : 0)) {
        errno = EMSGSIZE;
        return -1;
    }
    return 0;
}

static int wait_readable(int fd, int timeout_ms)
{
    struct pollfd waiting;
    int ready;

    waiting.fd = fd;
    waiting.events = POLLIN;
    waiting.revents = 0;
    do {
        ready = poll(&waiting, 1, timeout_ms);
    } while (ready < 0 && errno == EINTR);
    if (ready == 0) {
        errno = ETIMEDOUT;
        return -1;
    }
    return ready < 0 ? -1 : 0;
}

/*
 * Receive one record, with any descriptor that came with it.
 *
 * MSG_CMSG_CLOEXEC matters more than it looks: the broker forks command
 * processes, and a received descriptor that stayed open across an exec would
 * hand a protected object to a program that never asked for one and does not
 * know it has it.
 */
static ssize_t receive_record(int fd, void *header, size_t header_size,
                              void *payload, size_t payload_size,
                              int *received_fd)
{
    struct iovec io[2];
    struct msghdr message;
    union {
        char bytes[CMSG_SPACE(sizeof(int) * ACE_MEDIATOR_MAX_FDS)];
        struct cmsghdr align;
    } control;
    struct cmsghdr *entry;
    ssize_t got;

    if (received_fd)
        *received_fd = -1;
    memset(&message, 0, sizeof(message));
    io[0].iov_base = header;
    io[0].iov_len = header_size;
    io[1].iov_base = payload;
    io[1].iov_len = payload_size;
    message.msg_iov = io;
    message.msg_iovlen = payload && payload_size ? 2 : 1;
    message.msg_control = control.bytes;
    message.msg_controllen = sizeof(control.bytes);
    do {
        got = recvmsg(fd, &message, MSG_CMSG_CLOEXEC);
    } while (got < 0 && errno == EINTR);
    if (got < 0)
        return -1;
    if (got == 0) {
        /* EOF: the mediator is gone.  Not an error to diagnose further --
           this is how a mediator that died is always reported. */
        errno = ECONNRESET;
        return -1;
    }
    for (entry = CMSG_FIRSTHDR(&message); entry;
         entry = CMSG_NXTHDR(&message, entry)) {
        int count;

        if (entry->cmsg_level != SOL_SOCKET || entry->cmsg_type != SCM_RIGHTS)
            continue;
        count = (int)((entry->cmsg_len - CMSG_LEN(0)) / sizeof(int));
        /* A peer that sent more descriptors than the protocol allows is
           either broken or probing.  Close them all rather than keeping the
           ones that fit. */
        if (count > (int)ACE_MEDIATOR_MAX_FDS || !received_fd) {
            for (int index = 0; index < count; index++) {
                int spare;

                memcpy(&spare, CMSG_DATA(entry) + sizeof(int) * index,
                       sizeof(spare));
                close(spare);
            }
            continue;
        }
        if (count == 1)
            memcpy(received_fd, CMSG_DATA(entry), sizeof(*received_fd));
    }
    if (message.msg_flags & MSG_TRUNC) {
        if (received_fd && *received_fd >= 0) {
            close(*received_fd);
            *received_fd = -1;
        }
        errno = EMSGSIZE;
        return -1;
    }
    if ((size_t)got < header_size) {
        errno = EPROTO;
        return -1;
    }
    return got;
}

static void reap_child(pid_t child, int timeout_ms)
{
    int elapsed = 0;

    if (child <= 0)
        return;
    while (elapsed < timeout_ms) {
        pid_t done = waitpid(child, NULL, WNOHANG);

        if (done == child || (done < 0 && errno != EINTR))
            return;
        usleep(20000);
        elapsed += 20;
    }
    /* Deliberately no kill().  A privileged process is not the broker's to
       signal, and a mediator still unmounting is doing something that must
       not be interrupted half way.  Leaving it is the safe outcome; it exits
       on its own once the channel is closed. */
}

static struct ace_mediator *fail(struct ace_mediator *mediator, int listener,
                                 const char *path)
{
    int saved = errno;

    if (listener >= 0)
        close(listener);
    if (path)
        unlink(path);
    if (mediator) {
        if (mediator->fd >= 0)
            close(mediator->fd);
        reap_child(mediator->launched_pid, MEDIATOR_EXIT_TIMEOUT_MS);
        free(mediator);
    }
    errno = saved;
    return NULL;
}

struct ace_mediator *ace_mediator_start_as(uint32_t wanted_capabilities,
                                           uid_t expected_uid,
                                           const char *program, int elevate)
{
    struct ace_mediator *mediator;
    struct sockaddr_un address;
    struct ucred peer;
    socklen_t peer_size = sizeof(peer);
    struct ace_mediator_hello hello;
    struct ace_mediator_hello_reply reply;
    char directory[PATH_MAX];
    char path[sizeof(address.sun_path)];
    char resolved[PATH_MAX];
    uint8_t nonce[ACE_MEDIATOR_NONCE_LENGTH];
    char nonce_text[ACE_MEDIATOR_NONCE_LENGTH * 2 + 1];
    int listener = -1;
    int channel = -1;
    enum elevation method = ELEVATE_NONE;
    pid_t child;

    if (elevate) {
        method = choose_elevation();
        if (method == ELEVATE_NONE) {
            /* Nothing here can make this session privileged, which is a
               reportable outcome and not a crash: what the user gets is the
               unprivileged session they would have had without --root. */
            errno = EACCES;
            return NULL;
        }
    }

    if (fill_random(nonce, sizeof(nonce)) != 0)
        return NULL;
    for (size_t index = 0; index < sizeof(nonce); index++)
        snprintf(nonce_text + index * 2, 3, "%02x", nonce[index]);
    if (runtime_directory(directory, sizeof(directory)) != 0)
        return NULL;
    if (snprintf(path, sizeof(path), "%s/mediator-%s.sock", directory,
                 nonce_text) >= (int)sizeof(path)) {
        errno = ENAMETOOLONG;
        return NULL;
    }
    if (!program) {
        if (mediator_program(resolved, sizeof(resolved)) != 0)
            return NULL;
        program = resolved;
    }

    mediator = calloc(1, sizeof(*mediator));
    if (!mediator)
        return NULL;
    mediator->fd = -1;
    mediator->next_request_id = 1;

    listener = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (listener < 0)
        return fail(mediator, -1, NULL);
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    strcpy(address.sun_path, path);
    unlink(path);
    if (bind(listener, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        listen(listener, 1) != 0)
        return fail(mediator, listener, NULL);

    child = fork();
    if (child < 0)
        return fail(mediator, listener, path);
    if (child == 0) {
        /* The mediator must not be in the terminal's foreground process
           group: a Ctrl-C at the shell is meant to reach ACE and become a
           CANCEL message, not to be delivered as a signal to a root process
           in the middle of an unmount. */
        setsid();
        switch (method) {
        case ELEVATE_SUDO:
            execl("/usr/bin/sudo", "sudo", "-n", program, path, (char *)NULL);
            break;
        case ELEVATE_PKEXEC:
            if (polkit_action_available()) {
                if (polkit_authorise(getppid()) != 0)
                    break;
                execl("/usr/bin/pkexec", "pkexec",
                      "--disable-internal-agent", program, path,
                      (char *)NULL);
            } else {
                /* A per-user install may not be in polkit's compiled-in
                   action directory. Keep the generic pkexec fallback usable
                   there; system installs use the ACE-specific action above. */
                execl("/usr/bin/pkexec", "pkexec", program, path,
                      (char *)NULL);
            }
            break;
        case ELEVATE_NONE:
            execl(program, program, path, (char *)NULL);
            break;
        }
        _exit(127);
    }
    mediator->launched_pid = child;

    if (wait_readable(listener, MEDIATOR_ACCEPT_TIMEOUT_MS) != 0)
        return fail(mediator, listener, path);
    do {
        channel = accept4(listener, NULL, NULL, SOCK_CLOEXEC);
    } while (channel < 0 && errno == EINTR);
    if (channel < 0)
        return fail(mediator, listener, path);
    mediator->fd = channel;
    close(listener);
    listener = -1;
    /* The rendezvous has served its purpose.  Removing it now means a late
       or second connection has nothing to reach, and that the name does not
       linger describing a channel that is already established. */
    unlink(path);

    if (getsockopt(channel, SOL_SOCKET, SO_PEERCRED, &peer, &peer_size) != 0 ||
        peer_size != sizeof(peer)) {
        errno = EPROTO;
        return fail(mediator, -1, NULL);
    }
    /* The check the whole arrangement rests on.  Anyone may find a socket;
       nobody may claim a uid. */
    if (peer.uid != expected_uid || peer.pid <= 0) {
        errno = EPERM;
        return fail(mediator, -1, NULL);
    }
    mediator->peer_pid = peer.pid;

    memset(&hello, 0, sizeof(hello));
    hello.magic = ACE_MEDIATOR_MAGIC;
    hello.version = ACE_MEDIATOR_PROTOCOL_VERSION;
    hello.broker_pid = (int32_t)getpid();
    hello.requested_capabilities = wanted_capabilities;
    memcpy(hello.nonce, nonce, sizeof(nonce));
    if (send_record(channel, &hello, sizeof(hello), NULL, 0) != 0)
        return fail(mediator, -1, NULL);

    if (wait_readable(channel, MEDIATOR_REPLY_TIMEOUT_MS) != 0)
        return fail(mediator, -1, NULL);
    if (receive_record(channel, &reply, sizeof(reply), NULL, 0, NULL) < 0)
        return fail(mediator, -1, NULL);
    /* Checked on the first field either side reads, rather than after a
       payload has been misparsed -- which between a user process and a root
       one is not a diagnostic nicety. */
    if (reply.magic != ACE_MEDIATOR_MAGIC ||
        reply.version != ACE_MEDIATOR_PROTOCOL_VERSION) {
        errno = EPROTO;
        return fail(mediator, -1, NULL);
    }
    if (reply.status != ACE_MEDIATOR_OK) {
        errno = reply.status == ACE_MEDIATOR_UNAUTHORISED ? EPERM : EPROTO;
        return fail(mediator, -1, NULL);
    }
    mediator->capabilities = reply.granted_capabilities;
    mediator->authorisation_seconds = reply.authorisation_seconds;
    return mediator;
}

struct ace_mediator *ace_mediator_start(uint32_t wanted_capabilities)
{
    return ace_mediator_start_as(wanted_capabilities, 0, NULL, 1);
}

uint32_t ace_mediator_capabilities(const struct ace_mediator *mediator)
{
    return mediator ? mediator->capabilities : 0;
}

uint32_t ace_mediator_authorisation_seconds(const struct ace_mediator *mediator)
{
    return mediator ? mediator->authorisation_seconds : 0;
}

pid_t ace_mediator_pid(const struct ace_mediator *mediator)
{
    return mediator ? mediator->peer_pid : -1;
}

int ace_mediator_request(struct ace_mediator *mediator,
                         struct ace_mediator_request *request,
                         const void *payload,
                         struct ace_mediator_response *response,
                         void *reply, size_t reply_size, int *received_fd)
{
    ssize_t got;

    if (!mediator || !request || !response) {
        errno = EINVAL;
        return -1;
    }
    if (mediator->closed || mediator->fd < 0) {
        errno = ENOTCONN;
        return -1;
    }
    if (request->payload_length > ACE_MEDIATOR_MAX_PAYLOAD) {
        errno = EMSGSIZE;
        return -1;
    }
    request->magic = ACE_MEDIATOR_MAGIC;
    request->request_id = mediator->next_request_id++;
    if (send_record(mediator->fd, request, sizeof(*request), payload,
                    request->payload_length) != 0) {
        mediator->closed = 1;
        return -1;
    }
    if (wait_readable(mediator->fd, MEDIATOR_REPLY_TIMEOUT_MS) != 0) {
        mediator->closed = 1;
        return -1;
    }
    got = receive_record(mediator->fd, response, sizeof(*response), reply,
                         reply_size, received_fd);
    if (got < 0) {
        mediator->closed = 1;
        return -1;
    }
    if (response->magic != ACE_MEDIATOR_MAGIC ||
        response->request_id != request->request_id) {
        /* A reply that does not answer the request just sent means the two
           sides have lost their place in the conversation.  There is no
           resynchronising from here that is worth trusting with root. */
        mediator->closed = 1;
        errno = EPROTO;
        return -1;
    }
    return 0;
}

struct ace_mediator *ace_mediator_access_worker(struct ace_mediator *volume)
{
    struct ace_mediator_request request;
    struct ace_mediator_response response;
    struct ace_mediator *worker;
    int32_t worker_pid = -1;
    int received = -1;

    memset(&request, 0, sizeof(request));
    request.operation = ACE_MEDIATOR_SPAWN_ACCESS;
    if (ace_mediator_request(volume, &request, NULL, &response, &worker_pid,
                             sizeof(worker_pid), &received) != 0)
        return NULL;
    if (response.status != ACE_MEDIATOR_OK) {
        if (received >= 0)
            close(received);
        errno = response.status == ACE_MEDIATOR_REFUSED ? ENOTCONN : EPROTO;
        return NULL;
    }
    /* A success that arrived without its descriptor is not a success.  On a
       privileged channel, "I meant to send you one" is not a thing to
       assume. */
    if (received < 0 || !(response.flags & ACE_MEDIATOR_FLAG_HAS_FD)) {
        errno = EPROTO;
        return NULL;
    }
    worker = calloc(1, sizeof(*worker));
    if (!worker) {
        close(received);
        return NULL;
    }
    worker->fd = received;
    worker->next_request_id = 1;
    worker->capabilities = ACE_MEDIATOR_CAP_ACCESS;
    worker->authorisation_seconds = volume->authorisation_seconds;
    /* No launched_pid: this process was forked by the mediator, not by us, so
       it is not ours to wait for.  Its parent reaps it. */
    worker->launched_pid = -1;
    /*
     * Taken from the reply rather than from SO_PEERCRED.  A socketpair
     * reports the credentials of the process that created it, so asking the
     * kernel here would name the supervisor and look right while being
     * useless.  This pid is diagnostic only -- nothing is authorised by it --
     * so the supervisor's word is the appropriate source.
     */
    worker->peer_pid = response.payload_length == sizeof(worker_pid)
                           ? (pid_t)worker_pid : -1;
    return worker;
}

int ace_mediator_prepare_view(struct ace_mediator *mediator, char *root,
                              size_t root_size)
{
    struct ace_mediator_request request;
    struct ace_mediator_response response;

    if (!mediator || !root || root_size == 0) {
        errno = EINVAL;
        return -1;
    }
    memset(&request, 0, sizeof(request));
    request.operation = ACE_MEDIATOR_VOLUME_PREPARE_VIEW;
    if (ace_mediator_request(mediator, &request, NULL, &response, root,
                             root_size, NULL) != 0)
        return -1;
    if (response.status != ACE_MEDIATOR_OK) {
        errno = response.host_errno ? response.host_errno : EPERM;
        return -1;
    }
    if (response.payload_length == 0 ||
        response.payload_length > root_size ||
        root[response.payload_length - 1] != '\0') {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int ace_mediator_mount(struct ace_mediator *mediator, const char *kernel_name,
                       const char *filesystem_type, char *view_path,
                       size_t view_path_size)
{
    struct ace_mediator_request request;
    struct ace_mediator_response response;
    char payload[ACE_MEDIATOR_MAX_PAYLOAD];
    size_t name_length;
    size_t type_length;

    if (!mediator || !kernel_name || !filesystem_type || !view_path) {
        errno = EINVAL;
        return -1;
    }
    name_length = strlen(kernel_name);
    type_length = strlen(filesystem_type);
    if (name_length + type_length + 2 > sizeof(payload)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(payload, kernel_name, name_length + 1);
    memcpy(payload + name_length + 1, filesystem_type, type_length + 1);

    memset(&request, 0, sizeof(request));
    request.operation = ACE_MEDIATOR_VOLUME_MOUNT;
    request.payload_length = (uint32_t)(name_length + type_length + 2);
    if (ace_mediator_request(mediator, &request, payload, &response, view_path,
                             view_path_size, NULL) != 0)
        return -1;
    if (response.status != ACE_MEDIATOR_OK) {
        /* Distinguished so the caller can skip what this build will not mount
           and stop for what it could not. */
        errno = response.status == ACE_MEDIATOR_UNSUPPORTED
                    ? ENOTSUP
                    : (response.host_errno ? response.host_errno : EPERM);
        return -1;
    }
    if (response.payload_length == 0 ||
        response.payload_length > view_path_size ||
        view_path[response.payload_length - 1] != '\0') {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int ace_mediator_access(struct ace_mediator *worker, uint32_t operation,
                        const char *path, const char *second, uint32_t flags,
                        uint32_t mode, int *received_fd)
{
    struct ace_mediator_request request;
    struct ace_mediator_response response;
    char payload[ACE_MEDIATOR_MAX_PAYLOAD];
    size_t first_length;
    size_t total;

    if (received_fd)
        *received_fd = -1;
    if (!worker || !path || !*path || *path == '/') {
        errno = EINVAL;
        return -1;
    }
    first_length = strlen(path) + 1;
    total = first_length;
    if (second) {
        if (!*second || *second == '/') {
            errno = EINVAL;
            return -1;
        }
        total += strlen(second) + 1;
    }
    if (total > sizeof(payload)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(payload, path, first_length);
    if (second)
        memcpy(payload + first_length, second, strlen(second) + 1);

    memset(&request, 0, sizeof(request));
    request.operation = operation;
    request.flags = flags;
    request.mode = mode;
    request.first_path_length = second ? (uint32_t)first_length : 0;
    request.payload_length = (uint32_t)total;
    if (ace_mediator_request(worker, &request, payload, &response, NULL, 0,
                             received_fd) != 0)
        return -1;
    if (response.status != ACE_MEDIATOR_OK && response.host_errno)
        errno = response.host_errno;
    return response.status;
}

int ace_mediator_ping(struct ace_mediator *mediator)
{
    struct ace_mediator_request request;
    struct ace_mediator_response response;

    memset(&request, 0, sizeof(request));
    request.operation = ACE_MEDIATOR_PING;
    if (ace_mediator_request(mediator, &request, NULL, &response, NULL, 0,
                             NULL) != 0)
        return -1;
    return response.status == ACE_MEDIATOR_OK ? 0 : -1;
}

int ace_mediator_cancel(struct ace_mediator *mediator, uint64_t request_id)
{
    struct ace_mediator_request request;
    struct ace_mediator_response response;

    memset(&request, 0, sizeof(request));
    request.operation = ACE_MEDIATOR_CANCEL;
    /* The id being cancelled travels in device_id rather than request_id:
       this message is itself a request and needs an id of its own, and
       overloading the field that correlates replies would make a cancel
       impossible to answer. */
    request.device_id = request_id;
    if (ace_mediator_request(mediator, &request, NULL, &response, NULL, 0,
                             NULL) != 0)
        return -1;
    return response.status == ACE_MEDIATOR_OK ? 0 : -1;
}

int ace_mediator_drop_privilege(struct ace_mediator *mediator)
{
    struct ace_mediator_request request;
    struct ace_mediator_response response;
    int outcome;

    memset(&request, 0, sizeof(request));
    request.operation = ACE_MEDIATOR_DROP_PRIVILEGE;
    outcome = ace_mediator_request(mediator, &request, NULL, &response, NULL, 0,
                                   NULL);
    if (outcome == 0 && response.status != ACE_MEDIATOR_OK)
        outcome = -1;
    if (mediator)
        mediator->closed = 1;
    return outcome;
}

void ace_mediator_close(struct ace_mediator *mediator)
{
    if (!mediator)
        return;
    if (!mediator->closed && mediator->fd >= 0) {
        struct ace_mediator_request request;

        memset(&request, 0, sizeof(request));
        request.magic = ACE_MEDIATOR_MAGIC;
        request.operation = ACE_MEDIATOR_SHUTDOWN;
        request.request_id = mediator->next_request_id++;
        /* Best effort.  If it does not arrive, closing the descriptor below
           gives the mediator EOF, which means the same thing and is the path
           a crashed broker takes anyway. */
        (void)send_record(mediator->fd, &request, sizeof(request), NULL, 0);
    }
    if (mediator->fd >= 0)
        close(mediator->fd);
    reap_child(mediator->launched_pid, MEDIATOR_EXIT_TIMEOUT_MS);
    free(mediator);
}
