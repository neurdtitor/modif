#ifndef MODIF_REGEX_H
#define MODIF_REGEX_H

#include <stddef.h>

typedef struct Regex Regex;

/* Compile a pattern into a Thompson NFA. Returns NULL on a syntax error;
 * re_error() then describes it. The matcher runs in linear time per start
 * position, so no pattern can trigger catastrophic backtracking. */
Regex *re_compile(const char *pat);
void re_free(Regex *re);
const char *re_error(void);

/* Find the leftmost match at or after `from` in s[0..len). Anchors are
 * relative to the whole subject: ^ matches only at offset 0, $ only at len.
 * On success returns 1 and stores the match span in *m0 and *m1 (m1 > m0). */
int re_match(Regex *re, const char *s, size_t len, size_t from,
             size_t *m0, size_t *m1);

/* 1 if the last char of s (or the boundary between pos-1 and pos) is a word
 * char; used internally but also handy for tests. */
int re_isword(unsigned char c);

#endif