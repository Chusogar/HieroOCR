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

#ifndef __ANDROID__   /* en Android el programa empieza en android.c */

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
        "  -B           corrige la iluminación desigual (fotos de páginas hechas con\n"
        "               el móvil, sombras)\n"
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

/* Servicios de la plataforma en escritorio: no hay selector propio (se puede
 * soltar un archivo en la ventana) y el MdC se guarda en el fichero de -o. */
int platform_is_mobile(void) { return getenv("HIEROOCR_SIMULAR_MOVIL") != NULL; }
void platform_pick_file(void) { fprintf(stderr, "Arrastra una imagen o un PDF a la ventana.\n"); }
void platform_take_photo(void) { fprintf(stderr, "No hay cámara en la versión de escritorio.\n"); }
void platform_share(const char *path, const char *text) { (void)path; (void)text; }
const char *platform_mdc_path(const Options *o) { return o->output ? o->output : "salida.mdc"; }

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
    while ((c = getopt_long(argc, argv, "o:gr:p:f:U:t:FAj:a:RBuT:m:L:D:h", longopts, NULL)) != -1) {
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
        case 'B': o.flatten = 1; break;
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
    if (optind >= argc && !o.train && !o.evaluate && !o.gui) {
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

    app_find_data(&o);

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
    int have_model = app_load_templates(&o, &ts, 0);
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
    if (ts.n == 0) {
        fprintf(stderr, "No hay plantillas con las que comparar. Indica la fuente (-f) y UnicodeData.txt (-U)\n"
                        "o una carpeta de plantillas aprendidas (-t).\n");
        if (!o.gui)
            return 1;
    }

    Doc doc = {0};
    if (input && doc_open(&doc, input, &o) != 0)
        return 1;

    int rc = 0;
    if (o.gui) {
        rc = gui_run(&doc, &o, &ts);
    } else {
        FILE *out = stdout;
        if (o.output && !(out = fopen(o.output, "w"))) {
            perror(o.output);
            rc = 1;
        } else {
            for (int i = 0; i < doc.np; i++) {
                Page *p = &doc.pages[i];
                if (process_page(p, &o, &ts) != 0) {
                    rc = 1;
                    continue;
                }
                Str s = {0};
                if (doc.np > 1) {
                    char hdr[64];
                    snprintf(hdr, sizeof hdr, "++página %d+s-!\n", p->pageno);
                    str_puts(&s, hdr);
                }
                page_to_mdc(p, &o, &s, i == doc.np - 1);
                if (s.s)
                    fputs(s.s, out);
                free(s.s);
                page_free(p);   /* libera memoria página a página */
            }
            if (out != stdout)
                fclose(out);
        }
    }
    doc_free(&doc);
    ts_free(&ts);
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
    return rc;
}

#endif /* __ANDROID__ */
