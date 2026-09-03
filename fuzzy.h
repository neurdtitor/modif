#ifndef MODIF_FUZZY_H
#define MODIF_FUZZY_H

#include "edit.h"

int fuzzy_match(const char *q, const char *t, int *score);
void fuzzy_build(Editor *E);
void fuzzy_filter(Editor *E);
void fuzzy_free(Editor *E);

#endif