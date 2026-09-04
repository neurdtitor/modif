CC      ?= cc
CFLAGS  ?= -O2 -std=c11 -Wall -Wextra
LDLIBS   = $(shell uname -s | grep -q Linux && echo -lutil)

# Auto-discover sources: language highlighters are just dropped in as
# hl_*.c files and are picked up with no Makefile edits.
OBJS = $(patsubst %.c,%.o,$(wildcard *.c))

modif: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDLIBS)

%.o: %.c config.h
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f modif $(OBJS)

.PHONY: clean