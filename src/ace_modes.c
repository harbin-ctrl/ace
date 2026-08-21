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
        else if (strcmp(argv[index], "--user") == 0 ||
                 strcmp(argv[index], "--deviceview") == 0 ||
                 strcmp(argv[index], "--mountview") == 0) {
            errno = EINVAL;
            return -1;
        }
        else
            argv[output++] = argv[index];
    }
    argv[output] = NULL;
    *argc = output;
    return 0;
}





/*
 * ace_mode_elevate_if_needed() used to live here.  It probed for sudo, fell
 * back to pkexec, and re-executed this program as root with the switches it
 * started with.
 *
 * It is gone because --root no longer means "become root".  It means this
 * session may ask a separate privileged process for narrowly scoped help.
 * The shell, the console, the commands, and the broker stay the user's own
 * processes for their whole lives -- which is the only way they can keep
 * using the user's session bus, display connection, configuration and HOME,
 * none of which a root process can borrow.
 *
 * The elevation that remains lives in ace_mediator_client.c, and what it
 * elevates is the mediator, not ACE.
 */

static int configure(const struct ace_mode_options *options, int identity_only)
{
    /*
     * What --root means now.
     *
     * It used to be answered by geteuid(), because the process had re-executed
     * itself as root and the kernel's opinion was the truth.  Nothing
     * re-executes any more, so the question is no longer "am I root" but "is
     * this session allowed to ask the mediator for help", and only the
     * switches and the inherited session state can answer that.
     *
     * The environment carries it to child processes because a command started
     * by the shell must agree with the shell about what kind of session it is
     * in, and it is a separate process that cannot ask.
     */
    const char *inherited_privilege = getenv(ACE_MODE_PRIVILEGE_ENV);
    int root;
    int device_view;
    char owner_text[32];

    if (!options) {
        errno = EINVAL;
        return -1;
    }
    /*
     * Running ACE as root is refused rather than accommodated.  A root shell
     * would have root's session bus, root's configuration and root's HOME,
     * and would be a different user's desktop wearing this one's name.  The
     * privilege this session may want is available without any of that.
     */
    if (geteuid() == 0) {
        errno = EPERM;
        return -1;
    }
    root = options->root || (inherited_privilege &&
                             strcmp(inherited_privilege, "root") == 0);
    /* The view is no longer a product choice.  A normal session uses the
       host's existing mounts; an authorised session gets the mediator-owned
       device view as part of that authorization. */
    device_view = root;
    (void)identity_only;
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
    const char *value;

    if (configured_root >= 0)
        return configured_root;
    /* Asked before configure(), which happens in short-lived helpers.  The
       session's answer is in the environment, never in the effective uid --
       an ACE process is never root, so geteuid() would answer "no" to a
       question it was not being asked. */
    value = getenv(ACE_MODE_PRIVILEGE_ENV);
    configured_root = value && strcmp(value, "root") == 0;
    return configured_root;
}

int ace_mode_is_device_view(void)
{
    const char *value;

    if (configured_device_view >= 0)
        return configured_device_view;
    value = getenv(ACE_MODE_VIEW_ENV);
    configured_device_view = value && strcmp(value, "device") == 0;
    return configured_device_view;
}

uid_t ace_mode_owner_uid(void)
{
    if (configured_owner == (uid_t)-1)
        configured_owner = initial_owner_uid();
    return configured_owner;
}
