# hieroocr — OCR de jeroglíficos egipcios a MdC

Programa en C con SDL2. Lee un **PDF** o una **imagen** (JPG, PNG, TIFF…) con texto
jeroglífico impreso en renglones horizontales y escribe la transcripción en
**Manuel de Codage** (MdC), lista para pegar en JSesh.

```
hieroocr -p 71 moldenke.pdf -o pagina71.mdc      # línea de órdenes
hieroocr -g moldenke.pdf                         # interfaz para revisar y enseñar
```

## Cómo funciona

1. **Entrada**: las imágenes se cargan con SDL2_image. Los PDF se rasterizan con
   `pdftoppm` (poppler-utils), por defecto a 300 ppp.
2. **Binarización** por el método de Otsu.
3. **Renglones** por perfil de proyección horizontal. Se unen los fragmentos de un
   mismo renglón y se descartan las bandas bajas, como los números de página.
4. **Subrayados**: los trazos horizontales más largos que 1,2 renglones se borran y se
   recuerdan. Los signos que quedan encima salen entre `$r … $b`, porque en las ediciones
   así se marca la tinta roja.
5. **Signos**: componentes conexas con un radio de unión pequeño, para soldar trazos
   rotos. Se filtran motas, restos de subrayado y marcas de lápiz.
6. **Clasificación**: cada signo se normaliza a 40×40 y se compara con todas las
   plantillas. La puntuación combina la distancia de chamfer, la correlación de las
   imágenes suavizadas, la proporción ancho/alto y la altura relativa al renglón
   (menor = mejor). Se guardan los 5 mejores candidatos.
7. **MdC**: los signos que se solapan en horizontal forman un cuadrado. Dentro del
   cuadrado se apilan con `:` y los que comparten piso se unen con `*`. Los cuadrados
   se separan con `-`, cada renglón acaba en `!` y cada página en `!!`.

### Plantillas

- **De la fuente**: al arrancar se dibujan los 1071 signos de *Noto Sans Egyptian
  Hieroglyphs*. El código Gardiner de cada uno sale de su nombre en `UnicodeData.txt`
  (`EGYPTIAN HIEROGLYPH AA013` → `Aa13`). Unos 220 signos frecuentes tienen
  preferencia; con `-A` se tratan todos por igual.
- **Aprendidas**: cada vez que corriges un signo en la interfaz, su imagen se guarda en
  `plantillas/` (`G17__0.950__…png`). Como salen de la misma edición y tipografía,
  pesan bastante más que las de la fuente.

**Importante:** con solo las plantillas de la fuente el acierto es modesto, porque la
tipografía de cada edición difiere mucho del dibujo de Noto. El uso previsto es este:
corriges unas decenas de signos en las primeras páginas, pulsas **R** para reclasificar
y las páginas siguientes salen mucho mejor. En la prueba con Moldenke, enseñar **un solo**
junco (M17) hizo que se reconocieran 35 de una página que antes daba 1.

## Compilar

Dependencias: SDL2, SDL2_image, SDL2_ttf, `pkg-config`, un compilador C11 y, para los
PDF, `pdftoppm`.

**Debian / Ubuntu**
```
sudo apt install build-essential pkg-config libsdl2-dev libsdl2-image-dev \
                 libsdl2-ttf-dev poppler-utils fonts-noto-core unicode-data
make
```

**Fedora**: `sudo dnf install SDL2-devel SDL2_image-devel SDL2_ttf-devel poppler-utils google-noto-sans-egyptian-hieroglyphs-fonts`
y descarga `UnicodeData.txt` de unicode.org.

**macOS (Homebrew)**: `brew install sdl2 sdl2_image sdl2_ttf poppler pkg-config`. Descarga
la fuente Noto Sans Egyptian Hieroglyphs y `UnicodeData.txt` y pásalas con `-f` y `-U`
(o déjalas en `datos/`).

**Windows**: MSYS2/MinGW con los paquetes `mingw-w64-x86_64-SDL2`, `-SDL2_image`,
`-SDL2_ttf` y `-poppler`, y `make`.

Si la fuente o `UnicodeData.txt` no están en las rutas habituales, el programa los busca
también en el directorio actual y en `datos/`.

## Uso

```
hieroocr [opciones] entrada.(pdf|jpg|png|...)

  -o FICHERO   escribe el MdC en FICHERO (por defecto, salida estándar;
               en la interfaz, salida.mdc)
  -g           abre la interfaz SDL2
  -r PPP       resolución de rasterizado del PDF (300)
  -p N         sólo la página N del PDF
  -f FUENTE    fuente jeroglífica TTF
  -U FICHERO   UnicodeData.txt
  -t CARPETA   plantillas aprendidas (./plantillas)
  -F           sólo plantillas aprendidas (sin la fuente)
  -A           no favorecer los signos frecuentes
  -j PX        radio de unión de trazos (automático)
  -a PX        área mínima de un signo (automática)
  -R           texto de derecha a izquierda
  -u           añade ++?+s tras los cuadrados con signos dudosos
  -T UMBRAL    umbral de duda (0.11)
```

Sin `-p`, un PDF se procesa entero página a página. Con libros largos conviene
indicar la página o usar la interfaz, que procesa cada página al abrirla.

## Interfaz

| Acción | Tecla / ratón |
|---|---|
| Seleccionar signo | clic izquierdo · Tab / Mayús+Tab |
| Aceptar el candidato *n* | 1 … 5 |
| Escribir el código a mano | Intro, código Gardiner, Intro (Esc cancela) |
| Borrar un signo (mota, número de línea…) | Supr |
| Unir con el signo siguiente (signo partido) | M |
| Reclasificar la página con lo aprendido | R |
| Activar o desactivar el aprendizaje | L |
| Guardar el MdC de todo el documento | S |
| Cambiar de página | AvPág / RePág |
| Zoom / mover / ajustar | rueda o + −, botón derecho arrastrando o flechas, F |
| Salir | Q |

Colores de los recuadros: **verde** = seguro, **ámbar** = dudoso, **rojo** = muy dudoso,
**azul** = corregido por ti. Las líneas magenta son los subrayados detectados.

## Limitaciones

- Solo texto en renglones horizontales. Las columnas verticales no están soportadas;
  `-R` invierte el sentido de lectura.
- No distingue las cifras y letras latinas de la página (números de línea, marcas
  marginales): salen como signos y se borran con Supr.
- Los cuadrados complejos (un signo grande junto a una pila de dos pequeños) pueden
  agruparse de forma distinta a como lo haría un editor humano.
- Las lagunas, los signos restituidos y los facsímiles hieráticos no se detectan.
- Sin plantillas aprendidas, el resultado es un borrador que hay que revisar.
