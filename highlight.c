#include <stdlib.h>
#include <string.h>
#include "highlight.h"

/* Syntax highlighting framework. The regex engine (regex.c) does the matching;
 * this file owns the language registry, the per-line block-state machine, and
 * a per-buffer cache so multi-line constructs (block comments, triple-quoted
 * strings) color correctly. The base ships no languages — each is a small,
 * self-registering file (see the 03-regex-hl patch). */

#define HL_MAX_LANGS 16
#define HL_MAX_SPANS 64

/* A language with its rule regexes compiled once at registration. */
typedef struct {
    const Lang *lang;
    Regex **re;
} HLang;

static HLang registry[HL_MAX_LANGS];
static size_t nlangs;

void hl_register(const Lang *lang) {
    if (nlangs >= HL_MAX_LANGS) return;
    Regex **re = malloc(lang->nrules * sizeof(Regex *));
    for (size_t i = 0; i < lang->nrules; i++)
        re[i] = re_compile(lang->rules[i].re); /* NULL on a bad rule: skipped */
    registry[nlangs].lang = lang;
    registry[nlangs].re = re;
    nlangs++;
}

const Lang *hl_lang_for(const char *path) {
    if (!path) return NULL;
    size_t plen = strlen(path);
    for (size_t i = 0; i < nlangs; i++) {
        const Lang *l = registry[i].lang;
        for (size_t s = 0; s < 4 && l->suffixes[s]; s++) {
            size_t slen = strlen(l->suffixes[s]);
            if (plen >= slen + 1 && path[plen - slen - 1] == '.' &&
                !memcmp(path + plen - slen, l->suffixes[s], slen))
                return l;
        }
    }
    return NULL;
}

/* ------------------------- per-line state machine -------------------------- */

/* Scan one line: fill `out` with up to maxout color spans and return the
 * block state carried into the next line. `state` is the incoming state. */
static int hl_line(const HLang *hl, const char *s, size_t len, int state,
                   HLSpan *out, size_t maxout) {
    const Lang *lang = hl->lang;
    size_t nsp = 0;
    size_t pos = 0;

#define EMIT(a, b, c) \
    do { if (out && nsp < maxout && (b) > (a)) { \
        out[nsp].start = (a); out[nsp].end = (b); out[nsp].color = (c); nsp++; } } while (0)

    while (pos < len) {
        if (state != 0) {
            /* inside a block: color until the earliest rule that clears it */
            size_t bs = (size_t)-1, be = 0;
            unsigned clr = 0;
            for (size_t r = 0; r < lang->nrules; r++) {
                if (!(lang->rules[r].state_off & (unsigned)state)) continue;
                size_t m0, m1;
                if (hl->re[r] && re_match(hl->re[r], s, len, pos, &m0, &m1)) {
                    if (m0 < bs) { bs = m0; be = m1; clr = lang->rules[r].state_off; }
                }
            }
            if (bs == (size_t)-1) {
                EMIT(pos, len, lang->block_color); /* block continues next line */
                pos = len;
                break;
            }
            if (bs > pos) EMIT(pos, bs, lang->block_color);
            EMIT(bs, be, lang->block_color);
            state &= ~(int)clr;
            pos = be > bs ? be : bs + 1;
        } else {
            /* normal: earliest rule match wins; ties go to table order */
            size_t bs = (size_t)-1, be = 0;
            int color = -1;
            unsigned son = 0, soff = 0;
            for (size_t r = 0; r < lang->nrules; r++) {
                size_t m0, m1;
                if (hl->re[r] && re_match(hl->re[r], s, len, pos, &m0, &m1)) {
                    if (m0 < bs) {
                        bs = m0; be = m1;
                        color = lang->rules[r].color;
                        son = lang->rules[r].state_on;
                        soff = lang->rules[r].state_off;
                    }
                }
            }
            if (bs == (size_t)-1) break;
            EMIT(bs, be, color);
            state |= (int)son;
            state &= ~(int)soff;
            pos = be > bs ? be : bs + 1;
        }
    }
    return state;
#undef EMIT
}

/* ------------------------- per-buffer highlight state ---------------------- */

struct HL {
    const Lang *lang;
    const HLang *reg;   /* registry entry: compiled rules */
    int *state;         /* state[i] = block state at the start of line i */
    size_t nstate;
    size_t computed;    /* states 0..computed are valid */
    size_t dirty;       /* first line whose state may be stale */
};

static void hl_reset(HL *hl) {
    free(hl->state);
    hl->state = NULL;
    hl->nstate = 0;
    hl->computed = (size_t)-1;
    hl->dirty = 0;
}

HL *hl_new(void) {
    HL *hl = calloc(1, sizeof *hl);
    if (hl) hl_reset(hl);
    return hl;
}

void hl_free(HL *hl) {
    if (!hl) return;
    hl_reset(hl);
    free(hl);
}

void hl_bind(HL *hl, Buffer *b) {
    hl_reset(hl);
    hl->lang = hl_lang_for(b->name);
    hl->reg = NULL;
    if (hl->lang)
        for (size_t i = 0; i < nlangs; i++)
            if (registry[i].lang == hl->lang) { hl->reg = &registry[i]; break; }
}

void hl_dirty(HL *hl, size_t line) {
    if (!hl->lang) return;
    if (hl->dirty == (size_t)-1 || line < hl->dirty) hl->dirty = line;
    if (hl->computed != (size_t)-1 && line <= hl->computed) hl->computed = line - 1;
}

static int ensure_state(HL *hl, size_t need) {
    if (hl->nstate >= need) return 1;
    size_t ncap = hl->nstate ? hl->nstate * 2 : 32;
    while (ncap < need) ncap *= 2;
    int *ns = realloc(hl->state, ncap * sizeof(int));
    if (!ns) return 0;
    hl->state = ns;
    hl->nstate = ncap;
    return 1;
}

/* Recompute per-line state so state[line] is valid: from the first dirty line
 * (its entering state is still correct) or, when merely extending the cache,
 * from just past the last computed line. */
static void hl_recompute(HL *hl, Buffer *b, size_t line) {
    size_t from;
    if (hl->dirty != (size_t)-1 && hl->dirty <= line) from = hl->dirty;
    else if (hl->computed != (size_t)-1 && hl->computed < line) from = hl->computed + 1;
    else return; /* state up to `line` is already valid */
    if (!ensure_state(hl, b->nlines + 1)) return;
    if (from == 0) hl->state[0] = 0;
    char *tmp = NULL;
    size_t tcap = 0;
    for (size_t i = from; i <= line && i < b->nlines; i++) {
        Line *l = &b->lines[i];
        size_t need = l->len + 1;
        if (need > tcap) {
            tcap = need;
            tmp = realloc(tmp, tcap);
        }
        buf_line_slice(b, i, tmp, tcap);
        hl->state[i + 1] = hl_line(hl->reg, tmp, l->len, hl->state[i], NULL, 0);
    }
    free(tmp);
    hl->computed = line;
    hl->dirty = (size_t)-1;
}

size_t hl_spans(HL *hl, Buffer *b, size_t line, HLSpan *out, size_t maxout) {
    if (!hl->lang || !hl->reg || line >= b->nlines) return 0;
    if (!ensure_state(hl, b->nlines + 1)) return 0;
    hl_recompute(hl, b, line);

    Line *l = &b->lines[line];
    char *tmp = malloc(l->len + 1);
    if (!tmp) return 0;
    buf_line_slice(b, line, tmp, l->len + 1);
    size_t n = maxout < HL_MAX_SPANS ? maxout : HL_MAX_SPANS;
    HLSpan buf[HL_MAX_SPANS];
    int st = hl_line(hl->reg, tmp, l->len, hl->state[line], buf, n);
    free(tmp);
    if (line + 1 < hl->nstate) hl->state[line + 1] = st;
    if (out && n) memcpy(out, buf, n * sizeof(HLSpan));
    return n;
}