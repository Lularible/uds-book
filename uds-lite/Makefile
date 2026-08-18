# Makefile -- uds-lite teaching-grade UDS diagnostic stack
#
# Usage:
#   make                          # build for the native architecture (x86-64)
#   make ARCH=arm64               # ARM64 cross compilation (requires aarch64-linux-gnu-gcc)
#   make CC=clang                 # use the clang compiler
#   make clean                    # remove all build artifacts
#
# Run:
#   Terminal 1: ./build/x86-64/uds_server      (or ./build/arm64/uds_server)
#   Terminal 2: ./build/x86-64/uds_client
#   Terminal 2: ./build/x86-64/uds_shell

# -- Architecture detection ------------------------------
NATIVE_ARCH := $(shell uname -m)
ifeq ($(NATIVE_ARCH),x86_64)
  DETECTED_ARCH = x86-64
else ifeq ($(NATIVE_ARCH),aarch64)
  DETECTED_ARCH = arm64
else
  DETECTED_ARCH = $(NATIVE_ARCH)
endif

ARCH     ?= $(DETECTED_ARCH)
BUILD_DIR = build/$(ARCH)

# -- Compiler ---------------------------------------------
# Default: x86-64 uses gcc, arm64 uses aarch64-linux-gnu-gcc
# Can be overridden on the command line: make CC=clang ARCH=arm64
CC_BASE = gcc
CC_ARM64 = aarch64-linux-gnu-gcc
ifeq ($(ARCH),arm64)
  CC = $(CC_ARM64)
else
  CC = $(CC_BASE)
endif

CFLAGS  = -Wall -Wextra -O2 -std=c99
LDFLAGS =

# -- Object files -----------------------------------------
OBJS = $(BUILD_DIR)/uds_server.o $(BUILD_DIR)/uds_client.o $(BUILD_DIR)/uds_shell.o $(BUILD_DIR)/uds_msg.o
BINS = $(BUILD_DIR)/uds_server $(BUILD_DIR)/uds_client $(BUILD_DIR)/uds_shell

.PHONY: all clean run-server run-client run-shell info

all: $(BINS)
	@echo "  Build complete: $(ARCH) ($(CC))"
	@echo "  Binaries: $(BUILD_DIR)/"

info:
	@echo "  Architecture : $(ARCH)"
	@echo "  Detected     : $(DETECTED_ARCH)"
	@echo "  Compiler     : $(CC)"
	@echo "  Build dir    : $(BUILD_DIR)"

# -- Linking ----------------------------------------------
$(BUILD_DIR)/uds_server: $(BUILD_DIR)/uds_server.o $(BUILD_DIR)/uds_msg.o | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/uds_client: $(BUILD_DIR)/uds_client.o $(BUILD_DIR)/uds_msg.o | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/uds_shell: $(BUILD_DIR)/uds_shell.o $(BUILD_DIR)/uds_msg.o | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# -- Compilation ------------------------------------------
$(BUILD_DIR)/uds_server.o: uds_server.c uds_common.h uds_msg.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/uds_client.o: uds_client.c uds_common.h uds_msg.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/uds_shell.o: uds_shell.c uds_common.h uds_msg.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/uds_msg.o: uds_msg.c uds_common.h uds_msg.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# -- Convenience run targets ------------------------------
run-server: $(BUILD_DIR)/uds_server
	$(BUILD_DIR)/uds_server

run-client: $(BUILD_DIR)/uds_client
	$(BUILD_DIR)/uds_client

run-shell: $(BUILD_DIR)/uds_shell
	$(BUILD_DIR)/uds_shell

# -- Cleanup ----------------------------------------------
clean:
	rm -rf build/
