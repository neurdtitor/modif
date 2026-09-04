#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <errno.h>
#include "edit.h"
#include "config.h"
#include "fuzzy.h"
#include "clipboard.h"

Editor E;
int g_quit = 0;

/* ------------------------------- undo log -------------------------------- */

typedef struct {
    int type; /* 0 = insert, 1 = delete */
    size_t pos;
    char *data;
    size_t len;
} UEntry;

static UEntry *ustack;
static size_t ucap, un, upos;

static void trunc_redo(void) {
    for (size_t i = upos; i < un; i++) free(ustack[i].data);
    un = upos;
}

static void rec_insert(size_t pos, const char *s, size_t n) {
    if (!n) return;
    if (upos > 0) {
        UEntry *p = &ustack[upos - 1];
        if (p->type == 0 && p->pos + p->len == pos) {
            p->data = realloc(p->data, p->len + n + 1);
            memcpy(p->data + p->len, s, n);
            p->data[p->len + n] = '\0';
            p->len += n;
            return;
        }
    }
    trunc_redo();
    if (upos == ucap) {
        ucap = ucap ? ucap * 2 : 64;
        ustack = realloc(ustack, ucap * sizeof(UEntry));
    }
    ustack[upos].type = 0;
    ustack[upos].pos = pos;
    ustack[upos].data = malloc(n + 1);
    memcpy(ustack[upos].data, s, n);
    ustack[upos].data[n] = '\0';
    ustack[upos].len = n;
    un = ++upos;
}

static void rec_delete(size_t pos, const char *s, size_t n) {
    if (!n) return;
    if (upos > 0) {
        UEntry *p = &ustack[upos - 1];
        if (p->type == 1) {
            if (pos + n == p->pos) { /* backspace: prepend */
                char *nd = malloc(n + p->len + 1);
                memcpy(nd, s, n);
                memcpy(nd + n, p->data, p->len);
                nd[n + p->len] = '\0';
                free(p->data);
                p->data = nd;
                p->len += n;
                p->pos = pos;
                return;
            }
            if (pos == p->pos + p->len) { /* delete key: append */
                p->data = realloc(p->data, p->len + n + 1);
                memcpy(p->data + p->len, s, n);
                p->data[p->len + n] = '\0';
                p->len += n;
                return;
            }
        }
    }
    trunc_redo();
    if (upos == ucap) {
        ucap = ucap ? ucap * 2 : 64;
        ustack = realloc(ustack, ucap * sizeof(UEntry));
    }
    ustack[upos].type = 1;
    ustack[upos].pos = pos;
    ustack[upos].data = malloc(n + 1);
    memcpy(ustack[upos].data, s, n);
    ustack[upos].data[n] = '\0';
    ustack[upos].len = n;
    un = ++upos;
}

static void ed_insert_chars(size_t pos, const char *s, size_t n) {
    if (!n) return;
    size_t li = buf_line_of(&E.buf, pos);
    rec_insert(pos, s, n);
    buf_insert(&E.buf, pos, s, n);
    E.dirty = 1;
    E.cur = pos + n;
    if (E.hl) hl_dirty(E.hl, li);
}

static void ed_del_range(size_t pos, size_t n) {
    if (!n) return;
    size_t li = buf_line_of(&E.buf, pos);
    char *s = gb_slice(&E.buf.gb, pos, n);
    rec_delete(pos, s, n);
    free(s);
    buf_delete(&E.buf, pos, n);
    E.dirty = 1;
    if (E.hl) hl_dirty(E.hl, li);
}

static void ed_backspace(void) {
    if (E.cur == 0) return;
    ed_del_range(E.cur - 1, 1);
    E.cur--;
}

static void ed_delchar(void) {
    size_t len = gb_len(&E.buf.gb);
    if (E.cur >= len) return;
    ed_del_range(E.cur, 1);
}

/* ------------------------------ basic helpers ----------------------------- */

static size_t cur_line(void) { return buf_line_of(&E.buf, E.cur); }

static void ed_move_line(long dy) {
    long li = (long)cur_line();
    size_t off = E.cur - E.buf.lines[(size_t)li].start;
    long nli = li + dy;
    if (nli < 0) { E.cur = 0; return; }
    if (nli >= (long)E.buf.nlines) { E.cur = gb_len(&E.buf.gb); return; }
    Line *l = &E.buf.lines[(size_t)nli];
    E.cur = l->start + (off > l->len ? l->len : off);
}

static void jump_lines(long delta) {
    long li = (long)cur_line();
    long target = li + delta;
    if (target < 0) target = 0;
    if (target >= (long)E.buf.nlines) target = (long)E.buf.nlines - 1;
    E.cur = E.buf.lines[(size_t)target].start;
}

/* ------------------------------- yank / paste ----------------------------- */

static char *ybuf;
static size_t ylen;

static void yank_set(char *s, size_t n) {
    free(ybuf);
    ybuf = s;
    ylen = n;
}

/* ------------------------------- clipboard -------------------------------- */

static void sel_bounds(size_t *lo, size_t *hi); /* visual mode helper */

/* Push the yank buffer (or the visual selection) into the system clipboard. */
void cmd_clip_copy(void) {
    if (E.mode == MODE_VISUAL) {
        size_t lo, hi;
        sel_bounds(&lo, &hi);
        E.mode = MODE_NORMAL;
        if (hi == lo) { edit_set_status("nothing to copy"); return; }
        char *s = gb_slice(&E.buf.gb, lo, hi - lo);
        int ok = clip_copy(s, hi - lo);
        free(s);
        if (ok == 0) edit_set_status("copied %zu bytes to clipboard", hi - lo);
        else edit_set_status("clipboard unavailable");
        return;
    }
    if (!ylen) { edit_set_status("nothing to copy (yank or select first)"); return; }
    if (clip_copy(ybuf, ylen) == 0)
        edit_set_status("copied %zu bytes to clipboard", ylen);
    else
        edit_set_status("clipboard unavailable");
}

/* Pull the system clipboard into the buffer at the cursor. */
void cmd_clip_paste(void) {
    size_t n = 0;
    char *s = clip_paste(&n);
    if (!s) { edit_set_status("clipboard unavailable"); return; }
    ed_insert_chars(E.cur, s, n);
    edit_set_status("pasted %zu bytes from clipboard", n);
    free(s);
}

/* --------------------------------- motions -------------------------------- */

void cmd_left(void) { if (E.cur > 0) E.cur--; }
void cmd_right(void) {
    size_t len = gb_len(&E.buf.gb);
    if (E.cur < len) E.cur++;
}
void cmd_up(void) { ed_move_line(-1); }
void cmd_down(void) { ed_move_line(1); }

void cmd_word_next(void) {
    size_t len = gb_len(&E.buf.gb);
    if (E.cur >= len) return;
    size_t i = E.cur;
    while (i < len) {
        char c = gb_at(&E.buf.gb, i);
        if (c == '\n' || c == ' ') break;
        i++;
    }
    while (i < len && gb_at(&E.buf.gb, i) == ' ') i++;
    E.cur = i;
}

void cmd_word_prev(void) {
    if (E.cur == 0) return;
    size_t i = E.cur;
    while (i > 0) {
        char c = gb_at(&E.buf.gb, i - 1);
        if (c != ' ' && c != '\n') break;
        i--;
    }
    while (i > 0) {
        char c = gb_at(&E.buf.gb, i - 1);
        if (c == ' ' || c == '\n') break;
        i--;
    }
    E.cur = i;
}

void cmd_word_end(void) {
    size_t len = gb_len(&E.buf.gb);
    if (E.cur >= len) return;
    size_t i = E.cur;
    while (i < len && gb_at(&E.buf.gb, i) == ' ') i++;
    /* already on the last char of a word: step into the next word so a
     * repeated e keeps advancing (vim behavior) */
    if (i < len && (i + 1 >= len || gb_at(&E.buf.gb, i + 1) == ' ' ||
                    gb_at(&E.buf.gb, i + 1) == '\n')) {
        i++;
        while (i < len && gb_at(&E.buf.gb, i) == ' ') i++;
    }
    while (i < len) {
        char c = gb_at(&E.buf.gb, i);
        if (c == ' ' || c == '\n') break;
        i++;
    }
    if (i > E.cur) E.cur = i - 1;
}

void cmd_line_start(void) { E.cur = E.buf.lines[cur_line()].start; }
void cmd_line_end(void) {
    Line *l = &E.buf.lines[cur_line()];
    E.cur = l->start + l->len;
}
void cmd_line_first(void) {
    Line *l = &E.buf.lines[cur_line()];
    size_t i = l->start;
    size_t end = l->start + l->len;
    while (i < end && gb_at(&E.buf.gb, i) == ' ') i++;
    E.cur = i;
}
void cmd_goto_top(void) { E.cur = 0; }
void cmd_goto_bottom(void) {
    size_t li = E.buf.nlines - 1;
    if (E.buf.lines[li].len == 0 && li > 0) li--; /* skip trailing empty line */
    E.cur = E.buf.lines[li].start;
}

void cmd_half_down(void) { jump_lines((long)(E.screen_rows - 1) / 2); }
void cmd_half_up(void) { jump_lines(-(long)(E.screen_rows - 1) / 2); }
void cmd_page_down(void) { jump_lines((long)(E.screen_rows - 2)); }
void cmd_page_up(void) { jump_lines(-(long)(E.screen_rows - 2)); }

/* ------------------------------ insert modes ------------------------------ */

void cmd_enter_insert(void) { E.mode = MODE_INSERT; }
void cmd_append(void) {
    Line *l = &E.buf.lines[cur_line()];
    if (E.cur < l->start + l->len) E.cur++;
    E.mode = MODE_INSERT;
}
void cmd_insert_bol(void) { E.cur = E.buf.lines[cur_line()].start; E.mode = MODE_INSERT; }
void cmd_append_eol(void) {
    Line *l = &E.buf.lines[cur_line()];
    E.cur = l->start + l->len;
    E.mode = MODE_INSERT;
}
void cmd_open_below(void) {
    size_t li = cur_line();
    Line *l = &E.buf.lines[li];
    if (l->len == 0 && l->start == gb_len(&E.buf.gb) && li > 0) {
        li--;
        l = &E.buf.lines[li];
    }
    E.cur = l->start + l->len;
    ed_insert_chars(E.cur, "\n", 1);
    E.mode = MODE_INSERT;
}
void cmd_open_above(void) {
    size_t s = E.buf.lines[cur_line()].start;
    ed_insert_chars(s, "\n", 1);
    E.cur = s;
    E.mode = MODE_INSERT;
}

void cmd_delete_char(void) { ed_delchar(); }
void cmd_backspace(void) { ed_backspace(); }

/* ---------------------------- line delete/yank ---------------------------- */

static size_t ncount;   /* pending count prefix, 0 = none */
static int op;          /* pending operator: 0 = none, OP_DELETE, OP_YANK */
static int leader;      /* leader key pressed: 1 = waiting for the next key */

#define OP_DELETE 1
#define OP_YANK 2

static void count_reset(void) { ncount = 0; op = 0; }

/* Consume the pending count (defaulting to 1) and clear operator state. */
static size_t count_take(void) {
    size_t c = ncount ? ncount : 1;
    count_reset();
    return c;
}

void cmd_delete_line(void) { op = OP_DELETE; }
void cmd_yank_line(void) { op = OP_YANK; }

/* Motions that can be extended into a range by d/y, and linewise ones. */
static int is_motion(cmd_fn fn) {
    return fn == cmd_left || fn == cmd_right || fn == cmd_up || fn == cmd_down ||
           fn == cmd_word_next || fn == cmd_word_prev || fn == cmd_word_end ||
           fn == cmd_line_start || fn == cmd_line_end || fn == cmd_line_first ||
           fn == cmd_goto_top || fn == cmd_goto_bottom;
}

static int is_linewise(cmd_fn fn) {
    return fn == cmd_up || fn == cmd_down || fn == cmd_goto_top || fn == cmd_goto_bottom;
}

static int is_repeatable(cmd_fn fn) {
    return is_motion(fn) || fn == cmd_delete_char || fn == cmd_backspace ||
           fn == cmd_paste || fn == cmd_paste_before || fn == cmd_undo ||
           fn == cmd_redo || fn == cmd_search_next || fn == cmd_search_prev ||
           fn == cmd_half_down || fn == cmd_half_up || fn == cmd_page_up ||
           fn == cmd_page_down;
}

/* Apply dd / yy with an optional line count. */
static void do_line_op(int which) {
    size_t c = count_take();
    size_t li = cur_line();
    size_t e = li + c - 1;
    if (e >= E.buf.nlines) e = E.buf.nlines - 1;
    Line *l0 = &E.buf.lines[li], *l1 = &E.buf.lines[e];
    size_t s = l0->start;
    size_t end = l1->start + l1->len;
    if (e < E.buf.nlines - 1) end++; /* keep trailing newline */
    if (which == OP_DELETE) {
        yank_set(gb_slice(&E.buf.gb, s, end - s), end - s);
        ed_del_range(s, end - s);
        E.cur = s;
    } else {
        yank_set(gb_slice(&E.buf.gb, s, end - s), end - s);
    }
}

/* Apply d/y over the range swept by a counted motion (e.g. 2dj, d3w, dG). */
static void do_motion_op(cmd_fn fn) {
    int which = op; /* count_take() clears op, so capture it first */
    size_t c = count_take();
    size_t start = E.cur;
    if (is_linewise(fn)) start = E.buf.lines[cur_line()].start;
    for (size_t i = 0; i < c; i++) fn();
    size_t end = E.cur;
    size_t lo = start < end ? start : end;
    size_t hi = start < end ? end : start;
    size_t s = lo, n = hi - lo;
    if (is_linewise(fn)) {
        size_t ll = buf_line_of(&E.buf, lo);
        size_t lh = buf_line_of(&E.buf, hi);
        s = E.buf.lines[ll].start;
        size_t hend = E.buf.lines[lh].start + E.buf.lines[lh].len;
        if (lh < E.buf.nlines - 1) hend++;
        n = hend - s;
    } else if (fn == cmd_word_end && n) {
        n++; /* de includes the word's final char */
    }
    if (!n) return;
    if (which == OP_DELETE) {
        yank_set(gb_slice(&E.buf.gb, s, n), n);
        ed_del_range(s, n);
        E.cur = s;
    } else {
        yank_set(gb_slice(&E.buf.gb, s, n), n);
        E.cur = start; /* keep cursor at the yank anchor */
    }
}

void cmd_paste(void) {
    if (!ylen) return;
    ed_insert_chars(E.cur, ybuf, ylen);
}
void cmd_paste_before(void) {
    if (!ylen) return;
    size_t li = buf_line_of(&E.buf, E.cur);
    rec_insert(E.cur, ybuf, ylen);
    buf_insert(&E.buf, E.cur, ybuf, ylen);
    E.dirty = 1;
    E.cur += ylen; /* leave the cursor after the pasted text */
    if (E.hl) hl_dirty(E.hl, li);
}

/* --------------------------------- undo ----------------------------------- */

void cmd_undo(void) {
    if (!upos) return;
    UEntry *e = &ustack[upos - 1];
    size_t li = buf_line_of(&E.buf, e->pos);
    if (e->type == 0) {
        buf_delete(&E.buf, e->pos, e->len);
        E.cur = e->pos;
    } else {
        buf_insert(&E.buf, e->pos, e->data, e->len);
        E.cur = e->pos + e->len;
    }
    E.dirty = 1;
    upos--;
    if (E.hl) hl_dirty(E.hl, li);
}

void cmd_redo(void) {
    if (upos >= un) return;
    UEntry *e = &ustack[upos];
    size_t li = buf_line_of(&E.buf, e->pos);
    if (e->type == 0) {
        buf_insert(&E.buf, e->pos, e->data, e->len);
        E.cur = e->pos + e->len;
    } else {
        buf_delete(&E.buf, e->pos, e->len);
        E.cur = e->pos;
    }
    E.dirty = 1;
    upos++;
    if (E.hl) hl_dirty(E.hl, li);
}

/* --------------------------------- search --------------------------------- */

/* Search uses the base regex engine: the query is compiled once per change
 * and matched against a contiguous copy of the buffer (the gap buffer is not
 * contiguous). Anchors (^, $, \b) are relative to the whole buffer. */

static char *buf_copy(size_t *len) {
    *len = gb_len(&E.buf.gb);
    return gb_slice(&E.buf.gb, 0, *len);
}

void cmd_search(void) {
    E.scur = E.cur;
    E.sqlen = 0;
    E.sq[0] = '\0';
    E.smatch = (size_t)-1;
    E.sactive = 0;
    if (E.sqre) { re_free(E.sqre); E.sqre = NULL; }
    E.mode = MODE_SEARCH;
}

static void search_recompile(void) {
    if (E.sqre) { re_free(E.sqre); E.sqre = NULL; }
    if (E.sqlen) {
        E.sqre = re_compile(E.sq);
        if (!E.sqre) edit_set_status("bad pattern: %s", re_error());
    }
}

static void search_update(void) {
    if (!E.sqlen) {
        E.smatch = (size_t)-1;
        E.cur = E.scur;
        return;
    }
    if (!E.sqre) return;
    size_t len;
    char *s = buf_copy(&len);
    size_t m0, m1;
    int ok = re_match(E.sqre, s, len, E.scur, &m0, &m1);
    if (!ok) ok = re_match(E.sqre, s, len, 0, &m0, &m1);
    free(s);
    if (ok) { E.smatch = m0; E.smlen = m1 - m0; E.cur = m0; }
    else { E.smatch = (size_t)-1; edit_set_status("no match"); }
}

static void search_key(int key) {
    switch (key) {
    case CTRL('Q'):
        E.mode = MODE_NORMAL;
        cmd_quit();
        break;
    case ESC:
    case CTRL('C'):
    case CTRL('G'):
        E.cur = E.scur;
        E.sqlen = 0;
        E.sq[0] = '\0';
        E.sactive = 0;
        E.smatch = (size_t)-1;
        if (E.sqre) { re_free(E.sqre); E.sqre = NULL; }
        E.mode = MODE_NORMAL;
        break;
    case ENTER:
        E.sactive = 1;
        E.mode = MODE_NORMAL;
        break;
    case BACKSPACE:
    case CTRL('H'):
        if (E.sqlen) {
            E.sq[--E.sqlen] = '\0';
            search_recompile();
            search_update();
        }
        break;
    default:
        if (key >= 32 && key <= 126 && E.sqlen < sizeof E.sq - 1) {
            E.sq[E.sqlen++] = (char)key;
            E.sq[E.sqlen] = '\0';
            search_recompile();
            search_update();
        }
        break;
    }
}

void cmd_search_next(void) {
    if (!E.sqlen || !E.sqre) return;
    size_t len;
    char *s = buf_copy(&len);
    size_t from = (E.smatch != (size_t)-1)
                      ? E.smatch + (E.smlen ? E.smlen : 1)
                      : E.cur;
    size_t m0, m1;
    int ok = re_match(E.sqre, s, len, from, &m0, &m1);
    if (!ok) ok = re_match(E.sqre, s, len, 0, &m0, &m1);
    free(s);
    if (ok) { E.smatch = m0; E.smlen = m1 - m0; E.cur = m0; }
    E.sactive = 1;
}

void cmd_search_prev(void) {
    if (!E.sqlen || !E.sqre) return;
    size_t len;
    char *s = buf_copy(&len);
    size_t limit = (E.smatch != (size_t)-1) ? E.smatch : len + 1;
    size_t from = 0, last0 = (size_t)-1, last1 = 0, m0, m1;
    while (re_match(E.sqre, s, len, from, &m0, &m1)) {
        if (m0 >= limit) break;
        last0 = m0;
        last1 = m1;
        from = m1 > m0 ? m1 : m0 + 1;
    }
    free(s);
    if (last0 != (size_t)-1) { E.smatch = last0; E.smlen = last1 - last0; E.cur = last0; }
    E.sactive = 1;
}

/* ------------------------------ visual mode ------------------------------- */

void cmd_visual(void) {
    E.mark = E.cur;
    E.mode = MODE_VISUAL;
}

static void sel_bounds(size_t *lo, size_t *hi) {
    if (E.mark <= E.cur) { *lo = E.mark; *hi = E.cur; }
    else { *lo = E.cur; *hi = E.mark; }
}

static void sel_delete(void) {
    size_t lo, hi;
    sel_bounds(&lo, &hi);
    if (hi == lo) { E.mode = MODE_NORMAL; return; }
    yank_set(gb_slice(&E.buf.gb, lo, hi - lo), hi - lo);
    ed_del_range(lo, hi - lo);
    E.cur = lo;
    E.mode = MODE_NORMAL;
}

static void sel_yank(void) {
    size_t lo, hi;
    sel_bounds(&lo, &hi);
    if (hi == lo) { E.mode = MODE_NORMAL; return; }
    yank_set(gb_slice(&E.buf.gb, lo, hi - lo), hi - lo);
    E.mode = MODE_NORMAL;
}

static void visual_key(int key) {
    if (key == ESC || key == 'v') { E.mode = MODE_NORMAL; count_reset(); return; }
    if (key == 'x' || key == 'd') { sel_delete(); count_reset(); return; }
    if (key == 'y') { sel_yank(); count_reset(); return; }
    if (key >= '1' && key <= '9') {
        ncount = ncount * 10 + (size_t)(key - '0');
        if (ncount > 1000000) ncount = 1000000;
        return;
    }
    if (key == '0' && ncount) {
        ncount *= 10;
        if (ncount > 1000000) ncount = 1000000;
        return;
    }
    cmd_fn fn = NULL;
    for (size_t i = 0; i < NORMAL_KEYS_LEN; i++)
        if (normal_keys[i].key == key) { fn = normal_keys[i].fn; break; }
    if (fn == cmd_half_down || fn == cmd_half_up ||
        fn == cmd_page_down || fn == cmd_page_up) {
        count_reset();
        fn();
        return;
    }
    if (fn && is_motion(fn)) {
        size_t c = count_take();
        while (c--) fn();
        return;
    }
    count_reset();
}

/* --------------------------- buffer list switching ------------------------- */

/* The editor edits E.buf, which shares its storage with the buffer's slot in
 * the list (bl_add / buf_load copy the struct, never the contents). Before
 * switching away, buf_sync() folds E.buf's live content, view, and gap
 * metadata back into the slot; buf_load() pulls another slot in and resets the
 * undo log, which is a single global linear history and never spans buffers. */

static void buf_sync(void) {
    if (!E.bl->n) return;
    E.bl->bufs[E.bl->cur] = E.buf; /* full copy: content + view + metadata */
    E.bl->bufs[E.bl->cur].dirty = E.dirty; /* E.dirty is the live dirty flag */
}

static void buf_load(size_t i) {
    E.bl->cur = i;
    E.buf = E.bl->bufs[i]; /* shares storage with the slot */
    E.cur = E.buf.cur;     /* lift the saved view state into the editor */
    E.top = E.buf.top;
    E.left = E.buf.left;
    E.mark = E.buf.mark;
    E.dirty = E.buf.dirty;
    for (size_t k = 0; k < un; k++) free(ustack[k].data); /* undo never spans buffers */
    un = 0;
    upos = 0;
    E.mode = MODE_NORMAL;
    leader = 0;
    count_reset();
    if (E.hl) hl_bind(E.hl, &E.buf); /* pick a language for the new buffer */
}

static void buf_add(Buffer *nb) {
    buf_sync();
    bl_add(E.bl, nb);
    buf_load(E.bl->cur);
}

static void buf_switch(int dir) {
    if (E.bl->n < 2) return;
    buf_sync();
    bl_switch(E.bl, dir);
    buf_load(E.bl->cur);
}

void cmd_buf_prev(void) { buf_switch(-1); }
void cmd_buf_next(void) { buf_switch(1); }

static void buf_list(void) {
    buf_sync();
    char tmp[160];
    size_t off = 0;
    off += (size_t)snprintf(tmp + off, sizeof tmp - off, "%zu buffer(s):", E.bl->n);
    for (size_t i = 0; i < E.bl->n && off < sizeof tmp; i++) {
        const char *n = E.bl->bufs[i].name ? E.bl->bufs[i].name : "[no name]";
        off += (size_t)snprintf(tmp + off, sizeof tmp - off, " %zu%s%s%s",
                                i + 1, i == E.bl->cur ? "%" : "", n,
                                E.bl->bufs[i].dirty ? "*" : "");
    }
    edit_set_status("%s", tmp);
}

/* ------------------------------ : command mode ---------------------------- */

void cmd_colon(void) {
    E.mode = MODE_CMD;
    E.cmdlen = 0;
    E.cmdq[0] = '\0';
}

static int any_dirty(void) {
    if (E.dirty) return 1; /* current buffer's live flag */
    for (size_t i = 0; i < E.bl->n; i++)
        if (i != E.bl->cur && E.bl->bufs[i].dirty) return 1;
    return 0;
}

static void cmd_execute(void) {
    char *c = E.cmdq;
    if (!strcmp(c, "w") || !strcmp(c, "write")) {
        cmd_save();
    } else if (!strcmp(c, "q")) {
        if (E.dirty)
            edit_set_status("No write since last change (use :q! to quit)");
        else if (any_dirty())
            edit_set_status("another buffer has unsaved changes (use :q! to quit)");
        else
            g_quit = 1;
    } else if (!strcmp(c, "q!") || !strcmp(c, "quit!")) {
        g_quit = 1;
    } else if (!strcmp(c, "wq") || !strcmp(c, "x")) {
        cmd_save();
        if (!E.dirty && !any_dirty()) g_quit = 1;
    } else if (!strcmp(c, "wq!")) {
        cmd_save();
        g_quit = 1;
    } else if (!strcmp(c, "bn") || !strcmp(c, "bnext")) {
        buf_switch(1);
    } else if (!strcmp(c, "bp") || !strcmp(c, "bprev")) {
        buf_switch(-1);
    } else if (!strcmp(c, "ls") || !strcmp(c, "buffers")) {
        buf_list();
    } else if (c[0] != '\0') {
        edit_set_status("unknown command: %s", c);
    }
}

static void cmd_key(int key) {
    switch (key) {
    case ESC:
    case CTRL('G'):
    case CTRL('C'):
        E.mode = MODE_NORMAL;
        break;
    case CTRL('Q'):
        E.mode = MODE_NORMAL;
        cmd_quit();
        break;
    case ENTER:
        cmd_execute();
        E.mode = MODE_NORMAL;
        break;
    case BACKSPACE:
    case CTRL('H'):
        if (E.cmdlen) {
            E.cmdq[--E.cmdlen] = '\0';
        }
        break;
    default:
        if (key >= 32 && key <= 126 && E.cmdlen < sizeof E.cmdq - 1) {
            E.cmdq[E.cmdlen++] = (char)key;
            E.cmdq[E.cmdlen] = '\0';
        }
        break;
    }
}

/* ----------------------------- file I/O, panes ----------------------------- */

void cmd_save(void) {
    if (!E.buf.name) {
        edit_set_status("no file name");
        return;
    }
    if (buf_save(&E.buf) == 0) {
        E.dirty = 0;
        edit_set_status("saved %s (%zu bytes)", E.buf.name, gb_len(&E.buf.gb));
    } else {
        edit_set_status("write error: %s", strerror(errno));
    }
}

static int quit_times = 3;

void cmd_quit(void) {
    if (any_dirty() && quit_times) {
        edit_set_status("WARNING: unsaved changes. Press Ctrl-Q %d more times.",
                        quit_times);
        quit_times--;
        return;
    }
    g_quit = 1;
}

void cmd_toggle_term(void) {
    E.term_open = !E.term_open;
    E.mode = MODE_NORMAL;
    E.focus = E.term_open ? FOCUS_TERM : FOCUS_EDIT;
}

void cmd_switch_focus(void) {
    if (!E.term_open) return;
    E.focus = (E.focus == FOCUS_TERM) ? FOCUS_EDIT : FOCUS_TERM;
    if (E.focus == FOCUS_TERM) E.mode = MODE_NORMAL;
}

void cmd_fuzzy(void) {
    E.mode = MODE_FUZZY;
    E.focus = FOCUS_EDIT;
    E.fqlen = 0;
    E.fq[0] = '\0';
    E.fsel = 0;
    fuzzy_build(&E);
    fuzzy_filter(&E);
}

/* ------------------------------- fuzzy keys ------------------------------- */

static void fuzzy_key(int key) {
    switch (key) {
    case CTRL('Q'):
        E.mode = MODE_NORMAL;
        cmd_quit();
        break;
    case ESC:
    case CTRL('C'):
    case CTRL('G'):
        fuzzy_free(&E);
        E.mode = MODE_NORMAL;
        break;
    case ENTER:
        if (E.fcount && E.fsel < (int)E.fcount) {
            char path[1024];
            snprintf(path, sizeof path, "%s", E.flist[E.fidx[E.fsel]]);
            fuzzy_free(&E);
            edit_open(path);
            return;
        }
        fuzzy_free(&E);
        E.mode = MODE_NORMAL;
        break;
    case BACKSPACE:
    case CTRL('H'):
        if (E.fqlen) {
            E.fq[--E.fqlen] = '\0';
            fuzzy_filter(&E);
        }
        break;
    case ARROW_DOWN:
    case CTRL('N'):
        if (E.fcount) E.fsel = (E.fsel + 1) % (int)E.fcount;
        break;
    case ARROW_UP:
    case CTRL('P'):
        if (E.fcount) E.fsel = (E.fsel - 1 + (int)E.fcount) % (int)E.fcount;
        break;
    default:
        if (key >= 32 && key <= 126 && E.fqlen < sizeof E.fq - 1) {
            E.fq[E.fqlen++] = (char)key;
            E.fq[E.fqlen] = '\0';
            fuzzy_filter(&E);
        }
        break;
    }
}

/* ------------------------------ dispatch / api ---------------------------- */

/* Resolve a key after the leader key. Returns 1 if the key was consumed.
 * An unmapped key just cancels the pending leader. */
static int leader_handle(int key) {
    if (!leader) return 0;
    leader = 0;
    count_reset();
    for (size_t i = 0; i < LEADER_KEYS_LEN; i++)
        if (leader_keys[i].key == key) { leader_keys[i].fn(); return 1; }
    return 0;
}

void edit_handle_key(int key) {
    if (key != CTRL('Q')) quit_times = 3;
    if (E.mode == MODE_SEARCH) { search_key(key); return; }
    if (E.mode == MODE_FUZZY) { fuzzy_key(key); return; }
    if (E.mode == MODE_CMD) { cmd_key(key); return; }
    if (E.mode == MODE_VISUAL) {
        for (size_t i = 0; i < GLOBAL_KEYS_LEN; i++)
            if (global_keys[i].key == key) { global_keys[i].fn(); return; }
        if (leader_handle(key)) return;
        if (key == LEADER_KEY) { leader = 1; count_reset(); return; }
        visual_key(key);
        return;
    }

    for (size_t i = 0; i < GLOBAL_KEYS_LEN; i++)
        if (global_keys[i].key == key) { global_keys[i].fn(); return; }

    if (E.mode == MODE_INSERT) {
        char c;
        switch (key) {
        case ESC: E.mode = MODE_NORMAL; break;
        case BACKSPACE:
        case CTRL('H'): ed_backspace(); break;
        case DEL_KEY: ed_delchar(); break;
        case ENTER: ed_insert_chars(E.cur, "\n", 1); break;
        case TAB: ed_insert_chars(E.cur, "\t", 1); break;
        default:
            if (key >= 32 && key <= 126) {
                c = (char)key;
                ed_insert_chars(E.cur, &c, 1);
            }
            break;
        }
        return;
    }

    /* normal mode */
    if (leader_handle(key)) return;
    if (key == LEADER_KEY) { leader = 1; count_reset(); return; }
    if (key == ESC) { count_reset(); return; }
    if (key >= '1' && key <= '9') {
        ncount = ncount * 10 + (size_t)(key - '0');
        if (ncount > 1000000) ncount = 1000000;
        return;
    }
    if (key == '0' && ncount) {
        ncount *= 10;
        if (ncount > 1000000) ncount = 1000000;
        return;
    }
    cmd_fn fn = NULL;
    for (size_t i = 0; i < NORMAL_KEYS_LEN; i++)
        if (normal_keys[i].key == key) { fn = normal_keys[i].fn; break; }
    if (!fn) { count_reset(); return; }

    if (op && is_motion(fn)) { do_motion_op(fn); return; }
    if (op && fn == cmd_delete_line && op == OP_DELETE) { do_line_op(OP_DELETE); return; }
    if (op && fn == cmd_yank_line && op == OP_YANK) { do_line_op(OP_YANK); return; }
    if (op) count_reset(); /* a non-motion cancels the pending operator */

    if (fn == cmd_delete_line) { op = OP_DELETE; return; }
    if (fn == cmd_yank_line) { op = OP_YANK; return; }

    if (fn == cmd_goto_top || fn == cmd_goto_bottom) {
        if (ncount) {
            size_t li = ncount - 1;
            if (li >= E.buf.nlines) li = E.buf.nlines - 1;
            E.cur = E.buf.lines[li].start;
            count_reset();
        } else {
            fn();
        }
        return;
    }
    {
        size_t c = count_take();
        if (is_repeatable(fn))
            while (c--) fn();
        else
            fn();
    }
}

void edit_after(void) {
    size_t len = gb_len(&E.buf.gb);
    if (E.cur > len) E.cur = len;
    size_t content_rows = E.screen_rows > 1 ? (size_t)(E.screen_rows - 1) : 1;
    size_t li = cur_line();
    if (li < E.top) E.top = li;
    if (li >= E.top + content_rows) E.top = li - content_rows + 1;
    if (E.buf.nlines > content_rows && E.top + content_rows > E.buf.nlines)
        E.top = E.buf.nlines - content_rows;
    if (E.buf.nlines <= content_rows) E.top = 0;
    size_t col = edit_line_col();
    if (col < E.left) E.left = col;
    size_t ec = E.ed_cols > 0 ? (size_t)E.ed_cols : (size_t)E.screen_cols;
    if (ec > 0 && col >= E.left + ec) E.left = col - ec + 1;
}

size_t edit_line_col(void) {
    size_t li = cur_line();
    size_t off = E.cur - E.buf.lines[li].start;
    size_t col = 0;
    for (size_t i = 0; i < off && i < E.buf.lines[li].len; i++) {
        if (gb_at(&E.buf.gb, E.buf.lines[li].start + i) == '\t')
            col = (col / MODIF_TABSTOP + 1) * MODIF_TABSTOP;
        else col++;
    }
    return col;
}

void edit_status(void) {
    if (E.mode == MODE_SEARCH) {
        snprintf(E.status, sizeof E.status, "SEARCH: %s", E.sq);
        return;
    }
    if (E.mode == MODE_FUZZY) {
        snprintf(E.status, sizeof E.status, "FUZZY: %s  (%zu matches)",
                 E.fq, E.fcount);
        return;
    }
    if (E.mode == MODE_CMD) {
        snprintf(E.status, sizeof E.status, ":%s", E.cmdq);
        return;
    }
    if (E.msg[0] && time(NULL) - E.msg_time < 5) {
        strncpy(E.status, E.msg, sizeof E.status - 1);
        E.status[sizeof E.status - 1] = '\0';
        return;
    }
    size_t li = cur_line();
    size_t col = E.cur - E.buf.lines[li].start;
    const char *m = E.mode == MODE_INSERT ? "INSERT"
                   : E.mode == MODE_VISUAL ? "VISUAL" : "NORMAL";
    const char *fname = E.buf.name ? E.buf.name : "[no file]";
    snprintf(E.status, sizeof E.status,
             "modif %s  %s  [%s]  %zu/%zu  %zu:%zu/%zu%s%s",
             MODIF_VERSION, fname, m, E.bl->cur + 1, E.bl->n, li + 1, col + 1,
             E.buf.nlines,
             E.dirty ? "  *modified*" : "",
             E.focus == FOCUS_TERM ? "  [TERM]" : "");
}

void edit_set_status(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.msg, sizeof E.msg, fmt, ap);
    va_end(ap);
    E.msg_time = time(NULL);
}

void edit_init(void) {
    E.bl = malloc(sizeof(BufList));
    bl_init(E.bl);
    E.hl = hl_new();
    buf_init(&E.buf);
    buf_build_lines(&E.buf); /* always at least one empty line */
    buf_add(&E.buf);
    E.mode = MODE_NORMAL;
    E.focus = FOCUS_EDIT;
    E.term_open = 0;
    E.screen_rows = 24;
    E.screen_cols = 80;
    E.ed_cols = 80;
    E.cur = 0;
    E.top = 0;
    E.left = 0;
    E.mark = 0;
    E.cmdlen = 0;
    E.cmdq[0] = '\0';
    E.dirty = 0;
    E.status[0] = '\0';
    E.msg[0] = '\0';
    E.msg_time = 0;
    E.sqlen = 0;
    E.smatch = (size_t)-1;
    E.smlen = 0;
    E.sactive = 0;
    E.scur = 0;
    E.sqre = NULL;
    E.flist = NULL;
    E.fn = 0;
    E.fcap = 0;
    E.fscore = NULL;
    E.fidx = NULL;
    E.fcount = 0;
    E.fqlen = 0;
    E.fsel = 0;
    leader = 0;
    count_reset();
}

void edit_open(const char *path) {
    /* switch to an already-open buffer with the same name */
    for (size_t i = 0; i < E.bl->n; i++)
        if (E.bl->bufs[i].name && !strcmp(E.bl->bufs[i].name, path)) {
            buf_sync();
            buf_load(i);
            goto opened;
        }
    /* reuse the unnamed empty buffer in place when it is current */
    if (!E.buf.name && !E.dirty && gb_len(&E.buf.gb) == 0) {
        Buffer nb;
        buf_init(&nb);
        if (buf_open(&nb, path) == -1) {
            edit_set_status("cannot open %s: %s", path, strerror(errno));
            buf_free(&nb);
            return;
        }
        buf_sync();
        buf_free(&E.bl->bufs[E.bl->cur]);
        E.bl->bufs[E.bl->cur] = nb;
        buf_load(E.bl->cur);
        goto opened;
    }
    {
        Buffer nb;
        buf_init(&nb);
        if (buf_open(&nb, path) == -1) {
            edit_set_status("cannot open %s: %s", path, strerror(errno));
            buf_free(&nb);
            return;
        }
        buf_add(&nb);
    }
opened:
    E.cmdlen = 0;
    E.cmdq[0] = '\0';
    E.mode = MODE_NORMAL;
    E.focus = FOCUS_EDIT;
    E.term_open = 0;
    E.sqlen = 0;
    E.smatch = (size_t)-1;
    E.smlen = 0;
    E.sactive = 0;
    if (E.sqre) { re_free(E.sqre); E.sqre = NULL; }
    leader = 0;
    count_reset();
}
