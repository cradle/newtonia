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

-include $(DEPFILES)

%.o: %.cpp
	$(COMPILE) -o $@ $<

%.o: %.mm
	$(CC) $(CFLAGS) -c -o $@ $<
