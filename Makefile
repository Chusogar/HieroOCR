CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11
CFLAGS  += -D_DEFAULT_SOURCE $(shell pkg-config --cflags sdl2 SDL2_image SDL2_ttf)
LDLIBS  += $(shell pkg-config --libs sdl2 SDL2_image SDL2_ttf) -lm

SRC = src/main.c src/image.c src/segment.c src/classify.c src/mdc.c src/train.c src/gui.c
OBJ = $(SRC:.c=.o)

hieroocr: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDLIBS)

src/%.o: src/%.c src/hieroocr.h
	$(CC) $(CFLAGS) -c -o $@ $<

entrenar: hieroocr
	./hieroocr --entrenar

clean:
	rm -f $(OBJ) hieroocr

.PHONY: clean entrenar
