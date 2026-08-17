#ifndef ACE_CLIPBOARD_DEVICE_H
#define ACE_CLIPBOARD_DEVICE_H

#include <exec/io.h>
#include <exec/types.h>

typedef int (*ace_clipboard_cancelled)(void *context);

/* Exec-runtime seam for the native clipboard.device implementation. */
int ace_clipboard_device_owns_request(const struct IORequest *request);
LONG ace_clipboard_device_open(ULONG unit, struct IORequest *request);
void ace_clipboard_device_close(struct IORequest *request);
LONG ace_clipboard_device_io(struct IORequest *request,
                             ace_clipboard_cancelled cancelled,
                             void *cancel_context);
void ace_clipboard_device_abort(struct IORequest *request);

#endif
