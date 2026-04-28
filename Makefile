CC = cc
CFLAGS = -Wall -Wextra -O2 -std=c99
LDFLAGS = -lX11 -lGL -lXext -lXrandr -lGLEW -lm
TARGET = cboomer

SRC_DIR = src

# XShm support (disabled by default, enable with: make USE_XSHM=1)
ifdef USE_XSHM
CFLAGS += -DUSE_XSHM
endif

all: $(TARGET)

$(TARGET): $(SRC_DIR)/cboomer.c $(SRC_DIR)/screenshot.h $(SRC_DIR)/la.h $(SRC_DIR)/config.h
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC_DIR)/cboomer.c $(LDFLAGS)

release: $(TARGET)
	mkdir -p release
	sharun lib4bin --with-wrappe --strip --dst-dir release ./$(TARGET)

clean:
	rm -f $(TARGET)
	rm -rf release

.PHONY: all clean
