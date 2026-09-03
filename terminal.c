#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "terminal.h"

static struct termios orig;
static int rawmode = 0;

int term_raw(void) {
    if (rawmode) return 0;
    if (!isatty(STDIN_FILENO)) return -1;
    if (tcgetattr(STDIN_FILENO, &orig) == -1) return -1;
    struct termios raw = orig;
    raw.c_iflag &= (tcflag_t)~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= (tcflag_t)~(OPOST);
    raw.c_cflag |= CS8;
    raw.c_lflag &= (tcflag_t)~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) return -1;
    rawmode = 1;
    return 0;
}

void term_restore(void) {
    if (rawmode) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);
        rawmode = 0;
    }
}

int term_size(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
        return -1;
    *rows = ws.ws_row;
    *cols = ws.ws_col;
    return 0;
}