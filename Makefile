CFLAGS=-fomit-frame-pointer -O2 -s -g -Wall
binaries=timeoutd dump_utmp

all: dump_utmp timeoutd

timeoutd:	timeoutd.c Makefile
	$(CC) $(CFLAGS) -o timeoutd.o -c timeoutd.c -DTIMEOUTDX11
	$(CC) $(CFLAGS) -o timeoutd timeoutd.o -lXss -lXext -lX11

dump_utmp: dump_utmp.c
	$(CC) $(CFLAGS) -o dump_utmp dump_utmp.c

install:
	install -o root -g root -m 751 timeoutd /usr/sbin/timeoutd
	install -o root -g root -m 444 timeoutd.8 /usr/share/man/man8
	install -o root -g root -m 444 timeouts.5 /usr/share/man/man5

clean:
	rm -f $(binaries) *.o
