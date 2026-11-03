/*
 * hieroocr - OCR de jeroglíficos egipcios con salida en Manuel de Codage (MdC)
 * Cabecera común.
 */
#ifndef HIEROOCR_H
#define HIEROOCR_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#define NORM      40   /* lado de la rejilla normalizada para comparar signos */
#define CODE_LEN  16   /* longitud máxima de un código Gardiner (p. ej. "Aa13", "N35A") */
#define NCAND      5   /* candidatos que se guardan por signo */

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
    int       processed;
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
    int      common;            /* 1 si es un signo frecuente (lista de Gardiner usual) */
    float    aspect;            /* ancho / alto */
    float    relh;              /* altura relativa al cuadrado (<=0 si se desconoce) */
    float    dt[NORM * NORM];   /* transformada de distancia */
    float    blur[NORM * NORM]; /* imagen suavizada normalizada (correlación) */
    uint16_t ink[NORM * NORM];  /* posiciones con tinta */
    int      nink;
} Template;

typedef struct { Template *t; int n, cap; } TemplateSet;

typedef struct {
    int         dpi;            /* resolución al rasterizar PDF */
    int         page;           /* página concreta del PDF (0 = todas) */
    int         join;           /* radio de unión de componentes (0 = automático) */
    int         min_area;       /* área mínima de un signo (0 = automática) */
    int         rtl;            /* lectura de derecha a izquierda */
    int         mark_uncertain; /* añadir ++?+s tras los signos dudosos */
    int         no_font;        /* no usar plantillas generadas desde la fuente */
    int         gui;
    int         all_signs;      /* no favorecer los signos frecuentes */
    float       uncertain_thr;
    const char *font;
    const char *unicode;
    const char *tmpl_dir;
    const char *output;
    const char *screenshot;     /* sólo para pruebas: guarda un fotograma de la GUI y sale */
} Options;

typedef struct { char *s; size_t n, cap; } Str;

/* image.c */
int  gray_load(const char *path, Gray *g);
void gray_free(Gray *g);
void binarize_otsu(const Gray *g, Bin *b);
int  pdf_render(const char *pdf, int dpi, int page, char tmpdir[512], char ***files, int *nfiles);
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
int      valid_code(const char *s);

/* mdc.c */
void str_puts(Str *s, const char *t);
void line_to_mdc(const Page *p, int line, const Options *o, Str *out);
void page_to_mdc(const Page *p, const Options *o, Str *out, int last_page);

/* gui.c */
int gui_run(Page *pages, int npages, const Options *o, TemplateSet *ts);

/* main.c */
int process_page(Page *p, const Options *o, const TemplateSet *ts);

#endif
