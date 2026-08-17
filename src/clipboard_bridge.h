#ifndef ACE_CLIPBOARD_BRIDGE_H
#define ACE_CLIPBOARD_BRIDGE_H

#include <stddef.h>

/* The Amiga clipboard device exposes 256 independent units. */
#define ACE_CLIPBOARD_UNIT_COUNT 256u

/* Shared ACE backing directory used by CLIPS: and clipboard.device. */
int ace_clipboard_store_root(char *result, size_t result_size);
int ace_clipboard_store_prepare(void);
int ace_clipboard_store_load(unsigned unit, unsigned char **data,
                             size_t *size);
int ace_clipboard_store_commit(unsigned unit, const void *data, size_t size);
int ace_clipboard_store_exists(unsigned unit);
int ace_clipboard_store_count(void);
void ace_clipboard_store_deleted_path(const char *path);

/* Unit 0 is mirrored to the host text clipboard. */
int ace_clipboard_host_refresh(void);

#endif
