#ifndef MODIF_EDIT_H
#define MODIF_EDIT_H

#include <stddef.h>
#include <time.h>
#include "buffer.h"

typedef void (*cmd_fn)(void);
typedef struct { int key; cmd_fn fn; } KeyBind;

enum { MODE_NORMAL = 0, MODE_INSERT, MODE_SEARCH, MODE_FUZZY };
enum { FOCUS_EDIT = 0, FOCUS_TERM };

typedef struct {
    Buffer buf;
    int mode;
    int focus;
    int term_open;
    int screen_rows, screen_cols;
    int ed_cols;
    size_t cur;
    size_t top;
    size_t left;
    int dirty;
    char status[160];
    char msg[160];
    time_t msg_time;
    /* search state */
    char sq[256];
    size_t sqlen;
    size_t smatch;
    int sactive;
    size_t scur;
    /* fuzzy finder state */
    char **flist;
    size_t fn;
    int *fscore;
    size_t *fidx;
    size_t fcount;
    char fq[128];
    size_t fqlen;
    int fsel;
} Editor;

extern Editor E;
extern int g_quit;

/* Commands, wired in config.h. */
void cmd_left(void);
void cmd_right(void);
void cmd_up(void);
void cmd_down(void);
void cmd_word_next(void);
void cmd_word_prev(void);
void cmd_word_end(void);
void cmd_line_start(void);
void cmd_line_end(void);
void cmd_line_first(void);
void cmd_goto_top(void);
void cmd_goto_bottom(void);
void cmd_enter_insert(void);
void cmd_append(void);
void cmd_insert_bol(void);
void cmd_append_eol(void);
void cmd_open_below(void);
void cmd_open_above(void);
void cmd_delete_char(void);
void cmd_backspace(void);
void cmd_delete_line(void);
void cmd_yank_line(void);
void cmd_paste(void);
void cmd_paste_before(void);
void cmd_undo(void);
void cmd_redo(void);
void cmd_search(void);
void cmd_search_next(void);
void cmd_search_prev(void);
void cmd_half_down(void);
void cmd_half_up(void);
void cmd_page_up(void);
void cmd_page_down(void);
void cmd_save(void);
void cmd_quit(void);
void cmd_fuzzy(void);
void cmd_toggle_term(void);
void cmd_switch_focus(void);

/* Core API used by main.c. */
void edit_init(void);
void edit_open(const char *path);
void edit_handle_key(int key);
void edit_after(void);
void edit_status(void);
void edit_set_status(const char *fmt, ...);
size_t edit_line_col(void);

#endif