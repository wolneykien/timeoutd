CFLAGS = -fomit-frame-pointer -O2 -s -g -Wall
binaries = timeoutd dump_utmp

sbindir = /usr/sbin
man5dir = /usr/share/man/man5
man8dir = /usr/share/man/man8
sysconfdir = /etc
pkgsysconfdir = $(sysconfdir)/timeoutd
initdir = $(sysconfdir)/rc.d/init.d

INSTALL = install
DESTDIR =

all: dump_utmp timeoutd

timeoutd: timeoutd.c Makefile
	$(CC) $(CFLAGS) -o timeoutd.o -c timeoutd.c -DTIMEOUTDX11
	$(CC) $(CFLAGS) -o timeoutd timeoutd.o -lXss -lXext -lX11

dump_utmp: dump_utmp.c
	$(CC) $(CFLAGS) -o dump_utmp dump_utmp.c

install:
	$(INSTALL) -D -m 0751 timeoutd $(DESTDIR)$(sbindir)/timeoutd
	$(INSTALL) -D -m 0444 timeoutd.8 $(DESTDIR)$(man8dir)/timeoutd.8
	$(INSTALL) -D -m 0444 timeouts.5 $(DESTDIR)$(man5dir)/timeouts.5
	$(INSTALL) -D -m 0444 timeouts $(DESTDIR)$(pkgsysconfdir)/timeouts
	$(INSTALL) -D -m 0755 timeoutd.init $(DESTDIR)/$(initdir)/timeoutd

clean:
	rm -f $(binaries) *.o
