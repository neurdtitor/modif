#ifndef MODIF_TERMINAL_H
#define MODIF_TERMINAL_H

int term_raw(void);
void term_restore(void);
int term_size(int *rows, int *cols);

#endif