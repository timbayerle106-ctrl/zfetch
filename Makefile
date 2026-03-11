# zfetch v3.1 - Ultra-Fast Parallel System Fetcher
# Supports: Linux, macOS, Android/Termux

CC ?= gcc
CFLAGS = -O3 -flto -fomit-frame-pointer -fno-exceptions -fno-unwind-tables -fno-stack-protector
CFLAGS += -Wall -Wextra -std=c11 -pthread
LDFLAGS = -s -flto -O3 -pthread

# Platform detection
UNAME_S := $(shell uname -s 2>/dev/null || echo "Linux")

ifeq ($(UNAME_S),Linux)
    CFLAGS += -D_GNU_SOURCE
    LDFLAGS += -ldl
endif
ifeq ($(UNAME_S),Darwin)
    CFLAGS += -D_DARWIN_C_SOURCE
    LDFLAGS += -framework CoreFoundation -framework IOKit
endif

# Android cross-compilation (use with: make ANDROID=1)
ifdef ANDROID
    CC = aarch64-linux-android-clang
    CFLAGS += -D__ANDROID__ -fPIE
    LDFLAGS += -static
endif

# Termux native build detection
ifneq ($(wildcard /data/data/com.termux),)
    CFLAGS += -D__ANDROID__
endif

TARGET = zfetch
SRC = src/main.c

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

clean:
	rm -f $(TARGET)

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/

# Android builds
android-arm64:
	@echo "Cross-compiling for Android ARM64..."
	aarch64-linux-gnu-gcc $(CFLAGS) -D__ANDROID__ -static -o zfetch-android-arm64 $(SRC)

android-arm:
	@echo "Cross-compiling for Android ARM..."
	arm-linux-gnueabihf-gcc $(CFLAGS) -D__ANDROID__ -static -o zfetch-android-arm $(SRC)

# Termux build (run inside Termux)
termux:
	$(CC) $(CFLAGS) $(LDFLAGS) -o zfetch $(SRC)
