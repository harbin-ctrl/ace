#ifndef ACE_MODES_H
#define ACE_MODES_H

#include <sys/types.h>

#define ACE_MODE_PRIVILEGE_ENV "ACE_MODE_PRIVILEGE"
#define ACE_MODE_VIEW_ENV "ACE_MODE_VIEW"
#define ACE_MODE_OWNER_UID_ENV "ACE_MODE_OWNER_UID"

struct ace_mode_options {
    int root;
};

/* Remove ACE's authorization switch from argv, preserving every other word. */
int ace_mode_parse(int *argc, char **argv, struct ace_mode_options *options);

/* Validate the requested combination, apply defaults, and publish it.  Fails
 * with EPERM when the process is running as root: ACE is never root, and the
 * privilege a session may want comes from the fmm instead. */
int ace_mode_configure(const struct ace_mode_options *options);
/* Configure the identity requested by flags without acquiring privileges.
 * Used only by --print-socket so start/stop scripts can find a future broker. */
int ace_mode_configure_identity(const struct ace_mode_options *options);

int ace_mode_is_root(void);
int ace_mode_is_device_view(void);
uid_t ace_mode_owner_uid(void);

#endif
