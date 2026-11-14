#!/usr/bin/env python3
"""
Exporta el diccionario y el corpus de OpenGlyph a ficheros TSV que usa hieroocr.

  datos/openglyph_diccionario.tsv   secuencia de signos -> transliteración, traducción
  datos/openglyph_corpus.tsv        renglones de los textos como secuencias de signos,
                                    con su MdC, transliteración y traducción si las hay
  datos/openglyph_textos.tsv        lista de textos del corpus

Las secuencias de signos se escriben siempre con códigos Gardiner (los mismos que
devuelve el OCR): los códigos fonéticos de MdC (nfr, ra, Hr...) se traducen con la
tabla de JSesh (herramientas/codigos_jsesh.tsv) y la de unilíteros.

Uso:  python3 OpenGlyph/exportar.py        (desde la raíz del repositorio)
Sólo usa la biblioteca estándar de Python 3.
"""
import os
import re
import sys
from collections import Counter

AQUI = os.path.dirname(os.path.abspath(__file__))
RAIZ = os.path.dirname(AQUI)
SCRIPT = os.path.join(AQUI, "libs", "openglyph.script")
TEXTOS = os.path.join(AQUI, "libs", "textos")
CODIGOS = os.path.join(AQUI, "herramientas", "codigos_jsesh.tsv")
DATOS = os.path.join(RAIZ, "datos")

# Unilíteros de MdC (JSesh los trata aparte de su tabla).
UNILITEROS = {
    "A": "G1", "i": "M17", "y": "Z4", "a": "D36", "w": "G43", "W": "Z7", "b": "D58",
    "p": "Q3", "f": "I9", "m": "G17", "n": "N35", "r": "D21", "h": "O4", "H": "V28",
    "x": "Aa1", "X": "F32", "z": "O34", "s": "S29", "S": "N37", "q": "N29", "k": "V31",
    "g": "W11", "t": "X1", "T": "V13", "d": "D46", "D": "I10", "M": "Y5", "N": "N35",
    "ii": "M17A", "Z": "Z1",
}
# Códigos fonéticos frecuentes que faltan (o difieren) en la tabla de JSesh de 2006.
COMPLEMENTO = {
    "sA": "G39", "zA": "G39", "nsw": "M23", "nswt": "M23", "bit": "L2", "Hwt": "O6",
    "aA": "O29", "wr": "G36", "Dw": "N26", "tA": "N16", "pt": "N1", "Sms": "T18",
    "Dsr": "D45", "mAa": "Aa11", "Axt": "N27", "bA": "G29", "kA": "D28", "Ax": "G25",
    "HqA": "S38", "wAH": "V29", "smA": "F36", "Ssp": "O42", "sS": "Y3", "mSa": "A12",
    "sr": "A21", "idb": "N21", "nDs": "G37", "Ss": "V6", "inr": "O39", "Hm": "U36",
    "nTr": "R8", "ra": "N5", "mn": "Y5", "mi": "W19", "iAw": "A19", "nxt": "M3",
    "Sps": "A50", "xrw": "A2", "Xrd": "A17", "Xnm": "W9", "Ts": "V4", "mH": "V22",
}
GARDINER = re.compile(r"^(Aa|NL|NU|[A-IK-Z])(\d+)([A-Za-z]?)$")


def gardiner(code):
    """'m28a' -> 'M28A', 'aa1' -> 'Aa1'. None si no es un código Gardiner."""
    m = GARDINER.match(code)
    if not m:
        m = re.match(r"^(aa|nl|nu|[a-ik-z])(\d+)([A-Za-z]?)$", code)
        if not m or code in CANON:   # 'a1' podría ser fonético: no lo es, pero por si acaso
            return None
    pre = m.group(1)
    pre = "Aa" if pre.lower() == "aa" else pre.upper()
    return "%s%d%s" % (pre, int(m.group(2)), m.group(3).upper())


def cargar_codigos():
    canon = {}
    with open(CODIGOS, encoding="utf-8") as f:
        for linea in f:
            if linea.startswith("#"):
                continue
            c = linea.rstrip("\n").split("\t")
            if len(c) >= 2 and c[1]:
                canon[c[0]] = c[1]
    canon.update(COMPLEMENTO)
    for k, v in UNILITEROS.items():
        canon.setdefault(k, v)
    return canon


CANON = {}
desconocidos = Counter()


def signo(tok):
    """Un token MdC -> código Gardiner (o None si no es un signo)."""
    if not tok or tok.isdigit():
        return None
    m = re.match(r"^J(\d+[A-Za-z]?)$", tok)   # MdC de 1988: la categoría Aa se escribía J
    if m:
        tok = "Aa" + m.group(1)
    g = gardiner(tok)
    if g:
        return g
    c = CANON.get(tok)
    if c:
        return gardiner(c) or c
    desconocidos[tok] += 1
    return None


def signos_mdc(texto):
    """Lista de códigos Gardiner de un fragmento MdC, en orden de lectura."""
    s = re.sub(r"\+\+.*?(\+s|$)", " ", texto)          # comentarios
    s = re.sub(r"\|[^-\s!]*", " ", s)                   # números de renglón |1,2
    s = re.sub(r"\$[a-zA-Z0-9]*", " ", s)               # colores $r $b
    s = re.sub(r"#\d*[a-z]?", " ", s)                   # sombreados
    s = re.sub(r"\\[A-Za-z]*\d*", "", s)                # modificadores \R90 \i
    s = re.sub(r"\^+|\.\.|//|h/|v/", " ", s)
    toks = re.split(r"[-:*&()!_\s<>\[\]{},;=^\"'?.]+", s)
    out = []
    for t in toks:
        t = re.sub(r"^[<>0-9hsf]?<|>[0-9]?$", "", t)      # <1 ... 2> de los cartuchos
        g = signo(t)
        if g:
            out.append(g)
    return out


def leer(path):
    data = open(path, "rb").read()
    try:
        txt = data.decode("utf-8")
    except UnicodeDecodeError:
        txt = data.decode("latin-1")
    return txt.replace("\r", "")


def registros(txt):
    """Divide un texto MdC en renglones: (mdc, transliteración, traducción)."""
    lineas = [l for l in txt.split("\n") if not l.startswith("@GLYPH")]
    txt = "\n".join(lineas)
    partes = re.split(r"(\+\+|\+[stlbi])", txt)
    modo = "s"
    actual = {"s": "", "t": "", "l": ""}
    salida = []

    def cerrar():
        if any(v.strip() for v in actual.values()):
            salida.append({k: " ".join(v.split()) for k, v in actual.items()})
        for k in actual:
            actual[k] = ""

    for p in partes:
        if p in ("+s", "+t", "+l", "+b", "+i"):
            modo = {"+b": "l", "+i": "l"}.get(p, p[1])
            continue
        if p == "++":
            modo = "c"
            continue
        if modo == "c":
            continue
        if modo == "s":
            trozos = p.split("!")
            for i, tr in enumerate(trozos):
                actual["s"] += " " + tr
                if i < len(trozos) - 1:
                    cerrar()
        else:
            # la transliteración y la traducción terminan donde vuelve el MdC
            actual[modo] += " " + p.replace("\n", " ")
    cerrar()

    # "+t" en su propio renglón (Ptahhotep...): pertenece al renglón anterior
    final = []
    for r in salida:
        sig = signos_mdc(r["s"])
        if not sig and final and (r["t"] or r["l"]) and not final[-1]["sig"] is None:
            prev = final[-1]
            if r["t"] and not prev["t"]:
                prev["t"] = r["t"]
                if r["l"] and not prev["l"]:
                    prev["l"] = r["l"]
                continue
            if r["l"] and not prev["l"] and not re.match(r"^[\d .,]+\w{0,8}$", r["l"]):
                prev["l"] = r["l"]
                continue
        if sig:
            final.append({"sig": sig, "s": r["s"].strip(" -"), "t": r["t"], "l": r["l"]})
    return final


def cabecera(txt, clave):
    m = re.search(r"\+\+" + clave + r"\s+(.*?)\+s", txt)
    return " ".join(m.group(1).split()) if m else ""


def limpia(s):
    return " ".join(s.replace("\t", " ").split())


def exportar_corpus():
    textos, corpus = [], []
    rutas = []
    for d, _, fs in os.walk(TEXTOS):
        for f in fs:
            if f.lower().endswith((".gly", ".hie")):
                rutas.append(os.path.join(d, f))
    rutas.sort()
    for ruta in rutas:
        rel = os.path.relpath(ruta, TEXTOS)
        txt = leer(ruta)
        nombre = cabecera(txt, "NAME") or os.path.splitext(os.path.basename(ruta))[0]
        regs = registros(txt)
        if not regs:
            continue
        tid = len(textos)
        textos.append((tid, rel, limpia(nombre), limpia(cabecera(txt, "COMMENT"))))
        for i, r in enumerate(regs):
            corpus.append((tid, i + 1, " ".join(r["sig"]), limpia(r["s"]), limpia(r["t"]), limpia(r["l"])))
    return textos, corpus


def unescape(s):
    return re.sub(r"\\u([0-9a-fA-F]{4})", lambda m: chr(int(m.group(1), 16)), s)


def valores_sql(linea):
    """INSERT INTO T VALUES('a','b','c') -> ['a','b','c'] (comillas '' escapadas)."""
    m = re.match(r"INSERT INTO (\w+) VALUES\((.*)\)\s*$", linea)
    if not m:
        return None, None
    vals, cur, dentro, i, s = [], "", False, 0, m.group(2)
    while i < len(s):
        c = s[i]
        if dentro:
            if c == "'" and i + 1 < len(s) and s[i + 1] == "'":
                cur += "'"
                i += 1
            elif c == "'":
                dentro = False
            else:
                cur += c
        elif c == "'":
            dentro = True
        elif c == ",":
            vals.append(cur)
            cur = ""
        i += 1
    vals.append(cur)
    return m.group(1), [unescape(v) for v in vals]


def exportar_diccionario():
    dic = []
    with open(SCRIPT, encoding="latin-1") as f:
        for linea in f:
            tabla, v = valores_sql(linea.rstrip("\r\n"))
            if tabla not in ("DICTIONARY", "SIGNS") or not v or len(v) < 3:
                continue
            sig = signos_mdc(v[0].replace(" - ", "-"))
            if not sig:
                continue
            tr, trad = limpia(v[1]), limpia(v[2])
            if tr in ("?", "") and trad in ("?", ""):
                continue
            dic.append((" ".join(sig), "" if tr == "?" else tr, "" if trad == "?" else trad,
                        "signo" if tabla == "SIGNS" else "palabra"))
    # sin duplicados exactos
    vistos, out = set(), []
    for d in dic:
        if d not in vistos:
            vistos.add(d)
            out.append(d)
    return out


def main():
    global CANON
    CANON = cargar_codigos()
    os.makedirs(DATOS, exist_ok=True)
    textos, corpus = exportar_corpus()
    dic = exportar_diccionario()

    # frecuencia de cada palabra del diccionario en el corpus (para ordenar resultados)
    ngr = Counter()
    for c in corpus:
        s = c[2].split()
        for n in range(1, 9):
            for i in range(len(s) - n + 1):
                ngr[" ".join(s[i:i + n])] += 1

    cab = "# Generado por OpenGlyph/exportar.py a partir de OpenGlyph (LGPL, J. A. García Sánchez).\n"
    with open(os.path.join(DATOS, "openglyph_diccionario.tsv"), "w", encoding="utf-8") as f:
        f.write(cab + "# signos\ttransliteración (MdC)\ttraducción\ttipo\tfrecuencia en el corpus\n")
        for d in dic:
            f.write("%s\t%s\t%s\t%s\t%d\n" % (d + (ngr.get(d[0], 0),)))
    with open(os.path.join(DATOS, "mdc_fonetico.tsv"), "w", encoding="utf-8") as f:
        f.write(cab + "# código fonético MdC\tcódigo Gardiner (tabla de JSesh y unilíteros)\n")
        for k in sorted(CANON):
            g = gardiner(CANON[k]) or CANON[k]
            if not gardiner(k) and g and GARDINER.match(g):
                f.write("%s\t%s\n" % (k, g))
    with open(os.path.join(DATOS, "openglyph_textos.tsv"), "w", encoding="utf-8") as f:
        f.write(cab + "# id\tarchivo\tnombre\tcomentario\n")
        for t in textos:
            f.write("%d\t%s\t%s\t%s\n" % t)
    with open(os.path.join(DATOS, "openglyph_corpus.tsv"), "w", encoding="utf-8") as f:
        f.write(cab + "# texto\trenglón\tsignos\tMdC\ttransliteración\ttraducción\n")
        for c in corpus:
            f.write("%d\t%d\t%s\t%s\t%s\t%s\n" % c)

    nsig = sum(len(c[2].split()) for c in corpus)
    print("Diccionario: %d entradas (%d con aparición en el corpus)"
          % (len(dic), sum(1 for d in dic if ngr.get(d[0]))), file=sys.stderr)
    print("Corpus: %d textos, %d renglones, %d signos (%d con transliteración, %d con traducción)"
          % (len(textos), len(corpus), nsig, sum(1 for c in corpus if c[4]),
             sum(1 for c in corpus if c[5])), file=sys.stderr)
    if desconocidos:
        top = ", ".join("%s×%d" % kv for kv in desconocidos.most_common(25))
        print("Códigos MdC sin traducir (%d distintos, %d apariciones): %s"
              % (len(desconocidos), sum(desconocidos.values()), top), file=sys.stderr)


if __name__ == "__main__":
    main()
