#!/bin/sh
# Compila HieroOCR.app para macOS (universal: Apple silicon e Intel, macOS 11+),
# con SDL2, SDL2_image y SDL2_ttf dentro, y la empaqueta:
#   apple/HieroOCR-macos.zip   y   apple/HieroOCR-macos.dmg
# Necesita Xcode (o sus herramientas de línea de órdenes) y CMake.
#   sh apple/preparar.sh && sh apple/compilar-macos.sh
set -e
cd "$(dirname "$0")"
[ -f deps/SDL/CMakeLists.txt ] || sh preparar.sh
B=build-macos
cmake -S .. -B $B -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
cmake --build $B --config Release -j "$(sysctl -n hw.ncpu)"

APP=HieroOCR.app
rm -rf "$APP" && cp -R "$B/HieroOCR.app" "$APP"
R=$APP/Contents/Resources
mkdir -p "$R/datos" "$R/licencias"
cp deps/datos/* "$R/datos/"
rm -f "$R/datos/DejaVuSans.ttf"   # (en macOS la interfaz usa Arial del sistema)
# el icono
IS=build-macos/HieroOCR.iconset
rm -rf "$IS" && mkdir -p "$IS"
for s in 16 32 128 256 512; do
    sips -z $s $s icono-macos.png --out "$IS/icon_${s}x${s}.png" > /dev/null
    sips -z $((s * 2)) $((s * 2)) icono-macos.png --out "$IS/icon_${s}x${s}@2x.png" > /dev/null
done
iconutil -c icns "$IS" -o "$R/HieroOCR.icns"
cp deps/SDL/LICENSE.txt "$R/licencias/SDL2.txt"
cp deps/SDL_image/LICENSE.txt "$R/licencias/SDL2_image.txt"
cp deps/SDL_ttf/LICENSE.txt "$R/licencias/SDL2_ttf.txt"
cp ../OpenGlyph/licenses/LICENSE.txt "$R/licencias/OpenGlyph.txt"
cp ../OpenGlyph/licenses/COPYING.LIB "$R/licencias/OpenGlyph-LGPL.txt"
# firma ad hoc (sin ella macOS no abre programas arm64); sin notarizar
codesign --force --deep --sign - "$APP"

rm -f HieroOCR-macos.zip HieroOCR-macos.dmg
ditto -c -k --keepParent "$APP" HieroOCR-macos.zip
D=build-macos/dmg
rm -rf "$D" && mkdir -p "$D" && cp -R "$APP" "$D/" && ln -s /Applications "$D/Aplicaciones"
cp LEEME-macos.txt "$D/LEEME.txt"
hdiutil create -quiet -volname HieroOCR -srcfolder "$D" -ov -format UDZO HieroOCR-macos.dmg
ls -la HieroOCR-macos.zip HieroOCR-macos.dmg
