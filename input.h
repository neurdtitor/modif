#ifndef MODIF_INPUT_H
#define MODIF_INPUT_H

#include <stddef.h>

#define ESC 27
#define ENTER 13
#define TAB 9
#define BACKSPACE 127

#define CTRL(c) ((c) & 0x1f)

enum {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN,
    KEY_F1,
    KEY_F2,
    KEY_F3,
    KEY_F4,
    KEY_F5,
    KEY_F6,
    KEY_F7,
    KEY_F8,
    KEY_F9,
    KEY_F10,
    KEY_F11,
    KEY_F12
};

/* Decode one key from buf. Returns the decoded key or 0 if more bytes are
 * needed / the sequence is unknown. *consumed receives bytes used. */
int kb_decode(const char *buf, size_t len, int *consumed);

/* Re-encode a decoded key back into raw bytes (for forwarding to a PTY). */
void kb_to_bytes(int key, char *out, size_t *outlen);

#endif