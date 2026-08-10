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
        # Deliberately NOT -static-libstdc++ here. The game is itself a C++
        # program and already has libstdc++ and libgcc loaded, so a static copy
        # in this library means two C++ runtimes in one process. That is a real
        # hazard around std::mutex, std::thread and TLS, which this library uses
        # on every frame, and it is not worth it for a dependency the host
        # process guarantees is present.
        #
        # Windows is the opposite case and keeps -static: the game there is not
        # built with mingw, so nothing guarantees its runtime exists.
        #
        # -z nodelete keeps this library mapped even if the runtime dlclose()s
        # it. This library owns threads, and a core dump showed two of them
        # jumping to tiny garbage addresses at once, one inside the singleton
        # accessor and one inside the reader loop, which is what unmapping code
        # out from under running threads looks like. Staying mapped costs a few
        # dozen KB for the life of the process and removes that whole class of
        # failure.
        #
        # -z now resolves every PLT entry at load time instead of lazily, and
        # -z relro then makes the GOT read-only. A core dump caught a call to
        # __cxa_guard_acquire landing on 0x33a6, which is the PLT stub's
        # unrelocated file offset: the slot still held the raw value with no load
        # bias applied. Binding eagerly means those stubs are never used at all,
        # and a read-only GOT turns any stray write into a fault at the writer
        # rather than a mystery jump much later.
        LINK_EXTRA := -Wl,--no-undefined -Wl,-z,nodelete -Wl,-z,now -Wl,-z,relro
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
