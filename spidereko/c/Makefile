# spidereko - build with:  make        install for this user:  make install
CC      ?= cc
CFLAGS  ?= -O2
CFLAGS  += -std=gnu11 -Wall -Wextra -Wno-format-truncation $(shell pkg-config --cflags cairo x11 xext xrandr)
LDLIBS  += $(shell pkg-config --libs cairo x11 xext xrandr) -lm
OBJS     = main.o config.o sim.o paint.o proc.o gui.o ui.o

spidereko: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

%.o: %.c spidereko.h
	$(CC) $(CFLAGS) -c $< -o $@

install: spidereko
	./spidereko -install

uninstall: spidereko
	./spidereko -uninstall

clean:
	rm -f $(OBJS) spidereko

.PHONY: install uninstall clean
