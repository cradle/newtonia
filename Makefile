CC = g++
CFLAGS = -Wall -O3
OSX_LIBS = -framework GLUT -framework OpenGL -framework SDL2 -framework SDL2_mixer
OSX_CFLAGS = $(CFLAGS) -arch i386 -arch ppc
LIBS = -lglut -lGL -lGLU
COMPILE = $(CC) $(CFLAGS) -c 
OBJFILES := $(patsubst %.cpp,%.o,$(wildcard *.cpp) $(wildcard */*.cpp))

all: newtonia

osx: $(OBJFILES)
	CFLAGS="$(OSX_CFLAGS)" $(CC) -o newtonia $(OBJFILES) $(OSX_LIBS)

newtonia: $(OBJFILES)
	$(CC) -o newtonia $(OBJFILES) $(LIBS)

clean:
	rm -rf $(OBJFILES) newtonia

%.o: %.cpp
	$(COMPILE) -o $@ $<
