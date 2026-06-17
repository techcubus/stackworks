CC     = gcc
NK_FLAGS = -DNK_INCLUDE_FIXED_TYPES -DNK_INCLUDE_STANDARD_IO \
           -DNK_INCLUDE_DEFAULT_ALLOCATOR -DNK_INCLUDE_VERTEX_BUFFER_OUTPUT \
           -DNK_INCLUDE_FONT_BAKING -DNK_INCLUDE_DEFAULT_FONT \
           -DNK_INCLUDE_STANDARD_VARARGS
CFLAGS = -Wall -Wextra -std=c11 -g -D_POSIX_C_SOURCE=200809L \
         $(shell sdl2-config --cflags) -Isrc $(NK_FLAGS) -MMD -MP
LDFLAGS = $(shell sdl2-config --libs) $(shell pkg-config --libs SDL2_ttf) -lm

SRCS = src/main.c src/stack.c src/woba.c \
       src/ui/render.c src/ui/card_view.c src/ui/menu.c
OBJS = $(SRCS:.c=.o)
DEPS = $(OBJS:.o=.d)
TARGET = stackworks

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

tools/rsrcextract: tools/rsrcextract.c
	$(CC) -Wall -Wextra -std=c11 -g -o $@ $<

tools/pict2ppm: tools/pict2ppm.c
	$(CC) -Wall -Wextra -std=c11 -g -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

-include $(DEPS)

clean:
	rm -f $(OBJS) $(DEPS) $(TARGET) tools/rsrcextract tools/pict2ppm

.PHONY: clean tools/rsrcextract
