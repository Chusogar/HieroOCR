/*
 * Carga de imágenes (JPG, PNG, ... vía SDL2_image), rasterizado de PDF
 * (vía pdftoppm de poppler-utils) y binarización por el método de Otsu.
 */
#include "hieroocr.h"

#include <SDL.h>
#include <SDL_image.h>
#include <dirent.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int gray_load(const char *path, Gray *g)
{
    SDL_Surface *s = IMG_Load(path);
    if (!s) {
        fprintf(stderr, "No se puede abrir %s: %s\n", path, IMG_GetError());
        return -1;
    }
    SDL_Surface *c = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(s);
    if (!c) {
        fprintf(stderr, "Conversión de %s fallida: %s\n", path, SDL_GetError());
        return -1;
    }
    g->w = c->w;
    g->h = c->h;
    g->px = malloc((size_t)g->w * g->h);
    if (!g->px) {
        SDL_FreeSurface(c);
        return -1;
    }
    SDL_LockSurface(c);
    for (int y = 0; y < g->h; y++) {
        const uint8_t *row = (const uint8_t *)c->pixels + (size_t)y * c->pitch;
        for (int x = 0; x < g->w; x++) {
            const uint8_t *q = row + 4 * x;   /* RGBA32: R,G,B,A en memoria */
            int lum = (299 * q[0] + 587 * q[1] + 114 * q[2]) / 1000;
            lum = (lum * q[3] + 255 * (255 - q[3])) / 255;   /* transparente = blanco */
            g->px[(size_t)y * g->w + x] = (uint8_t)lum;
        }
    }
    SDL_UnlockSurface(c);
    SDL_FreeSurface(c);
    return 0;
}

void gray_free(Gray *g)
{
    free(g->px);
    g->px = NULL;
    g->w = g->h = 0;
}

void binarize_otsu(const Gray *g, Bin *b)
{
    size_t n = (size_t)g->w * g->h;
    double hist[256] = {0};
    for (size_t i = 0; i < n; i++)
        hist[g->px[i]]++;

    double sum = 0;
    for (int t = 0; t < 256; t++)
        sum += t * hist[t];

    double sumB = 0, wB = 0, best = -1;
    int thr = 127;
    for (int t = 0; t < 256; t++) {
        wB += hist[t];
        if (wB == 0)
            continue;
        double wF = (double)n - wB;
        if (wF == 0)
            break;
        sumB += t * hist[t];
        double mB = sumB / wB, mF = (sum - sumB) / wF;
        double v = wB * wF * (mB - mF) * (mB - mF);
        if (v > best) {
            best = v;
            thr = t;
        }
    }
    /* Páginas casi vacías pueden dar umbrales absurdos. */
    if (thr < 60)  thr = 60;
    if (thr > 200) thr = 200;

    b->w = g->w;
    b->h = g->h;
    b->px = malloc(n);
    for (size_t i = 0; i < n; i++)
        b->px[i] = g->px[i] <= thr;
}

/*
 * Corrige la iluminación desigual de una foto (sombras, viñeteado, luz
 * lateral): estima el brillo del papel por bloques (percentil 90, que ignora
 * la tinta), lo suaviza, lo interpola y divide cada píxel por él. Después de
 * esto el papel queda casi blanco en toda la imagen y Otsu funciona igual que
 * con una página escaneada.
 */
void gray_flatten(Gray *g)
{
    int w = g->w, h = g->h;
    int B = (w > h ? w : h) / 48;
    if (B < 16)
        B = 16;
    int gw = (w + B - 1) / B, gh = (h + B - 1) / B;
    float *bg = malloc(sizeof(float) * (size_t)gw * gh);
    float *tmp = malloc(sizeof(float) * (size_t)gw * gh);
    if (!bg || !tmp) {
        free(bg);
        free(tmp);
        return;
    }
    for (int by = 0; by < gh; by++)
        for (int bx = 0; bx < gw; bx++) {
            int hist[256] = {0}, n = 0;
            for (int y = by * B; y < (by + 1) * B && y < h; y++)
                for (int x = bx * B; x < (bx + 1) * B && x < w; x++) {
                    hist[g->px[(size_t)y * w + x]]++;
                    n++;
                }
            int target = n - n / 10, acc = 0, v = 0;
            for (v = 0; v < 255; v++) {
                acc += hist[v];
                if (acc >= target)
                    break;
            }
            bg[by * gw + bx] = (float)v;
        }
    /* Bloques casi sin papel (dibujos, bordes oscuros): tomar el máximo de los vecinos. */
    for (int k = 0; k < 2; k++) {
        for (int by = 0; by < gh; by++)
            for (int bx = 0; bx < gw; bx++) {
                float m = 0;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        int x = bx + dx, y = by + dy;
                        if (x >= 0 && y >= 0 && x < gw && y < gh && bg[y * gw + x] > m)
                            m = bg[y * gw + x];
                    }
                float c = bg[by * gw + bx];
                tmp[by * gw + bx] = c < 0.6f * m ? m : c;
            }
        memcpy(bg, tmp, sizeof(float) * (size_t)gw * gh);
    }
    for (int k = 0; k < 2; k++) {   /* suavizado 3x3 */
        for (int by = 0; by < gh; by++)
            for (int bx = 0; bx < gw; bx++) {
                float s = 0;
                int n = 0;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        int x = bx + dx, y = by + dy;
                        if (x >= 0 && y >= 0 && x < gw && y < gh) {
                            s += bg[y * gw + x];
                            n++;
                        }
                    }
                tmp[by * gw + bx] = s / n;
            }
        memcpy(bg, tmp, sizeof(float) * (size_t)gw * gh);
    }
    for (int y = 0; y < h; y++) {
        float fy = (y + 0.5f) / B - 0.5f;
        int y0 = (int)floorf(fy);
        float ty = fy - y0;
        int ya = y0 < 0 ? 0 : y0, yb = y0 + 1 >= gh ? gh - 1 : y0 + 1;
        if (ya >= gh) ya = gh - 1;
        for (int x = 0; x < w; x++) {
            float fx = (x + 0.5f) / B - 0.5f;
            int x0 = (int)floorf(fx);
            float tx = fx - x0;
            int xa = x0 < 0 ? 0 : x0, xb = x0 + 1 >= gw ? gw - 1 : x0 + 1;
            if (xa >= gw) xa = gw - 1;
            float b = (1 - ty) * ((1 - tx) * bg[ya * gw + xa] + tx * bg[ya * gw + xb]) +
                      ty * ((1 - tx) * bg[yb * gw + xa] + tx * bg[yb * gw + xb]);
            if (b < 24)
                b = 24;
            float v = g->px[(size_t)y * w + x] * 255.0f / b;
            g->px[(size_t)y * w + x] = (uint8_t)(v > 255 ? 255 : v);
        }
    }
    free(bg);
    free(tmp);
}

/* Entrecomilla para el shell: 'abc' -> 'abc', con ' escapadas. */
static void shell_quote(const char *in, char *out, size_t n)
{
    size_t k = 0;
    out[k++] = '\'';
    for (; *in && k + 6 < n; in++) {
        if (*in == '\'') {
            memcpy(out + k, "'\\''", 4);
            k += 4;
        } else {
            out[k++] = *in;
        }
    }
    out[k++] = '\'';
    out[k] = 0;
}

static int is_png(const struct dirent *d)
{
    size_t l = strlen(d->d_name);
    return l > 4 && strcmp(d->d_name + l - 4, ".png") == 0;
}

/*
 * Analiza una especificación de páginas: "71", "71-92", "71-" (hasta el final),
 * "-10" (desde el principio) o listas separadas por comas: "3,5,7-9".
 * Devuelve el número de rangos (0 si la especificación no es válida).
 * Un extremo 0 significa "sin límite" (principio o final del documento).
 */
int parse_page_ranges(const char *spec, int (*ranges)[2], int max)
{
    int n = 0;
    const char *s = spec;
    while (*s) {
        while (*s == ' ' || *s == ',')
            s++;
        if (!*s)
            break;
        if (n >= max)
            return 0;
        char *end;
        long a = 0, b = 0;
        if (*s == '-') {                      /* "-N" */
            a = 1;
            b = strtol(s + 1, &end, 10);
            if (end == s + 1 || b < 1)
                return 0;
        } else {
            a = strtol(s, &end, 10);
            if (end == s || a < 1)
                return 0;
            if (*end == '-') {
                const char *t = end + 1;
                if (*t == 0 || *t == ',' || *t == ' ') {
                    b = 0;                    /* "N-": hasta el final */
                    end = (char *)t;
                } else {
                    b = strtol(t, &end, 10);
                    if (end == t || b < a)
                        return 0;
                }
            } else {
                b = a;
            }
        }
        if (*end && *end != ',' && *end != ' ')
            return 0;
        ranges[n][0] = (int)a;
        ranges[n][1] = (int)b;
        n++;
        s = end;
    }
    return n;
}

/* pdftoppm nombra las páginas "p-071.png": devuelve 71 (0 si no hay número). */
int page_number_from_path(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot)
        return 0;
    const char *d = dot;
    while (d > path && d[-1] >= '0' && d[-1] <= '9')
        d--;
    if (d == dot || d == path || d[-1] != '-')
        return 0;
    return atoi(d);
}

int pdf_render(const char *pdf, int dpi, const char *pages, char tmpdir[512], char ***files, int *nfiles)
{
    int ranges[64][2];
    int nr = 1;
    ranges[0][0] = 0;
    ranges[0][1] = 0;
    if (pages && *pages) {
        nr = parse_page_ranges(pages, ranges, 64);
        if (nr == 0) {
            fprintf(stderr, "Rango de páginas no válido: «%s» (ejemplos: 71, 71-92, 71-, 3,5,7-9)\n", pages);
            tmpdir[0] = 0;
            return -1;
        }
    }

    const char *t = getenv("TMPDIR");
    if (!t || !*t)
        t = "/tmp";
    snprintf(tmpdir, 512, "%s/hieroocr-XXXXXX", t);
    if (!mkdtemp(tmpdir)) {
        perror("mkdtemp");
        tmpdir[0] = 0;
        return -1;
    }

    char qpdf[2200], qprefix[1200], prefix[600], cmd[4096], fl[48];
    snprintf(prefix, sizeof prefix, "%s/p", tmpdir);
    shell_quote(pdf, qpdf, sizeof qpdf);
    shell_quote(prefix, qprefix, sizeof qprefix);

    int ok = 0;
    for (int i = 0; i < nr; i++) {
        int f = ranges[i][0], l = ranges[i][1];
        fl[0] = 0;
        if (f > 0 && l > 0)
            snprintf(fl, sizeof fl, "-f %d -l %d ", f, l);
        else if (f > 0)
            snprintf(fl, sizeof fl, "-f %d ", f);
        if (f > 0 && l == f)
            fprintf(stderr, "Rasterizando la página %d de %s a %d ppp...\n", f, pdf, dpi);
        else if (f > 0 && l > 0)
            fprintf(stderr, "Rasterizando páginas %d-%d de %s a %d ppp...\n", f, l, pdf, dpi);
        else if (f > 0)
            fprintf(stderr, "Rasterizando desde la página %d de %s a %d ppp...\n", f, pdf, dpi);
        else
            fprintf(stderr, "Rasterizando %s entero a %d ppp...\n", pdf, dpi);
        snprintf(cmd, sizeof cmd, "pdftoppm -r %d %s-png %s %s", dpi, fl, qpdf, qprefix);
        if (system(cmd) == 0)
            ok++;
        else
            fprintf(stderr, "Aviso: pdftoppm ha fallado con el rango %d-%d "
                            "(¿fuera del documento? ¿está instalado poppler-utils?)\n", f, l);
    }

    struct dirent **nl;
    int n = scandir(tmpdir, &nl, is_png, alphasort);
    if (!ok || n <= 0) {
        if (n > 0) {
            for (int i = 0; i < n; i++)
                free(nl[i]);
            free(nl);
        }
        fprintf(stderr, "El PDF no ha producido ninguna página.\n");
        tmpdir_remove(tmpdir);
        tmpdir[0] = 0;
        return -1;
    }
    *files = malloc((size_t)n * sizeof(char *));
    for (int i = 0; i < n; i++) {
        size_t len = strlen(tmpdir) + strlen(nl[i]->d_name) + 2;
        (*files)[i] = malloc(len);
        snprintf((*files)[i], len, "%s/%s", tmpdir, nl[i]->d_name);
        free(nl[i]);
    }
    free(nl);
    *nfiles = n;
    fprintf(stderr, "%d página(s) preparadas.\n", n);
    return 0;
}

void tmpdir_remove(const char *dir)
{
    DIR *d = opendir(dir);
    if (!d)
        return;
    struct dirent *e;
    char p[1200];
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.')
            continue;
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        unlink(p);
    }
    closedir(d);
    rmdir(dir);
}
