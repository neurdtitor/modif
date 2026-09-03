CC      ?= cc
CFLAGS  ?= -O2 -std=c11 -Wall -Wextra
LDLIBS   = $(shell uname -s | grep -q Linux && echo -lutil)

OBJS = main.o buffer.o terminal.o input.o edit.o fuzzy.o pty.o

modif: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDLIBS)

%.o: %.c config.h
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f modif $(OBJS)

.PHONY: clean