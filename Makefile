CC = g++
SDL2_CFLAGS := $(shell sdl2-config --cflags)
SDL2_LIBS   := $(shell sdl2-config --libs) -lSDL2_mixer
CFLAGS = -Wall -O3 $(SDL2_CFLAGS)
OSX_LIBS = -framework GLUT -framework OpenGL -framework SDL2 -framework SDL2_mixer
OSX_CFLAGS = $(CFLAGS) -arch i386 -arch ppc
LIBS = -lglut -lGL -lGLU $(SDL2_LIBS)
COMPILE = $(CC) $(CFLAGS) -c
OBJFILES := $(patsubst %.cpp,%.o,$(wildcard *.cpp) $(wildcard */*.cpp))

all: newtonia

osx: $(OBJFILES)
	CFLAGS="$(OSX_CFLAGS)" $(CC) -o newtonia $(OBJFILES) $(OSX_LIBS)
	cp icon.icns Newtonia.app/Contents/Resources/icon.icns

newtonia: $(OBJFILES)
	$(CC) -o newtonia $(OBJFILES) $(LIBS)

clean:
	rm -rf $(OBJFILES) newtonia

%.o: %.cpp
	$(COMPILE) -o $@ $<
