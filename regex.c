#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "regex.h"

/* Thompson-NFA regex engine. Compiles the pattern into an NFA once, then
 * matches by simulating all NFA states in lockstep over the input — linear
 * time in the subject per start position, so no pattern can blow up
 * exponentially (no backtracking). The subset covered is the practical core:
 * literals, ., ^, $, \b, char classes with ranges and negation, escapes
 * (\d \D \w \W \s \S \n \t ...), * + ?, groups, and | alternation.
 *
 * Leftmost-match semantics: re_match() tries each start offset from `from`
 * onward and returns the first match found (leftmost-shortest). */

enum { SPLIT = 0, CHAR, ANY, CLASS, ANCHOR, MATCH };

enum { ANCHOR_BOL = 0, ANCHOR_EOL, ANCHOR_WB };

typedef struct St {
    int type;
    int c;                    /* CHAR: byte; ANCHOR: which anchor */
    unsigned char cls[32];    /* CLASS: 256-bit set */
    int out, out1;            /* SPLIT uses both, the rest just out */
    int lastlist;             /* simulation scratch */
} St;

typedef struct { int *s; size_t n, cap; } List;

struct Regex {
    St *st;
    int n, cap;
    int start;
    List l1, l2;              /* simulation lists */
    unsigned listid;          /* stamp to dedupe addstate */
};

static char errbuf[128];

const char *re_error(void) { return errbuf; }

int re_isword(unsigned char c) { return isalnum(c) || c == '_'; }

static int grow_states(Regex *re) {
    if (re->n == re->cap) {
        re->cap = re->cap ? re->cap * 2 : 64;
        St *ns = realloc(re->st, (size_t)re->cap * sizeof(St));
        if (!ns) return 0;
        re->st = ns;
    }
    return 1;
}

static int newstate(Regex *re, int type) {
    if (!grow_states(re)) return -1;
    St *s = &re->st[re->n];
    memset(s, 0, sizeof *s);
    s->type = type;
    s->out = s->out1 = -1;
    return re->n++;
}

/* ------------------------------ compilation ------------------------------- */

typedef struct { int start; int *out; size_t nout, ocap; } Frag;

static void frag_add_out(Frag *f, int sid) {
    if (f->nout == f->ocap) {
        f->ocap = f->ocap ? f->ocap * 2 : 8;
        f->out = realloc(f->out, f->ocap * sizeof(int));
    }
    f->out[f->nout++] = sid;
}

/* Wire every loose pointer in f->out to state `s`. A state's out, then out1,
 * is patched (split states expose out1 as their loose pointer). */
static void patch_re(Regex *re, Frag *f, int s) {
    for (size_t i = 0; i < f->nout; i++) {
        St *st = &re->st[f->out[i]];
        if (st->out == -1) st->out = s;
        else st->out1 = s;
    }
}

static void frag_free(Frag *f) {
    free(f->out);
    f->out = NULL;
    f->nout = f->ocap = 0;
}

typedef struct {
    Regex *re;
    const char *p;
    size_t i, n;
    int err;
} Parser;

static int perr(Parser *pa, const char *msg) {
    if (!pa->err) {
        snprintf(errbuf, sizeof errbuf, "%s", msg);
        pa->err = 1;
    }
    return -1;
}

static int peek(Parser *pa) { return pa->i < pa->n ? (unsigned char)pa->p[pa->i] : -1; }

static int nextc(Parser *pa) { return pa->i < pa->n ? (unsigned char)pa->p[pa->i++] : -1; }

/* Character-class bitset helpers. */
static void cls_set(unsigned char cls[32], unsigned char c) { cls[c >> 3] |= (unsigned char)(1u << (c & 7)); }
static int cls_get(const unsigned char cls[32], unsigned char c) { return (cls[c >> 3] >> (c & 7)) & 1; }

static void cls_add_range(unsigned char cls[32], unsigned char lo, unsigned char hi) {
    for (unsigned int c = lo; c <= hi; c++) cls_set(cls, (unsigned char)c);
}

static void cls_add_escape(unsigned char cls[32], unsigned char c) {
    switch (c) {
    case 'd': cls_add_range(cls, '0', '9'); break;
    case 'D':
        for (int i = 0; i < 256; i++)
            if (!(i >= '0' && i <= '9')) cls_set(cls, (unsigned char)i);
        break;
    case 'w': cls_add_range(cls, '0', '9'); cls_add_range(cls, 'A', 'Z');
              cls_add_range(cls, 'a', 'z'); cls_set(cls, '_'); break;
    case 'W':
        for (int i = 0; i < 256; i++)
            if (!re_isword((unsigned char)i)) cls_set(cls, (unsigned char)i);
        break;
    case 's': cls_set(cls, ' '); cls_set(cls, '\t'); cls_set(cls, '\n');
              cls_set(cls, '\r'); cls_set(cls, '\f'); cls_set(cls, '\v'); break;
    case 'S':
        for (int i = 0; i < 256; i++)
            if (!(i == ' ' || i == '\t' || i == '\n' || i == '\r' ||
                  i == '\f' || i == '\v')) cls_set(cls, (unsigned char)i);
        break;
    case 'n': cls_set(cls, '\n'); break;
    case 't': cls_set(cls, '\t'); break;
    case 'r': cls_set(cls, '\r'); break;
    case 'f': cls_set(cls, '\f'); break;
    case 'v': cls_set(cls, '\v'); break;
    default:  cls_set(cls, c); break;
    }
}

/* Literal-escape: what `\x` means when not a class escape. */
static int escape_char(int c) {
    switch (c) {
    case 'n': return '\n';
    case 't': return '\t';
    case 'r': return '\r';
    case 'f': return '\f';
    case 'v': return '\v';
    default:  return c;
    }
}

static Frag parse_alt(Parser *pa);

static Frag parse_atom(Parser *pa) {
    Regex *re = pa->re;
    int c = nextc(pa);
    Frag f = { -1, NULL, 0, 0 };

    if (c == '(') {
        Frag a = parse_alt(pa);
        if (pa->err) return f;
        if (nextc(pa) != ')') { perr(pa, "unmatched ( )"); frag_free(&a); return f; }
        return a;
    }
    if (c == '[') {
        int neg = peek(pa) == '^';
        if (neg) nextc(pa);
        unsigned char cls[32] = {0};
        int any = 0;
        int first = 1;
        while (1) {
            int ch = nextc(pa);
            if (ch == -1) { perr(pa, "unterminated [ ]"); return f; }
            if (ch == ']' && !first) break;
            first = 0;
            if (ch == '\\') {
                int e = nextc(pa);
                if (e == -1) { perr(pa, "unterminated [ ]"); return f; }
                cls_add_escape(cls, (unsigned char)e);
                any = 1;
                continue;
            }
            /* range a-z */
            if (peek(pa) == '-' && pa->i + 1 < pa->n) {
                int hi = pa->p[pa->i + 1];
                if (hi != ']') {
                    nextc(pa); /* - */
                    nextc(pa); /* hi */
                    if (ch > hi) { perr(pa, "invalid character range"); return f; }
                    cls_add_range(cls, (unsigned char)ch, (unsigned char)hi);
                    any = 1;
                    continue;
                }
            }
            cls_set(cls, (unsigned char)ch);
            any = 1;
        }
        if (!any) { perr(pa, "empty character class"); return f; }
        if (neg) {
            unsigned char all[32];
            memset(all, 0, sizeof all);
            for (int i = 0; i < 256; i++)
                if (!cls_get(cls, (unsigned char)i)) cls_set(all, (unsigned char)i);
            memcpy(cls, all, sizeof cls);
        }
        int s = newstate(re, CLASS);
        memcpy(re->st[s].cls, cls, sizeof cls);
        f.start = s;
        frag_add_out(&f, s);
        return f;
    }
    if (c == '.') {
        int s = newstate(re, ANY);
        f.start = s;
        frag_add_out(&f, s);
        return f;
    }
    if (c == '^') {
        int s = newstate(re, ANCHOR);
        re->st[s].c = ANCHOR_BOL;
        f.start = s;
        frag_add_out(&f, s);
        return f;
    }
    if (c == '$') {
        int s = newstate(re, ANCHOR);
        re->st[s].c = ANCHOR_EOL;
        f.start = s;
        frag_add_out(&f, s);
        return f;
    }
    if (c == '\\') {
        int e = nextc(pa);
        if (e == -1) { perr(pa, "dangling backslash"); return f; }
        if (e == 'b') {
            int s = newstate(re, ANCHOR);
            re->st[s].c = ANCHOR_WB;
            f.start = s;
            frag_add_out(&f, s);
            return f;
        }
        /* class escapes turn into a CLASS state */
        if (e == 'd' || e == 'D' || e == 'w' || e == 'W' || e == 's' || e == 'S') {
            unsigned char cls[32] = {0};
            cls_add_escape(cls, (unsigned char)e);
            int s = newstate(re, CLASS);
            memcpy(re->st[s].cls, cls, sizeof cls);
            f.start = s;
            frag_add_out(&f, s);
            return f;
        }
        /* escaped metacharacters are literal bytes; no metachar check */
        int s = newstate(re, CHAR);
        re->st[s].c = escape_char(e);
        f.start = s;
        frag_add_out(&f, s);
        return f;
    }
    if (c == ')' || c == '|' || c == '*' || c == '+' || c == '?' ||
        c == '{' || c == '}') {
        perr(pa, "unexpected metacharacter");
        return f;
    }
    int s = newstate(re, CHAR);
    re->st[s].c = c;
    f.start = s;
    frag_add_out(&f, s);
    return f;
}

static Frag parse_piece(Parser *pa) {
    Frag a = parse_atom(pa);
    if (pa->err) return a;
    Regex *re = pa->re;
    int q = peek(pa);
    if (q == '*' || q == '+' || q == '?') {
        nextc(pa);
        if (q == '*') {
            /* split: out = body, out1 = loop exit (the only loose end) */
            int sp = newstate(re, SPLIT);
            re->st[sp].out = a.start;
            re->st[sp].out1 = -1;
            patch_re(re, &a, sp);   /* body ends loop back to the split */
            free(a.out); a.out = NULL; a.nout = 0; a.ocap = 0;
            frag_add_out(&a, sp);
            a.start = sp;
        } else if (q == '+') {
            int sp = newstate(re, SPLIT);
            re->st[sp].out = a.start;
            re->st[sp].out1 = -1;
            patch_re(re, &a, sp);
            free(a.out); a.out = NULL; a.nout = 0; a.ocap = 0;
            frag_add_out(&a, sp);   /* start stays the first atom */
        } else { /* ? */
            /* split: out = body, out1 = skip; both paths are loose ends */
            int sp = newstate(re, SPLIT);
            re->st[sp].out = a.start;
            re->st[sp].out1 = -1;
            a.start = sp;
            frag_add_out(&a, sp);
        }
    }
    return a;
}

static Frag parse_concat(Parser *pa) {
    Frag f = { -1, NULL, 0, 0 };
    int first = 1;
    while (!pa->err) {
        int c = peek(pa);
        if (c == -1 || c == ')' || c == '|') break;
        Frag a = parse_piece(pa);
        if (pa->err) { frag_free(&f); return f; }
        if (first) {
            f = a;
            first = 0;
        } else {
            /* wire f's loose ends to a's start; only a's ends stay loose */
            patch_re(pa->re, &f, a.start);
            free(f.out);
            f.out = a.out;
            f.nout = a.nout;
            f.ocap = a.ocap;
        }
    }
    if (first) { /* empty alternative: matches empty string */
        int m = newstate(pa->re, MATCH);
        f.start = m;
        frag_add_out(&f, m);
    }
    return f;
}

static Frag parse_alt(Parser *pa) {
    Frag f = parse_concat(pa);
    if (pa->err) return f;
    while (peek(pa) == '|') {
        nextc(pa);
        Frag b = parse_concat(pa);
        if (pa->err) { frag_free(&f); return f; }
        int sp = newstate(pa->re, SPLIT);
        pa->re->st[sp].out = f.start;
        pa->re->st[sp].out1 = b.start;
        int *tmp = realloc(f.out, (f.nout + b.nout) * sizeof(int));
        if (tmp) {
            f.out = tmp;
            memcpy(f.out + f.nout, b.out, b.nout * sizeof(int));
            f.nout += b.nout;
        }
        free(b.out);
        f.start = sp;
    }
    return f;
}

Regex *re_compile(const char *pat) {
    Regex *re = calloc(1, sizeof *re);
    if (!re) return NULL;
    errbuf[0] = '\0';

    Parser pa = { re, pat, 0, strlen(pat), 0 };
    Frag f = parse_alt(&pa);
    if (pa.i != pa.n && !pa.err) perr(&pa, "trailing characters");
    if (pa.err || f.start == -1) {
        frag_free(&f);
        re_free(re);
        return NULL;
    }
    int m = newstate(re, MATCH);
    patch_re(re, &f, m);
    re->start = f.start;
    frag_free(&f);
    return re;
}

void re_free(Regex *re) {
    if (!re) return;
    free(re->st);
    free(re->l1.s);
    free(re->l2.s);
    free(re);
}

/* -------------------------------- matching -------------------------------- */

/* Add `sid` and its epsilon/anchored successors to the list. `pos` is the
 * current offset in the subject, used to evaluate anchors. */
static void addstate(Regex *re, List *l, int sid, const char *s, size_t pos,
                     size_t len) {
    if (sid < 0) return;
    St *st = &re->st[sid];
    if (st->lastlist == (int)re->listid) return;
    st->lastlist = (int)re->listid;
    switch (st->type) {
    case SPLIT:
        addstate(re, l, st->out, s, pos, len);
        addstate(re, l, st->out1, s, pos, len);
        break;
    case ANCHOR:
        if (st->c == ANCHOR_BOL) {
            if (pos == 0) addstate(re, l, st->out, s, pos, len);
        } else if (st->c == ANCHOR_EOL) {
            if (pos == len) addstate(re, l, st->out, s, pos, len);
        } else { /* word boundary */
            int before = pos > 0 && re_isword((unsigned char)s[pos - 1]);
            int after = pos < len && re_isword((unsigned char)s[pos]);
            if (before != after) addstate(re, l, st->out, s, pos, len);
        }
        break;
    default:
        if (l->n == l->cap) {
            l->cap = l->cap ? l->cap * 2 : 32;
            int *ns = realloc(l->s, l->cap * sizeof(int));
            if (!ns) return;
            l->s = ns;
        }
        l->s[l->n++] = sid;
        break;
    }
}

/* True if the current state list contains a MATCH state. */
static int list_has_match(Regex *re, List *l) {
    for (size_t i = 0; i < l->n; i++)
        if (re->st[l->s[i]].type == MATCH) return 1;
    return 0;
}

/* Run the NFA from subject offset `start` and return the longest match that
 * starts there: simulate across the whole subject and remember the last
 * position at which a MATCH state is reached (greedy semantics, like the
 * `*`/`+` of grep and vim rather than the shortest match of a bare NFA). */
static int match_from(Regex *re, const char *s, size_t len, size_t start,
                      size_t *end) {
    List *cl = &re->l1, *nl = &re->l2;
    re->listid++;
    cl->n = 0;
    addstate(re, cl, re->start, s, start, len);

    int found = 0;
    size_t best = start;
    for (size_t pos = start; pos <= len; pos++) {
        if (list_has_match(re, cl)) { found = 1; best = pos; }
        if (pos == len) break;

        re->listid++;
        nl->n = 0;
        for (size_t i = 0; i < cl->n; i++) {
            St *st = &re->st[cl->s[i]];
            if (st->type == CHAR && s[pos] == (char)st->c)
                addstate(re, nl, st->out, s, pos + 1, len);
            else if (st->type == ANY && s[pos] != '\n')
                addstate(re, nl, st->out, s, pos + 1, len);
            else if (st->type == CLASS && cls_get(st->cls, (unsigned char)s[pos]))
                addstate(re, nl, st->out, s, pos + 1, len);
        }
        if (nl->n == 0) break; /* nothing can consume further */
        List *t = cl; cl = nl; nl = t;
    }
    if (found) { *end = best; return 1; }
    return 0;
}

int re_match(Regex *re, const char *s, size_t len, size_t from,
             size_t *m0, size_t *m1) {
    if (!re || !s || from > len) return 0;
    for (size_t start = from; start <= len; start++) {
        size_t end;
        if (match_from(re, s, len, start, &end)) {
            if (m0) *m0 = start;
            if (m1) *m1 = end;
            return 1;
        }
    }
    return 0;
}