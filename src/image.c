/*
 * Carga de imágenes (JPG, PNG, ... vía SDL2_image), rasterizado de PDF
 * (vía pdftoppm de poppler-utils) y binarización por el método de Otsu.
 */
#include "hieroocr.h"

#include <SDL.h>
#include <SDL_image.h>
#include <dirent.h>
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

int pdf_render(const char *pdf, int dpi, int page, char tmpdir[512], char ***files, int *nfiles)
{
    const char *t = getenv("TMPDIR");
    if (!t || !*t)
        t = "/tmp";
    snprintf(tmpdir, 512, "%s/hieroocr-XXXXXX", t);
    if (!mkdtemp(tmpdir)) {
        perror("mkdtemp");
        tmpdir[0] = 0;
        return -1;
    }

    char qpdf[2200], qprefix[1200], prefix[600], cmd[4096];
    snprintf(prefix, sizeof prefix, "%s/p", tmpdir);
    shell_quote(pdf, qpdf, sizeof qpdf);
    shell_quote(prefix, qprefix, sizeof qprefix);
    if (page > 0)
        snprintf(cmd, sizeof cmd, "pdftoppm -r %d -f %d -l %d -png %s %s",
                 dpi, page, page, qpdf, qprefix);
    else
        snprintf(cmd, sizeof cmd, "pdftoppm -r %d -png %s %s", dpi, qpdf, qprefix);

    fprintf(stderr, "Rasterizando %s a %d ppp...\n", pdf, dpi);
    if (system(cmd) != 0) {
        fprintf(stderr, "pdftoppm ha fallado (¿está instalado poppler-utils?)\n");
        tmpdir_remove(tmpdir);
        tmpdir[0] = 0;
        return -1;
    }

    struct dirent **nl;
    int n = scandir(tmpdir, &nl, is_png, alphasort);
    if (n <= 0) {
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
