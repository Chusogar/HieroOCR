#!/bin/sh
# Compila HieroOCR para iOS / iPadOS 14+ con SDL2, SDL2_image y SDL2_ttf dentro:
#   sh apple/compilar-ios.sh             -> apple/HieroOCR-ios.ipa (iPhone / iPad, SIN FIRMAR)
#   sh apple/compilar-ios.sh simulador   -> apple/HieroOCR-ios-simulador.zip (HieroOCR.app)
# El .ipa hay que firmarlo para instalarlo (Sideloadly, AltStore, o Xcode con una
# cuenta de Apple; ver apple/LEEME-ios.txt). Necesita Xcode y CMake.
set -e
cd "$(dirname "$0")"
[ -f deps/SDL/CMakeLists.txt ] || sh preparar.sh
K=${1:-dispositivo}
if [ "$K" = simulador ]; then SDK=iphonesimulator; else SDK=iphoneos; fi
B=build-ios-$K
cmake -S .. -B $B -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=$SDK \
      -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 \
      -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY
cmake --build $B --config Release -j "$(sysctl -n hw.ncpu)"

APP=$B/HieroOCR.app
mkdir -p "$APP/datos" "$APP/licencias"
cp deps/datos/* "$APP/datos/"
for i in "60x60@2x 120" "60x60@3x 180" "76x76@2x~ipad 152" "83.5x83.5@2x~ipad 167"; do
    set -- $i
    sips -z $2 $2 icono-ios.png --out "$APP/AppIcon$1.png" > /dev/null
done
cp deps/SDL/LICENSE.txt "$APP/licencias/SDL2.txt"
cp deps/SDL_image/LICENSE.txt "$APP/licencias/SDL2_image.txt"
cp deps/SDL_ttf/LICENSE.txt "$APP/licencias/SDL2_ttf.txt"
cp ../OpenGlyph/licenses/LICENSE.txt "$APP/licencias/OpenGlyph.txt"


if [ "$K" = simulador ]; then
    codesign --force --sign - "$APP"   # (el simulador acepta la firma ad hoc)
    rm -f HieroOCR-ios-simulador.zip
    (cd $B && ditto -c -k --keepParent HieroOCR.app ../HieroOCR-ios-simulador.zip)
    ls -la HieroOCR-ios-simulador.zip
else
    P=$B/ipa
    rm -rf "$P" && mkdir -p "$P/Payload" && cp -R "$APP" "$P/Payload/"
    rm -f HieroOCR-ios.ipa
    (cd "$P" && zip -qr ../../HieroOCR-ios.ipa Payload)
    ls -la HieroOCR-ios.ipa
fi
