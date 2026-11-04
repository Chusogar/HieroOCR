/*
 * Importación de un conjunto de datos de fotografías de jeroglíficos organizado
 * en carpetas por código de Gardiner (p. ej. el de R. Fuentes-Ferrer et al.,
 * https://github.com/rfuentesfe/EgyptianHieroglyphicText, con dataset/g17/g17_1.png, ...).
 *
 * Las fotos son de signos en relieve o pintados sobre piedra, con luz y sombras,
 * así que no se binarizan como una página impresa: se extrae el CONTORNO del
 * signo a partir de los bordes (suavizado + gradiente de Sobel + umbral
 * adaptativo + limpieza de manchas). El contorno se compara bien con los signos
 * huecos de las ediciones impresas y con las variantes "contorno" del modelo.
 */
#include "hieroocr.h"

#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PHOTO_SIDE 112   /* lado mayor al que se reduce cada foto */

static int cmp_float(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

/* Reduce (media por áreas) para que el lado mayor mida 'side'. */
static float *resize_gray(const Gray *g, int side, int *ow, int *oh)
{
    float s = (float)side / (float)(g->w > g->h ? g->w : g->h);
    if (s > 1.0f)
        s = 1.0f;
    *ow = (int)lroundf(g->w * s);
    *oh = (int)lroundf(g->h * s);
    if (*ow < 8) *ow = 8;
    if (*oh < 8) *oh = 8;
    float *o = calloc((size_t)(*ow) * (*oh), sizeof(float));
    for (int y = 0; y < *oh; y++) {
        int y0 = y * g->h / *oh, y1 = (y + 1) * g->h / *oh;
        if (y1 <= y0) y1 = y0 + 1;
        for (int x = 0; x < *ow; x++) {
            int x0 = x * g->w / *ow, x1 = (x + 1) * g->w / *ow;
            if (x1 <= x0) x1 = x0 + 1;
            float acc = 0;
            int n = 0;
            for (int yy = y0; yy < y1 && yy < g->h; yy++)
                for (int xx = x0; xx < x1 && xx < g->w; xx++) {
                    acc += g->px[(size_t)yy * g->w + xx];
                    n++;
                }
            o[(size_t)y * (*ow) + x] = n ? acc / n : 255;
        }
    }
    return o;
}

static void box_blur(float *a, int w, int h)
{
    float *t = malloc(sizeof(float) * (size_t)w * h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float s = 0;
            int n = 0;
            for (int d = -1; d <= 1; d++)
                if (x + d >= 0 && x + d < w) { s += a[(size_t)y * w + x + d]; n++; }
            t[(size_t)y * w + x] = s / n;
        }
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float s = 0;
            int n = 0;
            for (int d = -1; d <= 1; d++)
                if (y + d >= 0 && y + d < h) { s += t[(size_t)(y + d) * w + x]; n++; }
            a[(size_t)y * w + x] = s / n;
        }
    free(t);
}

/* Borra componentes conexas (8-vecindad) de menos de 'minpix' píxeles. */
static void remove_small(uint8_t *m, int w, int h, int minpix)
{
    int *stack = malloc(sizeof(int) * (size_t)w * h);
    uint8_t *seen = calloc((size_t)w * h, 1);
    int *comp = malloc(sizeof(int) * (size_t)w * h);
    for (int i = 0; i < w * h; i++) {
        if (!m[i] || seen[i])
            continue;
        int sp = 0, nc = 0;
        stack[sp++] = i;
        seen[i] = 1;
        while (sp) {
            int p = stack[--sp];
            comp[nc++] = p;
            int px = p % w, py = p / w;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int nx = px + dx, ny = py + dy;
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                        continue;
                    int q = ny * w + nx;
                    if (m[q] && !seen[q]) {
                        seen[q] = 1;
                        stack[sp++] = q;
                    }
                }
        }
        if (nc < minpix)
            for (int k = 0; k < nc; k++)
                m[comp[k]] = 0;
    }
    free(stack);
    free(seen);
    free(comp);
}

/* Foto -> máscara de contorno recortada (1 = tinta). NULL si no queda nada útil. */
uint8_t *photo_to_mask(const Gray *g, int *ow, int *oh)
{
    int w, h;
    float *a = resize_gray(g, PHOTO_SIDE, &w, &h);
    box_blur(a, w, h);
    box_blur(a, w, h);

    float *mag = calloc((size_t)w * h, sizeof(float));
    for (int y = 1; y < h - 1; y++)
        for (int x = 1; x < w - 1; x++) {
#define A(dx, dy) a[(size_t)(y + (dy)) * w + x + (dx)]
            float gx = (A(1, -1) + 2 * A(1, 0) + A(1, 1)) - (A(-1, -1) + 2 * A(-1, 0) + A(-1, 1));
            float gy = (A(-1, 1) + 2 * A(0, 1) + A(1, 1)) - (A(-1, -1) + 2 * A(0, -1) + A(1, -1));
#undef A
            mag[(size_t)y * w + x] = sqrtf(gx * gx + gy * gy);
        }
    free(a);

    /* Umbral adaptativo: el 16 % de píxeles con más gradiente, y nunca por debajo
     * del 22 % del máximo (así la textura de la piedra no entra). */
    size_t n = (size_t)w * h;
    float *sorted = malloc(sizeof(float) * n);
    memcpy(sorted, mag, sizeof(float) * n);
    qsort(sorted, n, sizeof(float), cmp_float);
    float thr = sorted[(size_t)(n * 0.84)];
    float mx = sorted[n - 1];
    free(sorted);
    if (thr < 0.22f * mx)
        thr = 0.22f * mx;

    uint8_t *m = calloc(n, 1);
    for (size_t i = 0; i < n; i++)
        m[i] = mag[i] > thr && mag[i] > 8.0f;
    free(mag);
    remove_small(m, w, h, (w * h) / 300 + 6);
    uint8_t *c = crop_mask(m, w, h, ow, oh);
    free(m);
    if (c && (*ow < 4 || *oh < 4)) {
        free(c);
        return NULL;
    }
    return c;
}

/* "g17" -> "G17", "aa15" -> "Aa15", "n33b" -> "N33B", "nl5a" -> "NL5A". */
int code_from_folder(const char *name, char *code)
{
    char tmp[CODE_LEN];
    size_t n = strlen(name);
    if (n == 0 || n >= CODE_LEN)
        return -1;
    for (size_t i = 0; i <= n; i++)
        tmp[i] = (char)toupper((unsigned char)name[i]);
    int i = 0;
    char pre[3] = {0};
    while (i < 2 && isalpha((unsigned char)tmp[i]) && !isdigit((unsigned char)tmp[i])) {
        pre[i] = tmp[i];
        i++;
    }
    if (i == 0 || !isdigit((unsigned char)tmp[i]))
        return -1;
    if (strcmp(pre, "AA") == 0)
        strcpy(pre, "Aa");
    else if (i == 2 && strcmp(pre, "NL") != 0 && strcmp(pre, "NU") != 0)
        return -1;
    int num = atoi(tmp + i);
    while (isdigit((unsigned char)tmp[i]))
        i++;
    if (!valid_code(tmp))   /* sólo letras y números */
        return -1;
    snprintf(code, CODE_LEN, "%s%d%s", pre, num, tmp + i);
    return valid_code(code) ? 0 : -1;
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static int is_image(const char *f)
{
    const char *d = strrchr(f, '.');
    if (!d)
        return 0;
    char e[8] = {0};
    for (int i = 0; i < 7 && d[i + 1]; i++)
        e[i] = (char)tolower((unsigned char)d[i + 1]);
    return !strcmp(e, "png") || !strcmp(e, "jpg") || !strcmp(e, "jpeg") ||
           !strcmp(e, "bmp") || !strcmp(e, "tif") || !strcmp(e, "tiff");
}

/* Recorre DIR/<código>/<imagen> llamando a cb para cada foto convertida a contorno.
 * 'index' es la posición de la foto dentro de su clase (orden alfabético). */
int dataset_walk(const char *dir, DatasetCallback cb, void *ud)
{
    DIR *d = opendir(dir);
    if (!d) {
        fprintf(stderr, "No se puede abrir el conjunto de datos %s\n", dir);
        return -1;
    }
    char **classes = NULL;
    int nc = 0, cap = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.')
            continue;
        if (nc == cap) {
            cap = cap ? cap * 2 : 64;
            classes = realloc(classes, sizeof(char *) * (size_t)cap);
        }
        classes[nc++] = strdup(e->d_name);
    }
    closedir(d);
    qsort(classes, (size_t)nc, sizeof(char *), cmp_str);

    int total = 0, bad = 0;
    for (int c = 0; c < nc; c++) {
        char code[CODE_LEN];
        if (code_from_folder(classes[c], code) != 0)
            continue;
        char sub[1200];
        snprintf(sub, sizeof sub, "%s/%s", dir, classes[c]);
        DIR *sd = opendir(sub);
        if (!sd)
            continue;
        char **files = NULL;
        int nf = 0, fcap = 0;
        while ((e = readdir(sd))) {
            if (e->d_name[0] == '.' || !is_image(e->d_name))
                continue;
            if (nf == fcap) {
                fcap = fcap ? fcap * 2 : 64;
                files = realloc(files, sizeof(char *) * (size_t)fcap);
            }
            files[nf++] = strdup(e->d_name);
        }
        closedir(sd);
        qsort(files, (size_t)nf, sizeof(char *), cmp_str);
        for (int i = 0; i < nf; i++) {
            char path[1500];
            snprintf(path, sizeof path, "%s/%s", sub, files[i]);
            Gray g;
            if (gray_load(path, &g) == 0) {
                int w, h;
                uint8_t *m = photo_to_mask(&g, &w, &h);
                gray_free(&g);
                if (m) {
                    cb(code, i, m, w, h, ud);
                    free(m);
                    total++;
                } else {
                    bad++;
                }
            } else {
                bad++;
            }
            free(files[i]);
        }
        free(files);
    }
    for (int c = 0; c < nc; c++)
        free(classes[c]);
    free(classes);
    if (bad)
        fprintf(stderr, "  %d imágenes no se pudieron usar\n", bad);
    return total;
}
