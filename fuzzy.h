#ifndef MODIF_FUZZY_H
#define MODIF_FUZZY_H

#include "edit.h"

int fuzzy_match(const char *q, const char *t, int *score);
void fuzzy_build(Editor *ed);
void fuzzy_filter(Editor *ed);
void fuzzy_free(Editor *ed);

#endif