#!/bin/sh
# Prepara el proyecto Android de HieroOCR:
#  - descarga SDL2, SDL2_image y SDL2_ttf (con FreeType) en app/jni/
#  - copia las clases Java de SDL a app/src/main/java/org/libsdl/app/
#  - copia el modelo, la lista de Gardiner y la fuente jeroglífica a assets/datos/
# Después: ./gradlew assembleRelease   (o abrir la carpeta android/ en Android Studio)
set -e
cd "$(dirname "$0")"

SDL_TAG=release-2.32.10
IMG_TAG=release-2.8.12
TTF_TAG=release-2.24.0
JNI=app/jni

clone() {   # clone REPO TAG DESTINO
    if [ ! -f "$3/Android.mk" ] && [ ! -f "$3/include/freetype/freetype.h" ]; then
        rm -rf "$3"
        echo "Descargando $1 ($2)..."
        git -c advice.detachedHead=false clone -q --depth 1 -b "$2" "https://github.com/libsdl-org/$1.git" "$3"
    fi
}
clone SDL "$SDL_TAG" "$JNI/SDL"
clone SDL_image "$IMG_TAG" "$JNI/SDL_image"
clone SDL_ttf "$TTF_TAG" "$JNI/SDL_ttf"
FT_BRANCH=$(git config -f "$JNI/SDL_ttf/.gitmodules" submodule.external/freetype.branch)
clone freetype "$FT_BRANCH" "$JNI/SDL_ttf/external/freetype"

mkdir -p app/src/main/java/org/libsdl/app
cp "$JNI"/SDL/android-project/app/src/main/java/org/libsdl/app/*.java app/src/main/java/org/libsdl/app/

DATOS=app/src/main/assets/datos
mkdir -p "$DATOS"
cp ../datos/modelo.hmod ../datos/gardiner.tsv "$DATOS"/
# Diccionario y corpus de OpenGlyph (OpenGlyph/exportar.py)
cp ../datos/openglyph_diccionario.tsv ../datos/openglyph_corpus.tsv ../datos/openglyph_textos.tsv \
   ../datos/mdc_fonetico.tsv "$DATOS"/
# El modelo entrenado con fotografías (make entrenar-fotos) no se distribuye; si
# lo tienes y quieres llevarlo en tu propio APK: INCLUIR_MODELO_FOTOS=1 ./preparar.sh
# (también se puede cargar desde la aplicación eligiendo el archivo .hmod).
if [ "${INCLUIR_MODELO_FOTOS:-0}" = 1 ] && [ -f ../datos/modelo_fotos.hmod ]; then
    cp ../datos/modelo_fotos.hmod "$DATOS"/
    echo "Incluido datos/modelo_fotos.hmod (sólo para uso propio)."
else
    rm -f "$DATOS"/modelo_fotos.hmod
fi
FUENTE=NotoSansEgyptianHieroglyphs-Regular.ttf
if [ ! -f "$DATOS/$FUENTE" ]; then
    for f in ../datos/$FUENTE /usr/share/fonts/truetype/noto/$FUENTE /usr/share/fonts/noto/$FUENTE; do
        if [ -f "$f" ]; then cp "$f" "$DATOS/"; break; fi
    done
fi
if [ ! -f "$DATOS/$FUENTE" ]; then
    echo "Descargando $FUENTE..."
    curl -fsSL -o "$DATOS/$FUENTE" \
        "https://raw.githubusercontent.com/notofonts/notofonts.github.io/main/fonts/NotoSansEgyptianHieroglyphs/hinted/ttf/$FUENTE"
fi
echo "Listo. Compila con: cd android && ./gradlew assembleRelease"
