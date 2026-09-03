#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "input.h"

static int decode_csi(const char *buf, size_t len, int *consumed) {
    /* buf[0] == '[', buf[1..] = params, final byte at the end. */
    size_t i = 1;
    while (i < len &&
           (buf[i] == ';' || buf[i] == '?' || buf[i] == '<' || buf[i] == '=' ||
            (buf[i] >= '0' && buf[i] <= '9')))
        i++;
    if (i >= len) { *consumed = (int)i; return 0; } /* partial sequence */
    char fin = buf[i];
    int p1 = 0;
    size_t j = 1;
    while (j < len && buf[j] >= '0' && buf[j] <= '9') {
        p1 = p1 * 10 + (buf[j] - '0');
        j++;
    }
    *consumed = (int)i + 1;
    switch (fin) {
    case 'A': return ARROW_UP;
    case 'B': return ARROW_DOWN;
    case 'C': return ARROW_RIGHT;
    case 'D': return ARROW_LEFT;
    case 'H': return HOME_KEY;
    case 'F': return END_KEY;
    case '~':
        switch (p1) {
        case 1:
        case 7: return HOME_KEY;
        case 2: return DEL_KEY; /* insert -> treat as insert; no insert cmd yet */
        case 3: return DEL_KEY;
        case 4:
        case 8: return END_KEY;
        case 5: return PAGE_UP;
        case 6: return PAGE_DOWN;
        case 11: return KEY_F1;
        case 12: return KEY_F2;
        case 13: return KEY_F3;
        case 14: return KEY_F4;
        case 15: return KEY_F5;
        case 17: return KEY_F6;
        case 18: return KEY_F7;
        case 19: return KEY_F8;
        case 20: return KEY_F9;
        case 21: return KEY_F10;
        case 23: return KEY_F11;
        case 24: return KEY_F12;
        default: return 0;
        }
    default: return 0;
    }
}

int kb_decode(const char *buf, size_t len, int *consumed) {
    if (!len) return 0;
    unsigned char c = (unsigned char)buf[0];
    if (c != ESC) {
        *consumed = 1;
        return c;
    }
    if (len < 2) return 0;
    if (buf[1] == '[') {
        int r = decode_csi(buf + 1, len - 1, consumed);
        if (*consumed > 0) (*consumed)++; /* count the leading ESC */
        return r;
    }
    if (buf[1] == 'O') {
        if (len < 3) { *consumed = 2; return 0; } /* partial ESC O ... */
        *consumed = 3;
        switch (buf[2]) {
        case 'A': return ARROW_UP;
        case 'B': return ARROW_DOWN;
        case 'C': return ARROW_RIGHT;
        case 'D': return ARROW_LEFT;
        case 'H': return HOME_KEY;
        case 'F': return END_KEY;
        default: return 0;
        }
    }
    *consumed = 2;
    return (unsigned char)buf[1];
}

void kb_to_bytes(int key, char *out, size_t *outlen) {
    if (key >= 1 && key <= 26) { out[0] = (char)key; *outlen = 1; return; }
    if (key >= 32 && key <= 126) { out[0] = (char)key; *outlen = 1; return; }
    switch (key) {
    case ENTER: out[0] = '\r'; *outlen = 1; break;
    case '\n': out[0] = '\r'; *outlen = 1; break;
    case TAB: out[0] = '\t'; *outlen = 1; break;
    case ESC: out[0] = '\x1b'; *outlen = 1; break;
    case BACKSPACE: out[0] = 0x7f; *outlen = 1; break;
    case DEL_KEY: memcpy(out, "\x1b[3~", 5); *outlen = 5; break;
    case ARROW_UP: memcpy(out, "\x1b[A", 3); *outlen = 3; break;
    case ARROW_DOWN: memcpy(out, "\x1b[B", 3); *outlen = 3; break;
    case ARROW_RIGHT: memcpy(out, "\x1b[C", 3); *outlen = 3; break;
    case ARROW_LEFT: memcpy(out, "\x1b[D", 3); *outlen = 3; break;
    case HOME_KEY: memcpy(out, "\x1b[H", 3); *outlen = 3; break;
    case END_KEY: memcpy(out, "\x1b[F", 3); *outlen = 3; break;
    case PAGE_UP: memcpy(out, "\x1b[5~", 4); *outlen = 4; break;
    case PAGE_DOWN: memcpy(out, "\x1b[6~", 4); *outlen = 4; break;
    case KEY_F1: memcpy(out, "\x1bOP", 3); *outlen = 3; break;
    case KEY_F2: memcpy(out, "\x1bOQ", 3); *outlen = 3; break;
    case KEY_F3: memcpy(out, "\x1bOR", 3); *outlen = 3; break;
    case KEY_F4: memcpy(out, "\x1bOS", 3); *outlen = 3; break;
    default: out[0] = ' '; *outlen = 1; break;
    }
}