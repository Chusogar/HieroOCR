/*
 * Entrenamiento del reconocedor a partir de la lista completa de signos de
 * Gardiner (datos/gardiner.tsv, construida a partir de Wikipedia y Unicode).
 *
 * Para cada signo se dibuja su glifo con la fuente jeroglífica y se generan
 * variantes que imitan cómo cambia un mismo signo de una edición impresa a otra:
 * contorno hueco (fino y grueso, como en las tipografías del XIX), trazo grueso
 * o fino, pequeñas rotaciones y cambios de proporción. Cada variante se guarda
 * normalizada (40x40, 1 bit por píxel) en un modelo .hmod, que se carga en
 * milisegundos y no necesita la fuente.
 *
 * La evaluación usa muestras nuevas, generadas a otra resolución y con
 * parámetros aleatorios distintos de los del entrenamiento, y compara el
 * modelo entrenado con la referencia de una sola plantilla por signo.
 */
#include "hieroocr.h"

#include <SDL.h>
#include <SDL_ttf.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MODEL_MAGIC  "HIEROMOD1"
#define TRAIN_SIZE   96      /* px de la fuente al entrenar */
#define EVAL_SIZE    56      /* px de la fuente al evaluar (otra resolución) */
#define NBITS        ((NORM * NORM + 7) / 8)
#define VARIANT_PHOTO 20     /* plantilla sacada de una fotografía del conjunto de datos */

static const char *variant_names[] = {
    "original", "contorno fino", "contorno grueso", "trazo grueso", "trazo fino",
    "girado +4°", "girado -4°", "ancho", "estrecho", "contorno girado +3°", "contorno girado -3°"
};
#define NVARIANTS ((int)(sizeof variant_names / sizeof variant_names[0]))

/* ---------- operaciones sobre máscaras (1 = tinta) ---------- */

static uint8_t *pad_mask(const uint8_t *m, int w, int h, int p, int *ow, int *oh)
{
    *ow = w + 2 * p;
    *oh = h + 2 * p;
    uint8_t *o = calloc((size_t)(*ow) * (*oh), 1);
    for (int y = 0; y < h; y++)
        memcpy(o + (size_t)(y + p) * (*ow) + p, m + (size_t)y * w, (size_t)w);
    return o;
}

/* Dilatación (grow=1) o erosión (grow=0) con un disco de radio r. */
static uint8_t *morph(const uint8_t *m, int w, int h, int r, int grow)
{
    uint8_t *o = malloc((size_t)w * h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int v = grow ? 0 : 1;
            for (int dy = -r; dy <= r && v == (grow ? 0 : 1); dy++)
                for (int dx = -r; dx <= r; dx++) {
                    if (dx * dx + dy * dy > r * r)
                        continue;
                    int yy = y + dy, xx = x + dx;
                    int in = (yy >= 0 && yy < h && xx >= 0 && xx < w) ? m[(size_t)yy * w + xx] : 0;
                    if (grow && in) { v = 1; break; }
                    if (!grow && !in) { v = 0; break; }
                }
            o[(size_t)y * w + x] = (uint8_t)v;
        }
    return o;
}

/* Contorno: píxeles de tinta con algún vecino vacío, engrosado a 'thick' píxeles. */
static uint8_t *outline(const uint8_t *m, int w, int h, int thick)
{
    uint8_t *er = morph(m, w, h, thick, 0);
    uint8_t *o = malloc((size_t)w * h);
    for (size_t i = 0; i < (size_t)w * h; i++)
        o[i] = m[i] && !er[i];
    free(er);
    return o;
}

static uint8_t *rotate(const uint8_t *m, int w, int h, float deg, int *ow, int *oh)
{
    float a = deg * (float)M_PI / 180.0f, c = cosf(a), s = sinf(a);
    *ow = (int)ceilf(fabsf(w * c) + fabsf(h * s)) + 2;
    *oh = (int)ceilf(fabsf(w * s) + fabsf(h * c)) + 2;
    uint8_t *o = calloc((size_t)(*ow) * (*oh), 1);
    float cx = w / 2.0f, cy = h / 2.0f, ox = *ow / 2.0f, oy = *oh / 2.0f;
    for (int y = 0; y < *oh; y++)
        for (int x = 0; x < *ow; x++) {
            float dx = x - ox, dy = y - oy;
            int sx = (int)lroundf(c * dx + s * dy + cx), sy = (int)lroundf(-s * dx + c * dy + cy);
            if (sx >= 0 && sx < w && sy >= 0 && sy < h)
                o[(size_t)y * (*ow) + x] = m[(size_t)sy * w + sx];
        }
    return o;
}

static uint8_t *scale_x(const uint8_t *m, int w, int h, float f, int *ow)
{
    *ow = (int)lroundf(w * f);
    if (*ow < 1)
        *ow = 1;
    uint8_t *o = malloc((size_t)(*ow) * h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < *ow; x++) {
            int sx = (int)(x / f);
            if (sx >= w) sx = w - 1;
            o[(size_t)y * (*ow) + x] = m[(size_t)y * w + sx];
        }
    return o;
}

static int count_ink(const uint8_t *m, size_t n)
{
    int c = 0;
    for (size_t i = 0; i < n; i++)
        c += m[i];
    return c;
}

/* Variante v de un glifo (máscara ya rellenada con margen). Devuelve máscara propia o NULL. */
static uint8_t *make_variant(const uint8_t *g, int w, int h, int v, int scale, int *ow, int *oh)
{
    int t1 = scale > 60 ? 2 : 1, t2 = scale > 60 ? 4 : 2;   /* grosores según el tamaño */
    *ow = w;
    *oh = h;
    switch (v) {
    case 0: { uint8_t *o = malloc((size_t)w * h); memcpy(o, g, (size_t)w * h); return o; }
    case 1: return outline(g, w, h, t1);
    case 2: return outline(g, w, h, t2);
    case 3: return morph(g, w, h, t1 + 1, 1);
    case 4: {
        uint8_t *o = morph(g, w, h, 1, 0);
        if (count_ink(o, (size_t)w * h) < 0.30 * count_ink(g, (size_t)w * h)) {
            free(o);
            return NULL;   /* el signo es demasiado fino para adelgazarlo */
        }
        return o;
    }
    case 5: return rotate(g, w, h, 4.0f, ow, oh);
    case 6: return rotate(g, w, h, -4.0f, ow, oh);
    case 7: return scale_x(g, w, h, 1.18f, ow);
    case 8: return scale_x(g, w, h, 0.85f, ow);
    case 9: case 10: {
        uint8_t *ol = outline(g, w, h, t1);
        uint8_t *o = rotate(ol, w, h, v == 9 ? 3.0f : -3.0f, ow, oh);
        free(ol);
        return o;
    }
    }
    return NULL;
}

/* ---------- formato del modelo ---------- */

typedef struct {
    char     code[CODE_LEN];
    uint32_t cp;
    uint8_t  common, variant, pad[2];
    float    aspect, relh;
    uint8_t  bits[NBITS];
} ModelRec;

static void pack_bits(const uint8_t *m, uint8_t *bits)
{
    memset(bits, 0, NBITS);
    for (int i = 0; i < NORM * NORM; i++)
        if (m[i])
            bits[i >> 3] |= (uint8_t)(1 << (i & 7));
}

static void unpack_bits(const uint8_t *bits, uint8_t *m)
{
    for (int i = 0; i < NORM * NORM; i++)
        m[i] = (bits[i >> 3] >> (i & 7)) & 1;
}

int ts_load_model(TemplateSet *ts, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    char magic[10] = {0};
    int32_t n = 0, nv = 0;
    float quad = 0;
    if (fread(magic, 1, 9, f) != 9 || strcmp(magic, MODEL_MAGIC) != 0 ||
        fread(&n, 4, 1, f) != 1 || fread(&nv, 4, 1, f) != 1 || fread(&quad, 4, 1, f) != 1 || n <= 0) {
        fprintf(stderr, "%s no es un modelo de hieroocr válido.\n", path);
        fclose(f);
        return -1;
    }
    int loaded = 0;
    ModelRec r;
    uint8_t m[NORM * NORM];
    for (int i = 0; i < n && fread(&r, sizeof r, 1, f) == 1; i++) {
        Template *t = ts_add(ts);
        unpack_bits(r.bits, m);
        template_from_norm(t, m, r.aspect);
        memcpy(t->code, r.code, CODE_LEN);
        t->code[CODE_LEN - 1] = 0;
        t->cp = r.cp;
        t->common = r.common || is_common_code(t->code);
        t->variant = r.variant;
        t->relh = r.relh;
        loaded++;
    }
    fclose(f);
    fprintf(stderr, "Modelo %s: %d plantillas (%d variantes por signo)\n", path, loaded, nv);
    return loaded;
}

/* ---------- entrenamiento ---------- */

static int cmp_int(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}

/* Altura de un "cuadrado" en píxeles para un tamaño de fuente: percentil 90 de las alturas. */
static float quadrat_height(TTF_Font *ft)
{
    int n = signs_count(), k = 0;
    int *hs = malloc(sizeof(int) * (size_t)(n ? n : 1));
    for (int i = 0; i < n; i++) {
        int w, h;
        uint8_t *g = render_glyph(ft, sign_at(i)->cp, &w, &h);
        if (g) {
            hs[k++] = h;
            free(g);
        }
    }
    qsort(hs, (size_t)k, sizeof(int), cmp_int);
    float q = k ? (float)hs[(k * 9) / 10] : 1.0f;
    free(hs);
    return q;
}

typedef struct {
    FILE *f;
    int32_t n;
    int photos, holdout;      /* holdout: 1 = omitir las fotos de prueba (índice % 5 == 4) */
    TemplateSet *ts;          /* si no es NULL, se añaden aquí en vez de escribirse */
} PhotoSink;

static void photo_record(const char *code, int index, const uint8_t *mask, int w, int h, void *ud)
{
    PhotoSink *ps = ud;
    if (ps->holdout && index % 5 == 4)
        return;
    uint8_t nm[NORM * NORM];
    normalize_mask(mask, w, h, nm);
    const SignInfo *si = sign_find(code);
    if (ps->ts) {
        Template *t = ts_add(ps->ts);
        template_from_norm(t, nm, (float)w / (float)h);
        snprintf(t->code, CODE_LEN, "%s", code);
        t->cp = si ? si->cp : 0;
        t->common = 1;
        t->variant = VARIANT_PHOTO;
        t->relh = -1.0f;
    } else {
        ModelRec r;
        memset(&r, 0, sizeof r);
        snprintf(r.code, CODE_LEN, "%s", code);
        r.cp = si ? si->cp : 0;
        r.common = 1;
        r.variant = VARIANT_PHOTO;
        r.aspect = (float)w / (float)h;
        r.relh = -1.0f;   /* las fotos no dicen nada del tamaño relativo */
        pack_bits(nm, r.bits);
        fwrite(&r, sizeof r, 1, ps->f);
        ps->n++;
    }
    ps->photos++;
}

int train_model(const Options *o)
{
    if (signs_count() == 0) {
        fprintf(stderr, "Falta la lista de signos (datos/gardiner.tsv; opción -L).\n");
        return 1;
    }
    if (!o->font) {
        fprintf(stderr, "Falta la fuente jeroglífica (opción -f).\n");
        return 1;
    }
    TTF_Font *ft = TTF_OpenFont(o->font, TRAIN_SIZE);
    if (!ft) {
        fprintf(stderr, "No se puede abrir %s: %s\n", o->font, TTF_GetError());
        return 1;
    }
    const char *out = o->model ? o->model : "datos/modelo.hmod";
    char tmp[1100];
    snprintf(tmp, sizeof tmp, "%s.tmp", out);
    FILE *f = fopen(tmp, "wb");
    if (!f) {
        fprintf(stderr, "No se puede escribir %s: %s\n", tmp, strerror(errno));
        TTF_CloseFont(ft);
        return 1;
    }

    float quad = quadrat_height(ft);
    int32_t n = 0, nv = NVARIANTS;
    fwrite(MODEL_MAGIC, 1, 9, f);
    fwrite(&n, 4, 1, f);          /* se reescribe al final */
    fwrite(&nv, 4, 1, f);
    fwrite(&quad, 4, 1, f);

    int nsigns = 0, missing = 0, per_variant[NVARIANTS] = {0};
    fprintf(stderr, "Entrenando con %d signos de la lista y %d variantes por signo...\n",
            signs_count(), NVARIANTS);
    for (int i = 0; i < signs_count(); i++) {
        const SignInfo *si = sign_at(i);
        int w, h;
        uint8_t *g0 = render_glyph(ft, si->cp, &w, &h);
        if (!g0) {
            missing++;
            fprintf(stderr, "  %s (U+%05X): la fuente no tiene este signo\n", si->code, si->cp);
            continue;
        }
        int pw, ph;
        uint8_t *g = pad_mask(g0, w, h, 6, &pw, &ph);
        free(g0);
        float relh = h / quad;
        for (int v = 0; v < NVARIANTS; v++) {
            int vw, vh;
            uint8_t *vm = make_variant(g, pw, ph, v, TRAIN_SIZE, &vw, &vh);
            if (!vm)
                continue;
            int cw, ch;
            uint8_t *c = crop_mask(vm, vw, vh, &cw, &ch);
            free(vm);
            if (!c)
                continue;
            ModelRec r;
            memset(&r, 0, sizeof r);
            uint8_t nm[NORM * NORM];
            normalize_mask(c, cw, ch, nm);
            free(c);
            snprintf(r.code, CODE_LEN, "%s", si->code);
            r.cp = si->cp;
            r.common = (uint8_t)is_common_code(si->code);
            r.variant = (uint8_t)v;
            r.aspect = (float)cw / (float)ch;
            r.relh = relh;
            pack_bits(nm, r.bits);
            fwrite(&r, sizeof r, 1, f);
            n++;
            per_variant[v]++;
        }
        free(g);
        nsigns++;
        if (nsigns % 100 == 0)
            fprintf(stderr, "  %d signos...\n", nsigns);
    }
    int nphotos = 0;
    if (o->dataset) {
        fprintf(stderr, "Añadiendo las fotografías de %s (contornos)...\n", o->dataset);
        PhotoSink ps = {f, n, 0, 0, NULL};
        dataset_walk(o->dataset, photo_record, &ps);
        n = ps.n;
        nphotos = ps.photos;
    }
    fseek(f, 9, SEEK_SET);
    fwrite(&n, 4, 1, f);
    fclose(f);
    TTF_CloseFont(ft);
    if (rename(tmp, out) != 0) {
        fprintf(stderr, "No se puede crear %s: %s\n", out, strerror(errno));
        return 1;
    }
    fprintf(stderr, "Modelo guardado en %s: %d signos, %d plantillas", out, nsigns, n);
    if (missing)
        fprintf(stderr, " (%d signos de la lista no están en la fuente)", missing);
    fprintf(stderr, ".\n");
    for (int v = 0; v < NVARIANTS; v++)
        fprintf(stderr, "  variante %-22s %d\n", variant_names[v], per_variant[v]);
    if (nphotos)
        fprintf(stderr, "  fotografías (contornos)         %d\n", nphotos);
    return 0;
}

/* ---------- evaluación ---------- */

static uint32_t rng_state;
static float frand(void)   /* xorshift, reproducible */
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return (rng_state & 0xFFFFFF) / (float)0x1000000;
}

/* Muestra de prueba: otro tamaño de fuente, estilo, giro y proporción aleatorios y bordes ruidosos. */
static uint8_t *test_sample(TTF_Font *ft, uint32_t cp, int *ow, int *oh)
{
    int w, h;
    uint8_t *g0 = render_glyph(ft, cp, &w, &h);
    if (!g0)
        return NULL;
    int pw, ph;
    uint8_t *g = pad_mask(g0, w, h, 5, &pw, &ph);
    free(g0);

    int style = (int)(frand() * 4);   /* 0 relleno, 1 contorno, 2 contorno grueso, 3 negrita */
    uint8_t *s = style == 1 ? outline(g, pw, ph, 1)
               : style == 2 ? outline(g, pw, ph, 2)
               : style == 3 ? morph(g, pw, ph, 1, 1) : NULL;
    if (s) { free(g); g = s; }

    int rw, rh;
    uint8_t *r = rotate(g, pw, ph, (frand() - 0.5f) * 10.0f, &rw, &rh);   /* ±5° */
    free(g);
    int sw;
    uint8_t *sc = scale_x(r, rw, rh, 0.85f + frand() * 0.30f, &sw);       /* 0,85-1,15 */
    free(r);

    /* ruido en los bordes: invierte un 12 % de los píxeles de frontera */
    for (int y = 1; y < rh - 1; y++)
        for (int x = 1; x < sw - 1; x++) {
            size_t i = (size_t)y * sw + x;
            int edge = sc[i] != sc[i - 1] || sc[i] != sc[i + 1] || sc[i] != sc[i - sw] || sc[i] != sc[i + sw];
            if (edge && frand() < 0.12f)
                sc[i] ^= 1;
        }
    uint8_t *c = crop_mask(sc, sw, rh, ow, oh);
    free(sc);
    return c;
}

static int rank_of(const Glyph *g, const TemplateSet *ts, const char *code)
{
    for (int k = 0; k < g->ncand; k++)
        if (strcmp(ts->t[g->cand[k]].code, code) == 0)
            return k;
    return -1;
}

int evaluate_model(const Options *o, TemplateSet *full)
{
    if (!o->font) {
        fprintf(stderr, "La evaluación necesita la fuente jeroglífica (-f).\n");
        return 1;
    }
    TTF_Font *ft = TTF_OpenFont(o->font, EVAL_SIZE);
    if (!ft) {
        fprintf(stderr, "No se puede abrir %s: %s\n", o->font, TTF_GetError());
        return 1;
    }
    /* Referencia: una sola plantilla por signo (el dibujo original), como antes de entrenar. */
    TemplateSet base = {0};
    base.view = 1;
    for (int i = 0; i < full->n; i++)
        if (full->t[i].variant == 0 && !full->t[i].user) {
            if (base.n == base.cap) {
                base.cap = base.cap ? base.cap * 2 : 1024;
                base.t = realloc(base.t, sizeof(Template) * (size_t)base.cap);
            }
            base.t[base.n++] = full->t[i];
        }

    float quad = quadrat_height(ft);
    const int SAMPLES = 2;
    int total = 0, b1 = 0, b5 = 0, m1 = 0, m5 = 0;
    int sec_tot[32] = {0}, sec_ok[32] = {0};
    char sec_name[32][4] = {{0}};
    int nsec = 0;
    rng_state = 2463534242u;
    fprintf(stderr, "Evaluando con %d muestras nuevas por signo (fuente a %d px, estilos, giros y ruido aleatorios)...\n",
            SAMPLES, EVAL_SIZE);
    for (int i = 0; i < signs_count(); i++) {
        const SignInfo *si = sign_at(i);
        if (!ts_codepoint(full, si->code))
            continue;
        int sidx = -1;
        for (int k = 0; k < nsec; k++)
            if (strcmp(sec_name[k], si->section) == 0)
                sidx = k;
        if (sidx < 0 && nsec < 32) {
            sidx = nsec++;
            snprintf(sec_name[sidx], 4, "%s", si->section);
        }
        for (int s = 0; s < SAMPLES; s++) {
            int w, h;
            uint8_t *m = test_sample(ft, si->cp, &w, &h);
            if (!m)
                continue;
            float relh = h / quad;
            Glyph gb, gm;
            memset(&gb, 0, sizeof gb);
            memset(&gm, 0, sizeof gm);
            classify_mask(m, w, h, relh, &base, &gb);
            classify_mask(m, w, h, relh, full, &gm);
            free(m);
            int rb = rank_of(&gb, &base, si->code), rm = rank_of(&gm, full, si->code);
            if (rm != 0 && getenv("HIEROOCR_FALLOS"))
                fprintf(stderr, "FALLO %s -> %s\n", si->code, gm.ncand ? full->t[gm.cand[0]].code : "?");
            total++;
            b1 += rb == 0;
            b5 += rb >= 0;
            m1 += rm == 0;
            m5 += rm >= 0;
            if (sidx >= 0) {
                sec_tot[sidx]++;
                sec_ok[sidx] += rm == 0;
            }
        }
        if ((i + 1) % 200 == 0)
            fprintf(stderr, "  %d signos evaluados...\n", i + 1);
    }
    TTF_CloseFont(ft);
    free(base.t);
    if (!total) {
        fprintf(stderr, "No hay muestras que evaluar.\n");
        return 1;
    }
    printf("\nEvaluación sobre %d muestras (%d signos de la lista de Gardiner):\n", total, total / SAMPLES);
    printf("                                 acierto 1º   entre los 5 primeros\n");
    printf("  sin entrenar (1 plantilla)       %5.1f %%       %5.1f %%\n", 100.0 * b1 / total, 100.0 * b5 / total);
    printf("  modelo entrenado (%2d variantes)  %5.1f %%       %5.1f %%\n", NVARIANTS,
           100.0 * m1 / total, 100.0 * m5 / total);
    printf("\nAcierto 1º del modelo por sección:\n ");
    for (int k = 0; k < nsec; k++)
        printf(" %s %.0f%%", sec_name[k], 100.0 * sec_ok[k] / (sec_tot[k] ? sec_tot[k] : 1));
    printf("\n");
    return 0;
}

/* ---------- evaluación con fotografías reservadas ---------- */

typedef struct {
    const TemplateSet *font, *both;
    int total, f1, f5, b1, b5, unknown;
} PhotoEval;

static void photo_test(const char *code, int index, const uint8_t *mask, int w, int h, void *ud)
{
    PhotoEval *pe = ud;
    if (index % 5 != 4)
        return;   /* sólo las fotos reservadas */
    Glyph a, b;
    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    classify_mask(mask, w, h, -1.0f, pe->font, &a);
    classify_mask(mask, w, h, -1.0f, pe->both, &b);
    int ra = rank_of(&a, pe->font, code), rb = rank_of(&b, pe->both, code);
    pe->total++;
    pe->f1 += ra == 0;
    pe->f5 += ra >= 0;
    pe->b1 += rb == 0;
    pe->b5 += rb >= 0;
    pe->unknown += !ts_codepoint(pe->font, code);
}

int evaluate_photos(const Options *o, const TemplateSet *model)
{
    /* Modelo sin fotos (sólo la fuente) */
    TemplateSet font = {0}, both = {0};
    font.view = 1;
    for (int i = 0; i < model->n; i++)
        if (model->t[i].variant != VARIANT_PHOTO && !model->t[i].user) {
            if (font.n == font.cap) {
                font.cap = font.cap ? font.cap * 2 : 4096;
                font.t = realloc(font.t, sizeof(Template) * (size_t)font.cap);
            }
            font.t[font.n++] = model->t[i];
        }
    /* Fuente + fotos de entrenamiento (4 de cada 5) */
    both.view = 0;
    for (int i = 0; i < font.n; i++) {
        Template *t = ts_add(&both);
        *t = font.t[i];
        t->ink = malloc(sizeof(uint16_t) * (size_t)(t->nink ? t->nink : 1));
        memcpy(t->ink, font.t[i].ink, sizeof(uint16_t) * (size_t)t->nink);
    }
    fprintf(stderr, "Preparando las fotografías de entrenamiento (4 de cada 5 por clase)...\n");
    PhotoSink ps = {NULL, 0, 0, 1, &both};
    dataset_walk(o->dataset, photo_record, &ps);
    fprintf(stderr, "Clasificando las fotografías reservadas (1 de cada 5)...\n");
    PhotoEval pe = {&font, &both, 0, 0, 0, 0, 0, 0};
    dataset_walk(o->dataset, photo_test, &pe);
    free(font.t);
    ts_free(&both);
    if (!pe.total)
        return 1;
    printf("\nEvaluación con %d fotografías reservadas de %s:\n", pe.total, o->dataset);
    printf("                                        acierto 1º   entre los 5 primeros\n");
    printf("  sólo la fuente (sin fotos)              %5.1f %%       %5.1f %%\n",
           100.0 * pe.f1 / pe.total, 100.0 * pe.f5 / pe.total);
    printf("  fuente + %5d fotos de entrenamiento    %5.1f %%       %5.1f %%\n", ps.photos,
           100.0 * pe.b1 / pe.total, 100.0 * pe.b5 / pe.total);
    if (pe.unknown)
        printf("  (%d de las fotos de prueba son de signos que no están en la fuente)\n", pe.unknown);
    return 0;
}
