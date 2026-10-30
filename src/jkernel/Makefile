CROSS_COMPILE ?= aarch64-none-elf-
CC := $(CROSS_COMPILE)gcc

MAKEFILE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
PROJECT_ROOT := $(abspath $(MAKEFILE_DIR)/../..)
BUILD_DIR ?= $(PROJECT_ROOT)/build/jkernel
ELF := $(BUILD_DIR)/jkernel.elf

CFLAGS := -std=c11 -O2 -Wall -Wextra -Werror \
          -mcpu=cortex-a57 -ffreestanding -fno-builtin \
          -fno-stack-protector -fno-pic -fno-pie \
          -fno-unwind-tables -fno-asynchronous-unwind-tables
LDFLAGS := -nostdlib -static -no-pie \
           -Wl,-T,$(MAKEFILE_DIR)/linker.ld \
           -Wl,--build-id=none

SOURCES := $(MAKEFILE_DIR)/start.S $(MAKEFILE_DIR)/kernel.c \
           $(MAKEFILE_DIR)/pl011.c $(MAKEFILE_DIR)/zesh.c

.PHONY: all clean
all: $(ELF)

$(ELF): $(SOURCES) $(MAKEFILE_DIR)/linker.ld \
	    $(MAKEFILE_DIR)/pl011.h $(MAKEFILE_DIR)/zesh.h
	mkdir -p "$(BUILD_DIR)"
	$(CC) $(CFLAGS) $(LDFLAGS) -o "$@" $(SOURCES)

clean:
	rm -rf "$(BUILD_DIR)"
