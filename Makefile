CC = g++
CFLAGS = -Wall -O3
LIBS = -lglut
COMPILE = $(CC) $(CFLAGS) -c 
OBJFILES := $(patsubst %.cpp,%.o,$(wildcard *.cpp) $(wildcard */*.cpp))

all: newtonia

newtonia: $(OBJFILES)
	$(CC) -o newtonia $(OBJFILES) $(LIBS)

%.o: %.cpp
	$(COMPILE) -o $@ $<
