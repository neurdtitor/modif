#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include "buffer.h"

#define GB_MIN_CAP 512

void gb_init(GapBuf *g) {
    g->data = NULL;
    g->cap = 0;
    g->gap_start = 0;
    g->gap_end = 0;
}

void gb_free(GapBuf *g) {
    free(g->data);
    gb_init(g);
}

size_t gb_len(GapBuf *g) {
    return g->cap - (g->gap_end - g->gap_start);
}

char gb_at(GapBuf *g, size_t pos) {
    if (pos < g->gap_start) return g->data[pos];
    return g->data[pos + (g->gap_end - g->gap_start)];
}

static void gb_grow(GapBuf *g, size_t need) {
    if (g->cap - (g->gap_end - g->gap_start) >= need) return;
    size_t len = gb_len(g);
    size_t ncap = g->cap ? g->cap * 2 : GB_MIN_CAP;
    while (ncap - len < need) ncap *= 2;
    char *nd = malloc(ncap);
    size_t gs = g->gap_start;
    if (gs) memcpy(nd, g->data, gs);
    if (len - gs) memcpy(nd + gs + (ncap - len), g->data + g->gap_end, len - gs);
    free(g->data);
    g->data = nd;
    g->cap = ncap;
    g->gap_start = gs;
    g->gap_end = gs + (ncap - len);
}

static void gb_gap_to(GapBuf *g, size_t pos) {
    size_t len = gb_len(g);
    if (pos > len) pos = len;
    if (pos == g->gap_start) return;
    if (pos > g->gap_start) {
        size_t n = pos - g->gap_start;
        memmove(g->data + g->gap_start, g->data + g->gap_end, n);
        g->gap_end += n;
        g->gap_start += n;
    } else {
        size_t n = g->gap_start - pos;
        memmove(g->data + g->gap_end - n, g->data + pos, n);
        g->gap_start = pos;
        g->gap_end -= n;
    }
}

void gb_insert(GapBuf *g, size_t pos, const char *s, size_t n) {
    if (!n) return;
    gb_grow(g, n);
    gb_gap_to(g, pos);
    memcpy(g->data + g->gap_start, s, n);
    g->gap_start += n;
}

void gb_delete(GapBuf *g, size_t pos, size_t n) {
    size_t len = gb_len(g);
    if (pos > len || !n) return;
    if (pos + n > len) n = len - pos;
    /* Place the gap before the region, then grow it over the region.
     * The region sits right after the gap, so absorbing it into the gap
     * deletes it without touching any other bytes. */
    gb_gap_to(g, pos);
    g->gap_end += n;
}

size_t gb_find(GapBuf *g, const char *needle, size_t n, size_t from) {
    size_t len = gb_len(g);
    if (!n || from > len || n > len) return (size_t)-1;
    for (size_t i = from; i + n <= len; i++) {
        size_t j;
        for (j = 0; j < n; j++)
            if (gb_at(g, i + j) != (unsigned char)needle[j]) break;
        if (j == n) return i;
    }
    return (size_t)-1;
}

size_t gb_rfind(GapBuf *g, const char *needle, size_t n, size_t before) {
    size_t len = gb_len(g);
    if (!n || before > len || n > len) return (size_t)-1;
    for (long s = (long)(len - n); s >= 0; s--) {
        if ((size_t)s >= before) continue;
        size_t j;
        for (j = 0; j < n; j++)
            if (gb_at(g, (size_t)s + j) != (unsigned char)needle[j]) break;
        if (j == n) return (size_t)s;
    }
    return (size_t)-1;
}

char *gb_slice(GapBuf *g, size_t pos, size_t n) {
    char *out = malloc(n + 1);
    for (size_t i = 0; i < n; i++) out[i] = gb_at(g, pos + i);
    out[n] = '\0';
    return out;
}

/* --------------------------- Buffer (line table) --------------------------- */

void buf_init(Buffer *b) {
    gb_init(&b->gb);
    b->lines = NULL;
    b->nlines = 0;
    b->lcap = 0;
    b->name = NULL;
}

void buf_free(Buffer *b) {
    gb_free(&b->gb);
    free(b->lines);
    free(b->name);
    buf_init(b);
}

static void line_add(Buffer *b, size_t start, size_t len) {
    if (b->nlines == b->lcap) {
        b->lcap = b->lcap ? b->lcap * 2 : 64;
        b->lines = realloc(b->lines, b->lcap * sizeof(Line));
    }
    b->lines[b->nlines].start = start;
    b->lines[b->nlines].len = len;
    b->nlines++;
}

void buf_build_lines(Buffer *b) {
    b->nlines = 0;
    size_t len = gb_len(&b->gb);
    size_t start = 0;
    for (size_t i = 0; i < len; i++) {
        if (gb_at(&b->gb, i) == '\n') {
            line_add(b, start, i - start);
            start = i + 1;
        }
    }
    line_add(b, start, len - start);
}

int buf_open(Buffer *b, const char *path) {
    free(b->name);
    b->name = strdup(path);
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        if (errno == ENOENT) {
            buf_build_lines(b); /* still need at least one empty line */
            return 0;
        }
        return -1;
    }
    off_t sz = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    if (sz > 0) {
        char *tmp = malloc((size_t)sz);
        size_t got = 0;
        ssize_t r;
        while (got < (size_t)sz && (r = read(fd, tmp + got, (size_t)sz - got)) > 0)
            got += (size_t)r;
        close(fd);
        if (got) gb_insert(&b->gb, 0, tmp, got);
        free(tmp);
    } else {
        close(fd);
    }
    buf_build_lines(b);
    return 0;
}

int buf_save(Buffer *b) {
    int fd = open(b->name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    size_t len = gb_len(&b->gb);
    if (b->gb.gap_start) {
        if (write(fd, b->gb.data, b->gb.gap_start) != (ssize_t)b->gb.gap_start)
            goto err;
    }
    size_t right_len = len - b->gb.gap_start;
    if (right_len) {
        if (write(fd, b->gb.data + b->gb.gap_end, right_len) != (ssize_t)right_len)
            goto err;
    }
    close(fd);
    return 0;
err:
    close(fd);
    return -1;
}

void buf_insert(Buffer *b, size_t pos, const char *s, size_t n) {
    if (!n) return;
    size_t li = buf_line_of(b, pos);
    int has_nl = 0;
    for (size_t i = 0; i < n; i++)
        if (s[i] == '\n') { has_nl = 1; break; }
    gb_insert(&b->gb, pos, s, n);
    if (has_nl) {
        buf_build_lines(b);
        return;
    }
    b->lines[li].len += n;
    for (size_t i = li + 1; i < b->nlines; i++) b->lines[i].start += n;
}

void buf_delete(Buffer *b, size_t pos, size_t n) {
    if (!n) return;
    size_t len = gb_len(&b->gb);
    if (pos >= len) return;
    if (pos + n > len) n = len - pos;
    int has_nl = 0;
    for (size_t i = 0; i < n; i++)
        if (gb_at(&b->gb, pos + i) == '\n') { has_nl = 1; break; }
    gb_delete(&b->gb, pos, n);
    if (has_nl) {
        buf_build_lines(b);
        return;
    }
    size_t li = buf_line_of(b, pos);
    b->lines[li].len -= n;
    for (size_t i = li + 1; i < b->nlines; i++) b->lines[i].start -= n;
}

size_t buf_line_of(Buffer *b, size_t pos) {
    size_t lo = 0, hi = b->nlines - 1;
    while (lo < hi) {
        size_t mid = (lo + hi + 1) / 2;
        if (b->lines[mid].start <= pos) lo = mid;
        else hi = mid - 1;
    }
    return lo;
}

void buf_line_slice(Buffer *b, size_t li, char *out, size_t cap) {
    Line *l = &b->lines[li];
    size_t n = l->len < cap - 1 ? l->len : cap - 1;
    for (size_t i = 0; i < n; i++) out[i] = gb_at(&b->gb, l->start + i);
    out[n] = '\0';
}