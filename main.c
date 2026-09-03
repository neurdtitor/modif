#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <errno.h>
#include "terminal.h"
#include "input.h"
#include "edit.h"
#include "config.h"
#include "pty.h"

/* ------------------------------- append buffer ---------------------------- */

struct abuf { char *b; int len; int cap; };
#define ABUF_INIT { NULL, 0, 0 }

static void abAppend(struct abuf *ab, const char *s, int len) {
    if (ab->len + len > ab->cap) {
        int ncap = ab->cap ? ab->cap * 2 : 4096;
        while (ncap < ab->len + len) ncap *= 2;
        ab->b = realloc(ab->b, (size_t)ncap);
        ab->cap = ncap;
    }
    memcpy(ab->b + ab->len, s, (size_t)len);
    ab->len += len;
}

static void abFree(struct abuf *ab) { free(ab->b); }

/* Write the whole frame. stdout is blocking, so this completes once the
 * terminal drains; a paused terminal is the only thing that stalls here,
 * which is fine since no input is expected then. */
static void write_all(int fd, const char *buf, size_t n) {
    while (n > 0) {
        ssize_t r = write(fd, buf, n);
        if (r < 0) {
            if (errno == EINTR) continue;
            return;
        }
        buf += r;
        n -= (size_t)r;
    }
}

/* --------------------------------- terminal -------------------------------- */

static Pty term;
static int term_alive = 0;

static char inq[8192];
static size_t inqlen = 0;

static void sync_term(void) {
    int trows = E.screen_rows - 1;
    int tcols = E.screen_cols - E.ed_cols - 1;
    if (trows < 1) trows = 1;
    if (tcols < 2) tcols = 2;
    if (E.term_open && !term_alive) {
        if (pty_open(&term, trows, tcols) != 0) {
            E.term_open = 0;
            edit_set_status("cannot spawn terminal");
        } else {
            term_alive = 1;
        }
    } else if (!E.term_open && term_alive) {
        pty_close(&term);
        term_alive = 0;
    } else if (term_alive && (term.rows != trows || term.cols != tcols)) {
        pty_resize(&term, trows, tcols);
    }
}

/* --------------------------------- rendering ------------------------------- */

static void rgb_of(int idx, int *r, int *g, int *b) {
    if (idx < 16) {
        static const int basic[16][3] = {
            {0, 0, 0}, {128, 0, 0}, {0, 128, 0}, {128, 128, 0},
            {0, 0, 128}, {128, 0, 128}, {0, 128, 128}, {192, 192, 192},
            {128, 128, 128}, {255, 0, 0}, {0, 255, 0}, {255, 255, 0},
            {0, 0, 255}, {255, 0, 255}, {0, 255, 255}, {255, 255, 255}};
        *r = basic[idx][0]; *g = basic[idx][1]; *b = basic[idx][2];
    } else if (idx < 232) {
        int n = idx - 16;
        int cr = n / 36, cg = (n / 6) % 6, cb = n % 6;
        *r = cr ? 55 + cr * 40 : 0;
        *g = cg ? 55 + cg * 40 : 0;
        *b = cb ? 55 + cb * 40 : 0;
    } else {
        int v = 8 + (idx - 232) * 10;
        *r = v; *g = v; *b = v;
    }
}

static void ab_fg(struct abuf *ab, int idx) {
    char buf[32];
    if (idx < 0) { abAppend(ab, "\x1b[39m", 5); return; }
    int r, g, b;
    rgb_of(idx, &r, &g, &b);
    int n = snprintf(buf, sizeof buf, "\x1b[38;2;%d;%d;%dm", r, g, b);
    abAppend(ab, buf, n);
}

static void ab_bg(struct abuf *ab, int idx) {
    char buf[32];
    if (idx < 0) { abAppend(ab, "\x1b[49m", 5); return; }
    int r, g, b;
    rgb_of(idx, &r, &g, &b);
    int n = snprintf(buf, sizeof buf, "\x1b[48;2;%d;%d;%dm", r, g, b);
    abAppend(ab, buf, n);
}

/* Render a line's visible window [skip, skip+maxc), expanding tabs and
 * highlighting occurrences of q and the selection range hl0..hl1
 * (raw char indices within the line) in reverse video. */
static void render_line(struct abuf *ab, const char *raw, size_t rawlen,
                        size_t skip, size_t maxc, const char *q, size_t qlen,
                        size_t hl0, size_t hl1) {
    size_t col = 0;
    size_t mstart[32];
    size_t nm = 0;
    if (q && qlen) {
        for (size_t i = 0; i + qlen <= rawlen && nm < 32; i++)
            if (!memcmp(raw + i, q, qlen)) mstart[nm++] = i;
    }
    size_t mi = 0;
    for (size_t i = 0; i < rawlen; i++) {
        if (nm && mi < nm && i == mstart[mi] + qlen) mi++;
        int inm = (nm && mi < nm && i >= mstart[mi]) || (i >= hl0 && i < hl1);
        char c = raw[i];
        if (c == '\t') {
            size_t stop = (col / MODIF_TABSTOP + 1) * MODIF_TABSTOP;
            while (col < stop) {
                if (col >= skip && col < skip + maxc) {
                    if (inm) abAppend(ab, "\x1b[7m", 4);
                    abAppend(ab, " ", 1);
                    if (inm) abAppend(ab, "\x1b[27m", 5);
                }
                col++;
            }
        } else {
            if (col >= skip && col < skip + maxc) {
                if (inm) abAppend(ab, "\x1b[7m", 4);
                abAppend(ab, &c, 1);
                if (inm) abAppend(ab, "\x1b[27m", 5);
            }
            col++;
        }
        if (col >= skip + maxc) break;
    }
    while (col < skip + maxc) {
        abAppend(ab, " ", 1);
        col++;
    }
}

static int vis_len(const char *s) {
    int n = 0;
    for (size_t i = 0; s[i]; i++) {
        if (s[i] == '\x1b') {
            i++;
            if (s[i] == '[') {
                while (s[i] && !(s[i] >= '@' && s[i] <= '~')) i++;
            }
            continue;
        }
        n++;
    }
    return n;
}

static void render_fuzzy_line(struct abuf *ab, int y, int poptop, int poph) {
    char tmp[1024];
    int rel = y - poptop;
    if (rel == 0) {
        snprintf(tmp, sizeof tmp, "FUZZY: %s", E.fq);
    } else {
        int vis = poph - 1;
        int n = (int)E.fcount;
        int start = E.fsel - (vis - 1) / 2;
        if (start < 0) start = 0;
        if (start + vis > n) start = n - vis;
        if (start < 0) start = 0;
        int li = rel - 1 + start;
        if (li < n) {
            const char *path = E.flist[E.fidx[li]];
            if (li == E.fsel)
                snprintf(tmp, sizeof tmp, "\x1b[7m> %s\x1b[27m", path);
            else
                snprintf(tmp, sizeof tmp, "  %s", path);
        } else {
            tmp[0] = '\0';
        }
    }
    int l = vis_len(tmp);
    if (l > E.ed_cols) l = E.ed_cols;
    abAppend(ab, tmp, (int)strlen(tmp));
    while (l < E.ed_cols) { abAppend(ab, " ", 1); l++; }
    abAppend(ab, "\x1b[0K", 4);
}

static void render_term_row(struct abuf *ab, int y) {
    int ncols = E.screen_cols - E.ed_cols - 1;
    int curfg = -1, curbg = -1, curbold = -1;
    for (int x = 0; x < ncols; x++) {
        Cell *c = NULL;
        if (x < term.cols) c = &term.cells[(size_t)y * term.cols + x];
        char ch = c ? (char)c->ch : ' ';
        int fg = c ? c->fg : -1;
        int bg = c ? c->bg : -1;
        int bd = c ? c->bold : 0;
        if (fg != curfg) { ab_fg(ab, fg); curfg = fg; }
        if (bg != curbg) { ab_bg(ab, bg); curbg = bg; }
        if (bd != curbold) {
            abAppend(ab, bd ? "\x1b[1m" : "\x1b[22m", bd ? 4 : 5);
            curbold = bd;
        }
        abAppend(ab, &ch, 1);
    }
    abAppend(ab, "\x1b[0m", 4);
}

static void render_content(struct abuf *ab) {
    int rows = E.screen_rows - 1;
    int maxc = E.ed_cols;
    int skip = (int)E.left;
    const char *q = (E.sqlen && (E.sactive || E.mode == MODE_SEARCH)) ? E.sq : NULL;
    size_t qlen = q ? E.sqlen : 0;
    size_t s0 = SIZE_MAX, s1 = 0;
    if (E.mode == MODE_VISUAL) {
        if (E.mark <= E.cur) { s0 = E.mark; s1 = E.cur; }
        else { s0 = E.cur; s1 = E.mark; }
    }

    int poptop = rows / 3;
    int poph = 0;
    if (E.mode == MODE_FUZZY) {
        int vis = E.fcount > 9 ? 9 : (int)E.fcount;
        poph = 1 + vis;
        if (poptop + poph > rows) poptop = rows - poph;
        if (poptop < 0) poptop = 0;
    }

    size_t need = (size_t)maxc * 8 + 1024;
    static char *raw;
    static size_t rawcap;
    if (need > rawcap) {
        raw = realloc(raw, need);
        rawcap = need;
    }
    for (int y = 0; y < rows; y++) {
        if (E.mode == MODE_FUZZY && y >= poptop && y < poptop + poph) {
            render_fuzzy_line(ab, y, poptop, poph);
        } else {
            size_t li = E.top + (size_t)y;
            if (li >= E.buf.nlines) {
                abAppend(ab, "~", 1);
                int n = maxc - 1;
                while (n-- > 0) abAppend(ab, " ", 1);
            } else {
                size_t hl0 = SIZE_MAX, hl1 = 0;
                if (E.mode == MODE_VISUAL) {
                    Line *l = &E.buf.lines[li];
                    size_t ls = l->start, le = l->start + l->len;
                    if (s1 > ls && s0 < le) {
                        hl0 = s0 > ls ? s0 - ls : 0;
                        hl1 = s1 < le ? s1 - ls : l->len;
                    }
                }
                buf_line_slice(&E.buf, li, raw, rawcap);
                render_line(ab, raw, strlen(raw), (size_t)skip, (size_t)maxc,
                            q, qlen, hl0, hl1);
            }
            abAppend(ab, "\x1b[0K", 4);
        }
        if (E.term_open) {
            abAppend(ab, "\x1b[7m", 4);
            abAppend(ab, "\x1b(0x\x1b(B", 7); /* VT100 line-drawing: vertical bar */
            abAppend(ab, "\x1b[0m", 4);
            if (term_alive && y < term.rows) {
                render_term_row(ab, y);
            } else {
                int nc = E.screen_cols - E.ed_cols - 1;
                while (nc-- > 0) abAppend(ab, " ", 1);
            }
        }
        abAppend(ab, "\r\n", 2);
    }
}

static void render_status(struct abuf *ab) {
    edit_status();
    abAppend(ab, "\x1b[0K", 4);
    abAppend(ab, "\x1b[7m", 4);
    int len = (int)strlen(E.status);
    if (len > E.screen_cols) len = E.screen_cols;
    abAppend(ab, E.status, len);
    while (len < E.screen_cols) { abAppend(ab, " ", 1); len++; }
    abAppend(ab, "\x1b[0m", 4);
}

static void place_cursor(struct abuf *ab) {
    char buf[32];
    int row = 0, col = 0;
    if (E.focus == FOCUS_TERM && term_alive) {
        row = term.cy;
        col = E.ed_cols + 1 + term.cx;
        if (col >= E.screen_cols) col = E.screen_cols - 1;
        if (row >= E.screen_rows - 1) row = E.screen_rows - 2;
    } else if (E.mode == MODE_CMD) {
        row = E.screen_rows - 1;
        col = 1 + (int)E.cmdlen;
        if (col >= E.screen_cols) col = E.screen_cols - 1;
    } else if (E.mode == MODE_FUZZY) {
        int rows = E.screen_rows - 1;
        int poptop = rows / 3;
        int vis = E.fcount > 9 ? 9 : (int)E.fcount;
        int poph = 1 + vis;
        if (poptop + poph > rows) poptop = rows - poph;
        if (poptop < 0) poptop = 0;
        row = poptop;
        col = 7 + (int)E.fqlen;
        if (col >= E.ed_cols) col = E.ed_cols - 1;
    } else {
        size_t li = buf_line_of(&E.buf, E.cur);
        row = (int)(li - E.top);
        if (row < 0) row = 0;
        if (row >= E.screen_rows - 1) row = E.screen_rows - 2;
        col = (int)(edit_line_col() - E.left);
        if (col < 0) col = 0;
        if (col >= E.ed_cols) col = E.ed_cols - 1;
    }
    snprintf(buf, sizeof buf, "\x1b[%d;%dH", row + 1, col + 1);
    abAppend(ab, buf, (int)strlen(buf));
}

static void render(void) {
    struct abuf ab = ABUF_INIT;
    abAppend(&ab, "\x1b[?25l", 6);
    abAppend(&ab, "\x1b[H", 3);
    render_content(&ab);
    render_status(&ab);
    place_cursor(&ab);
    abAppend(&ab, "\x1b[?25h", 6);
    write_all(STDOUT_FILENO, ab.b, (size_t)ab.len);
    abFree(&ab);
}

/* ---------------------------------- input ---------------------------------- */

static void handle_key(int key) {
    for (size_t i = 0; i < GLOBAL_KEYS_LEN; i++)
        if (global_keys[i].key == key) { edit_handle_key(key); return; }
    if (E.focus == FOCUS_TERM && term_alive) {
        char b[16];
        size_t n;
        kb_to_bytes(key, b, &n);
        pty_write(&term, b, n);
        return;
    }
    edit_handle_key(key);
}

static void process_stdin(void) {
again:;
    /* Blocking reads, gated by poll. VMIN=0 means a read returns whatever is
     * available immediately; a short read means the tty buffer is drained. */
    while (1) {
        size_t avail = sizeof inq - inqlen;
        ssize_t r = read(STDIN_FILENO, inq + inqlen, avail);
        if (r <= 0) break;
        inqlen += (size_t)r;
        if ((size_t)r < avail) break; /* drained */
    }
    size_t off = 0;
    while (off < inqlen) {
        int consumed = 0;
        int key = kb_decode(inq + off, inqlen - off, &consumed);
        if (key == 0) {
            if (inq[off] == ESC) {
                /* Escape-prefixed: a bare ESC is the ESC key, while a partial
                 * or unknown escape sequence needs the rest of its bytes. Wait
                 * briefly for them; if they never arrive, drop the whole
                 * sequence so the input buffer can never stall the editor. */
                fd_set rfds;
                struct timeval tv = { 0, 8000 };
                FD_ZERO(&rfds);
                FD_SET(STDIN_FILENO, &rfds);
                if (select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv) > 0) {
                    memmove(inq, inq + off, inqlen - off);
                    inqlen -= off;
                    goto again;
                }
                if (inqlen - off == 1) {
                    key = ESC;              /* bare ESC key */
                    consumed = 1;
                } else if (consumed < 1)
                    consumed = 1;           /* drop at least the ESC */
                /* else: drop the whole partial/unknown sequence */
            } else {
                consumed = 1; /* unknown byte: drop it so input can't stall */
            }
        }
        if (key)
            handle_key(key);
        off += (size_t)consumed;
    }
    memmove(inq, inq + off, inqlen - off);
    inqlen -= off;
}

/* ----------------------------------- main ---------------------------------- */

int main(int argc, char **argv) {
    if (term_size(&E.screen_rows, &E.screen_cols) == -1) {
        fprintf(stderr, "modif: cannot determine terminal size\n");
        return 1;
    }
    if (term_raw() == -1) {
        fprintf(stderr, "modif: not a terminal\n");
        return 1;
    }
    atexit(term_restore);
    edit_init();
    if (argc > 1) edit_open(argv[1]);
    E.ed_cols = E.screen_cols;

    while (!g_quit) {
        int rr, cc;
        if (term_size(&rr, &cc) == 0 &&
            (rr != E.screen_rows || cc != E.screen_cols)) {
            E.screen_rows = rr;
            E.screen_cols = cc;
        }
        E.ed_cols = E.term_open ? E.screen_cols / 2 : E.screen_cols;
        sync_term();
        edit_after();
        render();

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        int maxfd = STDIN_FILENO;
        if (term_alive) {
            FD_SET(term.master, &rfds);
            if (term.master > maxfd) maxfd = term.master;
        }
        int pr = select(maxfd + 1, &rfds, NULL, NULL, NULL);
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (term_alive && FD_ISSET(term.master, &rfds)) {
            char tbuf[8192];
            ssize_t n = read(term.master, tbuf, sizeof tbuf);
            if (n > 0) { pty_feed(&term, tbuf, (size_t)n); }
            else if (n == 0) {
                pty_close(&term);
                term_alive = 0;
                E.term_open = 0;
                E.focus = FOCUS_EDIT;
                E.mode = MODE_NORMAL;
                edit_set_status("terminal exited");
            }
        }
        if (FD_ISSET(STDIN_FILENO, &rfds)) process_stdin();
    }
    term_restore();
    return 0;
}