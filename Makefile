CC = g++
SDL2_CFLAGS := $(shell sdl2-config --cflags)
SDL2_LIBS   := $(shell sdl2-config --libs) -lSDL2_mixer
CFLAGS = -Wall -O3 -std=c++11 $(SDL2_CFLAGS)

UNAME := $(shell uname)
ANDROID_SRCS = gles2_compat.cpp android_main.cpp

ifeq ($(UNAME), Darwin)
  LIBS = -framework GLUT -framework OpenGL $(SDL2_LIBS)
  CFLAGS += -DGL_SILENCE_DEPRECATION -Wno-char-subscripts
  ALL_SRCS := $(filter-out $(ANDROID_SRCS),$(wildcard *.cpp) $(wildcard */*.cpp))
else
  LIBS = -lglut -lGL -lGLU $(SDL2_LIBS)
  ALL_SRCS := $(filter-out $(ANDROID_SRCS),$(wildcard *.cpp) $(wildcard */*.cpp))
endif

OSX_LIBS = -framework GLUT -framework OpenGL $(SDL2_LIBS)
OSX_CFLAGS = $(CFLAGS) -std=c++11 -arch i386 -arch ppc
COMPILE = $(CC) $(CFLAGS) -c
OBJFILES := $(patsubst %.cpp,%.o,$(ALL_SRCS))

all: newtonia

osx: $(OBJFILES)
	CFLAGS="$(OSX_CFLAGS)" $(CC) -o newtonia $(OBJFILES) $(OSX_LIBS)
	mkdir -p Newtonia.app/Contents/MacOS
	mkdir -p Newtonia.app/Contents/Resources
	cp newtonia Newtonia.app/Contents/MacOS/newtonia_bin
	cp -r audio Newtonia.app/Contents/Resources/audio
	printf '#!/bin/sh\ncd "$$(dirname "$$0")/../Resources"\nexec ../MacOS/newtonia_bin "$$@"\n' > Newtonia.app/Contents/MacOS/newtonia
	chmod +x Newtonia.app/Contents/MacOS/newtonia
	cp icon.icns Newtonia.app/Contents/Resources/icon.icns

newtonia: $(OBJFILES)
	$(CC) -o newtonia $(OBJFILES) $(LIBS)

clean:
	rm -rf $(OBJFILES) newtonia

# ============================================================
# Web / Emscripten target
# ============================================================
EMCC = emcc

# Exclude desktop and Android entry points; add web entry point
WEB_EXCL = glut.cpp android_main.cpp
WEB_SRCS := $(filter-out $(WEB_EXCL), $(wildcard *.cpp) $(wildcard */*.cpp))

WEB_FLAGS = -std=c++11 -O2 \
            -s USE_SDL=2 \
            -s USE_SDL_MIXER=2 \
            -s SDL2_MIXER_FORMATS='["wav","mp3"]' \
            -s FULL_ES2=1 \
            -s ALLOW_MEMORY_GROWTH=1 \
            -lidbfs.js \
            --shell-file web/shell.html

WEB_FLAGS += --preload-file audio@audio

.PHONY: web web-clean

web:
	mkdir -p web/dist
	tsc -p web/tsconfig.json
	$(EMCC) $(WEB_SRCS) $(WEB_FLAGS) -o web/dist/index.html
	cp web/main.js web/dist/main.js

web-clean:
	rm -rf web/dist

%.o: %.cpp
	$(COMPILE) -o $@ $<
