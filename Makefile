CC = g++
SDL2_CFLAGS := $(shell sdl2-config --cflags)
SDL2_LIBS   := $(shell sdl2-config --libs) -lSDL2_mixer
CFLAGS = -Wall -O3 -std=c++11 $(SDL2_CFLAGS)

UNAME := $(shell uname)
ANDROID_SRCS = android_main.cpp

ifeq ($(UNAME), Darwin)
  LIBS = -framework GLUT -framework OpenGL -framework AppKit $(SDL2_LIBS)
  CFLAGS += -DGL_SILENCE_DEPRECATION -Wno-char-subscripts
  ALL_SRCS := $(filter-out $(ANDROID_SRCS),$(wildcard *.cpp) $(wildcard */*.cpp))
else
  LIBS = -lglut -lGL -lGLU -lX11 $(SDL2_LIBS)
  ALL_SRCS := $(filter-out $(ANDROID_SRCS),$(wildcard *.cpp) $(wildcard */*.cpp))
endif

# Optional native netplay backend (macOS/Linux) — see NETPLAY.md.
# libdatachannel is not packaged by Homebrew; build it from source first:
#   ./build_netplay_deps.sh
#   make NETPLAY=1 NETPLAY_PREFIX=$PWD/netplay-libs
# Defines NEWTONIA_NET_RTC (activates net_transport_rtc.cpp and the menu's
# ONLINE row) and links the libdatachannel C API.
ifeq ($(NETPLAY),1)
  CFLAGS += -DNEWTONIA_NET_RTC
  LIBS += -ldatachannel
  ifneq ($(NETPLAY_PREFIX),)
    CFLAGS += -I$(NETPLAY_PREFIX)/include
    LIBS += -L$(NETPLAY_PREFIX)/lib -Wl,-rpath,$(NETPLAY_PREFIX)/lib
  endif
endif

CFLAGS += -MMD -MP
COMPILE = $(CC) $(CFLAGS) -c
OBJFILES := $(patsubst %.cpp,%.o,$(ALL_SRCS))
ifeq ($(UNAME), Darwin)
  OBJFILES += macos_window.o
endif
DEPFILES := $(OBJFILES:.o=.d)


all: newtonia

# Flavor stamp: the netplay seam files compile to EMPTY translation units
# without NEWTONIA_NET_RTC, so switching NETPLAY on or off between builds
# must rebuild everything — otherwise stale objects from the other flavor
# link against the wrong library set ("undefined symbols: _rtcCreate...").
FLAVOR := netplay-$(if $(filter 1,$(NETPLAY)),on,off)
.PHONY: FORCE
FORCE: ;
flavor.stamp: FORCE
	@[ "`cat flavor.stamp 2>/dev/null`" = "$(FLAVOR)" ] || echo "$(FLAVOR)" > flavor.stamp
$(OBJFILES): flavor.stamp

# --- macOS universal bundle ----------------------------------------------
# Two whole-program compiles (arm64 + x86_64) lipo'd together, mirroring
# the CI recipe. The x86_64 half links against the Rosetta Homebrew tree
# (/usr/local) — install it plus sdl2/sdl2_mixer there for local universal
# builds. With NETPLAY=1 the prefix must hold a UNIVERSAL libdatachannel:
#   ./build_netplay_deps.sh --universal
# and the dylib is embedded in the bundle at Contents/Frameworks.
OSX_SDL_ARM ?= /opt/homebrew
OSX_SDL_X86 ?= /usr/local
OSX_MIN = -mmacosx-version-min=12.0
ifeq ($(NETPLAY),1)
  OSX_NET_CFLAGS = -DNEWTONIA_NET_RTC -I$(NETPLAY_PREFIX)/include
  OSX_NET_LIBS = -L$(NETPLAY_PREFIX)/lib -ldatachannel \
                 -Wl,-rpath,@executable_path/../Frameworks \
                 -Wl,-rpath,$(NETPLAY_PREFIX)/lib
endif

newtonia-arm64: OSX_SDL = $(OSX_SDL_ARM)
newtonia-x86_64: OSX_SDL = $(OSX_SDL_X86)
newtonia-arm64 newtonia-x86_64: FORCE
	$(CC) -O3 -Wall -std=c++11 -arch $(patsubst newtonia-%,%,$@) $(OSX_MIN) \
	  -DGL_SILENCE_DEPRECATION -Wno-char-subscripts $(OSX_NET_CFLAGS) \
	  -I$(OSX_SDL)/include/SDL2 -D_THREAD_SAFE \
	  -o $@ $(ALL_SRCS) macos_window.mm \
	  -L$(OSX_SDL)/lib -lSDL2 -lSDL2_mixer $(OSX_NET_LIBS) \
	  -framework GLUT -framework OpenGL -framework AppKit

osx: newtonia-arm64 newtonia-x86_64
	lipo -create -output newtonia newtonia-arm64 newtonia-x86_64
	lipo -info newtonia
	mkdir -p Newtonia.app/Contents/MacOS
	mkdir -p Newtonia.app/Contents/Resources
	cp newtonia Newtonia.app/Contents/MacOS/Newtonia
	rm -rf Newtonia.app/Contents/Resources/audio
	cp -r audio Newtonia.app/Contents/Resources/audio
	cp icon.icns Newtonia.app/Contents/Resources/icon.icns
	sed 's/$${EXECUTABLE_NAME}/Newtonia/g' Newtonia-Info.plist > Newtonia.app/Contents/Info.plist
ifeq ($(NETPLAY),1)
	mkdir -p Newtonia.app/Contents/Frameworks
	cp $(NETPLAY_PREFIX)/lib/libdatachannel.*.dylib Newtonia.app/Contents/Frameworks/
endif

newtonia: $(OBJFILES)
	$(CC) -o newtonia $(OBJFILES) $(LIBS)

clean:
	rm -rf $(OBJFILES) $(DEPFILES) newtonia newtonia-arm64 newtonia-x86_64 flavor.stamp

# ============================================================
# Web / Emscripten target
# ============================================================
EMCC = emcc

# Exclude desktop and Android entry points; add web entry point
WEB_EXCL = glut.cpp android_main.cpp
WEB_SRCS := $(filter-out $(WEB_EXCL), $(wildcard *.cpp) $(wildcard */*.cpp))

# GROWABLE_ARRAYBUFFERS (default-on since emscripten 6.0.2) breaks Firefox:
# its TextDecoder rejects views over resizable ArrayBuffers, crashing
# UTF8ToString at startup. Requires emcc >= 4.0.12 to recognise the setting.
# ccall is used by the netplay test hooks (nwtest_* in net_transport_web.cpp)
# to pass SDP strings from JS; harmless to production pages.
WEB_FLAGS = -std=c++11 -O2 \
            -s USE_SDL=2 \
            -s USE_SDL_MIXER=2 \
            -s SDL2_MIXER_FORMATS='["wav","mp3"]' \
            -s FULL_ES2=1 \
            -s ALLOW_MEMORY_GROWTH=1 \
            -s GROWABLE_ARRAYBUFFERS=0 \
            -s EXPORTED_RUNTIME_METHODS='["ccall"]' \
            -lidbfs.js \
            --shell-file web/shell.html

WEB_FLAGS += --preload-file audio@audio

.PHONY: web web-clean

# Landing page (web/site/) is served at the root; the playable game lives at /play.
web:
	mkdir -p web/dist/play
	tsc -p web/tsconfig.json
	$(EMCC) $(WEB_SRCS) $(WEB_FLAGS) -o web/dist/play/index.html
	cp web/main.js web/dist/play/main.js
	cp web/site/index.html web/site/styles.css web/site/site.js web/site/icon.png web/dist/
	cp web/site/CNAME web/dist/CNAME

web-clean:
	rm -rf web/dist

-include $(DEPFILES)

%.o: %.cpp
	$(COMPILE) -o $@ $<

%.o: %.mm
	$(CC) $(CFLAGS) -c -o $@ $<
