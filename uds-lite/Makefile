# Makefile — uds-lite 教学级 UDS 诊断栈
#
# 使用方法:
#   make                          # 原生架构构建 (x86-64)
#   make ARCH=arm64               # ARM64 交叉编译 (需 aarch64-linux-gnu-gcc)
#   make CC=clang                 # 使用 clang 编译器
#   make clean                    # 清理所有构建产物
#
# 运行:
#   终端1: ./build/x86-64/uds_server          (或 ./build/arm64/uds_server)
#   终端2: ./build/x86-64/uds_client
#   终端2: ./build/x86-64/uds_shell

# ── 架构检测 ─────────────────────────────────
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

# ── 编译器 ───────────────────────────────────
# 默认: x86-64 用 gcc, arm64 用 aarch64-linux-gnu-gcc
# 可通过命令行动态覆盖: make CC=clang ARCH=arm64
CC_BASE = gcc
CC_ARM64 = aarch64-linux-gnu-gcc
ifeq ($(ARCH),arm64)
  CC = $(CC_ARM64)
else
  CC = $(CC_BASE)
endif

CFLAGS  = -Wall -Wextra -O2 -std=c99
LDFLAGS =

# ── 目标文件 ─────────────────────────────────
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

# ── 链接 ─────────────────────────────────────
$(BUILD_DIR)/uds_server: $(BUILD_DIR)/uds_server.o $(BUILD_DIR)/uds_msg.o | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/uds_client: $(BUILD_DIR)/uds_client.o $(BUILD_DIR)/uds_msg.o | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/uds_shell: $(BUILD_DIR)/uds_shell.o $(BUILD_DIR)/uds_msg.o | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# ── 编译 ─────────────────────────────────────
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

# ── 便捷运行 ─────────────────────────────────
run-server: $(BUILD_DIR)/uds_server
	$(BUILD_DIR)/uds_server

run-client: $(BUILD_DIR)/uds_client
	$(BUILD_DIR)/uds_client

run-shell: $(BUILD_DIR)/uds_shell
	$(BUILD_DIR)/uds_shell

# ── 清理 ─────────────────────────────────────
clean:
	rm -rf build/
