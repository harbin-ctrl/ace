#ifndef ACE_DOS_DEVICES_H
#define ACE_DOS_DEVICES_H

#include <stddef.h>

/* Discover filesystem-bearing host block devices for the broker's DOS list. */
void ace_dos_devices_discover(void);

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
