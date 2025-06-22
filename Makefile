MIN_MACOS_VERSION = 26.0
ARCH := $(shell uname -m)
BUILD_DIR = build
THIRD_PARTY_DIR = third-party

CC      = clang
CFLAGS  = -O2 -std=c23 -Wall -pthread -target $(ARCH)-apple-macos$(MIN_MACOS_VERSION)
SWIFTC  = swiftc
SWIFT_FLAGS = -O -whole-module-optimization \
              -parse-as-library -emit-object -static \
              -module-name AIBridge -emit-module \
              -target $(ARCH)-apple-macos$(MIN_MACOS_VERSION)


THIRD_PARTY_SOURCES  = $(wildcard $(THIRD_PARTY_DIR)/*.c)
THIRD_PARTY_OBJECTS  = $(THIRD_PARTY_SOURCES:$(THIRD_PARTY_DIR)/%.c=$(BUILD_DIR)/%.o)

SWIFT_OBJECT = $(BUILD_DIR)/AIBridge.o
SWIFT_MODULE = $(BUILD_DIR)/AIBridge.swiftmodule

.PHONY: all clean library momo

all: library momo

$(BUILD_DIR):
	@mkdir -p $@


$(SWIFT_OBJECT): bridge.swift | $(BUILD_DIR)
	$(SWIFTC) $(SWIFT_FLAGS) -o $@ $< \
		-emit-module-path $(SWIFT_MODULE)

$(BUILD_DIR)/libaibridge.a: $(SWIFT_OBJECT) | $(BUILD_DIR)
	libtool -static -o $@ $(SWIFT_OBJECT)


$(BUILD_DIR)/%.o: $(THIRD_PARTY_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(THIRD_PARTY_DIR) -c $< -o $@


library: $(BUILD_DIR)/libai.a

$(BUILD_DIR)/libai.a: ai.c $(THIRD_PARTY_OBJECTS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c ai.c -o $(BUILD_DIR)/ai.o
	libtool -static -o $@ $(BUILD_DIR)/ai.o $(THIRD_PARTY_OBJECTS)


momo: $(BUILD_DIR)/momo

$(BUILD_DIR)/momo: main.c $(BUILD_DIR)/libai.a $(BUILD_DIR)/libaibridge.a | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ \
		-o $@


clean:
	rm -rf $(BUILD_DIR)
