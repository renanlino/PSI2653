CC=gcc
CFLAGS=-Wall -Wextra
LIBS=-lm -lpthread

webserver: webserver.o
	$(CC) -o webserver webserver.o $(CFLAGS) $(LIBS)

clean:
	rm -f *.o
