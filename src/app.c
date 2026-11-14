/*
 * Partes comunes a la versión de escritorio y a la de Android: localizar los
 * datos, cargar el modelo y las plantillas, abrir documentos y procesar páginas.
 */
#include "hieroocr.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

static const char *font_paths[] = {
    "datos/NotoSansEgyptianHieroglyphs-Regular.ttf",
    "NotoSansEgyptianHieroglyphs-Regular.ttf",
    "/usr/share/fonts/truetype/noto/NotoSansEgyptianHieroglyphs-Regular.ttf",
    "/usr/share/fonts/noto/NotoSansEgyptianHieroglyphs-Regular.ttf",
    "/usr/share/fonts/google-noto/NotoSansEgyptianHieroglyphs-Regular.ttf",
    "/usr/local/share/fonts/NotoSansEgyptianHieroglyphs-Regular.ttf",
    "/system/fonts/NotoSansEgyptianHieroglyphs-Regular.ttf",   /* Android */
    NULL
};

static const char *unicode_paths[] = {
    "/usr/share/unicode/UnicodeData.txt",
    "/usr/share/unicode-data/UnicodeData.txt",
    "/usr/share/unidata/UnicodeData.txt",
    "UnicodeData.txt",
    "datos/UnicodeData.txt",
    NULL
};

static const char *model_paths[] = {
    "datos/modelo_fotos.hmod", "modelo_fotos.hmod",   /* fuente + fotografías, si se ha entrenado */
    "datos/modelo.hmod", "modelo.hmod", "/usr/local/share/hieroocr/modelo.hmod",
    "/usr/share/hieroocr/modelo.hmod", NULL
};

static const char *list_paths[] = {
    "datos/gardiner.tsv", "gardiner.tsv", "/usr/local/share/hieroocr/gardiner.tsv",
    "/usr/share/hieroocr/gardiner.tsv", NULL
};

static const char *first_existing(const char **list)
{
    for (; *list; list++)
        if (access(*list, R_OK) == 0)
            return *list;
    return NULL;
}

void app_find_data(Options *o)
{
    if (!o->font)
        o->font = first_existing(font_paths);
    if (!o->unicode)
        o->unicode = first_existing(unicode_paths);
    if (!o->data_dir) {
        static const char *dirs[] = {"datos", ".", "/usr/local/share/hieroocr", "/usr/share/hieroocr", NULL};
        for (const char **d = dirs; *d; d++) {
            char p[600];
            snprintf(p, sizeof p, "%s/openglyph_diccionario.tsv", *d);
            if (access(p, R_OK) == 0) {
                o->data_dir = *d;
                break;
            }
        }
    }
    if (o->data_dir && dict_load(o->data_dir, getenv("HIEROOCR_CORPUS_EXCLUIR")) > 0)
        fprintf(stderr, "OpenGlyph: %d entradas de diccionario, %d renglones de corpus\n",
                dict_count(), corpus_count());
    if (!o->list)
        o->list = first_existing(list_paths);
    if (o->list) {
        int ns = signs_load(o->list);
        if (ns > 0)
            fprintf(stderr, "Lista de Gardiner %s: %d signos\n", o->list, ns);
        else
            fprintf(stderr, "Aviso: no se puede leer la lista %s\n", o->list);
    }
}

/* Modelo entrenado (o, si no hay, la fuente) + plantillas aprendidas. */
int app_load_templates(Options *o, TemplateSet *ts, int model_from_search)
{
    ts_init(ts);
    if (!o->model || model_from_search)
        o->model = first_existing(model_paths);
    int have_model = 0;
    if (!o->no_font && o->model)
        have_model = ts_load_model(ts, o->model) > 0;
    if (!o->no_font && !have_model) {
        if (!o->font || !o->unicode)
            fprintf(stderr, "Aviso: falta la fuente jeroglífica o UnicodeData.txt; usa -f y -U.\n");
        else
            ts_load_font(ts, o->font, o->unicode);
    }
    ts_load_user(ts, o->tmpl_dir);
    return have_model;
}

int process_page(Page *p, const Options *o, const TemplateSet *ts)
{
    if (p->processed)
        return 0;
    if (gray_load(p->path, &p->gray))
        return -1;
    if (p->photo || o->flatten)
        gray_flatten(&p->gray);
    page_segment(p, o);
    classify_page(p, ts);
    if (!o->no_lm && lm_ready()) {
        int ch = lm_rerank_page(p, o, ts);
        if (ch)
            fprintf(stderr, "Página %d: el corpus ha cambiado %d signos\n", p->pageno, ch);
    }
    p->processed = 1;
    fprintf(stderr, "Página %d: %d renglones, %d signos\n", p->pageno, p->nlines, p->ng);
    return 0;
}

/* Documento hecho de imágenes ya preparadas (una por página). */
int doc_from_files(Doc *d, char **files, int n, int photo, int numbered)
{
    memset(d, 0, sizeof *d);
    if (n <= 0)
        return -1;
    d->pages = calloc((size_t)n, sizeof(Page));
    if (!d->pages)
        return -1;
    d->np = n;
    for (int i = 0; i < n; i++) {
        snprintf(d->pages[i].path, sizeof d->pages[i].path, "%s", files[i]);
        d->pages[i].photo = photo;
        d->pages[i].pageno = numbered ? page_number_from_path(files[i]) : 0;
        if (d->pages[i].pageno <= 0)
            d->pages[i].pageno = i + 1;
    }
    return 0;
}

/* Abre un PDF (rasterizándolo con pdftoppm) o una imagen. */
int doc_open(Doc *d, const char *input, const Options *o)
{
    memset(d, 0, sizeof *d);
    const char *dot = strrchr(input, '.');
    if (dot && strcasecmp(dot, ".pdf") == 0) {
        char tmpdir[512] = "";
        char **files = NULL;
        int n = 0;
        if (pdf_render(input, o->dpi, o->pages, tmpdir, &files, &n) != 0)
            return -1;
        int rc = doc_from_files(d, files, n, 0, 1);   /* "p-071.png" -> página 71 */
        for (int i = 0; i < n; i++)
            free(files[i]);
        free(files);
        snprintf(d->tmpdir, sizeof d->tmpdir, "%s", tmpdir);
        return rc;
    }
    char *one = (char *)input;
    return doc_from_files(d, &one, 1, o->flatten, 0);
}

void doc_free(Doc *d)
{
    for (int i = 0; i < d->np; i++)
        page_free(&d->pages[i]);
    free(d->pages);
    if (d->tmpdir[0])
        tmpdir_remove(d->tmpdir);
    memset(d, 0, sizeof *d);
}

/* MdC de todo el documento (procesa las páginas que falten). */
int doc_to_mdc(Doc *d, const Options *o, const TemplateSet *ts, Str *out)
{
    int ok = 0;
    for (int i = 0; i < d->np; i++) {
        if (process_page(&d->pages[i], o, ts) != 0)
            continue;
        if (d->np > 1) {
            char hdr[64];
            snprintf(hdr, sizeof hdr, "++página %d+s-!\n", d->pages[i].pageno);
            str_puts(out, hdr);
        }
        page_to_mdc(&d->pages[i], o, out, i == d->np - 1);
        ok++;
    }
    return ok;
}
