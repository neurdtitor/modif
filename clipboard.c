#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "clipboard.h"

/* System clipboard via tiny standard helpers: pbcopy/pbpaste on macOS,
 * wl-copy/wl-paste (Wayland) or xclip/xsel (X11) elsewhere. The first helper
 * that runs successfully wins, so having any one of them is enough. */

static const char *const clip_set_cmds[] = {
#if defined(__APPLE__)
    "pbcopy",
#else
    "wl-copy", "xclip", "xsel",
#endif
    NULL,
};

static const char *const clip_get_cmds[] = {
#if defined(__APPLE__)
    "pbpaste",
#else
    "wl-paste", "xclip", "xsel",
#endif
    NULL,
};

/* Feed data to a helper's stdin and wait for it to finish. 0 on success. */
static int clip_run(const char *const argv[], const char *data, size_t len) {
    int p[2];
    if (pipe(p) == -1) return -1;
    pid_t pid = fork();
    if (pid == -1) { close(p[0]); close(p[1]); return -1; }
    if (pid == 0) {
        close(p[1]);
        dup2(p[0], STDIN_FILENO);
        close(p[0]);
        execvp(argv[0], (char *const *)argv);
        _exit(127); /* helper not found: parent tries the next one */
    }
    close(p[0]);
    size_t off = 0;
    while (off < len) {
        ssize_t w = write(p[1], data + off, len - off);
        if (w < 0) {
            if (errno == EINTR) continue;
            break;
        }
        off += (size_t)w;
    }
    close(p[1]);
    int st = 0;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) && WEXITSTATUS(st) == 0 ? 0 : -1;
}

/* Capture a helper's stdout into a malloc'd NUL-terminated buffer. */
static char *clip_capture(const char *const argv[], size_t *outlen) {
    int p[2];
    if (pipe(p) == -1) return NULL;
    pid_t pid = fork();
    if (pid == -1) { close(p[0]); close(p[1]); return NULL; }
    if (pid == 0) {
        close(p[0]);
        dup2(p[1], STDOUT_FILENO);
        close(p[1]);
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }
    close(p[1]);
    size_t cap = 0, n = 0;
    char *buf = NULL;
    for (;;) {
        if (n + 4096 > cap) {
            cap = cap ? cap * 2 : 65536;
            buf = realloc(buf, cap + 1);
        }
        ssize_t r = read(p[0], buf + n, cap - n);
        if (r < 0) { if (errno == EINTR) continue; break; }
        if (r == 0) break;
        n += (size_t)r;
    }
    close(p[0]);
    int st = 0;
    waitpid(pid, &st, 0);
    if (!(WIFEXITED(st) && WEXITSTATUS(st) == 0)) { free(buf); return NULL; }
    if (buf) buf[n] = '\0';
    if (outlen) *outlen = n;
    return buf;
}

int clip_copy(const char *data, size_t len) {
    for (size_t i = 0; clip_set_cmds[i]; i++) {
        const char *argv[] = { clip_set_cmds[i], NULL };
        if (clip_run(argv, data, len) == 0) return 0;
    }
    return -1;
}

char *clip_paste(size_t *outlen) {
    for (size_t i = 0; clip_get_cmds[i]; i++) {
        const char *argv[] = { clip_get_cmds[i], NULL };
        char *s = clip_capture(argv, outlen);
        if (s) return s;
    }
    return NULL;
}