# Project configuration
BELLA_SDK_NAME    = bella_engine_sdk
EXECUTABLE_NAME   = vmaxtui
PLATFORM          = $(shell uname)
BUILD_TYPE        ?= release# Default to release build if not specified

# Common paths
BELLA_SDK_PATH    = ../bella_engine_sdk
LZFSE_PATH        = ../lzfse
LIBPLIST_PATH     = ../libplist
EFSW_PATH         = ../efsw
OBJ_DIR           = obj/$(PLATFORM)/$(BUILD_TYPE)
BIN_DIR           = bin/$(PLATFORM)/$(BUILD_TYPE)
OUTPUT_FILE       = $(BIN_DIR)/$(EXECUTABLE_NAME)

# Platform-specific configuration
ifeq ($(PLATFORM), Darwin)
    # macOS configuration
    SDK_LIB_EXT          = dylib
    LZFSE_LIB_NAME       = liblzfse.$(SDK_LIB_EXT)
    PLIST_LIB_NAME       = libplist-2.0.4.$(SDK_LIB_EXT)
    EFSW_LIB_NAME        = libefsw.$(SDK_LIB_EXT)
    MACOS_SDK_PATH       = /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
    
    # Compiler settings
    CC                   = clang
    CXX                  = clang++
    
    # Architecture flags
    ARCH_FLAGS           = -arch arm64 -mmacosx-version-min=11.0 -isysroot $(MACOS_SDK_PATH)
    
    # Linking flags - Use multiple rpath entries to look in executable directory
    LINKER_FLAGS         = $(ARCH_FLAGS) -framework Cocoa -framework IOKit -fvisibility=hidden -O5 \
                          -rpath @executable_path \
                          -rpath . 

    # Platform-specific libraries
    PLIST_LIB            = -lplist-2.0
    
else
    # Linux configuration
    SDK_LIB_EXT          = so
    LZFSE_LIB_NAME       = liblzfse.$(SDK_LIB_EXT)
    PLIST_LIB_NAME       = libplist.$(SDK_LIB_EXT)
    EFSW_LIB_NAME        = libefsw.$(SDK_LIB_EXT)
    
    # Compiler settings
    CC                   = gcc
    CXX                  = g++
    
    # Architecture flags
    ARCH_FLAGS           = -m64 -D_FILE_OFFSET_BITS=64
    
    # Linking flags
    LINKER_FLAGS         = $(ARCH_FLAGS) -fvisibility=hidden -O3 -Wl,-rpath,'$$ORIGIN' -Wl,-rpath,'$$ORIGIN/lib'
    
    # Platform-specific libraries
    PLIST_LIB            = -lplist
endif

# Common include and library paths
INCLUDE_PATHS      = -I$(BELLA_SDK_PATH)/src -I$(LZFSE_PATH)/src -I$(LIBPLIST_PATH)/include -I$(EFSW_PATH)/include -I$(EFSW_PATH)/src
SDK_LIB_PATH       = $(BELLA_SDK_PATH)/lib
SDK_LIB_FILE       = lib$(BELLA_SDK_NAME).$(SDK_LIB_EXT)
LZFSE_BUILD_DIR    = $(LZFSE_PATH)/build
PLIST_LIB_DIR      = $(LIBPLIST_PATH)/src/.libs
EFSW_LIB_DIR       = $(EFSW_PATH)/build

# Library flags
LIB_PATHS          = -L$(SDK_LIB_PATH) -L$(LZFSE_BUILD_DIR) -L$(PLIST_LIB_DIR) -L$(EFSW_LIB_DIR)
LIBRARIES          = -l$(BELLA_SDK_NAME) -lm -ldl -llzfse $(PLIST_LIB) -lefsw

# Build type specific flags
ifeq ($(BUILD_TYPE), debug)
    CPP_DEFINES = -D_DEBUG -DDL_USE_SHARED
    COMMON_FLAGS = $(ARCH_FLAGS) -fvisibility=hidden -g -O0 $(INCLUDE_PATHS)
else
    CPP_DEFINES = -DNDEBUG=1 -DDL_USE_SHARED
    COMMON_FLAGS = $(ARCH_FLAGS) -fvisibility=hidden -O3 $(INCLUDE_PATHS)
endif

# Language-specific flags
C_FLAGS            = $(COMMON_FLAGS) -std=c17
CXX_FLAGS          = $(COMMON_FLAGS) -std=c++17 -Wno-deprecated-declarations

# Objects
OBJECTS            = $(EXECUTABLE_NAME).o 
OBJECT_FILES       = $(patsubst %,$(OBJ_DIR)/%,$(OBJECTS))

# Build rules
$(OBJ_DIR)/$(EXECUTABLE_NAME).o: $(EXECUTABLE_NAME).cpp
	@mkdir -p $(@D)
	$(CXX) -c -o $@ $< $(CXX_FLAGS) $(CPP_DEFINES)

$(OUTPUT_FILE): $(OBJECT_FILES)
	@mkdir -p $(@D)
	$(CXX) -o $@ $(OBJECT_FILES) $(LINKER_FLAGS) $(LIB_PATHS) $(LIBRARIES)
	@echo "Copying libraries to $(BIN_DIR)..."
	@cp $(SDK_LIB_PATH)/$(SDK_LIB_FILE) $(BIN_DIR)/$(SDK_LIB_FILE)
	@cp $(LZFSE_BUILD_DIR)/$(LZFSE_LIB_NAME) $(BIN_DIR)/$(LZFSE_LIB_NAME)
	@cp $(PLIST_LIB_DIR)/$(PLIST_LIB_NAME) $(BIN_DIR)/
	@if [ -f $(EFSW_LIB_DIR)/$(EFSW_LIB_NAME) ]; then cp $(EFSW_LIB_DIR)/$(EFSW_LIB_NAME) $(BIN_DIR)/; fi
	@echo "Build complete: $(OUTPUT_FILE)"

# Add default target
all: $(OUTPUT_FILE)

.PHONY: clean cleanall all
clean:
	rm -f $(OBJ_DIR)/$(EXECUTABLE_NAME).o
	rm -f $(OUTPUT_FILE)
	rm -f $(BIN_DIR)/$(SDK_LIB_FILE)
	rm -f $(BIN_DIR)/*.dylib
	rmdir $(OBJ_DIR) 2>/dev/null || true
	rmdir $(BIN_DIR) 2>/dev/null || true

cleanall:
	rm -f obj/*/release/*.o
	rm -f obj/*/debug/*.o
	rm -f bin/*/release/$(EXECUTABLE_NAME)
	rm -f bin/*/debug/$(EXECUTABLE_NAME)
	rm -f bin/*/release/$(SDK_LIB_FILE)
	rm -f bin/*/debug/$(SDK_LIB_FILE)
	rm -f bin/*/release/*.dylib
	rm -f bin/*/debug/*.dylib
	rmdir obj/*/release 2>/dev/null || true
	rmdir obj/*/debug 2>/dev/null || true
	rmdir bin/*/release 2>/dev/null || true
	rmdir bin/*/debug 2>/dev/null || true
	rmdir obj/* 2>/dev/null || true
	rmdir bin/* 2>/dev/null || true
	rmdir obj 2>/dev/null || true
	rmdir bin 2>/dev/null || true
