CC = g++
CFLAGS = -Wall -O2
LIBS = -lglut
COMPILE = $(CC) $(CFLAGS) -c 
OBJFILES := $(patsubst %.cpp,%.o,$(wildcard *.cpp))

all: spaceship

spaceship: $(OBJFILES)
	$(CC) -o spaceship $(OBJFILES) $(LIBS)

%.o: %.cpp
	$(COMPILE) -o $@ $<
