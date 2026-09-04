#ifndef MODIF_HIGHLIGHT_H
#define MODIF_HIGHLIGHT_H

#include <stddef.h>
#include "buffer.h"
#include "regex.h"

/* A colored region within a line (raw char offsets, color is a 256-color
 * index or -1 for the terminal default). */
typedef struct {
    size_t start, end;
    int color;
} HLSpan;

/* One regex rule. `color` colors the matched region. `state_on` / `state_off`
 * set / clear bits in the per-line block state, which is how multi-line
 * constructs work: the opening of a C comment sets a bit and the closing
 * clears it, and everything between is `block_color`. */
typedef struct {
    const char *re;
    int color;
    unsigned state_on;
    unsigned state_off;
} HLRule;

/* A language: how to recognize its files (suffixes without the dot, e.g.
 * {"c","h"}) and its ordered rules — the first rule that matches earliest at
 * a position wins, so put the block/string rules before keywords. */
typedef struct {
    const char *name;
    const char *suffixes[4];
    const HLRule *rules;
    size_t nrules;
    int block_color; /* color used while a block-state bit is set */
} Lang;

/* Register a language. Language patches call this once (from a constructor in
 * their own file), so adding a language is a single new file. */
void hl_register(const Lang *lang);

/* The language for a path, or NULL if none matches. */
const Lang *hl_lang_for(const char *path);

/* Per-buffer highlighter state: the chosen language plus a cached per-line
 * block state, so highlighting stays correct across buffer switches and edits
 * only invalidate from the edited line onward. */
typedef struct HL HL;

HL *hl_new(void);
void hl_free(HL *hl);
void hl_bind(HL *hl, Buffer *b);       /* pick the language, reset the cache */
void hl_dirty(HL *hl, size_t line);    /* state at `line` and below is stale */

/* Fill `out` with the colored spans of buffer line `line` (at most maxout).
 * Returns the number of spans. Caches per-line state lazily. */
size_t hl_spans(HL *hl, Buffer *b, size_t line, HLSpan *out, size_t maxout);

#endif