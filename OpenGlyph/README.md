# OpenGlyph

Base de datos de jeroglíficos egipcios para aprender y traducir textos del egipcio
antiguo, de **Jesús Ángel García Sánchez (Chuso)**, publicada en SourceForge:
<https://sourceforge.net/projects/openglyph/> (versión 1.1 Beta, 2006).
Licencia **GNU LGPL 2 o posterior** (`licenses/LICENSE.txt`, `licenses/COPYING.LIB`).

Esta carpeta contiene el proyecto tal como lo instala `openglyph-install-1.1Beta.jar`:

| Carpeta | Contenido |
|---|---|
| `src/openglyph/` | fuentes Java de la aplicación (diccionario, interfaz, pantalla inicial) |
| `libs/openglyph.jar` | la aplicación compilada (`java -jar libs/openglyph.jar`) y sus bibliotecas: JSesh, HSQLDB, iText, Liquid Look and Feel, JGoodies Forms |
| `libs/openglyph.script` | la base de datos HSQLDB: **diccionario** (11 705 entradas: signos → transliteración → traducción), **lista de signos** (760) y ficha de textos |
| `libs/textos/` | el **corpus**: 438 textos en MdC (formatos Glyph `.gly` y TkSesh `.hie`): Urkunden IV, Coffin Texts, Ptahhotep, Sinuhé, el Náufrago, el Campesino elocuente, Los dos hermanos, estelas, óstraca de Deir el-Medina, cartas de la XX dinastía, gramática (ejemplos de clase)… |
| `doc/`, `userdoc/` | documentación técnica (javadoc) y de usuario |
| `licenses/` | licencias de OpenGlyph y de las bibliotecas que usa |
| `icons/` | iconos |

Muchos textos del corpus proceden de las transcripciones de Serge Rosmorduc
(TkSesh), como indica la cabecera de cada fichero.

## Cómo se ha extraído

El instalador es un IzPack cuyos ficheros están serializados en Java;
`herramientas/ExtraerIzPack.java` los saca con las propias clases del instalador:

```
javac -cp openglyph-install-1.1Beta.jar -d . herramientas/ExtraerIzPack.java
unzip openglyph-install-1.1Beta.jar 'packs/*'
java -Dsun.jnu.encoding=UTF-8 -cp openglyph-install-1.1Beta.jar:. ExtraerIzPack destino packs/pack*
```

`herramientas/CodigosJSesh.java` vuelca la tabla de códigos fonéticos de MdC del
`jsesh.jar` incluido (`herramientas/codigos_jsesh.tsv`: `nfr` → F35, `ra` → N5…).

## Integración con hieroocr

`exportar.py` convierte el diccionario y el corpus a ficheros TSV en `../datos/`, con
todas las secuencias de signos en **códigos Gardiner**, que es lo que devuelve el OCR:

```
python3 OpenGlyph/exportar.py
```

| Fichero | Contenido |
|---|---|
| `openglyph_diccionario.tsv` | 12 317 entradas (diccionario + fichas de signos), con su frecuencia en el corpus |
| `openglyph_corpus.tsv` | 11 374 renglones de 429 textos (unos 304 000 signos), con MdC original, transliteración y traducción cuando el texto las trae |
| `openglyph_textos.tsv` | nombre y archivo de cada texto |
| `mdc_fonetico.tsv` | códigos fonéticos de MdC → Gardiner (tabla de JSesh, unilíteros y algunos frecuentes que faltaban) |

Los códigos de la notación MdC de 1988 que usan algunos textos (categoría `J` en lugar
de `Aa`) se traducen también. Queda sin traducir un 0,6 % de los tokens, casi todos
coordenadas y metadatos de TkSesh (`pos`, `coord`).

hieroocr usa estos datos de tres maneras (ver el README principal):

1. **Buscador** de un signo o de una combinación de signos (por códigos Gardiner o
   fonéticos, con `?` como comodín), de una transliteración o de una palabra de la
   traducción, en el diccionario y en el corpus: `hieroocr --buscar "G17 D21"` o el
   botón **Diccionario** de la interfaz.
2. **Lectura** de cada renglón reconocido: se parte en palabras del diccionario y se
   busca el renglón del corpus más parecido (con su traducción, si la tiene).
3. **Modelo de lengua**: las frecuencias de pares de signos del corpus ayudan al OCR a
   elegir entre candidatos parecidos.
