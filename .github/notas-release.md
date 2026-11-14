OCR de jeroglíficos egipcios impresos con salida en **Manuel de Codage (MdC)**, con el diccionario y el corpus de **OpenGlyph** integrados.

## Descargas

| Archivo | Para |
|---|---|
| **HieroOCR-windows-x64.zip** | Windows 10/11 de 64 bits. Descomprímelo y haz doble clic en `hieroocr.exe`; arrastra a la ventana una imagen o un PDF. También funciona desde la consola (`hieroocr -h`). Incluye SDL2, PDFium para leer los PDF, los datos y la fuente. |
| **HieroOCR-android.apk** | Android 5.0 o posterior (móvil y tableta). Ábrelo en el móvil y permite instalar aplicaciones de origen desconocido. Puede abrir imágenes y PDF o hacer una foto de la página con la cámara. |

## Qué hace

- Reconoce los signos de páginas impresas (PDF, JPG, PNG…) y escribe el MdC, con cuadrados, renglones, páginas y pasajes subrayados (`$r … $b`).
- Interfaz para revisar y corregir: cada corrección se aprende como plantilla y mejora las páginas siguientes.
- Modelo entrenado con los 1072 signos de la lista de Gardiner.
- Corrección de iluminación para fotos de páginas hechas con el móvil.
- **OpenGlyph**:
  - buscador de un signo o una combinación de signos, por transliteración o por traducción, en un diccionario de ~12 300 entradas y un corpus de 438 textos;
  - lectura de cada renglón reconocido;
  - el corpus ayuda al OCR a elegir entre signos parecidos.

El modelo entrenado con fotografías (`make entrenar-fotos`) no se incluye porque su conjunto de datos no tiene licencia; en Android se puede cargar desde **Abrir**, y en Windows basta con copiar `modelo_fotos.hmod` a la carpeta `datos`.

El APK está firmado con una clave de depuración.
