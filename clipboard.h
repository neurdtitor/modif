#ifndef MODIF_CLIPBOARD_H
#define MODIF_CLIPBOARD_H

#include <stddef.h>

/* Copy len bytes to the system clipboard. Returns 0 on success. */
int clip_copy(const char *data, size_t len);

/* Fetch the system clipboard. Returns a malloc'd NUL-terminated string (or
 * NULL on failure) and stores its length in *outlen. */
char *clip_paste(size_t *outlen);

#endif