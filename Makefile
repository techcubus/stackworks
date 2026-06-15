CC     = gcc
NK_FLAGS = -DNK_INCLUDE_FIXED_TYPES -DNK_INCLUDE_STANDARD_IO \
           -DNK_INCLUDE_DEFAULT_ALLOCATOR -DNK_INCLUDE_VERTEX_BUFFER_OUTPUT \
           -DNK_INCLUDE_FONT_BAKING -DNK_INCLUDE_DEFAULT_FONT \
           -DNK_INCLUDE_STANDARD_VARARGS
CFLAGS = -Wall -Wextra -std=c11 -g -D_POSIX_C_SOURCE=200809L \
         $(shell sdl2-config --cflags) -Isrc $(NK_FLAGS)
LDFLAGS = $(shell sdl2-config --libs) -lm

SRCS = src/main.c src/stack.c src/render.c src/woba.c
OBJS = $(SRCS:.c=.o)
TARGET = cardviewer

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

src/%.o: src/%.c src/stack.h src/render.h src/woba.h
	$(CC) $(CFLAGS) -Isrc -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: clean
