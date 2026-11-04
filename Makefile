CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11
CFLAGS  += -D_DEFAULT_SOURCE $(shell pkg-config --cflags sdl2 SDL2_image SDL2_ttf)
LDLIBS  += $(shell pkg-config --libs sdl2 SDL2_image SDL2_ttf) -lm

SRC = src/main.c src/image.c src/segment.c src/classify.c src/mdc.c src/train.c src/dataset.c src/gui.c
OBJ = $(SRC:.c=.o)

hieroocr: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDLIBS)

src/%.o: src/%.c src/hieroocr.h
	$(CC) $(CFLAGS) -c -o $@ $<

entrenar: hieroocr
	./hieroocr --entrenar

# Modelo con las fotografías de R. Fuentes-Ferrer et al. (EgyptianHieroglyphicText).
DATASET_REPO = https://github.com/rfuentesfe/EgyptianHieroglyphicText
DATASET_DIR  = terceros/EgyptianHieroglyphicText

entrenar-fotos: hieroocr
	test -d $(DATASET_DIR) || git clone --depth 1 $(DATASET_REPO) $(DATASET_DIR)
	./hieroocr --entrenar -D $(DATASET_DIR)/dataset -m datos/modelo_fotos.hmod

clean:
	rm -f $(OBJ) hieroocr

.PHONY: clean entrenar entrenar-fotos
