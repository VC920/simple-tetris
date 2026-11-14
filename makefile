CC	= gcc
CFLAGS	= -Wall -Wextra

INCLUDE_FLAGS = -Ithird/SDL3/include -Ithird/SDL3_mixer/include

LIB_FLAGS = -Lthird/SDL3/lib -Lthird/SDL3_mixer/lib
LD_FLAGS  = -lSDL3 -lSDL3_mixer

all:
	$(CC) $(CFLAGS) main.c -o tetris $(INCLUDE_FLAGS) $(LIB_FLAGS) $(LD_FLAGS) -mwindows
run:
	./tetris
