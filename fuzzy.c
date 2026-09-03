#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>
#include "fuzzy.h"
#include <errno.h>

#define MAX_FILES 20000
#define MAX_DEPTH 5

int fuzzy_match(const char *q, const char *t, int *score) {
    size_t qlen = strlen(q), tlen = strlen(t);
    if (!qlen) { *score = 0; return 1; }
    if (qlen > tlen) return 0;
    size_t qi = 0;
    int total = 0;
    int prev = 0;
    for (size_t ti = 0; ti < tlen && qi < qlen; ti++) {
        unsigned char c = (unsigned char)t[ti];
        if (tolower(c) == tolower((unsigned char)q[qi])) {
            int b = 0;
            if (ti == 0) b += 5;
            if (prev && (prev == '_' || prev == '-' || prev == ' ' ||
                         prev == '.' || prev == '/' || prev == '\\'))
                b += 8;
            if (prev && islower(prev) && isupper(c)) b += 7;
            if (qi == 0 && prev == '/') b += 10;
            total += b;
            qi++;
        } else {
            total -= 1;
        }
        prev = c;
    }
    if (qi != qlen) return 0;
    /* Small preference for source files when scores tie. */
    const char *dot = strrchr(t, '.');
    if (dot && dot[1]) {
        static const char *src[] = {"c", "h", "py", "rs", "go", "js", "ts",
                                    "md", "txt", "sh", "cc", "cpp", "hpp",
                                    "toml", "json", "yaml", "yml", NULL};
        for (int i = 0; src[i]; i++)
            if (!strcmp(dot + 1, src[i])) { total += 2; break; }
    }
    if (score) *score = total;
    return 1;
}

static int skip_dir(const char *n) {
    return !strcmp(n, ".git") || !strcmp(n, ".hg") || !strcmp(n, ".svn") ||
           !strcmp(n, "node_modules") || !strcmp(n, "target") ||
           !strcmp(n, "build") || !strcmp(n, "dist");
}

static void walk(Editor *E, const char *dir, int depth) {
    if (E->fn >= MAX_FILES) return;
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *de;
    char path[PATH_MAX];
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.' || skip_dir(de->d_name)) continue;
        int atroot = (dir[0] == '.' && dir[1] == '\0');
        const char *prefix = atroot ? "" : dir;
        const char *sep = atroot ? "" : "/";
        snprintf(path, sizeof path, "%s%s%s", prefix, sep, de->d_name);
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth < MAX_DEPTH) walk(E, path, depth + 1);
        } else if (S_ISREG(st.st_mode)) {
            if (E->fn == E->fcap) {
                E->fcap = E->fcap ? E->fcap * 2 : 1024;
                E->flist = realloc(E->flist, E->fcap * sizeof(char *));
            }
            const char *show = (path[0] == '.' && path[1] == '/') ? path + 2 : path;
            E->flist[E->fn++] = strdup(show);
        }
    }
    closedir(d);
}

void fuzzy_build(Editor *E) {
    fuzzy_free(E);
    walk(E, ".", 0);
    E->fscore = malloc(E->fn * sizeof(int));
    E->fidx = malloc(E->fn * sizeof(size_t));
}

static int *g_sc;

static int cmp_fidx(const void *a, const void *b) {
    size_t ia = *(const size_t *)a, ib = *(const size_t *)b;
    int sa = g_sc[ia], sb = g_sc[ib];
    if (sa != sb) return sb - sa;
    return ia < ib ? -1 : ia > ib ? 1 : 0;
}

void fuzzy_filter(Editor *E) {
    size_t n = 0;
    if (E->fqlen) {
        for (size_t i = 0; i < E->fn; i++) {
            int sc;
            if (fuzzy_match(E->fq, E->flist[i], &sc)) {
                E->fscore[i] = sc;
                E->fidx[n++] = i;
            }
        }
        if (n > 1) {
            g_sc = E->fscore;
            qsort(E->fidx, n, sizeof(size_t), cmp_fidx);
        }
    } else {
        for (size_t i = 0; i < E->fn; i++) E->fidx[i] = i;
        n = E->fn;
    }
    E->fcount = n;
    if (E->fsel >= (int)n) E->fsel = n > 0 ? (int)n - 1 : 0;
}

void fuzzy_free(Editor *E) {
    for (size_t i = 0; i < E->fn; i++) free(E->flist[i]);
    free(E->flist);
    free(E->fscore);
    free(E->fidx);
    E->flist = NULL;
    E->fn = 0;
    E->fcap = 0;
    E->fscore = NULL;
    E->fidx = NULL;
    E->fcount = 0;
    E->fsel = 0;
}