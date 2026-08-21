#define _POSIX_C_SOURCE 200809L

#include "ace_modes.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int configured_root = -1;
static int configured_device_view = -1;
static uid_t configured_owner = (uid_t)-1;

static int parse_uid(const char *text, uid_t *result)
{
    char *end;
    unsigned long value;

    if (!text || !*text)
        return -1;
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno || *end || (uid_t)value != value)
        return -1;
    *result = (uid_t)value;
    return 0;
}

static uid_t initial_owner_uid(void)
{
    uid_t owner;
    const char *value = getenv(ACE_MODE_OWNER_UID_ENV);

    if (parse_uid(value, &owner) == 0)
        return owner;
    if (geteuid() == 0) {
        if (parse_uid(getenv("SUDO_UID"), &owner) == 0)
            return owner;
        if (parse_uid(getenv("PKEXEC_UID"), &owner) == 0)
            return owner;
    }
    return getuid();
}

int ace_mode_parse(int *argc, char **argv, struct ace_mode_options *options)
{
    int output = 1;

    if (!argc || !argv || !options || *argc < 1) {
        errno = EINVAL;
        return -1;
    }
    memset(options, 0, sizeof(*options));
    for (int index = 1; index < *argc; index++) {
        if (strcmp(argv[index], "--root") == 0)
            options->root = 1;
        else if (strcmp(argv[index], "--user") == 0)
            options->user = 1;
        else if (strcmp(argv[index], "--deviceview") == 0)
            options->device_view = 1;
        else if (strcmp(argv[index], "--mountview") == 0)
            options->mount_view = 1;
        else
            argv[output++] = argv[index];
    }
    argv[output] = NULL;
    *argc = output;
    if ((options->root && options->user) ||
        (options->device_view && options->mount_view)) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

static int run_and_wait(char *const argv[])
{
    pid_t child = fork();
    int status;

    if (child < 0)
        return -1;
    if (child == 0) {
        execv(argv[0], argv);
        _exit(127);
    }
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR)
            return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static void add_environment(char **arguments, size_t *used, size_t capacity,
                            char storage[][PATH_MAX + 64], size_t *stored,
                            const char *name, const char *value)
{
    int written;

    if (!value || !*value || *used + 1 >= capacity || *stored >= 16)
        return;
    written = snprintf(storage[*stored], PATH_MAX + 64, "%s=%s", name,
                       value);
    if (written < 0 || written >= PATH_MAX + 64)
        return;
    arguments[(*used)++] = storage[(*stored)++];
}

static int exec_elevated(const char *elevator, int noninteractive,
                         int argc, char **argv, uid_t owner)
{
    size_t capacity = (size_t)argc + 40;
    char **arguments = calloc(capacity, sizeof(*arguments));
    char storage[16][PATH_MAX + 64];
    char owner_text[32];
    char executable[PATH_MAX];
    ssize_t executable_length;
    size_t stored = 0;
    size_t used = 0;
    static const char *const preserved[] = {
        "ACE_SYS_DIR", "ACE_BROKER_SOCKET", "ACE_BROKER_BINARY",
        "ACE_MOUNT_ROOT", "ACE_SESSION", "XDG_RUNTIME_DIR",
        "WAYLAND_DISPLAY", "DISPLAY", "XAUTHORITY",
        "DBUS_SESSION_BUS_ADDRESS", "PATH"
    };

    if (!arguments)
        return -1;
    executable_length = readlink("/proc/self/exe", executable,
                                 sizeof(executable) - 1);
    if (executable_length < 0 ||
        (size_t)executable_length >= sizeof(executable) - 1) {
        free(arguments);
        return -1;
    }
    executable[executable_length] = '\0';
    arguments[used++] = (char *)elevator;
    if (strstr(elevator, "sudo") != NULL && noninteractive)
        arguments[used++] = "-n";
    arguments[used++] = "/usr/bin/env";
    snprintf(owner_text, sizeof(owner_text), "%lu", (unsigned long)owner);
    add_environment(arguments, &used, capacity, storage, &stored,
                    ACE_MODE_OWNER_UID_ENV, owner_text);
    for (size_t index = 0;
         index < sizeof(preserved) / sizeof(preserved[0]); index++)
        add_environment(arguments, &used, capacity, storage, &stored,
                        preserved[index], getenv(preserved[index]));
    for (int index = 0; index < argc && used + 1 < capacity; index++)
        arguments[used++] = index == 0 ? executable : argv[index];
    arguments[used] = NULL;
    execv(elevator, arguments);
    free(arguments);
    return -1;
}

static int run_elevated_and_wait(const char *elevator, int noninteractive,
                                 int argc, char **argv, uid_t owner)
{
    pid_t child = fork();
    int status;

    if (child < 0)
        return -1;
    if (child == 0) {
        (void)exec_elevated(elevator, noninteractive, argc, argv, owner);
        _exit(127);
    }
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR)
            return -1;
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
        _exit(0);
    errno = EACCES;
    return -1;
}

int ace_mode_elevate_if_needed(int argc, char **argv,
                               const struct ace_mode_options *options)
{
    uid_t owner;
    /* `sudo -n -v` fails with some otherwise NOPASSWD configurations;
     * executing true is the reliable noninteractive credential probe. */
    char *const sudo_probe[] = {
        "/usr/bin/sudo", "-n", "/usr/bin/true", NULL
    };
    char *const sudo_prompt[] = { "/usr/bin/sudo", "-v", NULL };

    if (!options || !options->root || geteuid() == 0)
        return 0;
    owner = initial_owner_uid();
    if (access("/usr/bin/sudo", X_OK) == 0 &&
        run_and_wait(sudo_probe) == 0)
        return exec_elevated("/usr/bin/sudo", 1, argc, argv, owner);
    if (access("/usr/bin/sudo", X_OK) == 0 && isatty(STDIN_FILENO) &&
        run_and_wait(sudo_prompt) == 0)
        return exec_elevated("/usr/bin/sudo", 1, argc, argv, owner);
    if (access("/usr/bin/pkexec", X_OK) == 0)
        return run_elevated_and_wait("/usr/bin/pkexec", 0, argc, argv,
                                     owner);
    errno = EACCES;
    return -1;
}

static int configure(const struct ace_mode_options *options, int identity_only)
{
    int root = identity_only && options && options->root ? 1 :
               identity_only && options && options->user ? 0 : geteuid() == 0;
    int device_view;
    const char *inherited_view = getenv(ACE_MODE_VIEW_ENV);
    char owner_text[32];

    if (!options) {
        errno = EINVAL;
        return -1;
    }
    if (options->user &&
        ((!identity_only && root) || (identity_only && geteuid() == 0))) {
        errno = EPERM;
        return -1;
    }
    if (!identity_only && options->root && !root) {
        errno = EACCES;
        return -1;
    }
    if (options->device_view)
        device_view = 1;
    else if (options->mount_view)
        device_view = 0;
    else if (inherited_view && strcmp(inherited_view, "device") == 0)
        device_view = 1;
    else if (inherited_view && strcmp(inherited_view, "mount") == 0)
        device_view = 0;
    else
        device_view = root;
    if (device_view && !root) {
        errno = EACCES;
        return -1;
    }
    configured_root = root;
    configured_device_view = device_view;
    configured_owner = initial_owner_uid();
    snprintf(owner_text, sizeof(owner_text), "%lu",
             (unsigned long)configured_owner);
    if (setenv(ACE_MODE_PRIVILEGE_ENV, root ? "root" : "user", 1) != 0 ||
        setenv(ACE_MODE_VIEW_ENV, device_view ? "device" : "mount", 1) != 0 ||
        setenv(ACE_MODE_OWNER_UID_ENV, owner_text, 1) != 0)
        return -1;
    return 0;
}

int ace_mode_configure(const struct ace_mode_options *options)
{
    return configure(options, 0);
}

int ace_mode_configure_identity(const struct ace_mode_options *options)
{
    return configure(options, 1);
}

int ace_mode_is_root(void)
{
    if (configured_root < 0)
        configured_root = geteuid() == 0;
    return configured_root;
}

int ace_mode_is_device_view(void)
{
    const char *value;

    if (configured_device_view >= 0)
        return configured_device_view;
    value = getenv(ACE_MODE_VIEW_ENV);
    configured_device_view = value ? strcmp(value, "device") == 0
                                   : geteuid() == 0;
    return configured_device_view;
}

uid_t ace_mode_owner_uid(void)
{
    if (configured_owner == (uid_t)-1)
        configured_owner = initial_owner_uid();
    return configured_owner;
}

const char *ace_mode_privilege_switch(void)
{
    return ace_mode_is_root() ? "--root" : "--user";
}

const char *ace_mode_view_switch(void)
{
    return ace_mode_is_device_view() ? "--deviceview" : "--mountview";
}
