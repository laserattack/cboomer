CC = cc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -lX11 -lGL -lXext -lXrandr -lGLEW -lm
TARGET = cboomer

# XShm support (disabled by default, enable with: make USE_XSHM=1)
ifdef USE_XSHM
CFLAGS += -DUSE_XSHM
endif

all: $(TARGET)

$(TARGET): cboomer.c screenshot.h la.h config.h
	$(CC) $(CFLAGS) -o $@ cboomer.c $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
