/*
 * hieroocr - OCR de jeroglíficos egipcios (PDF o imagen) con salida en MdC.
 */
#define SDL_MAIN_HANDLED
#include "hieroocr.h"

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

static const char *font_paths[] = {
    "/usr/share/fonts/truetype/noto/NotoSansEgyptianHieroglyphs-Regular.ttf",
    "/usr/share/fonts/noto/NotoSansEgyptianHieroglyphs-Regular.ttf",
    "/usr/share/fonts/google-noto/NotoSansEgyptianHieroglyphs-Regular.ttf",
    "/usr/local/share/fonts/NotoSansEgyptianHieroglyphs-Regular.ttf",
    "NotoSansEgyptianHieroglyphs-Regular.ttf",
    "datos/NotoSansEgyptianHieroglyphs-Regular.ttf",
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

static const char *first_existing(const char **list)
{
    for (; *list; list++)
        if (access(*list, R_OK) == 0)
            return *list;
    return NULL;
}

static const char *model_paths[] = {
    "datos/modelo_fotos.hmod", "modelo_fotos.hmod",   /* fuente + fotografías, si se ha entrenado */
    "datos/modelo.hmod", "modelo.hmod", "/usr/local/share/hieroocr/modelo.hmod",
    "/usr/share/hieroocr/modelo.hmod", NULL
};

static const char *list_paths[] = {
    "datos/gardiner.tsv", "gardiner.tsv", "/usr/local/share/hieroocr/gardiner.tsv",
    "/usr/share/hieroocr/gardiner.tsv", NULL
};

static void usage(const char *prog)
{
    fprintf(stderr,
        "Uso: %s [opciones] entrada.(pdf|jpg|png|...)\n"
        "     %s --entrenar [-D carpeta_fotos] [-L lista.tsv] [-f fuente.ttf] [-m modelo.hmod]\n"
        "     %s --evaluar  [-m modelo.hmod]\n"
        "\n"
        "  -o FICHERO   escribe el MdC en FICHERO (por defecto, salida estándar;\n"
        "               en la interfaz gráfica, salida.mdc)\n"
        "  -g           abre la interfaz SDL2 para revisar, corregir y enseñar signos\n"
        "  -r PPP       resolución de rasterizado del PDF (por defecto 300)\n"
        "  -p PÁGINAS   páginas del PDF que se procesan: 71, 71-92, 71- (hasta el\n"
        "               final), -10 (desde el principio) o listas: 3,5,7-9\n"
        "  -f FUENTE    fuente jeroglífica TTF (Noto Sans Egyptian Hieroglyphs)\n"
        "  -U FICHERO   UnicodeData.txt (para saber el código Gardiner de cada signo)\n"
        "  -t CARPETA   plantillas aprendidas (por defecto ./plantillas)\n"
        "  -F           no usar las plantillas de la fuente, sólo las aprendidas\n"
        "  -A           tratar por igual todos los signos de la fuente (sin favorecer\n"
        "               los frecuentes)\n"
        "  -j PX        radio de unión de trazos en píxeles (automático)\n"
        "  -a PX        área mínima de un signo en píxeles (automática)\n"
        "  -R           el texto se lee de derecha a izquierda\n"
        "  -u           marca los signos dudosos con ++?+s en el MdC\n"
        "  -T UMBRAL    puntuación a partir de la cual un signo es dudoso (0.11)\n"
        "  -m MODELO    modelo entrenado (por defecto datos/modelo_fotos.hmod si existe,\n"
        "               si no datos/modelo.hmod)\n"
        "  -L LISTA     lista de signos de Gardiner (por defecto datos/gardiner.tsv)\n"
        "  --entrenar   entrena el modelo con todos los signos de la lista y lo evalúa\n"
        "  -D CARPETA   añade al entrenamiento un conjunto de fotos organizado en\n"
        "               carpetas por código Gardiner (p. ej. EgyptianHieroglyphicText/dataset);\n"
        "               con --evaluar, mide el acierto con 1 de cada 5 fotos reservadas\n"
        "  --evaluar    mide el acierto del modelo con muestras sintéticas nuevas\n"
        "  -h           esta ayuda\n",
        prog, prog, prog);
}

int process_page(Page *p, const Options *o, const TemplateSet *ts)
{
    if (p->processed)
        return 0;
    if (gray_load(p->path, &p->gray))
        return -1;
    page_segment(p, o);
    classify_page(p, ts);
    p->processed = 1;
    fprintf(stderr, "Página %d: %d renglones, %d signos\n", p->pageno, p->nlines, p->ng);
    return 0;
}

int main(int argc, char **argv)
{
    Options o = {
        .dpi = 300, .uncertain_thr = 0.11f, .tmpl_dir = "plantillas",
    };
    static const struct option longopts[] = {
        {"captura", required_argument, NULL, 1000},
        {"entrenar", no_argument, NULL, 1001},
        {"evaluar", no_argument, NULL, 1002},
        {"ayuda", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };
    int c;
    while ((c = getopt_long(argc, argv, "o:gr:p:f:U:t:FAj:a:RuT:m:L:D:h", longopts, NULL)) != -1) {
        switch (c) {
        case 'o': o.output = optarg; break;
        case 'g': o.gui = 1; break;
        case 'r': o.dpi = atoi(optarg); break;
        case 'p': o.pages = optarg; break;
        case 'f': o.font = optarg; break;
        case 'U': o.unicode = optarg; break;
        case 't': o.tmpl_dir = optarg; break;
        case 'F': o.no_font = 1; break;
        case 'A': o.all_signs = 1; break;
        case 'j': o.join = atoi(optarg); break;
        case 'a': o.min_area = atoi(optarg); break;
        case 'R': o.rtl = 1; break;
        case 'u': o.mark_uncertain = 1; break;
        case 'T': o.uncertain_thr = (float)atof(optarg); break;
        case 1000: o.screenshot = optarg; o.gui = 1; break;
        case 1001: o.train = 1; break;
        case 1002: o.evaluate = 1; break;
        case 'm': o.model = optarg; break;
        case 'L': o.list = optarg; break;
        case 'D': o.dataset = optarg; break;
        case 'h': usage(argv[0]); return 0;
        default: usage(argv[0]); return 2;
        }
    }
    if (optind >= argc && !o.train && !o.evaluate) {
        usage(argv[0]);
        return 2;
    }
    if (o.dpi < 72 || o.dpi > 1200)
        o.dpi = 300;
    const char *input = optind < argc ? argv[optind] : NULL;

    SDL_SetMainReady();
    if (SDL_Init(o.gui ? SDL_INIT_VIDEO : 0) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG | IMG_INIT_TIF);
    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init: %s\n", TTF_GetError());
        return 1;
    }

    if (!o.font)
        o.font = first_existing(font_paths);
    if (!o.unicode)
        o.unicode = first_existing(unicode_paths);

    if (!o.list)
        o.list = first_existing(list_paths);
    if (o.list) {
        int ns = signs_load(o.list);
        if (ns > 0)
            fprintf(stderr, "Lista de Gardiner %s: %d signos\n", o.list, ns);
        else
            fprintf(stderr, "Aviso: no se puede leer la lista %s\n", o.list);
    }

    if (o.train) {
        int rc = train_model(&o);
        if (rc == 0) {
            if (!o.model)
                o.model = "datos/modelo.hmod";
            o.all_signs = 1;   /* la evaluación trata por igual todos los signos */
            classify_set_options(&o);
            TemplateSet tm;
            ts_init(&tm);
            if (ts_load_model(&tm, o.model) > 0) {
                rc = evaluate_model(&o, &tm);
                if (rc == 0 && o.dataset)
                    rc = evaluate_photos(&o, &tm);
            }
            ts_free(&tm);
        }
        TTF_Quit();
        SDL_Quit();
        return rc;
    }

    classify_set_options(&o);
    TemplateSet ts;
    ts_init(&ts);
    if (!o.model)
        o.model = first_existing(model_paths);
    int have_model = 0;
    if (!o.no_font && o.model)
        have_model = ts_load_model(&ts, o.model) > 0;
    if (o.evaluate) {
        if (!have_model) {
            fprintf(stderr, "No hay modelo que evaluar: ejecuta antes --entrenar.\n");
            return 1;
        }
        o.all_signs = 1;
        classify_set_options(&o);
        int rc = o.dataset ? evaluate_photos(&o, &ts) : evaluate_model(&o, &ts);
        ts_free(&ts);
        TTF_Quit();
        SDL_Quit();
        return rc;
    }
    if (!o.no_font && !have_model) {
        if (!o.font || !o.unicode)
            fprintf(stderr, "Aviso: falta la fuente jeroglífica o UnicodeData.txt; usa -f y -U.\n");
        else
            ts_load_font(&ts, o.font, o.unicode);
    }
    ts_load_user(&ts, o.tmpl_dir);
    if (ts.n == 0) {
        fprintf(stderr, "No hay plantillas con las que comparar. Indica la fuente (-f) y UnicodeData.txt (-U)\n"
                        "o una carpeta de plantillas aprendidas (-t).\n");
        if (!o.gui)
            return 1;
    }

    char tmpdir[512] = "";
    char **files = NULL;
    int nfiles = 0;
    const char *dot = strrchr(input, '.');
    if (dot && strcasecmp(dot, ".pdf") == 0) {
        if (pdf_render(input, o.dpi, o.pages, tmpdir, &files, &nfiles) != 0)
            return 1;
    } else {
        files = malloc(sizeof(char *));
        files[0] = strdup(input);
        nfiles = 1;
    }

    Page *pages = calloc((size_t)nfiles, sizeof(Page));
    for (int i = 0; i < nfiles; i++) {
        snprintf(pages[i].path, sizeof pages[i].path, "%s", files[i]);
        pages[i].pageno = tmpdir[0] ? page_number_from_path(files[i]) : 0;
        if (pages[i].pageno <= 0)
            pages[i].pageno = i + 1;
    }

    int rc = 0;
    if (o.gui) {
        rc = gui_run(pages, nfiles, &o, &ts);
        for (int i = 0; i < nfiles; i++)
            page_free(&pages[i]);
    } else {
        FILE *out = stdout;
        if (o.output && !(out = fopen(o.output, "w"))) {
            perror(o.output);
            rc = 1;
        } else {
            for (int i = 0; i < nfiles; i++) {
                if (process_page(&pages[i], &o, &ts) != 0) {
                    rc = 1;
                    continue;
                }
                Str s = {0};
                if (nfiles > 1) {
                    char hdr[64];
                    snprintf(hdr, sizeof hdr, "++página %d+s-!\n", pages[i].pageno);
                    str_puts(&s, hdr);
                }
                page_to_mdc(&pages[i], &o, &s, i == nfiles - 1);
                if (s.s)
                    fputs(s.s, out);
                free(s.s);
                page_free(&pages[i]);
            }
            if (out != stdout)
                fclose(out);
        }
    }

    if (tmpdir[0])
        tmpdir_remove(tmpdir);
    for (int i = 0; i < nfiles; i++)
        free(files[i]);
    free(files);
    free(pages);
    ts_free(&ts);
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
    return rc;
}
