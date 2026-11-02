.POSIX:

CONFIGFILE = config.mk
include $(CONFIGFILE)

OBJ =\
	fsshield.o

HDR =


all: fsshield
$(OBJ): $(HDR)

.c.o:
	$(CC) -c -o $@ $< $(CFLAGS) $(GOV_CFLAGS) $(CPPFLAGS) $(GOV_CPPFLAGS)

fsshield: $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDFLAGS) $(COV_LDFLAGS)
	$(BUILD_SUDO) sh -c 'chown -- 0:0 $@ && chmod -- 4755 $@'

install: fsshield
	mkdir -p -- "$(DESTDIR)$(PREFIX)/bin"
	mkdir -p -- "$(DESTDIR)$(MANPREFIX)/man1/"
	cp -- fsshield "$(DESTDIR)$(PREFIX)/bin/"
	cp -- fsshield.1 "$(DESTDIR)$(MANPREFIX)/man1/"

post-install:
	chown -- '0:0' "$(DESTDIR)$(PREFIX)/bin/fsshield"
	chmod -- 4755 "$(DESTDIR)$(PREFIX)/bin/fsshield"

uninstall:
	-rm -f -- "$(DESTDIR)$(PREFIX)/bin/fsshield"
	-rm -f -- "$(DESTDIR)$(MANPREFIX)/man1/fsshield.1"

clean:
	-rm -f -- *.o *.a *.lo *.su *.so *.so.* *.gch
	-rm -f -- *.gcov *.gcno *.gcda *.t *.to *.f *.fo
	-rm -f -- fsshield

.SUFFIXES:
.SUFFIXES: .o .c

.PHONY: all install uninstall post-install clean
