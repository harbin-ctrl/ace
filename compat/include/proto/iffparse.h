#ifndef AMIGA_SHELL_PROTO_IFFPARSE_H
#define AMIGA_SHELL_PROTO_IFFPARSE_H

#include <exec/libraries.h>
#include <libraries/iffparse.h>

extern struct Library *IFFParseBase;

struct IFFHandle *AllocIFF(void);
void FreeIFF(struct IFFHandle *iff);
LONG OpenIFF(struct IFFHandle *iff, LONG rw_mode);
void CloseIFF(struct IFFHandle *iff);
LONG ParseIFF(struct IFFHandle *iff, LONG mode);
struct ContextNode *CurrentChunk(struct IFFHandle *iff);
LONG ReadChunkBytes(struct IFFHandle *iff, APTR buffer, LONG bytes);
LONG WriteChunkBytes(struct IFFHandle *iff, APTR buffer, LONG bytes);
LONG PushChunk(struct IFFHandle *iff, LONG type, LONG id, LONG size);
LONG PopChunk(struct IFFHandle *iff);
LONG StopChunk(struct IFFHandle *iff, LONG type, LONG id);
LONG StopChunks(struct IFFHandle *iff, const LONG *pairs, LONG count);
void InitIFFasClip(struct IFFHandle *iff);
struct ClipboardHandle *OpenClipboard(LONG unit_number);
void CloseClipboard(struct ClipboardHandle *clip_handle);

#endif
