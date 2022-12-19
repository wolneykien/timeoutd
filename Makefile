CFLAGS=-g
timeoutd:	timeoutd.c Makefile
	#$(CC) $(CFLAGS) -o timeoutd timeoutd.c
	$(CC) $(CFLAGS) -o timeoutd.o -c timeoutd.c -DTIMEOUTDX11
	$(CC) $(CFLAGS) -o timeoutd -L/usr/lib timeoutd.o -lXss -lXext -lX11
	

install:
	install -o root -g root -m 751 timeoutd /usr/sbin/timeoutd
	install -o root -g root -m 444 timeoutd.8 /usr/share/man/man8
	install -o root -g root -m 444 timeouts.5 /usr/share/man/man5

clean:
	rm -f $(binaries) *.o
