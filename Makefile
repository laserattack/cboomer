CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -lX11 -lXext -lGL -lXrandr -lGLEW -lm
TARGET = cboomer

all: $(TARGET)

$(TARGET): cboomer.c screenshot.h la.h config.h
	$(CC) $(CFLAGS) -o $@ cboomer.c $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
