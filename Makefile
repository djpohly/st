# st - simple terminal
# See LICENSE file for copyright and license details.
.POSIX:

include config.mk

SRC = st.c x.c wl.o
OBJ = $(SRC:.c=.o)

all: options st wterm

options:
	@echo st build options:
	@echo "CFLAGS  = $(STCFLAGS)"
	@echo "LDFLAGS = $(STLDFLAGS)"
	@echo "CC      = $(CC)"

config.h:
	cp config.def.h config.h

WAYLAND_XML = $(wildcard wayland/*.xml)
WAYLAND_HDR = $(WAYLAND_XML:.xml=-client-protocol.h)
WAYLAND_SRC = $(WAYLAND_HDR:.h=.c)
WAYLAND_OBJ = $(WAYLAND_SRC:.c=.o)

wayland/%-client-protocol.c: wayland/%.xml
	wayland-scanner code < $< > $@

wayland/%-client-protocol.h: wayland/%.xml
	wayland-scanner client-header < $< > $@

.c.o:
	$(CC) $(STCFLAGS) -c -o $@ $<

st.o: config.h st.h win.h
x.o: arg.h st.h win.h
wl.o: arg.h st.h win.h $(WAYLAND_HDR) $(WAYLAND_SRC)

$(OBJ): config.h config.mk

st: st.o x.o
	$(CC) -o $@ $^ $(STLDFLAGS)

wterm: st.o wl.o $(WAYLAND_OBJ)
	$(CC) -o $@ $^ $(STLDFLAGS)

clean:
	rm -f st wterm $(OBJ) $(WAYLAND_OBJ) $(WAYLAND_HDR) $(WAYLAND_SRC) st-$(VERSION).tar.gz

dist: clean
	mkdir -p st-$(VERSION)
	cp -R LICENSE Makefile README config.mk config.def.h st.info st.1 arg.h $(SRC) st-$(VERSION)
	tar -cf - st-$(VERSION) | gzip > st-$(VERSION).tar.gz
	rm -rf st-$(VERSION)

install: st
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f st $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/st
	mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	sed "s/VERSION/$(VERSION)/g" < st.1 > $(DESTDIR)$(MANPREFIX)/man1/st.1
	chmod 644 $(DESTDIR)$(MANPREFIX)/man1/st.1
	tic -sx st.info
	@echo Please see the README file regarding the terminfo entry of st.

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/st
	rm -f $(DESTDIR)$(MANPREFIX)/man1/st.1

.PHONY: all options clean dist install uninstall
