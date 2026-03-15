CC = gcc
CFLAGS = -Wall -Wextra
LDFLAGS = -lX11 -lXext

all: screenshot

screenshot: screenshot.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f screenshot

.PHONY: all clean
