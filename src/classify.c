/*
 * Clasificación de signos por comparación con plantillas.
 *
 * Plantillas de dos orígenes:
 *   - de la fuente: cada signo Unicode "EGYPTIAN HIEROGLYPH X###" de
 *     UnicodeData.txt se dibuja con SDL2_ttf y se le asigna su código Gardiner;
 *   - aprendidas: PNG guardados desde la interfaz cuando el usuario corrige un
 *     signo. Como salen de la misma edición que se está leyendo, pesan más.
 *
 * La distancia entre un signo y una plantilla es una distancia de chamfer
 * simétrica sobre rejillas normalizadas de NORM x NORM, más penalizaciones
 * por diferencia de proporción (ancho/alto) y de altura relativa.
 */
#include "hieroocr.h"

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* Ajusta la máscara w x h a la rejilla NORM x NORM conservando la proporción. */
void normalize_mask(const uint8_t *src, int w, int h, uint8_t *dst)
{
    memset(dst, 0, NORM * NORM);
    float s = (NORM - 4) / (float)(w > h ? w : h);
    int tw = (int)(w * s + 0.5f), th = (int)(h * s + 0.5f);
    if (tw < 1) tw = 1;
    if (th < 1) th = 1;
    int ox = (NORM - tw) / 2, oy = (NORM - th) / 2;
    for (int j = 0; j < th; j++) {
        int sy0 = (int)(j / s), sy1 = (int)((j + 1) / s);
        if (sy0 >= h) sy0 = h - 1;
        if (sy1 <= sy0) sy1 = sy0 + 1;
        if (sy1 > h) sy1 = h;
        for (int i = 0; i < tw; i++) {
            int sx0 = (int)(i / s), sx1 = (int)((i + 1) / s);
            if (sx0 >= w) sx0 = w - 1;
            if (sx1 <= sx0) sx1 = sx0 + 1;
            if (sx1 > w) sx1 = w;
            int cnt = 0, tot = 0;
            for (int y = sy0; y < sy1; y++)
                for (int x = sx0; x < sx1; x++) {
                    cnt += src[(size_t)y * w + x];
                    tot++;
                }
            dst[(oy + j) * NORM + ox + i] = (uint8_t)(cnt * 4 >= tot && cnt > 0);
        }
    }
}

/* Transformada de distancia de chamfer (aprox. euclídea) en dos pasadas. */
static void dist_transform(const uint8_t *m, float *d)
{
    const float INF = 1e6f, D1 = 1.0f, D2 = 1.4142f;
    for (int i = 0; i < NORM * NORM; i++)
        d[i] = m[i] ? 0 : INF;
    for (int y = 0; y < NORM; y++)
        for (int x = 0; x < NORM; x++) {
            int i = y * NORM + x;
            float v = d[i];
            if (x > 0) v = fminf(v, d[i - 1] + D1);
            if (y > 0) {
                v = fminf(v, d[i - NORM] + D1);
                if (x > 0)        v = fminf(v, d[i - NORM - 1] + D2);
                if (x < NORM - 1) v = fminf(v, d[i - NORM + 1] + D2);
            }
            d[i] = v;
        }
    for (int y = NORM - 1; y >= 0; y--)
        for (int x = NORM - 1; x >= 0; x--) {
            int i = y * NORM + x;
            float v = d[i];
            if (x < NORM - 1) v = fminf(v, d[i + 1] + D1);
            if (y < NORM - 1) {
                v = fminf(v, d[i + NORM] + D1);
                if (x < NORM - 1) v = fminf(v, d[i + NORM + 1] + D2);
                if (x > 0)        v = fminf(v, d[i + NORM - 1] + D2);
            }
            d[i] = v;
        }
    for (int i = 0; i < NORM * NORM; i++)
        if (d[i] >= INF)
            d[i] = NORM;
}

/* Engrosa un píxel (3x3): iguala trazos finos y gruesos y tolera desplazamientos. */
static void dilate(const uint8_t *in, uint8_t *out)
{
    for (int y = 0; y < NORM; y++)
        for (int x = 0; x < NORM; x++) {
            uint8_t v = 0;
            for (int dy = -1; dy <= 1 && !v; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int yy = y + dy, xx = x + dx;
                    if (yy >= 0 && yy < NORM && xx >= 0 && xx < NORM && in[yy * NORM + xx]) {
                        v = 1;
                        break;
                    }
                }
            out[y * NORM + x] = v;
        }
}

/* Imagen suavizada (dos pasadas de caja 5x5), de media 0 y norma 1, para correlación. */
static void blur_vector(const uint8_t *m, float *b)
{
    float a[NORM * NORM], tmp[NORM * NORM];
    for (int i = 0; i < NORM * NORM; i++)
        a[i] = m[i];
    for (int pass = 0; pass < 2; pass++) {
        for (int y = 0; y < NORM; y++)
            for (int x = 0; x < NORM; x++) {
                float s = 0;
                for (int d = -2; d <= 2; d++) {
                    int xx = x + d;
                    if (xx >= 0 && xx < NORM) s += a[y * NORM + xx];
                }
                tmp[y * NORM + x] = s / 5;
            }
        for (int y = 0; y < NORM; y++)
            for (int x = 0; x < NORM; x++) {
                float s = 0;
                for (int d = -2; d <= 2; d++) {
                    int yy = y + d;
                    if (yy >= 0 && yy < NORM) s += tmp[yy * NORM + x];
                }
                a[y * NORM + x] = s / 5;
            }
    }
    double mean = 0, norm = 0;
    for (int i = 0; i < NORM * NORM; i++)
        mean += a[i];
    mean /= NORM * NORM;
    for (int i = 0; i < NORM * NORM; i++) {
        b[i] = (float)(a[i] - mean);
        norm += (double)b[i] * b[i];
    }
    norm = sqrt(norm);
    for (int i = 0; i < NORM * NORM; i++)
        b[i] = norm > 0 ? (float)(b[i] / norm) : 0;
}

/* Construye una plantilla a partir de una máscara ya normalizada a NORM x NORM. */
void template_from_norm(Template *t, const uint8_t *norm0, float aspect)
{
    uint8_t m[NORM * NORM];
    float d[NORM * NORM], b[NORM * NORM];
    dilate(norm0, m);
    blur_vector(m, b);
    dist_transform(m, d);
    for (int i = 0; i < NORM * NORM; i++) {
        float v = d[i] * 4.0f + 0.5f;
        t->dt[i] = (uint8_t)(v > 255 ? 255 : v);
        t->blur[i] = (int16_t)lrintf(b[i] * 32767.0f);
    }
    /* resumen 8x8 (bloques de 5x5), de media 0 y norma 1 */
    double mean = 0, nrm = 0;
    for (int by = 0; by < 8; by++)
        for (int bx = 0; bx < 8; bx++) {
            float acc = 0;
            for (int y = 0; y < 5; y++)
                for (int x = 0; x < 5; x++)
                    acc += b[(by * 5 + y) * NORM + bx * 5 + x];
            t->coarse[by * 8 + bx] = acc;
            mean += acc;
        }
    mean /= 64;
    for (int i = 0; i < 64; i++) {
        t->coarse[i] -= (float)mean;
        nrm += (double)t->coarse[i] * t->coarse[i];
    }
    nrm = sqrt(nrm);
    for (int i = 0; i < 64; i++)
        t->coarse[i] = nrm > 0 ? (float)(t->coarse[i] / nrm) : 0;

    int n = 0;
    for (int i = 0; i < NORM * NORM; i++)
        n += m[i] != 0;
    free(t->ink);
    t->ink = malloc(sizeof(uint16_t) * (size_t)(n ? n : 1));
    t->nink = 0;
    for (int i = 0; i < NORM * NORM; i++)
        if (m[i])
            t->ink[t->nink++] = (uint16_t)i;
    t->aspect = aspect;
}

static void make_template(Template *t, const uint8_t *mask, int w, int h)
{
    uint8_t m0[NORM * NORM];
    normalize_mask(mask, w, h, m0);
    template_from_norm(t, m0, (float)w / (float)h);
}

/* Recorta la máscara a su caja de tinta. Devuelve una copia nueva o NULL si está vacía. */
uint8_t *crop_mask(const uint8_t *m, int w, int h, int *cw, int *ch)
{
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (m[(size_t)y * w + x]) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
    if (x1 < 0)
        return NULL;
    *cw = x1 - x0 + 1;
    *ch = y1 - y0 + 1;
    uint8_t *o = malloc((size_t)(*cw) * (*ch));
    for (int y = 0; y < *ch; y++)
        memcpy(o + (size_t)y * (*cw), m + (size_t)(y0 + y) * w + x0, (size_t)(*cw));
    return o;
}

void ts_init(TemplateSet *ts)
{
    memset(ts, 0, sizeof *ts);
}

void ts_free(TemplateSet *ts)
{
    if (!ts->view)
        for (int i = 0; i < ts->n; i++)
            free(ts->t[i].ink);
    free(ts->t);
    memset(ts, 0, sizeof *ts);
}

Template *ts_add(TemplateSet *ts)
{
    if (ts->n == ts->cap) {
        ts->cap = ts->cap ? ts->cap * 2 : 256;
        ts->t = realloc(ts->t, sizeof(Template) * (size_t)ts->cap);
    }
    Template *t = &ts->t[ts->n++];
    memset(t, 0, sizeof *t);
    return t;
}

int valid_code(const char *s)
{
    size_t n = strlen(s);
    if (n == 0 || n >= CODE_LEN || !isalpha((unsigned char)s[0]))
        return 0;
    for (size_t i = 0; i < n; i++)
        if (!isalnum((unsigned char)s[i]))
            return 0;
    return 1;
}

/* "A001" -> "A1", "AA013" -> "Aa13", "N035A" -> "N35A". */
static int gardiner_from_name(const char *n, char *code)
{
    char pre[3];
    int i = 0;
    while (i < 2 && isupper((unsigned char)n[i])) {
        pre[i] = n[i];
        i++;
    }
    if (i == 0)
        return -1;
    pre[i] = 0;
    int num = 0, nd = 0;
    while (isdigit((unsigned char)n[i])) {
        num = num * 10 + (n[i] - '0');
        i++;
        nd++;
    }
    if (nd != 3)
        return -1;
    char suf[4] = {0};
    int k = 0;
    while (k < 3 && isupper((unsigned char)n[i]))
        suf[k++] = n[i++];
    if (n[i] != 0)
        return -1;
    if (strcmp(pre, "AA") == 0)
        strcpy(pre, "Aa");
    snprintf(code, CODE_LEN, "%s%d%s", pre, num, suf);
    return 0;
}

/* Signos frecuentes en egipcio medio y neoegipcio. Las demás plantillas de la
 * fuente (variantes raras, signos ptolemaicos...) reciben una pequeña
 * penalización para que no se cuelen en lugar de los habituales. */
static const char *common_signs[] = {
    "A1","A2","A7","A9","A12","A17","A21","A24","A28","A40","A50","A51","A55",
    "B1","C7","D1","D2","D3","D4","D9","D19","D21","D26","D28","D34","D35","D36",
    "D37","D39","D40","D41","D46","D52","D53","D54","D56","D58","D60",
    "E1","E9","E13","E20","E23","E34","F1","F4","F12","F13","F16","F18","F21",
    "F26","F27","F31","F32","F34","F35","F37","F40","F51",
    "G1","G4","G5","G7","G14","G17","G21","G25","G26","G28","G29","G31","G35",
    "G36","G37","G38","G39","G40","G41","G43","G47","H5","H6","H8",
    "I3","I9","I10","I12","K1","L1","M2","M3","M8","M9","M12","M16","M17",
    "M18","M20","M22","M23","M26","M29","M36","M42",
    "N1","N2","N5","N8","N14","N16","N17","N21","N23","N25","N26","N27","N28",
    "N29","N31","N33","N35","N35A","N36","N37","N40","N41","N49",
    "O1","O4","O28","O29","O31","O34","O39","O49","O50",
    "P1","P6","P8","Q1","Q2","Q3","Q7","R4","R8","R11",
    "S3","S12","S24","S28","S29","S33","S34","S35","S38","S40","S42",
    "T3","T14","T21","T22","T28","T30","U1","U6","U7","U13","U15","U28","U33","U36",
    "V1","V4","V6","V12","V13","V20","V22","V24","V26","V28","V29","V30","V31",
    "W9","W10","W11","W14","W19","W22","W24","W25",
    "X1","X2","X4","X8","Y1","Y2","Y3","Y5","Z1","Z2","Z3","Z4","Z5","Z7","Z9","Z11",
    "Aa1","Aa2","Aa11","Aa13","Aa15","Aa17","Aa18","Aa27","Aa28",
    NULL
};

/* ---------- lista de signos de Gardiner (datos/gardiner.tsv) ---------- */

static SignInfo *g_signs;
static int g_nsigns;

static void copy_field(char *dst, size_t n, const char *src)
{
    snprintf(dst, n, "%s", src ? src : "");
}

int signs_load(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    char line[1024];
    int cap = 0;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n')
            continue;
        line[strcspn(line, "\r\n")] = 0;
        char *fld[6] = {0};
        int nf = 0;
        char *s = line;
        while (nf < 6) {
            fld[nf++] = s;
            char *tab = strchr(s, '\t');
            if (!tab)
                break;
            *tab = 0;
            s = tab + 1;
        }
        if (nf < 2 || !valid_code(fld[0]))
            continue;
        if (g_nsigns == cap) {
            cap = cap ? cap * 2 : 1024;
            g_signs = realloc(g_signs, sizeof(SignInfo) * (size_t)cap);
        }
        SignInfo *si = &g_signs[g_nsigns++];
        memset(si, 0, sizeof *si);
        copy_field(si->code, sizeof si->code, fld[0]);
        si->cp = (uint32_t)strtoul(fld[1] + (strncmp(fld[1], "U+", 2) == 0 ? 2 : 0), NULL, 16);
        copy_field(si->section, sizeof si->section, nf > 2 ? fld[2] : "");
        copy_field(si->phon, sizeof si->phon, nf > 3 ? fld[3] : "");
        copy_field(si->desc, sizeof si->desc, nf > 4 ? fld[4] : "");
    }
    fclose(f);
    return g_nsigns;
}

int signs_count(void) { return g_nsigns; }
const SignInfo *sign_at(int i) { return (i >= 0 && i < g_nsigns) ? &g_signs[i] : NULL; }

const SignInfo *sign_find(const char *code)
{
    for (int i = 0; i < g_nsigns; i++)
        if (strcmp(g_signs[i].code, code) == 0)
            return &g_signs[i];
    return NULL;
}

/* Frecuente: está en la lista usual o tiene valor fonético según Wikipedia. */
int is_common_code(const char *code)
{
    for (const char **p = common_signs; *p; p++)
        if (strcmp(*p, code) == 0)
            return 1;
    const SignInfo *si = sign_find(code);
    return si && si->phon[0];
}

/* Dibuja un signo de la fuente y devuelve su máscara recortada (1 = tinta). */
uint8_t *render_glyph(void *ttf_font, uint32_t cp, int *w, int *h)
{
    TTF_Font *ft = ttf_font;
    if (!TTF_GlyphIsProvided32(ft, cp))
        return NULL;
    SDL_Color fg = {0, 0, 0, 255}, bg = {255, 255, 255, 255};
    SDL_Surface *s = TTF_RenderGlyph32_Shaded(ft, cp, fg, bg);
    if (!s)
        return NULL;
    uint8_t *m = calloc((size_t)s->w * s->h, 1);
    SDL_LockSurface(s);
    SDL_Palette *pal = s->format->palette;
    for (int y = 0; y < s->h; y++)
        for (int x = 0; x < s->w; x++) {
            uint8_t v = ((const uint8_t *)s->pixels)[(size_t)y * s->pitch + x];
            int lum = pal ? pal->colors[v].r : v;
            m[(size_t)y * s->w + x] = lum < 128;
        }
    SDL_UnlockSurface(s);
    uint8_t *c = crop_mask(m, s->w, s->h, w, h);
    free(m);
    SDL_FreeSurface(s);
    return c;
}

static int g_all_signs = 0;
static float g_variant_penalty = 0.005f;
static float g_photo_penalty = 0.0f;
void classify_set_options(const Options *o)
{
    g_all_signs = o->all_signs;
    const char *vp = getenv("HIEROOCR_PENALIZACION_VARIANTE");
    if (vp)
        g_variant_penalty = (float)atof(vp);
    const char *pp = getenv("HIEROOCR_PENALIZACION_FOTO");
    if (pp)
        g_photo_penalty = (float)atof(pp);
}

static int cmp_float(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

int ts_load_font(TemplateSet *ts, const char *font, const char *unicode)
{
    FILE *f = fopen(unicode, "r");
    if (!f) {
        fprintf(stderr, "No se puede abrir %s: %s\n", unicode, strerror(errno));
        return -1;
    }
    TTF_Font *ft = TTF_OpenFont(font, 96);
    if (!ft) {
        fprintf(stderr, "No se puede abrir la fuente %s: %s\n", font, TTF_GetError());
        fclose(f);
        return -1;
    }
    char line[512];
    int start = ts->n;
    while (fgets(line, sizeof line, f)) {
        unsigned cp;
        char name[256];
        if (sscanf(line, "%x;%255[^;]", &cp, name) != 2)
            continue;
        if (strncmp(name, "EGYPTIAN HIEROGLYPH ", 20) != 0)
            continue;
        char code[CODE_LEN];
        if (gardiner_from_name(name + 20, code))
            continue;
        int cw, ch;
        uint8_t *c = render_glyph(ft, cp, &cw, &ch);
        if (!c)
            continue;
        Template *t = ts_add(ts);
        make_template(t, c, cw, ch);
        snprintf(t->code, CODE_LEN, "%s", code);
        t->cp = cp;
        t->common = is_common_code(code);
        t->relh = (float)ch;   /* se normaliza abajo */
        free(c);
    }
    TTF_CloseFont(ft);
    fclose(f);

    /* Altura relativa: respecto a la de los signos más altos (≈ un cuadrado entero). */
    int n = ts->n - start;
    if (n > 0) {
        float *hs = malloc(sizeof(float) * (size_t)n);
        for (int i = 0; i < n; i++)
            hs[i] = ts->t[start + i].relh;
        qsort(hs, (size_t)n, sizeof(float), cmp_float);
        float q = hs[(n * 9) / 10];
        free(hs);
        for (int i = start; i < ts->n; i++)
            ts->t[i].relh /= q;
    }
    fprintf(stderr, "Plantillas de la fuente: %d signos\n", n);
    return n;
}

static int load_user_file(TemplateSet *ts, const char *dir, const char *fname)
{
    char code[CODE_LEN];
    const char *sep = strstr(fname, "__");
    size_t cl = sep ? (size_t)(sep - fname) : strlen(fname) - 4;
    if (cl == 0 || cl >= CODE_LEN)
        return -1;
    memcpy(code, fname, cl);
    code[cl] = 0;
    if (!valid_code(code))
        return -1;
    float relh = sep ? strtof(sep + 2, NULL) : -1.0f;

    char path[1200];
    snprintf(path, sizeof path, "%s/%s", dir, fname);
    Gray g;
    if (gray_load(path, &g))
        return -1;
    uint8_t *m = malloc((size_t)g.w * g.h);
    for (size_t i = 0; i < (size_t)g.w * g.h; i++)
        m[i] = g.px[i] < 128;
    int cw, ch;
    uint8_t *c = crop_mask(m, g.w, g.h, &cw, &ch);
    free(m);
    gray_free(&g);
    if (!c)
        return -1;
    Template *t = ts_add(ts);
    make_template(t, c, cw, ch);
    snprintf(t->code, CODE_LEN, "%s", code);
    t->user = 1;
    t->common = 1;
    t->relh = relh > 0 ? relh : -1.0f;
    free(c);
    return 0;
}

int ts_load_user(TemplateSet *ts, const char *dir)
{
    DIR *d = opendir(dir);
    if (!d)
        return 0;
    struct dirent *e;
    int n = 0;
    while ((e = readdir(d))) {
        size_t l = strlen(e->d_name);
        if (l > 4 && strcmp(e->d_name + l - 4, ".png") == 0)
            if (load_user_file(ts, dir, e->d_name) == 0)
                n++;
    }
    closedir(d);
    if (n)
        fprintf(stderr, "Plantillas aprendidas en %s: %d\n", dir, n);
    return n;
}

int ts_learn(TemplateSet *ts, const char *dir, const char *code,
             const uint8_t *mask, int w, int h, float relh)
{
    if (!valid_code(code))
        return -1;
    int cw, ch;
    uint8_t *c = crop_mask(mask, w, h, &cw, &ch);
    if (!c)
        return -1;

    if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "No se puede crear %s: %s\n", dir, strerror(errno));
    } else {
        SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, cw + 4, ch + 4, 24, SDL_PIXELFORMAT_RGB24);
        if (s) {
            SDL_FillRect(s, NULL, SDL_MapRGB(s->format, 255, 255, 255));
            SDL_LockSurface(s);
            for (int y = 0; y < ch; y++)
                for (int x = 0; x < cw; x++)
                    if (c[(size_t)y * cw + x]) {
                        uint8_t *q = (uint8_t *)s->pixels + (size_t)(y + 2) * s->pitch + 3 * (x + 2);
                        q[0] = q[1] = q[2] = 0;
                    }
            SDL_UnlockSurface(s);
            static int counter = 0;
            char path[1200];
            snprintf(path, sizeof path, "%s/%s__%.3f__%ld_%d.png",
                     dir, code, relh, (long)time(NULL), counter++);
            if (IMG_SavePNG(s, path) != 0)
                fprintf(stderr, "No se puede guardar %s: %s\n", path, IMG_GetError());
            SDL_FreeSurface(s);
        }
    }

    Template *t = ts_add(ts);
    make_template(t, c, cw, ch);
    snprintf(t->code, CODE_LEN, "%s", code);
    t->user = 1;
    t->common = 1;
    t->relh = relh > 0 ? relh : -1.0f;
    free(c);
    return 0;
}

uint32_t ts_codepoint(const TemplateSet *ts, const char *code)
{
    for (int i = 0; i < ts->n; i++)
        if (ts->t[i].cp && strcmp(ts->t[i].code, code) == 0)
            return ts->t[i].cp;
    return 0;
}

static void insert_candidate(Glyph *g, const TemplateSet *ts, int ti, float sc)
{
    const char *code = ts->t[ti].code;
    for (int k = 0; k < g->ncand; k++) {
        if (strcmp(ts->t[g->cand[k]].code, code) == 0) {
            if (sc >= g->cand_score[k])
                return;
            for (int j = k; j < g->ncand - 1; j++) {   /* lo quita y lo reinserta abajo */
                g->cand[j] = g->cand[j + 1];
                g->cand_score[j] = g->cand_score[j + 1];
            }
            g->ncand--;
            break;
        }
    }
    int pos = g->ncand;
    while (pos > 0 && g->cand_score[pos - 1] > sc)
        pos--;
    if (pos >= NCAND)
        return;
    int last = g->ncand < NCAND ? g->ncand : NCAND - 1;
    for (int j = last; j > pos; j--) {
        g->cand[j] = g->cand[j - 1];
        g->cand_score[j] = g->cand_score[j - 1];
    }
    g->cand[pos] = ti;
    g->cand_score[pos] = sc;
    if (g->ncand < NCAND)
        g->ncand++;
}

static float score_pair(const Template *q, const Template *t, float la)
{
    int sa = 0, sb = 0;
    for (int k = 0; k < q->nink; k++)
        sa += t->dt[q->ink[k]];
    for (int k = 0; k < t->nink; k++)
        sb += q->dt[t->ink[k]];
    int64_t ncc = 0;
    for (int k = 0; k < NORM * NORM; k++)
        ncc += (int32_t)q->blur[k] * t->blur[k];
    float fncc = (float)((double)ncc / (32767.0 * 32767.0));
    float cham = ((float)sa / q->nink + (float)sb / t->nink) / 4.0f;   /* dt guardada x4 */
    float sc = 0.5f * cham / (2.0f * NORM) + 0.25f * (1.0f - fncc);
    if (t->user)
        sc *= 0.6f;   /* misma edición y tipografía: la forma es mucho más fiable */
    else if (!t->common && !g_all_signs)
        sc += 0.03f;
    if (t->variant == 20)
        sc += g_photo_penalty;     /* contorno sacado de una foto: útil pero ruidoso */
    else if (t->variant > 0)
        sc += g_variant_penalty;   /* el dibujo original manda; las variantes desempatan */
    sc += 0.2f * fabsf(la);
    if (t->relh > 0 && q->relh > 0)
        sc += (t->user ? 0.35f : 0.12f) * fabsf(logf(q->relh / t->relh));
    return sc;
}

typedef struct { int i; float c; } Coarse;

static int cmp_coarse(const void *a, const void *b)
{
    float x = ((const Coarse *)a)->c, y = ((const Coarse *)b)->c;
    return (x < y) - (x > y);   /* de mayor a menor */
}

#define KEEP 400   /* plantillas que pasan el filtro rápido */

void classify_mask(const uint8_t *mask, int w, int h, float relh, const TemplateSet *ts, Glyph *g)
{
    Template q;
    memset(&q, 0, sizeof q);
    make_template(&q, mask, w, h);
    q.relh = relh;
    g->ncand = 0;

    Coarse *cs = malloc(sizeof(Coarse) * (size_t)(ts->n ? ts->n : 1));
    for (int pass = 0; pass < 2 && g->ncand == 0; pass++) {   /* 2ª pasada sin filtro de proporción */
        int m = 0;
        for (int i = 0; i < ts->n; i++) {
            const Template *t = &ts->t[i];
            if (t->nink == 0 || q.nink == 0)
                continue;
            float la = logf(q.aspect / t->aspect);
            if (pass == 0 && fabsf(la) > 1.0f)
                continue;
            float c = 0;
            for (int k = 0; k < 64; k++)
                c += q.coarse[k] * t->coarse[k];
            cs[m++] = (Coarse){i, t->user ? 2.0f + c : c};   /* las aprendidas siempre pasan */
        }
        if (m > KEEP) {
            qsort(cs, (size_t)m, sizeof(Coarse), cmp_coarse);
            m = KEEP;
        }
        for (int k = 0; k < m; k++) {
            const Template *t = &ts->t[cs[k].i];
            insert_candidate(g, ts, cs[k].i, score_pair(&q, t, logf(q.aspect / t->aspect)));
        }
    }
    free(cs);
    free(q.ink);
    g->score = g->ncand ? g->cand_score[0] : 1.0f;
    if (!g->manual) {
        if (g->ncand)
            snprintf(g->code, CODE_LEN, "%s", ts->t[g->cand[0]].code);
        else
            g->code[0] = 0;
    }
}

void classify_glyph(Page *p, int gi, const TemplateSet *ts)
{
    uint8_t *mask;
    int w, h;
    if (glyph_mask(p, gi, &mask, &w, &h))
        return;
    classify_mask(mask, w, h, p->line_h > 0 ? h / p->line_h : -1.0f, ts, &p->g[gi]);
    free(mask);
}

void classify_page(Page *p, const TemplateSet *ts)
{
    for (int i = 0; i < p->ng; i++)
        if (p->g[i].alive)
            classify_glyph(p, i, ts);
}
