# Publicar HieroOCR en Google Play

Todo lo que va en la ficha está en esta carpeta:

| Archivo | Dónde va en Play Console |
|---|---|
| `es-ES/`, `en-US/` (`title`, `short_description`, `full_description`) | Ficha de Play Store principal (español) y traducción al inglés |
| `icono-512.png` | Icono de la aplicación |
| `grafico-destacado.png` (1024×500) | Gráfico de funciones |
| `capturas/` | Capturas de pantalla del teléfono |
| `PRIVACIDAD.md` | URL de la política de privacidad (enlace a este archivo en GitHub) |
| `novedades/` | Notas de la versión (las sube el workflow) |

## 1. Una sola vez

1. Crea la cuenta de desarrollador en https://play.google.com/console (25 $).
2. **Crear aplicación**: nombre «HieroOCR», idioma predeterminado español (España),
   aplicación, gratuita.
3. Genera la clave de subida en tu ordenador (no la subas al repositorio):

   ```sh
   keytool -genkeypair -v -keystore hieroocr-subida.jks -storetype PKCS12 -alias hieroocr -keyalg RSA -keysize 4096 -validity 10000 -dname "CN=HieroOCR, O=HieroOCR, C=ES"
   base64 -w0 hieroocr-subida.jks > hieroocr-subida.txt      # macOS: base64 -i hieroocr-subida.jks -o hieroocr-subida.txt
   ```

4. En GitHub → Settings → Secrets and variables → Actions → *New repository secret*:
   `ANDROID_KEYSTORE_BASE64` (el contenido de `hieroocr-subida.txt`),
   `ANDROID_KEYSTORE_PASSWORD` y `ANDROID_KEY_ALIAS` (= `hieroocr`).
5. Actions → **Google Play** → *Run workflow*. Descarga el artefacto `HieroOCR-aab`.
6. En Play Console → Probar y publicar → **Pruebas internas** → Crear versión: acepta
   *Firma de aplicaciones de Play* y sube `HieroOCR.aab` (y `native-debug-symbols.zip`
   en la versión). Esta primera subida tiene que ser a mano.
7. Completa *Contenido de la aplicación*:
   - Política de privacidad: `https://github.com/Chusogar/HieroOCR/blob/main/android/play/PRIVACIDAD.md`
   - Anuncios: no contiene anuncios.
   - Acceso a la aplicación: todas las funciones están disponibles sin restricciones.
   - Seguridad de los datos: **no recoge ni comparte datos** (sin acceso a Internet).
   - Clasificación de contenido: cuestionario → categoría «Referencia, noticias o
     educación», todo «No» → apta para todos.
   - Público objetivo: 13 años o más (evita los requisitos de apps para niños).
   - Categoría: Educación.
8. Para que el workflow suba las siguientes versiones solo: Google Cloud Console →
   crea una cuenta de servicio y una clave JSON; en Play Console → Usuarios y permisos →
   invítala con permiso para *Publicar en pistas de prueba* (y producción). Guarda el
   JSON en el secreto `PLAY_SERVICE_ACCOUNT_JSON`.

## 2. Cada versión

Actions → **Google Play** → *Run workflow* → pista `internal` (o `alpha`, `beta`,
`production`). El `versionCode` es el número de ejecución, así que siempre crece.

## Cuentas personales nuevas

Si la cuenta es personal y se creó después de noviembre de 2023, antes de poder pasar a
producción Google exige una **prueba cerrada con al menos 12 testers durante 14 días
seguidos** (pista `alpha`). Las cuentas de organización no tienen este requisito.
