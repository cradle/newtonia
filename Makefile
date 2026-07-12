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

OSX_LIBS = -framework GLUT -framework OpenGL -framework AppKit $(SDL2_LIBS)
OSX_CFLAGS = $(CFLAGS) -std=c++11 -arch arm64 -arch x86_64
CFLAGS += -MMD -MP
COMPILE = $(CC) $(CFLAGS) -c
OBJFILES := $(patsubst %.cpp,%.o,$(ALL_SRCS))
ifeq ($(UNAME), Darwin)
  OBJFILES += macos_window.o
endif
DEPFILES := $(OBJFILES:.o=.d)

all: newtonia

osx: $(OBJFILES)
	CFLAGS="$(OSX_CFLAGS)" $(CC) -o newtonia $(OBJFILES) $(OSX_LIBS)
	mkdir -p Newtonia.app/Contents/MacOS
	mkdir -p Newtonia.app/Contents/Resources
	cp newtonia Newtonia.app/Contents/MacOS/Newtonia
	rm -rf Newtonia.app/Contents/Resources/audio
	cp -r audio Newtonia.app/Contents/Resources/audio
	cp icon.icns Newtonia.app/Contents/Resources/icon.icns
	sed 's/$${EXECUTABLE_NAME}/Newtonia/g' Newtonia-Info.plist > Newtonia.app/Contents/Info.plist

newtonia: $(OBJFILES)
	$(CC) -o newtonia $(OBJFILES) $(LIBS)

clean:
	rm -rf $(OBJFILES) $(DEPFILES) newtonia

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
WEB_FLAGS = -std=c++11 -O2 \
            -s USE_SDL=2 \
            -s USE_SDL_MIXER=2 \
            -s SDL2_MIXER_FORMATS='["wav","mp3"]' \
            -s FULL_ES2=1 \
            -s ALLOW_MEMORY_GROWTH=1 \
            -s GROWABLE_ARRAYBUFFERS=0 \
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

# ============================================================
# Steam build (local testing): make steam
# ============================================================
# Requires the Steamworks SDK unzipped at ./sdk (so that
# sdk/public/steam/steam_api.h exists). Produces ./newtonia-steam plus
# steam_appid.txt and the Steam runtime library beside it, so it can be
# launched directly from the repo root with the Steam client running.
# Steam objects build as *.steam.o so they never mix with the plain
# build's objects (different -D flags). The deploy-steam workflow remains
# the source of truth for shippable depots — this target is for local
# achievement/overlay testing only (never ship steam_appid.txt).
STEAM_APPID ?= 4536720
STEAM_SDK ?= sdk
STEAM_CFLAGS = $(CFLAGS) -DSTEAM_BUILD -I$(STEAM_SDK)/public
STEAM_OBJFILES := $(patsubst %.cpp,%.steam.o,$(ALL_SRCS))
ifeq ($(UNAME), Darwin)
  STEAM_OBJFILES += macos_window.steam.o
  STEAM_RUNTIME = libsteam_api.dylib
  STEAM_SDK_LIB = $(STEAM_SDK)/redistributable_bin/osx/libsteam_api.dylib
  STEAM_LINK = -L. -lsteam_api
else
  STEAM_RUNTIME = libsteam_api.so
  STEAM_SDK_LIB = $(STEAM_SDK)/redistributable_bin/linux64/libsteam_api.so
  STEAM_LINK = -L. -lsteam_api -Wl,-rpath,'$$ORIGIN'
endif
STEAM_DEPFILES := $(STEAM_OBJFILES:.o=.d)

.PHONY: steam steam-clean check-steam-sdk

steam: check-steam-sdk newtonia-steam steam_appid.txt

check-steam-sdk:
	@test -f $(STEAM_SDK)/public/steam/steam_api.h || { \
	  echo "Steamworks SDK not found at ./$(STEAM_SDK)/."; \
	  echo "Unzip steamworks_sdk.zip in the repo root (creates ./sdk/) and retry."; \
	  exit 1; }

$(STEAM_RUNTIME): $(STEAM_SDK_LIB)
	cp $< $@
ifeq ($(UNAME), Darwin)
	install_name_tool -id @loader_path/libsteam_api.dylib $@
endif

newtonia-steam: $(STEAM_OBJFILES) $(STEAM_RUNTIME)
	$(CC) -o $@ $(STEAM_OBJFILES) $(STEAM_LINK) $(LIBS)

steam_appid.txt:
	echo $(STEAM_APPID) > $@

steam-clean:
	rm -rf $(STEAM_OBJFILES) $(STEAM_DEPFILES) newtonia-steam $(STEAM_RUNTIME) steam_appid.txt

%.steam.o: %.cpp
	$(CC) $(STEAM_CFLAGS) -c -o $@ $<

%.steam.o: %.mm
	$(CC) $(STEAM_CFLAGS) -c -o $@ $<

-include $(DEPFILES)
-include $(STEAM_DEPFILES)

%.o: %.cpp
	$(COMPILE) -o $@ $<

%.o: %.mm
	$(CC) $(CFLAGS) -c -o $@ $<
