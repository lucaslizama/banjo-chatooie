# Builds the native half of the mod: a host shared library the game dlopens.
#
# This is a normal host build -- nothing here targets MIPS. The MIPS side is the
# Makefile in the parent directory.

BUILD_DIR := ../build/native

ifeq ($(OS),Windows_NT)
    LIB_EXT := .dll
else ifeq ($(shell uname),Darwin)
    LIB_EXT := .dylib
else
    LIB_EXT := .so
endif

# Must match the `name` in mod.toml's native_libraries entry -- the runtime
# derives the filename from it.
LIB_NAME := twitch_chat
TARGET   := $(BUILD_DIR)/$(LIB_NAME)$(LIB_EXT)

CXX      ?= g++
SRCS     := twitch_chat.cpp irc_client.cpp
OBJS     := $(addprefix $(BUILD_DIR)/, $(SRCS:.cpp=.o))
DEPS     := $(OBJS:.o=.d)

CXXFLAGS := -std=c++17 -O2 -fPIC -fvisibility=hidden -Wall -Wextra -pthread
LDFLAGS  := -shared -pthread

# Players won't have a matching libstdc++ next to the game, so link it in.
ifneq ($(OS),Windows_NT)
ifneq ($(shell uname),Darwin)
    LDFLAGS += -static-libstdc++ -static-libgcc -Wl,--no-undefined
endif
endif

ifeq ($(OS),Windows_NT)
    LDLIBS := -lws2_32
else
    LDLIBS :=
endif

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
