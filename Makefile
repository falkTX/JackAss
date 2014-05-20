#!/usr/bin/make -f
# Makefile for JackAss #
# -------------------- #
# Created by falkTX
#

# --------------------------------------------------------------

CXX     ?= g++
WINECXX ?= wineg++

# --------------------------------------------------------------

BASE_FLAGS = -Wall -Wextra -fpermissive -fPIC -DPIC -pipe -isystem vstsdk2.4
BASE_OPTS  = -O3 -ffast-math -mtune=generic -msse -msse2 -mfpmath=sse

ifeq ($(RASPPI),true)
# Raspberry-Pi optimization flags
BASE_OPTS  = -O3 -ffast-math -march=armv6 -mfpu=vfp -mfloat-abi=hard
endif

ifeq ($(DEBUG),true)
BASE_FLAGS += -DDEBUG -O0 -g
LINK_OPTS   =
else
BASE_FLAGS += -DNDEBUG $(BASE_OPTS) -fvisibility=hidden -fvisibility-inlines-hidden
LINK_OPTS   = -Wl,--strip-all
endif

# --------------------------------------------------------------
# Linux

LINUX_FLAGS  = $(BASE_FLAGS) -std=gnu++0x $(CXXFLAGS)
LINUX_FLAGS += $(LINK_OPTS) -ldl -lpthread -shared -Wl,--defsym,main=VSTPluginMain $(LDFLAGS)

# --------------------------------------------------------------
# Mac OS

MACOS_FLAGS  = $(BASE_FLAGS) -m32 $(CXXFLAGS)
MACOS_FLAGS += -ldl -lpthread -shared $(LDFLAGS)

# --------------------------------------------------------------
# Windows

WIN32_FLAGS  = $(BASE_FLAGS) -std=gnu++0x $(CXXFLAGS)
WIN32_FLAGS += -DPTW32_STATIC_LIB -I/opt/mingw32/include
WIN32_FLAGS += $(LINK_OPTS) -lpthread -shared $(LDFLAGS)

WIN64_FLAGS  = $(BASE_FLAGS) -std=gnu++0x $(CXXFLAGS)
WIN64_FLAGS += -DPTW32_STATIC_LIB -I/opt/mingw64/include
WIN64_FLAGS += $(LINK_OPTS) -lpthread -shared $(LDFLAGS)

# --------------------------------------------------------------
# Wine

WINE32_FLAGS  = $(BASE_FLAGS) -std=gnu++0x $(CXXFLAGS)
WINE32_FLAGS += -m32 -L/usr/lib32/wine -L/usr/lib/i386-linux-gnu/wine
WINE32_FLAGS += $(LINK_OPTS) -ldl -lpthread -shared $(LDFLAGS)

WINE64_FLAGS  = $(BASE_FLAGS) -std=gnu++0x $(CXXFLAGS)
WINE64_FLAGS += -m64 -L/usr/lib64/wine -L/usr/lib/x86_64-linux-gnu/wine
WINE64_FLAGS += $(LINK_OPTS) -ldl -lpthread -shared $(LDFLAGS)

# --------------------------------------------------------------

all: linux

linux:  JackAss.so
mac:    JackAss.dylib
win32:  JackAss32.dll
win64:  JackAss64.dll
wine32: JackAssWine32.dll
wine64: JackAssWine64.dll

# --------------------------------------------------------------

JackAss.so: JackAss.cpp
	$(CXX) $^ $(LINUX_FLAGS) -o JackAssFx.so
	$(CXX) $^ -DJACKASS_SYNTH $(LINUX_FLAGS) -o JackAss.so

JackAss.dylib: JackAss.cpp
	$(CXX) $^ $(MACOS_FLAGS) -o JackAssFx.dylib
	$(CXX) $^ -DJACKASS_SYNTH $(MACOS_FLAGS) -o JackAss.dylib

JackAss32.dll: JackAss.cpp windows.def
	$(CXX) $^ $(WIN32_FLAGS) -o JackAssFx32.dll
	$(CXX) $^ -DJACKASS_SYNTH $(WIN32_FLAGS) -o JackAss32.dll

JackAss64.dll: JackAss.cpp windows.def
	$(CXX) $^ $(WIN32_FLAGS) -o JackAssFx64.dll
	$(CXX) $^ -DJACKASS_SYNTH $(WIN32_FLAGS) -o JackAss64.dll

JackAssWine32.dll: JackAss.cpp windows.def
	$(WINECXX) $^ $(WINE32_FLAGS) -o JackAssFxWine32.dll
	$(WINECXX) $^ -DJACKASS_SYNTH $(WINE32_FLAGS) -o JackAssWine32.dll
	mv JackAssFxWine32.dll.so JackAssFxWine32.dll
	mv JackAssWine32.dll.so JackAssWine32.dll

JackAssWine64.dll: JackAss.cpp windows.def
	$(WINECXX) $^ $(WINE64_FLAGS) -o JackAssFxWine64.dll
	$(WINECXX) $^ -DJACKASS_SYNTH $(WINE64_FLAGS) -o JackAssWine64.dll
	mv JackAssFxWine64.dll.so JackAssFxWine64.dll
	mv JackAssWine64.dll.so JackAssWine64.dll

# --------------------------------------------------------------

clean:
	rm -f *.dll *.dylib *.so

debug:
	$(MAKE) DEBUG=true

# --------------------------------------------------------------
