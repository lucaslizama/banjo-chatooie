# Builds the native half of the mod: a host shared library the game dlopens.
#
# This is a normal host build -- nothing here targets MIPS. The MIPS side is the
# Makefile in the parent directory.

# TARGET_OS selects what to build for. The default builds for whatever machine
# you are on; `make TARGET_OS=windows` cross-compiles the DLL with mingw-w64,
# which is how the Windows release is produced from Linux.
#
# macOS is not in this list on purpose. Cross-compiling for it needs the macOS
# SDK from Xcode, which Apple does not permit redistributing, so a .dylib has to
# be built on a Mac. Doing that is just the default build on that machine.
TARGET_OS ?= host

# Must match `name` in mod.toml's native_libraries. Keep this comment on its own
# line: make keeps the whitespace before a trailing comment as part of the value.
LIB_NAME := banjo_chatooie_native

ifeq ($(TARGET_OS),windows)
    CXX       := x86_64-w64-mingw32-g++
    BUILD_DIR := ../build/native-windows
    LIB_EXT   := .dll
    LDLIBS    := -lws2_32
    # -static pulls in libstdc++, libgcc and winpthread, so the DLL does not
    # need any mingw runtime alongside it on the player's machine.
    LINK_EXTRA := -static -Wl,--no-undefined
else
    CXX       ?= g++
    BUILD_DIR := ../build/native
    LDLIBS    :=
    ifeq ($(OS),Windows_NT)
        LIB_EXT := .dll
        LDLIBS  := -lws2_32
    else ifeq ($(shell uname),Darwin)
        LIB_EXT := .dylib
    else
        LIB_EXT := .so
        # Players won't have a matching libstdc++ next to the game, so link it in.
        LINK_EXTRA := -static-libstdc++ -static-libgcc -Wl,--no-undefined
    endif
endif

TARGET   := $(BUILD_DIR)/$(LIB_NAME)$(LIB_EXT)

SRCS     := twitch_chat.cpp irc_client.cpp redemption_client.cpp
OBJS     := $(addprefix $(BUILD_DIR)/, $(SRCS:.cpp=.o))
DEPS     := $(OBJS:.o=.d)

CXXFLAGS := -std=c++17 -O2 -fPIC -fvisibility=hidden -Wall -Wextra -pthread
LDFLAGS  := -shared -pthread $(LINK_EXTRA)

all: $(TARGET)

$(TARGET): $(OBJS) | $(BUILD_DIR)
	$(CXX) $(OBJS) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/%.o: %.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -MMD -MF $(@:.o=.d) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)

.PHONY: all clean
