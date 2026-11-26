#!/bin/sh
# Prepara la compilación de HieroOCR para macOS e iOS (también vale en Linux):
#  - descarga SDL2, SDL2_image y SDL2_ttf (con FreeType) en apple/deps/
#    (las mismas versiones que Android y Windows)
#  - deja en apple/deps/datos/ lo que lleva la aplicación: el modelo, la lista de
#    Gardiner, el diccionario y el corpus de OpenGlyph, la fuente jeroglífica
#    (Noto) y, para la interfaz de iOS, DejaVu Sans
# Después: sh apple/compilar-macos.sh   o   sh apple/compilar-ios.sh
set -e
cd "$(dirname "$0")"

SDL_TAG=release-2.32.10
IMG_TAG=release-2.8.12
TTF_TAG=release-2.24.0
DEPS=deps
mkdir -p "$DEPS"

clone() {   # clone REPO TAG DESTINO
    if [ ! -f "$3/CMakeLists.txt" ]; then
        rm -rf "$3"
        echo "Descargando $1 ($2)..."
        git -c advice.detachedHead=false clone -q --depth 1 -b "$2" "https://github.com/libsdl-org/$1.git" "$3"
    fi
}
clone SDL "$SDL_TAG" "$DEPS/SDL"
clone SDL_image "$IMG_TAG" "$DEPS/SDL_image"
clone SDL_ttf "$TTF_TAG" "$DEPS/SDL_ttf"
FT_BRANCH=$(git config -f "$DEPS/SDL_ttf/.gitmodules" submodule.external/freetype.branch)
clone freetype "$FT_BRANCH" "$DEPS/SDL_ttf/external/freetype"

DATOS=$DEPS/datos
mkdir -p "$DATOS"
cp ../datos/modelo.hmod ../datos/gardiner.tsv ../datos/openglyph_diccionario.tsv ../datos/openglyph_corpus.tsv \
   ../datos/openglyph_textos.tsv ../datos/mdc_fonetico.tsv "$DATOS"/
# (el modelo entrenado con fotografías no se distribuye: INCLUIR_MODELO_FOTOS=1 para uso propio)
if [ "${INCLUIR_MODELO_FOTOS:-0}" = 1 ] && [ -f ../datos/modelo_fotos.hmod ]; then
    cp ../datos/modelo_fotos.hmod "$DATOS"/
else
    rm -f "$DATOS"/modelo_fotos.hmod
fi
FUENTE=NotoSansEgyptianHieroglyphs-Regular.ttf
if [ ! -f "$DATOS/$FUENTE" ]; then
    for f in ../android/app/src/main/assets/datos/$FUENTE /usr/share/fonts/truetype/noto/$FUENTE; do
        if [ -f "$f" ]; then cp "$f" "$DATOS/"; break; fi
    done
fi
if [ ! -f "$DATOS/$FUENTE" ]; then
    echo "Descargando $FUENTE..."
    curl -fsSL -o "$DATOS/$FUENTE" \
        "https://raw.githubusercontent.com/notofonts/notofonts.github.io/main/fonts/NotoSansEgyptianHieroglyphs/hinted/ttf/$FUENTE"
fi
if [ ! -f "$DATOS/DejaVuSans.ttf" ]; then
    if [ -f /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf ]; then
        cp /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf "$DATOS/"
    else
        echo "Descargando DejaVu Sans..."
        curl -fsSL -o "$DEPS/dejavu.zip" \
            https://github.com/dejavu-fonts/dejavu-fonts/releases/download/version_2_37/dejavu-fonts-ttf-2.37.zip
        unzip -q -o -j "$DEPS/dejavu.zip" dejavu-fonts-ttf-2.37/ttf/DejaVuSans.ttf dejavu-fonts-ttf-2.37/LICENSE -d "$DEPS"
        mv "$DEPS/DejaVuSans.ttf" "$DATOS/"
    fi
fi
echo "Listo."
