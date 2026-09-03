#ifndef MODIF_BUFFER_H
#define MODIF_BUFFER_H

#include <stddef.h>

/* Gap buffer: edits shift only the gap, never the whole file. */
typedef struct {
    char *data;
    size_t cap;
    size_t gap_start;   /* first byte of the gap */
    size_t gap_end;     /* one past the last byte of the gap */
} GapBuf;

/* Line table entry. start = file offset of first byte of the line,
 * len = number of bytes in the line, excluding its trailing newline. */
typedef struct {
    size_t start;
    size_t len;
} Line;

typedef struct {
    GapBuf gb;
    Line *lines;
    size_t nlines;
    size_t lcap;
    char *name;
} Buffer;

void gb_init(GapBuf *g);
void gb_free(GapBuf *g);
size_t gb_len(GapBuf *g);
char gb_at(GapBuf *g, size_t pos);
void gb_insert(GapBuf *g, size_t pos, const char *s, size_t n);
void gb_delete(GapBuf *g, size_t pos, size_t n);
size_t gb_find(GapBuf *g, const char *needle, size_t n, size_t from);
size_t gb_rfind(GapBuf *g, const char *needle, size_t n, size_t before);
char *gb_slice(GapBuf *g, size_t pos, size_t n);

void buf_init(Buffer *b);
void buf_free(Buffer *b);
int buf_open(Buffer *b, const char *path);
int buf_save(Buffer *b);
void buf_insert(Buffer *b, size_t pos, const char *s, size_t n);
void buf_delete(Buffer *b, size_t pos, size_t n);
size_t buf_line_of(Buffer *b, size_t pos);
void buf_line_slice(Buffer *b, size_t li, char *out, size_t cap);

#endif