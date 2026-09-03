#ifndef MODIF_CONFIG_H
#define MODIF_CONFIG_H

#include "edit.h"
#include "input.h"

#define MODIF_VERSION "0.0.1"
#define MODIF_TABSTOP 8

/* Leader key: press it, then one of leader_keys (e.g. <space>f = fuzzy).
 * Works in normal and visual mode. */
#define LEADER_KEY ' '

/* Global bindings, active in every mode. */
static const KeyBind global_keys[] = {
    { CTRL('S'), cmd_save },
    { CTRL('Q'), cmd_quit },
    { CTRL('F'), cmd_fuzzy },
    { CTRL('T'), cmd_toggle_term },
    { CTRL('W'), cmd_switch_focus },
};

/* Leader bindings: LEADER_KEY then one of these. */
static const KeyBind leader_keys[] = {
    { 'f', cmd_fuzzy },
    { 't', cmd_toggle_term },
    { 'w', cmd_switch_focus },
    { 's', cmd_save },
    { 'q', cmd_quit },
};

/* Normal-mode bindings. */
static const KeyBind normal_keys[] = {
    { 'h', cmd_left }, { 'j', cmd_down }, { 'k', cmd_up }, { 'l', cmd_right },
    { ARROW_LEFT, cmd_left }, { ARROW_RIGHT, cmd_right },
    { ARROW_UP, cmd_up }, { ARROW_DOWN, cmd_down },
    { 'w', cmd_word_next }, { 'b', cmd_word_prev }, { 'e', cmd_word_end },
    { '0', cmd_line_start }, { '$', cmd_line_end }, { '^', cmd_line_first },
    { 'g', cmd_goto_top }, { 'G', cmd_goto_bottom },
    { 'i', cmd_enter_insert }, { 'a', cmd_append },
    { 'I', cmd_insert_bol }, { 'A', cmd_append_eol },
    { 'o', cmd_open_below }, { 'O', cmd_open_above },
    { 'x', cmd_delete_char }, { 'X', cmd_backspace },
    { 'd', cmd_delete_line }, { 'y', cmd_yank_line },
    { 'p', cmd_paste }, { 'P', cmd_paste_before },
    { 'u', cmd_undo }, { CTRL('R'), cmd_redo },
    { '/', cmd_search }, { 'n', cmd_search_next }, { 'N', cmd_search_prev },
    { ':', cmd_colon }, { 'v', cmd_visual },
    { CTRL('D'), cmd_half_down }, { CTRL('U'), cmd_half_up },
    { PAGE_UP, cmd_page_up }, { PAGE_DOWN, cmd_page_down },
    { ENTER, cmd_down },
};

#define NORMAL_KEYS_LEN (sizeof(normal_keys) / sizeof(normal_keys[0]))
#define GLOBAL_KEYS_LEN (sizeof(global_keys) / sizeof(global_keys[0]))
#define LEADER_KEYS_LEN (sizeof(leader_keys) / sizeof(leader_keys[0]))

#endif