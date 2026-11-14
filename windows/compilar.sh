#!/bin/sh
# Compila hieroocr.exe para Windows (64 bits) con MinGW-w64 desde Linux y prepara
# un ZIP listo para usar: el programa, las DLL de SDL2 y PDFium, los datos y la fuente.
#
#   sudo apt install gcc-mingw-w64-x86-64 zip
#   sh windows/compilar.sh            # -> windows/HieroOCR-windows-x64.zip
set -e
cd "$(dirname "$0")"
RAIZ=..
DEP=dependencias
SDL=2.32.10 IMG=2.8.12 TTF=2.24.0
CC=${CC:-x86_64-w64-mingw32-gcc}
mkdir -p "$DEP"

baja() {   # baja URL FICHERO
    [ -f "$DEP/$2" ] || { echo "Descargando $2..."; curl -fsSL -o "$DEP/$2" "$1"; }
}
baja https://github.com/libsdl-org/SDL/releases/download/release-$SDL/SDL2-devel-$SDL-mingw.tar.gz sdl2.tar.gz
baja https://github.com/libsdl-org/SDL_image/releases/download/release-$IMG/SDL2_image-devel-$IMG-mingw.tar.gz sdl2_image.tar.gz
baja https://github.com/libsdl-org/SDL_ttf/releases/download/release-$TTF/SDL2_ttf-devel-$TTF-mingw.tar.gz sdl2_ttf.tar.gz
baja https://github.com/bblanchon/pdfium-binaries/releases/latest/download/pdfium-win-x64.tgz pdfium.tgz
for f in sdl2 sdl2_image sdl2_ttf; do tar xzf "$DEP/$f.tar.gz" -C "$DEP"; done
mkdir -p "$DEP/pdfium" && tar xzf "$DEP/pdfium.tgz" -C "$DEP/pdfium"

P=x86_64-w64-mingw32
INC="-I$DEP/SDL2-$SDL/$P/include/SDL2 -I$DEP/SDL2_image-$IMG/$P/include/SDL2 -I$DEP/SDL2_ttf-$TTF/$P/include/SDL2"
LIB="-L$DEP/SDL2-$SDL/$P/lib -L$DEP/SDL2_image-$IMG/$P/lib -L$DEP/SDL2_ttf-$TTF/$P/lib"
SRC="main app image segment classify mdc dict train dataset gui"

OUT=HieroOCR
rm -rf "$OUT" && mkdir -p "$OUT/datos" "$OUT/licencias"
FILES=""
for s in $SRC; do FILES="$FILES $RAIZ/src/$s.c"; done
echo "Compilando hieroocr.exe..."
$CC -O2 -std=c11 -Wall -Wextra -D_DEFAULT_SOURCE $INC $FILES -o "$OUT/hieroocr.exe" \
    $LIB -lSDL2_ttf -lSDL2_image -lSDL2 -lm -static-libgcc

cp "$DEP/SDL2-$SDL/$P/bin/SDL2.dll" "$DEP/SDL2_image-$IMG/$P/bin/SDL2_image.dll" \
   "$DEP/SDL2_ttf-$TTF/$P/bin/SDL2_ttf.dll" "$DEP/pdfium/bin/pdfium.dll" "$OUT/"
cp "$RAIZ"/datos/modelo.hmod "$RAIZ"/datos/gardiner.tsv "$RAIZ"/datos/openglyph_*.tsv \
   "$RAIZ"/datos/mdc_fonetico.tsv "$OUT/datos/"
FUENTE=NotoSansEgyptianHieroglyphs-Regular.ttf
if [ -f "$RAIZ/android/app/src/main/assets/datos/$FUENTE" ]; then
    cp "$RAIZ/android/app/src/main/assets/datos/$FUENTE" "$OUT/datos/"
else
    curl -fsSL -o "$OUT/datos/$FUENTE" \
        "https://raw.githubusercontent.com/notofonts/notofonts.github.io/main/fonts/NotoSansEgyptianHieroglyphs/hinted/ttf/$FUENTE"
fi
cp "$DEP/SDL2-$SDL/LICENSE.txt" "$OUT/licencias/SDL2.txt"
cp "$DEP/SDL2_image-$IMG/LICENSE.txt" "$OUT/licencias/SDL2_image.txt"
cp "$DEP/SDL2_ttf-$TTF/LICENSE.txt" "$OUT/licencias/SDL2_ttf.txt"
cp "$DEP/pdfium/LICENSE" "$OUT/licencias/pdfium-binaries.txt"
cp "$DEP/pdfium/licenses/pdfium.txt" "$OUT/licencias/pdfium.txt"
cp "$RAIZ/OpenGlyph/licenses/LICENSE.txt" "$OUT/licencias/OpenGlyph.txt"
cp "$RAIZ/OpenGlyph/licenses/COPYING.LIB" "$OUT/licencias/OpenGlyph-LGPL.txt"
cp LEEME.txt "$OUT/"
rm -f HieroOCR-windows-x64.zip
zip -qr HieroOCR-windows-x64.zip "$OUT"
ls -la HieroOCR-windows-x64.zip
