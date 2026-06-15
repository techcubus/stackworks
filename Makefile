CC     = gcc
CFLAGS = -Wall -Wextra -std=c11 -g -D_POSIX_C_SOURCE=200809L $(shell sdl2-config --cflags)
LDFLAGS = $(shell sdl2-config --libs)

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
