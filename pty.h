#ifndef MODIF_PTY_H
#define MODIF_PTY_H

#include <stddef.h>
#include <sys/types.h>

typedef struct {
    unsigned char ch;
    int fg; /* -1 = default, else 0..255 */
    int bg;
    unsigned char bold;
} Cell;

typedef struct {
    pid_t pid;
    int master;
    int rows, cols;
    Cell *cells;
    int cx, cy;
    int scroll_top, scroll_bot;
    int sav_cx, sav_cy;
    int fg, bg, bold;
    int state;
    char es[64];
    size_t eslen;
} Pty;

int pty_open(Pty *p, int rows, int cols);
void pty_close(Pty *p);
void pty_resize(Pty *p, int rows, int cols);
int pty_feed(Pty *p, const char *data, size_t n);
void pty_write(Pty *p, const char *data, size_t n);

#endif