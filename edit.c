#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <errno.h>
#include "edit.h"
#include "config.h"
#include "fuzzy.h"

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
    rec_insert(pos, s, n);
    buf_insert(&E.buf, pos, s, n);
    E.dirty = 1;
    E.cur = pos + n;
}

static void ed_del_range(size_t pos, size_t n) {
    if (!n) return;
    char *s = gb_slice(&E.buf.gb, pos, n);
    rec_delete(pos, s, n);
    free(s);
    buf_delete(&E.buf, pos, n);
    E.dirty = 1;
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

static char pend;

static void do_yank_line(void) {
    size_t li = cur_line();
    Line *l = &E.buf.lines[li];
    size_t s = l->start, n = l->len;
    size_t len = gb_len(&E.buf.gb);
    if (s + n < len) n++;
    yank_set(gb_slice(&E.buf.gb, s, n), n);
}

static void do_delete_line(void) {
    size_t li = cur_line();
    Line *l = &E.buf.lines[li];
    size_t s = l->start, n = l->len;
    size_t len = gb_len(&E.buf.gb);
    if (s + n < len) n++;
    yank_set(gb_slice(&E.buf.gb, s, n), n);
    ed_del_range(s, n);
    E.cur = s;
}

void cmd_delete_line(void) {
    if (pend == 'd') { pend = 0; do_delete_line(); }
    else pend = 'd';
}

void cmd_yank_line(void) {
    if (pend == 'y') { pend = 0; do_yank_line(); }
    else pend = 'y';
}

void cmd_paste(void) {
    if (!ylen) return;
    ed_insert_chars(E.cur, ybuf, ylen);
}
void cmd_paste_before(void) {
    if (!ylen) return;
    rec_insert(E.cur, ybuf, ylen);
    buf_insert(&E.buf, E.cur, ybuf, ylen);
    E.dirty = 1;
}

/* --------------------------------- undo ----------------------------------- */

void cmd_undo(void) {
    if (!upos) return;
    UEntry *e = &ustack[upos - 1];
    if (e->type == 0) {
        buf_delete(&E.buf, e->pos, e->len);
        E.cur = e->pos;
    } else {
        buf_insert(&E.buf, e->pos, e->data, e->len);
        E.cur = e->pos + e->len;
    }
    E.dirty = 1;
    upos--;
}

void cmd_redo(void) {
    if (upos >= un) return;
    UEntry *e = &ustack[upos];
    if (e->type == 0) {
        buf_insert(&E.buf, e->pos, e->data, e->len);
        E.cur = e->pos + e->len;
    } else {
        buf_delete(&E.buf, e->pos, e->len);
        E.cur = e->pos;
    }
    E.dirty = 1;
    upos++;
}

/* --------------------------------- search --------------------------------- */

void cmd_search(void) {
    E.scur = E.cur;
    E.sqlen = 0;
    E.sq[0] = '\0';
    E.smatch = (size_t)-1;
    E.sactive = 0;
    E.mode = MODE_SEARCH;
}

static void search_update(void) {
    if (!E.sqlen) {
        E.smatch = (size_t)-1;
        E.cur = E.scur;
        return;
    }
    E.smatch = gb_find(&E.buf.gb, E.sq, E.sqlen, E.scur);
    if (E.smatch == (size_t)-1) E.smatch = gb_find(&E.buf.gb, E.sq, E.sqlen, 0);
    if (E.smatch != (size_t)-1) E.cur = E.smatch;
    else edit_set_status("no match");
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
            search_update();
        }
        break;
    default:
        if (key >= 32 && key <= 126 && E.sqlen < sizeof E.sq - 1) {
            E.sq[E.sqlen++] = (char)key;
            E.sq[E.sqlen] = '\0';
            search_update();
        }
        break;
    }
}

void cmd_search_next(void) {
    if (!E.sqlen) return;
    size_t from = (E.smatch != (size_t)-1) ? E.smatch + E.sqlen : E.cur;
    E.smatch = gb_find(&E.buf.gb, E.sq, E.sqlen, from);
    if (E.smatch == (size_t)-1) E.smatch = gb_find(&E.buf.gb, E.sq, E.sqlen, 0);
    if (E.smatch != (size_t)-1) E.cur = E.smatch;
    E.sactive = 1;
}

void cmd_search_prev(void) {
    if (!E.sqlen) return;
    size_t before = (E.smatch != (size_t)-1) ? E.smatch : E.cur + E.sqlen;
    E.smatch = gb_rfind(&E.buf.gb, E.sq, E.sqlen, before);
    if (E.smatch == (size_t)-1)
        E.smatch = gb_rfind(&E.buf.gb, E.sq, E.sqlen, gb_len(&E.buf.gb) + 1);
    if (E.smatch != (size_t)-1) E.cur = E.smatch;
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
    if (key == ESC || key == 'v') { E.mode = MODE_NORMAL; return; }
    if (key == 'x' || key == 'd') { sel_delete(); return; }
    if (key == 'y') { sel_yank(); return; }
    cmd_fn fn = NULL;
    for (size_t i = 0; i < NORMAL_KEYS_LEN; i++)
        if (normal_keys[i].key == key) { fn = normal_keys[i].fn; break; }
    if (fn) {
        int keep = (fn == cmd_delete_line || fn == cmd_yank_line);
        fn();
        if (!keep) pend = 0;
    } else {
        pend = 0;
    }
}

/* ------------------------------ : command mode ---------------------------- */

void cmd_colon(void) {
    E.mode = MODE_CMD;
    E.cmdlen = 0;
    E.cmdq[0] = '\0';
}

static void cmd_execute(void) {
    char *c = E.cmdq;
    if (!strcmp(c, "w") || !strcmp(c, "write")) {
        cmd_save();
    } else if (!strcmp(c, "q")) {
        if (E.dirty)
            edit_set_status("No write since last change (use :q! to quit)");
        else
            g_quit = 1;
    } else if (!strcmp(c, "q!") || !strcmp(c, "quit!")) {
        g_quit = 1;
    } else if (!strcmp(c, "wq") || !strcmp(c, "x")) {
        cmd_save();
        if (!E.dirty) g_quit = 1;
    } else if (!strcmp(c, "wq!")) {
        cmd_save();
        g_quit = 1;
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
    if (E.dirty && quit_times) {
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

void edit_handle_key(int key) {
    if (key != CTRL('Q')) quit_times = 3;
    if (E.mode == MODE_SEARCH) { search_key(key); return; }
    if (E.mode == MODE_FUZZY) { fuzzy_key(key); return; }
    if (E.mode == MODE_CMD) { cmd_key(key); return; }
    if (E.mode == MODE_VISUAL) {
        for (size_t i = 0; i < GLOBAL_KEYS_LEN; i++)
            if (global_keys[i].key == key) { global_keys[i].fn(); return; }
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
    if (key == ESC) {
        pend = 0;
        return;
    }
    cmd_fn fn = NULL;
    for (size_t i = 0; i < NORMAL_KEYS_LEN; i++)
        if (normal_keys[i].key == key) { fn = normal_keys[i].fn; break; }
    if (fn) {
        int keep = (fn == cmd_delete_line || fn == cmd_yank_line);
        fn();
        if (!keep) pend = 0;
    } else {
        pend = 0;
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
            col = (col / 8 + 1) * 8;
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
             "modif %s  %s  [%s]  %zu:%zu/%zu%s%s",
             MODIF_VERSION, fname, m, li + 1, col + 1, E.buf.nlines,
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
    buf_init(&E.buf);
    buf_build_lines(&E.buf); /* always at least one empty line */
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
    E.sactive = 0;
    E.scur = 0;
    E.flist = NULL;
    E.fn = 0;
    E.fscore = NULL;
    E.fidx = NULL;
    E.fcount = 0;
    E.fqlen = 0;
    E.fsel = 0;
}

void edit_open(const char *path) {
    Buffer nb;
    buf_init(&nb);
    if (buf_open(&nb, path) == -1) {
        edit_set_status("cannot open %s: %s", path, strerror(errno));
        buf_free(&nb);
        return;
    }
    buf_free(&E.buf);
    E.buf = nb;
    E.cur = 0;
    E.top = 0;
    E.left = 0;
    E.mark = 0;
    E.cmdlen = 0;
    E.cmdq[0] = '\0';
    E.dirty = 0;
    E.mode = MODE_NORMAL;
    E.focus = FOCUS_EDIT;
    E.term_open = 0;
    E.sqlen = 0;
    E.smatch = (size_t)-1;
    E.sactive = 0;
    for (size_t i = 0; i < un; i++) free(ustack[i].data);
    un = 0;
    upos = 0;
    free(ybuf);
    ybuf = NULL;
    ylen = 0;
    pend = 0;
}