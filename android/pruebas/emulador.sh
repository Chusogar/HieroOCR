#!/bin/sh
# Prueba de la aplicación en un emulador (la ejecuta el workflow de GitHub Actions):
# instala el APK de depuración, abre un PDF y una foto girada (EXIF), toca un
# signo y guarda capturas de pantalla y el registro en capturas/.
set -x
P=org.hieroocr.app
mkdir -p capturas
captura() { sleep "$2"; adb exec-out screencap -p > "capturas/$1.png"; }

adb install -r apk/HieroOCR-depuracion.apk || exit 1
adb logcat -c
adb shell am start -W -n $P/.HieroActivity
captura 1-inicio 8

# Los archivos de prueba van a la caché de la aplicación (run-as: APK de depuración).
for f in pagina72.pdf foto72.jpg; do
    adb exec-in "run-as $P sh -c 'cat > /data/data/$P/cache/$f'" < android/pruebas/$f
done
adb shell run-as $P ls -la /data/data/$P/cache /data/data/$P/files/datos

adb shell am start -n $P/.HieroActivity --es archivo /data/data/$P/cache/pagina72.pdf
captura 2-pdf 45
adb shell input tap 300 900          # un signo de la página
captura 3-signo 6
adb shell input swipe 700 1200 300 1200 400   # arrastrar la página
captura 4-arrastre 4

adb shell am start -n $P/.HieroActivity --es archivo /data/data/$P/cache/foto72.jpg
captura 5-foto 45

adb shell run-as $P ls -la /data/data/$P/cache
adb shell run-as $P cat /data/data/$P/files/registro.txt > capturas/registro.txt
adb logcat -d -s hieroocr:V SDL:V SDL/APP:V AndroidRuntime:E DEBUG:V > capturas/logcat.txt
adb shell pidof $P && echo "La aplicación sigue en marcha." || { echo "La aplicación se ha cerrado."; exit 1; }
