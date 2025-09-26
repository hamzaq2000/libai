MIN_MACOS_VERSION = 26.0


ifdef CROSS_COMPILE
    ifeq ($(CROSS_COMPILE),x86_64)
        ARCH = x86_64
        ARCH_FLAGS = -target x86_64-apple-macos$(MIN_MACOS_VERSION)
        SWIFT_ARCH_FLAGS = -target x86_64-apple-macos$(MIN_MACOS_VERSION)
    else ifeq ($(CROSS_COMPILE),arm64e)
        ARCH = arm64e
        ARCH_FLAGS = -target arm64e-apple-macos$(MIN_MACOS_VERSION)
        SWIFT_ARCH_FLAGS = -target arm64e-apple-macos$(MIN_MACOS_VERSION)
    else
        $(error Unsupported CROSS_COMPILE target: $(CROSS_COMPILE). Supported: x86_64, arm64e)
    endif
else
    ARCH := $(shell uname -m)
    ARCH_FLAGS = -target $(ARCH)-apple-macos$(MIN_MACOS_VERSION) -march=native -mtune=native
    SWIFT_ARCH_FLAGS = -target $(ARCH)-apple-macos$(MIN_MACOS_VERSION)
endif

BUILD_DIR = build
THIRD_PARTY_DIR = third-party

GIT_TAG := $(shell git describe --tags --exact-match 2>/dev/null || echo "")
ifneq ($(GIT_TAG),)
    VERSION := $(shell echo $(GIT_TAG) | sed 's/^v//')
    MAJOR_VERSION := $(shell echo $(VERSION) | cut -d. -f1)
    MINOR_VERSION := $(shell echo $(VERSION) | cut -d. -f2)
    PATCH_VERSION := $(shell echo $(VERSION) | cut -d. -f3)
else
    MAJOR_VERSION = 0
    MINOR_VERSION = 0
    PATCH_VERSION = 0
    VERSION = $(MAJOR_VERSION).$(MINOR_VERSION).$(PATCH_VERSION)-dev
endif
COMPATIBILITY_VERSION = $(MAJOR_VERSION).0.0

MOMO_VERSION = "1.0.0"

VERSION_DEFINES = -DAI_VERSION_MAJOR=$(MAJOR_VERSION) -DAI_VERSION_MINOR=$(MINOR_VERSION) -DAI_VERSION_PATCH=$(PATCH_VERSION) -DAI_VERSION_STRING=\"$(VERSION)\"
MOMO_VERSION_DEFINES = -DMOMO_VERSION=\"$(MOMO_VERSION)\"

CC = clang
SWIFTC = swiftc

# Base flags
BASE_CFLAGS = -std=c23 -Wall -pthread $(ARCH_FLAGS)

# SDK path
SDK_PATH = $(shell xcrun --show-sdk-path)

# Release flags
REL_CFLAGS = $(BASE_CFLAGS) -O3 -flto -funroll-loops -fomit-frame-pointer -DNDEBUG
REL_SWIFT_FLAGS = -Ounchecked -whole-module-optimization -parse-as-library \
                  -module-name AIBridge $(SWIFT_ARCH_FLAGS) \
                  -enable-library-evolution \
                  -sdk $(SDK_PATH) \
                  -F$(SDK_PATH)/System/Library/Frameworks \
                  -Xlinker -framework -Xlinker FoundationModels

# Debug flags
DBG_CFLAGS = $(BASE_CFLAGS) -O0 -g -DDEBUG -fno-omit-frame-pointer
DBG_SWIFT_FLAGS = -Onone -g -parse-as-library \
                  -module-name AIBridge $(SWIFT_ARCH_FLAGS) \
                  -enable-library-evolution \
                  -sdk $(SDK_PATH) \
                  -F$(SDK_PATH)/System/Library/Frameworks \
                  -Xlinker -framework -Xlinker FoundationModels

THIRD_PARTY_SOURCES = $(wildcard $(THIRD_PARTY_DIR)/*.c)

# Object file paths organized by target/config/arch
STATIC_REL_OBJ_DIR = $(BUILD_DIR)/obj/static/$(ARCH)/release
STATIC_DBG_OBJ_DIR = $(BUILD_DIR)/obj/static/$(ARCH)/debug
DYNAMIC_REL_OBJ_DIR = $(BUILD_DIR)/obj/dynamic/$(ARCH)/release
DYNAMIC_DBG_OBJ_DIR = $(BUILD_DIR)/obj/dynamic/$(ARCH)/debug

STATIC_REL_THIRD_PARTY_OBJS = $(THIRD_PARTY_SOURCES:$(THIRD_PARTY_DIR)/%.c=$(STATIC_REL_OBJ_DIR)/%.o)
STATIC_DBG_THIRD_PARTY_OBJS = $(THIRD_PARTY_SOURCES:$(THIRD_PARTY_DIR)/%.c=$(STATIC_DBG_OBJ_DIR)/%.o)
DYNAMIC_REL_THIRD_PARTY_OBJS = $(THIRD_PARTY_SOURCES:$(THIRD_PARTY_DIR)/%.c=$(DYNAMIC_REL_OBJ_DIR)/%.o)
DYNAMIC_DBG_THIRD_PARTY_OBJS = $(THIRD_PARTY_SOURCES:$(THIRD_PARTY_DIR)/%.c=$(DYNAMIC_DBG_OBJ_DIR)/%.o)

.PHONY: all clean static-rel static-dbg dynamic-rel dynamic-dbg print-version print-momo-version

all: dynamic-rel

# Static release object files
$(STATIC_REL_OBJ_DIR)/%.o: $(THIRD_PARTY_DIR)/%.c | $(STATIC_REL_OBJ_DIR)
	$(CC) $(REL_CFLAGS) -I$(THIRD_PARTY_DIR) -c $< -o $@

$(STATIC_REL_OBJ_DIR)/ai.o: ai.c | $(STATIC_REL_OBJ_DIR)
	$(CC) $(REL_CFLAGS) $(VERSION_DEFINES) -c $< -o $@

$(STATIC_REL_OBJ_DIR)/AIBridge.o: bridge.swift | $(STATIC_REL_OBJ_DIR)
	$(SWIFTC) $(REL_SWIFT_FLAGS) -emit-object -static -o $@ $<

# Static debug object files
$(STATIC_DBG_OBJ_DIR)/%.o: $(THIRD_PARTY_DIR)/%.c | $(STATIC_DBG_OBJ_DIR)
	$(CC) $(DBG_CFLAGS) -I$(THIRD_PARTY_DIR) -c $< -o $@

$(STATIC_DBG_OBJ_DIR)/ai.o: ai.c | $(STATIC_DBG_OBJ_DIR)
	$(CC) $(DBG_CFLAGS) $(VERSION_DEFINES) -c $< -o $@

$(STATIC_DBG_OBJ_DIR)/AIBridge.o: bridge.swift | $(STATIC_DBG_OBJ_DIR)
	$(SWIFTC) $(DBG_SWIFT_FLAGS) -emit-object -static -o $@ $<

# Dynamic release object files
$(DYNAMIC_REL_OBJ_DIR)/%.o: $(THIRD_PARTY_DIR)/%.c | $(DYNAMIC_REL_OBJ_DIR)
	$(CC) $(REL_CFLAGS) -I$(THIRD_PARTY_DIR) -c $< -o $@

$(DYNAMIC_REL_OBJ_DIR)/ai_pic.o: ai.c | $(DYNAMIC_REL_OBJ_DIR)
	$(CC) $(REL_CFLAGS) $(VERSION_DEFINES) -fPIC -c $< -o $@

# Dynamic debug object files
$(DYNAMIC_DBG_OBJ_DIR)/%.o: $(THIRD_PARTY_DIR)/%.c | $(DYNAMIC_DBG_OBJ_DIR)
	$(CC) $(DBG_CFLAGS) -I$(THIRD_PARTY_DIR) -c $< -o $@

$(DYNAMIC_DBG_OBJ_DIR)/ai_pic.o: ai.c | $(DYNAMIC_DBG_OBJ_DIR)
	$(CC) $(DBG_CFLAGS) $(VERSION_DEFINES) -fPIC -c $< -o $@

# Static release build
static-rel: $(BUILD_DIR)/static/$(ARCH)/release/momo

$(BUILD_DIR)/static/$(ARCH)/release/libai.a: $(STATIC_REL_OBJ_DIR)/ai.o $(STATIC_REL_OBJ_DIR)/AIBridge.o | $(BUILD_DIR)/static/$(ARCH)/release
	libtool -static -o $@ $^

$(BUILD_DIR)/static/$(ARCH)/release/momo: main.c $(BUILD_DIR)/static/$(ARCH)/release/libai.a $(STATIC_REL_THIRD_PARTY_OBJS) | $(BUILD_DIR)/static/$(ARCH)/release
	$(CC) $(REL_CFLAGS) $(VERSION_DEFINES) $(MOMO_VERSION_DEFINES) \
		-framework ApplicationServices -framework CoreFoundation \
		$(BUILD_DIR)/static/$(ARCH)/release/libai.a $(STATIC_REL_THIRD_PARTY_OBJS) -o $@ $<

# Static debug build
static-dbg: $(BUILD_DIR)/static/$(ARCH)/debug/momo

$(BUILD_DIR)/static/$(ARCH)/debug/libai.a: $(STATIC_DBG_OBJ_DIR)/ai.o $(STATIC_DBG_OBJ_DIR)/AIBridge.o | $(BUILD_DIR)/static/$(ARCH)/debug
	libtool -static -o $@ $^

$(BUILD_DIR)/static/$(ARCH)/debug/momo: main.c $(BUILD_DIR)/static/$(ARCH)/debug/libai.a $(STATIC_DBG_THIRD_PARTY_OBJS) | $(BUILD_DIR)/static/$(ARCH)/debug
	$(CC) $(DBG_CFLAGS) $(VERSION_DEFINES) $(MOMO_VERSION_DEFINES) \
		-framework ApplicationServices -framework CoreFoundation \
		$(BUILD_DIR)/static/$(ARCH)/debug/libai.a $(STATIC_DBG_THIRD_PARTY_OBJS) -o $@ $<

# Dynamic release build
dynamic-rel: $(BUILD_DIR)/dynamic/$(ARCH)/release/momo

$(BUILD_DIR)/dynamic/$(ARCH)/release/libaibridge.dylib: bridge.swift | $(BUILD_DIR)/dynamic/$(ARCH)/release
	$(SWIFTC) $(REL_SWIFT_FLAGS) -emit-library \
		-Xlinker -install_name -Xlinker @rpath/libaibridge.dylib \
		-Xlinker -current_version -Xlinker $(VERSION) \
		-Xlinker -compatibility_version -Xlinker $(COMPATIBILITY_VERSION) \
		-o $@ $<

$(BUILD_DIR)/dynamic/$(ARCH)/release/libai.dylib: $(DYNAMIC_REL_OBJ_DIR)/ai_pic.o $(BUILD_DIR)/dynamic/$(ARCH)/release/libaibridge.dylib | $(BUILD_DIR)/dynamic/$(ARCH)/release
	$(CC) $(REL_CFLAGS) -fPIC -dynamiclib -install_name @rpath/libai.dylib \
		-current_version $(VERSION) -compatibility_version $(COMPATIBILITY_VERSION) \
		-L$(BUILD_DIR)/dynamic/$(ARCH)/release -laibridge -o $@ $(DYNAMIC_REL_OBJ_DIR)/ai_pic.o
	install_name_tool -change $(BUILD_DIR)/dynamic/$(ARCH)/release/libaibridge.dylib @rpath/libaibridge.dylib $@

$(BUILD_DIR)/dynamic/$(ARCH)/release/momo: main.c $(BUILD_DIR)/dynamic/$(ARCH)/release/libai.dylib $(BUILD_DIR)/dynamic/$(ARCH)/release/libaibridge.dylib $(DYNAMIC_REL_THIRD_PARTY_OBJS) | $(BUILD_DIR)/dynamic/$(ARCH)/release
	$(CC) $(REL_CFLAGS) $(VERSION_DEFINES) $(MOMO_VERSION_DEFINES) \
		-L$(BUILD_DIR)/dynamic/$(ARCH)/release -lai -laibridge \
		-framework ApplicationServices -framework CoreFoundation \
		-Wl,-rpath,@executable_path $(DYNAMIC_REL_THIRD_PARTY_OBJS) -o $@ $<
	install_name_tool -change $(BUILD_DIR)/dynamic/$(ARCH)/release/libaibridge.dylib @rpath/libaibridge.dylib $@

# Dynamic debug build
dynamic-dbg: $(BUILD_DIR)/dynamic/$(ARCH)/debug/momo

$(BUILD_DIR)/dynamic/$(ARCH)/debug/libaibridge.dylib: bridge.swift | $(BUILD_DIR)/dynamic/$(ARCH)/debug
	$(SWIFTC) $(DBG_SWIFT_FLAGS) -emit-library \
		-Xlinker -install_name -Xlinker @rpath/libaibridge.dylib \
		-Xlinker -current_version -Xlinker $(VERSION) \
		-Xlinker -compatibility_version -Xlinker $(COMPATIBILITY_VERSION) \
		-o $@ $<

$(BUILD_DIR)/dynamic/$(ARCH)/debug/libai.dylib: $(DYNAMIC_DBG_OBJ_DIR)/ai_pic.o $(BUILD_DIR)/dynamic/$(ARCH)/debug/libaibridge.dylib | $(BUILD_DIR)/dynamic/$(ARCH)/debug
	$(CC) $(DBG_CFLAGS) -fPIC -dynamiclib -install_name @rpath/libai.dylib \
		-current_version $(VERSION) -compatibility_version $(COMPATIBILITY_VERSION) \
		-L$(BUILD_DIR)/dynamic/$(ARCH)/debug -laibridge -o $@ $(DYNAMIC_DBG_OBJ_DIR)/ai_pic.o
	install_name_tool -change $(BUILD_DIR)/dynamic/$(ARCH)/debug/libaibridge.dylib @rpath/libaibridge.dylib $@

$(BUILD_DIR)/dynamic/$(ARCH)/debug/momo: main.c $(BUILD_DIR)/dynamic/$(ARCH)/debug/libai.dylib $(BUILD_DIR)/dynamic/$(ARCH)/debug/libaibridge.dylib $(DYNAMIC_DBG_THIRD_PARTY_OBJS) | $(BUILD_DIR)/dynamic/$(ARCH)/debug
	$(CC) $(DBG_CFLAGS) $(VERSION_DEFINES) $(MOMO_VERSION_DEFINES) \
		-L$(BUILD_DIR)/dynamic/$(ARCH)/debug -lai -laibridge \
		-framework ApplicationServices -framework CoreFoundation \
		-Wl,-rpath,@executable_path $(DYNAMIC_DBG_THIRD_PARTY_OBJS) -o $@ $<
	install_name_tool -change $(BUILD_DIR)/dynamic/$(ARCH)/debug/libaibridge.dylib @rpath/libaibridge.dylib $@

# Directory creation
$(STATIC_REL_OBJ_DIR):
	@mkdir -p $@

$(STATIC_DBG_OBJ_DIR):
	@mkdir -p $@

$(DYNAMIC_REL_OBJ_DIR):
	@mkdir -p $@

$(DYNAMIC_DBG_OBJ_DIR):
	@mkdir -p $@

$(BUILD_DIR)/static/$(ARCH)/release:
	@mkdir -p $@

$(BUILD_DIR)/static/$(ARCH)/debug:
	@mkdir -p $@

$(BUILD_DIR)/dynamic/$(ARCH)/release:
	@mkdir -p $@

$(BUILD_DIR)/dynamic/$(ARCH)/debug:
	@mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)

print-version:
	@echo $(VERSION)

print-momo-version:
	@echo $(MOMO_VERSION)