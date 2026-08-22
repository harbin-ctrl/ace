#ifndef ACE_DOS_DEVICES_H
#define ACE_DOS_DEVICES_H

#include <stddef.h>

/* Discover filesystem-bearing host block devices for the broker's DOS list. */
void ace_dos_devices_discover(void);

struct ace_privilege_connection;

/* Build the device view through the fmm, after discovery.  The mounts
 * are made in the fmm's private namespace, not this process's, and this
 * process never acquires the privilege to make them itself. */
int ace_dos_devices_prepare_device_view(struct ace_privilege_connection *fmm);

/* Where the fmm put the device roots, or "" when there is no device
 * view. Paths beneath it are reachable only through the CRM. */
const char *ace_dos_devices_view_root(void);

int ace_dos_devices_is_full_root(const char *path);

/* One device's root inside the view, for retrying a path the host's own mount
   tree covers up.  Fails when the device has no view root. */
int ace_dos_devices_device_view_root(const char *name, char *result,
                                     size_t result_size);

/* Return 1 for a unique DOS device/volume alias, 0 for none, -1 if ambiguous. */
int ace_dos_devices_lookup(const char *name);

/* Resolve a known filesystem volume to a host directory, mounting on demand. */
int ace_dos_devices_root(const char *name, char *result, size_t result_size);

/* Translate a canonical host path into the AmigaDOS volume-relative name. */
int ace_dos_devices_name_from_path(const char *path, char *result,
                                   size_t result_size);

/* Return the host mountpoint that is the hard floor for an Amiga volume. */
int ace_dos_devices_volume_root_for_path(const char *path, char *result,
                                         size_t result_size);

/* Release mounts created by the broker. */
void ace_dos_devices_shutdown(void);

/* Serialize the current DOS device list for diagnostics and future DOS APIs. */
int ace_dos_devices_list(char *result, size_t result_size);

/* Change a supported filesystem label and update the live DOS catalog. */
int ace_dos_devices_relabel(const char *name, const char *label);

#endif
