/*
 * hieroocr - OCR de jeroglíficos egipcios con salida en Manuel de Codage (MdC)
 * Cabecera común.
 */
#ifndef HIEROOCR_H
#define HIEROOCR_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir((p), 0755)
#endif

#define NORM      40   /* lado de la rejilla normalizada para comparar signos */
#define CODE_LEN  16   /* longitud máxima de un código Gardiner (p. ej. "Aa13", "N35A") */
#define NCAND      5   /* candidatos que se guardan por signo */

typedef struct { char *s; size_t n, cap; } Str;

/* Imagen en escala de grises (0 = negro, 255 = blanco). */
typedef struct { int w, h; uint8_t *px; } Gray;
/* Imagen binaria (1 = tinta). */
typedef struct { int w, h; uint8_t *px; } Bin;
/* Rectángulo con límites inclusivos. */
typedef struct { int x0, y0, x1, y1; } Rect;
/* Banda horizontal (un renglón de texto). */
typedef struct { int y0, y1; } Band;
/* Subrayado detectado (en Moldenke y otras ediciones marca la tinta roja). */
typedef struct { int line, x0, x1, y0, y1; } Underline;

typedef struct {
    Rect  r;
    int   line;                 /* renglón al que pertenece */
    int   alive;                /* 0 si el usuario lo ha borrado o fusionado */
    int   manual;               /* 1 si el código lo ha puesto el usuario */
    int   underlined;
    int   ncand;
    int   cand[NCAND];          /* índices de plantilla, de mejor a peor */
    float cand_score[NCAND];
    float score;                /* puntuación del mejor (menor = mejor) */
    char  code[CODE_LEN];       /* código Gardiner asignado */
} Glyph;

typedef struct {
    char      path[1024];       /* imagen de la página */
    int       pageno;           /* número de página en el documento original */
    int       processed;
    int       photo;            /* fotografía: corregir la iluminación antes de binarizar */
    Gray      gray;
    Bin       bin;
    int      *label;            /* componente conexa de cada píxel, -1 si no hay tinta */
    int       ncomp;
    int      *comp_glyph;       /* componente -> signo (-1 si se descartó) */
    Band     *lines;
    int       nlines;
    float     line_h;           /* altura típica de renglón en píxeles */
    Underline *ul;
    int       nul, capul;
    Glyph    *g;
    int       ng, capg;
} Page;

typedef struct {
    char     code[CODE_LEN];
    uint32_t cp;                /* punto de código Unicode (0 si no se conoce) */
    int      user;              /* 1 si procede de una plantilla aprendida */
    int      common;            /* 1 si es un signo frecuente o con valor fonético */
    int      variant;           /* variante de entrenamiento (0 = dibujo original) */
    float    aspect;            /* ancho / alto */
    float    relh;              /* altura relativa al cuadrado (<=0 si se desconoce) */
    uint8_t  dt[NORM * NORM];   /* transformada de distancia x4, saturada en 255 */
    int16_t  blur[NORM * NORM]; /* imagen suavizada de norma 1, x32767 (correlación) */
    float    coarse[64];        /* resumen 8x8 para el filtro rápido */
    uint16_t *ink;              /* posiciones con tinta */
    int      nink;
} Template;

typedef struct { Template *t; int n, cap; int view; } TemplateSet;   /* view: no posee las tintas */

/* Ficha de un signo de la lista de Gardiner (datos/gardiner.tsv). */
typedef struct {
    char     code[CODE_LEN];
    uint32_t cp;
    char     section[4];
    char     phon[96];          /* valores fonéticos */
    char     desc[96];          /* descripción */
} SignInfo;

typedef struct {
    int         dpi;            /* resolución al rasterizar PDF */
    const char *pages;          /* rango de páginas del PDF, p. ej. "71-92" (NULL = todas) */
    int         join;           /* radio de unión de componentes (0 = automático) */
    int         min_area;       /* área mínima de un signo (0 = automática) */
    int         rtl;            /* lectura de derecha a izquierda */
    int         mark_uncertain; /* añadir ++?+s tras los signos dudosos */
    int         no_font;        /* no usar plantillas generadas desde la fuente */
    int         gui;
    int         all_signs;      /* no favorecer los signos frecuentes */
    int         flatten;        /* corregir la iluminación desigual (fotos de páginas) */
    float       uncertain_thr;
    const char *font;
    const char *unicode;
    const char *tmpl_dir;
    const char *output;
    const char *screenshot;     /* sólo para pruebas: guarda un fotograma de la GUI y sale */
    const char *model;          /* modelo entrenado (.hmod) */
    const char *list;           /* lista de signos de Gardiner (gardiner.tsv) */
    int         train;          /* --entrenar */
    int         evaluate;       /* --evaluar */
    const char *dataset;        /* carpeta de fotos por código Gardiner (-D) */
    int         no_lm;          /* no usar el corpus para reordenar candidatos */
    int         reading;        /* --lectura: añadir la lectura de cada renglón al MdC */
    const char *data_dir;       /* carpeta con el diccionario y el corpus de OpenGlyph */
} Options;

/* Documento abierto: sus páginas y, si viene de un PDF, la carpeta temporal. */
typedef struct {
    Page *pages;
    int   np;
    char  tmpdir[512];
} Doc;

/* Petición que llega a la interfaz desde fuera (selector de archivos, cámara...). */
enum { REQ_DOCUMENT, REQ_PHOTO, REQ_MODEL, REQ_STATUS };
typedef struct {
    int    kind;
    int    n;
    char **files;               /* imágenes ya preparadas, una por página */
    char   msg[256];
} Request;

/* image.c */
int  gray_load(const char *path, Gray *g);
void gray_free(Gray *g);
void binarize_otsu(const Gray *g, Bin *b);
void gray_flatten(Gray *g);
int  pdf_render(const char *pdf, int dpi, const char *pages, char tmpdir[512], char ***files, int *nfiles);
int  parse_page_ranges(const char *spec, int (*ranges)[2], int max);
int  page_number_from_path(const char *path);
void tmpdir_remove(const char *dir);

/* segment.c */
int  page_segment(Page *p, const Options *o);
void page_free(Page *p);
int  glyph_mask(const Page *p, int gi, uint8_t **mask, int *w, int *h);
int  glyph_merge_next(Page *p, int gi);

/* classify.c */
void     ts_init(TemplateSet *ts);
void     ts_free(TemplateSet *ts);
int      ts_load_font(TemplateSet *ts, const char *font, const char *unicode);
int      ts_load_user(TemplateSet *ts, const char *dir);
int      ts_learn(TemplateSet *ts, const char *dir, const char *code,
                  const uint8_t *mask, int w, int h, float relh);
uint32_t ts_codepoint(const TemplateSet *ts, const char *code);
void     classify_glyph(Page *p, int gi, const TemplateSet *ts);
void     classify_set_options(const Options *o);
void     classify_page(Page *p, const TemplateSet *ts);
void     classify_mask(const uint8_t *mask, int w, int h, float relh,
                       const TemplateSet *ts, Glyph *g);
uint8_t *crop_mask(const uint8_t *m, int w, int h, int *cw, int *ch);
void     normalize_mask(const uint8_t *src, int w, int h, uint8_t *dst);
void     template_from_norm(Template *t, const uint8_t *norm0, float aspect);
Template *ts_add(TemplateSet *ts);
uint8_t *render_glyph(void *ttf_font, uint32_t cp, int *w, int *h);
int      is_common_code(const char *code);

/* Lista de signos y su información (classify.c) */
int             signs_load(const char *path);
int             signs_count(void);
const SignInfo *sign_at(int i);
const SignInfo *sign_find(const char *code);

/* dataset.c: conjuntos de fotos organizados por carpetas de código Gardiner */
typedef void (*DatasetCallback)(const char *code, int index, const uint8_t *mask,
                                int w, int h, void *ud);
uint8_t *photo_to_mask(const Gray *g, int *w, int *h);
int      code_from_folder(const char *name, char *code);
int      dataset_walk(const char *dir, DatasetCallback cb, void *ud);

/* train.c: entrenamiento con variantes y evaluación */
int ts_load_model(TemplateSet *ts, const char *path);
int train_model(const Options *o);
int evaluate_model(const Options *o, TemplateSet *ts);
int evaluate_photos(const Options *o, const TemplateSet *model);
int      valid_code(const char *s);

/* dict.c: diccionario y corpus de OpenGlyph */
typedef struct {
    int  *s, n;                 /* secuencia de signos (ids) */
    char *tr, *tr_u, *trad;     /* transliteración MdC, en Unicode, traducción */
    int   freq, is_sign;        /* apariciones en el corpus; 1 = ficha de signo */
} DictEntry;
typedef struct {
    int   text, line;
    int  *s, n;
    char *mdc, *tr, *trad;
} CorpusLine;
typedef struct { int kind, idx, match, pos; } DictHit;   /* kind 0 = diccionario, 1 = corpus */
typedef struct { int start, len, entry; } DictWord;

int  dict_load(const char *dir, const char *exclude);
int  dict_loaded(void);
int  dict_count(void);
int  corpus_count(void);
const DictEntry  *dict_entry(int i);
const CorpusLine *corpus_line(int i);
const char *corpus_text_name(int t);
const char *sign_code(int id);
int  sign_id_find(const char *code);
void translit_unicode(const char *mdc, char *out, size_t n);
int  dict_parse_query(const char *q, int *ids, int max);
int  dict_search_signs(const int *q, int m, DictHit *hits, int max, int *ntotal);
int  dict_search_text(const char *q, DictHit *hits, int max, int *ntotal);
int  dict_search(const char *q, DictHit *hits, int max, int *ntotal);
int  dict_read(const int *s, int n, DictWord *w, int maxw);
int  corpus_best_line(const int *s, int n, float *ratio);
int  lm_ready(void);
int  lm_rerank_page(Page *p, const Options *o, const TemplateSet *ts);
int  line_sign_ids(const Page *p, int line, const Options *o, int *ids, int *glyphs, int max);
void line_reading(const Page *p, int line, const Options *o, Str *out);

/* mdc.c */
int  line_glyph_order(const Page *p, int line, const Options *o, int *out);
void str_puts(Str *s, const char *t);
void line_to_mdc(const Page *p, int line, const Options *o, Str *out);
void page_to_mdc(const Page *p, const Options *o, Str *out, int last_page);

/* gui.c */
int      gui_run(Doc *doc, Options *o, TemplateSet *ts);
uint32_t gui_event_type(void);          /* evento SDL por el que llegan las peticiones */
void     request_free(Request *r);

/* app.c: partes comunes a escritorio y Android */
void app_find_data(Options *o);
int  app_load_templates(Options *o, TemplateSet *ts, int model_from_search);
int  process_page(Page *p, const Options *o, const TemplateSet *ts);
int  doc_from_files(Doc *d, char **files, int n, int photo, int numbered);
int  doc_open(Doc *d, const char *input, const Options *o);
void doc_free(Doc *d);
int  doc_to_mdc(Doc *d, const Options *o, const TemplateSet *ts, Str *out);

/* Servicios de la plataforma (main.c en escritorio, android.c en Android,
 * apple.m en macOS e iOS) */
int  platform_is_mobile(void);          /* 1: pantalla táctil, selector de archivos y cámara */
int  platform_can_open(void);           /* 1: hay selector de archivos (botón «Abrir») */
void platform_insets(int *top, int *bottom);   /* zona segura (muesca, barra de inicio), en puntos */
void platform_pick_file(void);
void platform_take_photo(void);
void platform_share(const char *path, const char *text);
const char *platform_mdc_path(const Options *o);

#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#define HIERO_IOS 1
#endif
/* apple.m: PDF con CoreGraphics (macOS e iOS) y arranque en macOS */
int  apple_pdf_render(const char *pdf, int dpi, int (*ranges)[2], int nr, const char *tmpdir);
void apple_startup(void);
#endif
#ifndef HIERO_IOS
#define HIERO_IOS 0
#endif

#endif
