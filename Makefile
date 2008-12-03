CC = g++
CFLAGS = -Wall -O2
LIBS = -lglut
COMPILE = $(CC) $(CFLAGS) -c 
OBJFILES := $(patsubst %.cpp,%.o,$(wildcard *.cpp))

all: newtonia

spaceship: $(OBJFILES)
	$(CC) -o newtonia $(OBJFILES) $(LIBS)

%.o: %.cpp
	$(COMPILE) -o $@ $<
