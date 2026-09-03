#ifndef __APPLE__
#define _XOPEN_SOURCE 700
#endif

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>

#ifdef __APPLE__
#include <util.h>
#else
#include <pty.h>
#endif

#include "pty.h"

int pty_open(Pty *p, int rows, int cols) {
    struct winsize ws;
    ws.ws_row = (unsigned short)rows;
    ws.ws_col = (unsigned short)cols;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;
    p->master = -1;
    p->pid = forkpty(&p->master, NULL, NULL, &ws);
    if (p->pid < 0) return -1;
    if (p->pid == 0) {
        const char *sh = getenv("SHELL");
        if (!sh || !*sh) sh = "/bin/sh";
        const char *base = strrchr(sh, '/');
        base = base ? base + 1 : sh;
        execl(sh, base, NULL);
        _exit(127);
    }
    p->rows = rows;
    p->cols = cols;
    p->cells = calloc((size_t)rows * cols, sizeof(Cell));
    p->cx = 0;
    p->cy = 0;
    p->scroll_top = 0;
    p->scroll_bot = rows - 1;
    p->sav_cx = 0;
    p->sav_cy = 0;
    p->fg = -1;
    p->bg = -1;
    p->bold = 0;
    p->state = 0;
    p->eslen = 0;
    int fl = fcntl(p->master, F_GETFL, 0);
    fcntl(p->master, F_SETFL, fl | O_NONBLOCK);
    return 0;
}

void pty_close(Pty *p) {
    if (p->master >= 0) {
        close(p->master);
        p->master = -1;
    }
    if (p->pid > 0) {
        kill(p->pid, SIGHUP);
        waitpid(p->pid, NULL, 0);
        p->pid = -1;
    }
    free(p->cells);
    p->cells = NULL;
}

void pty_resize(Pty *p, int rows, int cols) {
    if (rows < 1) rows = 1;
    if (cols < 1) cols = 1;
    Cell *nc = calloc((size_t)rows * cols, sizeof(Cell));
    int cr = rows < p->rows ? rows : p->rows;
    int cc = cols < p->cols ? cols : p->cols;
    for (int r = 0; r < cr; r++)
        memcpy(nc + (size_t)r * cols, p->cells + (size_t)r * p->cols,
               (size_t)cc * sizeof(Cell));
    free(p->cells);
    p->cells = nc;
    p->rows = rows;
    p->cols = cols;
    if (p->cy >= rows) p->cy = rows - 1;
    if (p->cx >= cols) p->cx = cols - 1;
    if (p->scroll_bot >= rows) p->scroll_bot = rows - 1;
    struct winsize ws;
    ws.ws_row = (unsigned short)rows;
    ws.ws_col = (unsigned short)cols;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;
    if (p->master >= 0) {
        ioctl(p->master, TIOCSWINSZ, &ws);
        kill(p->pid, SIGWINCH);
    }
}

void pty_write(Pty *p, const char *data, size_t n) {
    if (p->master < 0) return;
    ssize_t r = write(p->master, data, n);
    (void)r;
}

/* --------------------------- minimal VT100 parser -------------------------- */

static void cell_clear(Pty *p, int r, int c) {
    Cell *x = &p->cells[(size_t)r * p->cols + c];
    x->ch = ' ';
    x->fg = -1;
    x->bg = -1;
    x->bold = 0;
}

static void term_scroll_up(Pty *p) {
    int top = p->scroll_top, bot = p->scroll_bot;
    size_t row = (size_t)p->cols;
    memmove(p->cells + (size_t)top * row, p->cells + (size_t)(top + 1) * row,
            (size_t)(bot - top) * row * sizeof(Cell));
    for (int c = 0; c < p->cols; c++) cell_clear(p, bot, c);
}

static void term_scroll_down(Pty *p) {
    int top = p->scroll_top, bot = p->scroll_bot;
    size_t row = (size_t)p->cols;
    memmove(p->cells + (size_t)(top + 1) * row, p->cells + (size_t)top * row,
            (size_t)(bot - top) * row * sizeof(Cell));
    for (int c = 0; c < p->cols; c++) cell_clear(p, top, c);
}

static void term_index(Pty *p) {
    if (p->cy == p->scroll_bot) term_scroll_up(p);
    else if (p->cy < p->rows - 1) p->cy++;
}

static void term_rev_index(Pty *p) {
    if (p->cy == p->scroll_top) term_scroll_down(p);
    else if (p->cy > 0) p->cy--;
}

static void term_put(Pty *p, unsigned char c) {
    if (p->cx >= p->cols) {
        p->cx = 0;
        term_index(p);
    }
    if (p->cy < 0 || p->cy >= p->rows) return;
    Cell *x = &p->cells[(size_t)p->cy * p->cols + p->cx];
    x->ch = c;
    x->fg = p->fg;
    x->bg = p->bg;
    x->bold = (unsigned char)p->bold;
    p->cx++;
}

static void term_clear_line(Pty *p, int mode) {
    int c0, c1;
    if (mode == 0) { c0 = p->cx; c1 = p->cols - 1; }
    else if (mode == 1) { c0 = 0; c1 = p->cx; }
    else { c0 = 0; c1 = p->cols - 1; }
    for (int c = c0; c <= c1; c++) cell_clear(p, p->cy, c);
}

static void term_clear_screen(Pty *p, int mode) {
    if (mode == 2 || mode == 3) {
        for (int r = 0; r < p->rows; r++)
            for (int c = 0; c < p->cols; c++) cell_clear(p, r, c);
    } else if (mode == 0) {
        term_clear_line(p, 0);
        for (int r = p->cy + 1; r < p->rows; r++)
            for (int c = 0; c < p->cols; c++) cell_clear(p, r, c);
    } else if (mode == 1) {
        term_clear_line(p, 1);
        for (int r = 0; r < p->cy; r++)
            for (int c = 0; c < p->cols; c++) cell_clear(p, r, c);
    }
}

static void csi_params(Pty *p, int params[8], int *np) {
    *np = 0;
    int val = 0, in = 0;
    for (size_t i = 0; i < p->eslen; i++) {
        char ch = p->es[i];
        if (ch >= '0' && ch <= '9') { val = val * 10 + (ch - '0'); in = 1; }
        else if (ch == ';') {
            if (*np < 8) params[(*np)++] = in ? val : 0;
            val = 0;
            in = 0;
        }
    }
    if (*np < 8) params[(*np)++] = in ? val : 0;
}

static int rgb_to_256(int r, int g, int b) {
    return 16 + 36 * (r * 5 / 255) + 6 * (g * 5 / 255) + (b * 5 / 255);
}

static void sgr_set(Pty *p, int params[8], int np) {
    int i = 0;
    if (np == 0) { p->fg = -1; p->bg = -1; p->bold = 0; return; }
    while (i < np) {
        int m = params[i];
        switch (m) {
        case 0: p->fg = -1; p->bg = -1; p->bold = 0; break;
        case 1: p->bold = 1; break;
        case 22: p->bold = 0; break;
        case 39: p->fg = -1; break;
        case 49: p->bg = -1; break;
        case 38:
        case 48: {
            int isbg = (m == 48);
            if (i + 2 < np && params[i + 1] == 5) {
                int v = params[i + 2];
                if (isbg) p->bg = v; else p->fg = v;
                i += 2;
            } else if (i + 4 < np && params[i + 1] == 2) {
                int v = rgb_to_256(params[i + 2], params[i + 3], params[i + 4]);
                if (isbg) p->bg = v; else p->fg = v;
                i += 4;
            }
            break;
        }
        default:
            if (m >= 30 && m <= 37) p->fg = m - 30;
            else if (m >= 90 && m <= 97) p->fg = m - 90 + 8;
            else if (m >= 40 && m <= 47) p->bg = m - 40;
            else if (m >= 100 && m <= 107) p->bg = m - 100 + 8;
            break;
        }
        i++;
    }
}

static void handle_csi(Pty *p, char fin) {
    int params[8], np;
    csi_params(p, params, &np);
    int p1 = np > 0 ? params[0] : 0;
    int p2 = np > 1 ? params[1] : 0;
    switch (fin) {
    case 'H':
    case 'f':
        p->cy = (p1 ? p1 : 1) - 1;
        p->cx = (p2 ? p2 : 1) - 1;
        if (p->cy < 0) p->cy = 0;
        if (p->cy >= p->rows) p->cy = p->rows - 1;
        if (p->cx < 0) p->cx = 0;
        if (p->cx >= p->cols) p->cx = p->cols - 1;
        break;
    case 'A':
        p->cy -= p1 ? p1 : 1;
        if (p->cy < p->scroll_top) p->cy = p->scroll_top;
        break;
    case 'B':
        p->cy += p1 ? p1 : 1;
        if (p->cy > p->scroll_bot) p->cy = p->scroll_bot;
        break;
    case 'C':
        p->cx += p1 ? p1 : 1;
        if (p->cx >= p->cols) p->cx = p->cols - 1;
        break;
    case 'D':
        p->cx -= p1 ? p1 : 1;
        if (p->cx < 0) p->cx = 0;
        break;
    case 'G':
        p->cx = (p1 ? p1 : 1) - 1;
        if (p->cx < 0) p->cx = 0;
        if (p->cx >= p->cols) p->cx = p->cols - 1;
        break;
    case 'd':
        p->cy = (p1 ? p1 : 1) - 1;
        if (p->cy < 0) p->cy = 0;
        if (p->cy >= p->rows) p->cy = p->rows - 1;
        break;
    case 'J': term_clear_screen(p, p1); break;
    case 'K': term_clear_line(p, p1); break;
    case 'm': sgr_set(p, params, np); break;
    case 'r': {
        int top = (p1 ? p1 : 1) - 1;
        int bot = (p2 ? p2 : p->rows) - 1;
        if (top < 0) top = 0;
        if (bot >= p->rows) bot = p->rows - 1;
        if (top < bot) {
            p->scroll_top = top;
            p->scroll_bot = bot;
        }
        p->cy = p->scroll_top;
        p->cx = 0;
        break;
    }
    case 's': p->sav_cx = p->cx; p->sav_cy = p->cy; break;
    case 'u': p->cx = p->sav_cx; p->cy = p->sav_cy; break;
    default: break;
    }
}

int pty_feed(Pty *p, const char *data, size_t n) {
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)data[i];
        switch (p->state) {
        case 0:
            switch (c) {
            case 0x1b: p->state = 1; p->eslen = 0; break;
            case '\r': p->cx = 0; break;
            case '\n': term_index(p); break;
            case '\b': if (p->cx > 0) p->cx--; break;
            case '\t': {
                int t = (p->cx / 8 + 1) * 8;
                if (t > p->cols - 1) t = p->cols - 1;
                p->cx = t;
                break;
            }
            case 0x07:
            case 0x00: break;
            default:
                if (c >= 0x20) term_put(p, c);
                break;
            }
            break;
        case 1: /* ESC */
            p->eslen = 0;
            switch (c) {
            case '[': p->state = 2; break;
            case ']': p->state = 4; break;
            case '7': p->sav_cx = p->cx; p->sav_cy = p->cy; p->state = 0; break;
            case '8': p->cx = p->sav_cx; p->cy = p->sav_cy; p->state = 0; break;
            case 'M': term_rev_index(p); p->state = 0; break;
            case 'D': term_index(p); p->state = 0; break;
            case 'E': term_index(p); p->cx = 0; p->state = 0; break;
            case 'c': p->state = 0; break;
            case '(':
            case ')': p->state = 3; break;
            default: p->state = 0; break;
            }
            break;
        case 3: p->state = 0; break; /* charset select */
        case 2: /* CSI */
            if (c >= 0x40 && c <= 0x7e) {
                handle_csi(p, (char)c);
                p->state = 0;
            } else if (p->eslen < sizeof p->es - 1) {
                p->es[p->eslen++] = (char)c;
            }
            break;
        case 4: /* OSC */
            if (c == 0x07) p->state = 0;
            else if (c == 0x1b) p->state = 5;
            break;
        case 5:
            if (c == '\\') p->state = 0;
            else if (c != 0x1b) p->state = 4;
            break;
        }
    }
    return 0;
}