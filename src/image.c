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

/* Orientación EXIF (1-8) de un JPEG; 1 si no tiene o no es JPEG. */
static int exif_orientation(const char *path)
{
    SDL_RWops *rw = SDL_RWFromFile(path, "rb");
    if (!rw)
        return 1;
    uint8_t b[65536];
    size_t n = SDL_RWread(rw, b, 1, sizeof b);
    SDL_RWclose(rw);
    if (n < 4 || b[0] != 0xFF || b[1] != 0xD8)
        return 1;
    size_t i = 2;
    while (i + 4 < n && b[i] == 0xFF) {
        int marker = b[i + 1];
        size_t len = (size_t)b[i + 2] << 8 | b[i + 3];
        if (marker == 0xE1 && i + 10 < n && memcmp(b + i + 4, "Exif\0\0", 6) == 0) {
            const uint8_t *t = b + i + 10;
            size_t tn = n - (i + 10);
            if (len < 8 || tn < 8)
                return 1;
            int le = t[0] == 'I';
#define U16(p) (le ? (unsigned)((p)[0] | (p)[1] << 8) : (unsigned)((p)[0] << 8 | (p)[1]))
#define U32(p) (le ? (unsigned)((p)[0] | (p)[1] << 8 | (p)[2] << 16 | (unsigned)(p)[3] << 24) \
                   : (unsigned)((unsigned)(p)[0] << 24 | (p)[1] << 16 | (p)[2] << 8 | (p)[3]))
            size_t ifd = U32(t + 4);
            if (ifd + 2 > tn)
                return 1;
            unsigned cnt = U16(t + ifd);
            for (unsigned k = 0; k < cnt && ifd + 2 + 12 * (k + 1) <= tn; k++) {
                const uint8_t *e = t + ifd + 2 + 12 * k;
                if (U16(e) == 0x0112) {
                    unsigned v = U16(e + 8);
                    return v >= 1 && v <= 8 ? (int)v : 1;
                }
            }
#undef U16
#undef U32
            return 1;
        }
        if (marker == 0xDA)
            break;
        i += 2 + len;
    }
    return 1;
}

/* Endereza la imagen según la orientación EXIF (las fotos de los móviles). */
static void gray_orient(Gray *g, int o)
{
    if (o <= 1 || o > 8)
        return;
    int w = g->w, h = g->h, swap = o >= 5;
    int nw = swap ? h : w, nh = swap ? w : h;
    uint8_t *d = malloc((size_t)w * h);
    if (!d)
        return;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int X = x, Y = y;
            switch (o) {
            case 2: X = w - 1 - x; break;
            case 3: X = w - 1 - x; Y = h - 1 - y; break;
            case 4: Y = h - 1 - y; break;
            case 5: X = y; Y = x; break;
            case 6: X = h - 1 - y; Y = x; break;
            case 7: X = h - 1 - y; Y = w - 1 - x; break;
            case 8: X = y; Y = w - 1 - x; break;
            }
            d[(size_t)Y * nw + X] = g->px[(size_t)y * w + x];
        }
    free(g->px);
    g->px = d;
    g->w = nw;
    g->h = nh;
}

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
    gray_orient(g, exif_orientation(path));
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

/* Entrecomilla un argumento para system(): '...' en POSIX, "..." en cmd.exe. */
static void shell_quote(const char *in, char *out, size_t n)
{
    size_t k = 0;
#ifdef _WIN32
    out[k++] = '"';
    for (; *in && k + 4 < n; in++)
        if (*in != '"')
            out[k++] = *in;
    out[k++] = '"';
#else
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
#endif
    out[k] = 0;
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* Las imágenes .png de una carpeta, ordenadas por nombre. */
static int list_png(const char *dir, char ***files)
{
    DIR *d = opendir(dir);
    if (!d)
        return 0;
    int n = 0, cap = 0;
    char **f = NULL;
    struct dirent *e;
    while ((e = readdir(d))) {
        size_t l = strlen(e->d_name);
        if (l <= 4 || strcmp(e->d_name + l - 4, ".png") != 0)
            continue;
        if (n == cap) {
            cap = cap ? cap * 2 : 32;
            f = realloc(f, sizeof(char *) * (size_t)cap);
        }
        size_t len = strlen(dir) + l + 2;
        f[n] = malloc(len);
        snprintf(f[n], len, "%s/%s", dir, e->d_name);
        n++;
    }
    closedir(d);
    qsort(f, (size_t)n, sizeof(char *), cmp_str);
    *files = f;
    return n;
}

/* Carpeta temporal nueva (sin mkdtemp, que no existe en Windows). */
static int make_tmpdir(char tmpdir[512])
{
    const char *t = getenv("TMPDIR");
    if (!t || !*t) t = getenv("TEMP");
    if (!t || !*t) t = getenv("TMP");
    if (!t || !*t) t = "/tmp";
    static unsigned seq;
    for (int i = 0; i < 1000; i++) {
        snprintf(tmpdir, 512, "%s/hieroocr-%u-%u-%u", t, (unsigned)SDL_GetTicks(), (unsigned)rand() % 100000u, seq++);
        if (MKDIR(tmpdir) == 0)
            return 0;
    }
    tmpdir[0] = 0;
    return -1;
}

/*
 * PDFium (el motor de PDF de Chrome y del PdfRenderer de Android), cargado en
 * tiempo de ejecución si está junto al programa: pdfium.dll en Windows. Si no
 * está, se usa pdftoppm.
 */
typedef void *(*PF_LoadMem)(const void *, int, const char *);
typedef int (*PF_Count)(void *);
typedef void *(*PF_LoadPage)(void *, int);
typedef double (*PF_Size)(void *);
typedef void *(*PF_BmpCreate)(int, int, int);
typedef void (*PF_BmpFill)(void *, int, int, int, int, unsigned long);
typedef void (*PF_Render)(void *, void *, int, int, int, int, int, int);
typedef void *(*PF_BmpBuf)(void *);
typedef int (*PF_BmpStride)(void *);
typedef void (*PF_Free)(void *);
typedef void (*PF_Init)(void);

static struct {
    int tried, ok;
    PF_LoadMem load; PF_Count count; PF_LoadPage page; PF_Size width, height;
    PF_BmpCreate bcreate; PF_BmpFill bfill; PF_Render render; PF_BmpBuf bbuf; PF_BmpStride bstride;
    PF_Free bdestroy, closepage, closedoc;
} pf;

static int pdfium_load(void)
{
    if (pf.tried)
        return pf.ok;
    pf.tried = 1;
#ifdef _WIN32
    const char *names[] = {"pdfium.dll", NULL};
#else
    const char *names[] = {"libpdfium.so", NULL};
#endif
    void *lib = NULL;
    char *base = SDL_GetBasePath();
    for (int i = 0; names[i] && !lib; i++) {
        char p[1200];
        if (base) {
            snprintf(p, sizeof p, "%s%s", base, names[i]);
            lib = SDL_LoadObject(p);
        }
        if (!lib && getenv("HIEROOCR_PDFIUM"))
            lib = SDL_LoadObject(getenv("HIEROOCR_PDFIUM"));
#ifdef _WIN32
        if (!lib)
            lib = SDL_LoadObject(names[i]);
#endif
    }
    SDL_free(base);
    SDL_ClearError();
    if (!lib)
        return 0;
#define SYM(field, type, name) pf.field = (type)SDL_LoadFunction(lib, name); if (!pf.field) return 0
    PF_Init init = (PF_Init)SDL_LoadFunction(lib, "FPDF_InitLibrary");
    if (!init)
        return 0;
    SYM(load, PF_LoadMem, "FPDF_LoadMemDocument");
    SYM(count, PF_Count, "FPDF_GetPageCount");
    SYM(page, PF_LoadPage, "FPDF_LoadPage");
    SYM(width, PF_Size, "FPDF_GetPageWidth");
    SYM(height, PF_Size, "FPDF_GetPageHeight");
    SYM(bcreate, PF_BmpCreate, "FPDFBitmap_Create");
    SYM(bfill, PF_BmpFill, "FPDFBitmap_FillRect");
    SYM(render, PF_Render, "FPDF_RenderPageBitmap");
    SYM(bbuf, PF_BmpBuf, "FPDFBitmap_GetBuffer");
    SYM(bstride, PF_BmpStride, "FPDFBitmap_GetStride");
    SYM(bdestroy, PF_Free, "FPDFBitmap_Destroy");
    SYM(closepage, PF_Free, "FPDF_ClosePage");
    SYM(closedoc, PF_Free, "FPDF_CloseDocument");
#undef SYM
    init();
    pf.ok = 1;
    return 1;
}

static int pdfium_render(const char *pdf, int dpi, int (*ranges)[2], int nr, const char *tmpdir)
{
    /* el PDF entero en memoria: SDL_RWFromFile entiende rutas UTF-8 también en Windows */
    SDL_RWops *rw = SDL_RWFromFile(pdf, "rb");
    if (!rw) {
        fprintf(stderr, "No se puede abrir %s: %s\n", pdf, SDL_GetError());
        return -1;
    }
    Sint64 size = SDL_RWsize(rw);
    char *data = size > 0 ? malloc((size_t)size) : NULL;
    if (!data || SDL_RWread(rw, data, 1, (size_t)size) != (size_t)size) {
        SDL_RWclose(rw);
        free(data);
        return -1;
    }
    SDL_RWclose(rw);
    void *doc = pf.load(data, (int)size, NULL);
    if (!doc) {
        fprintf(stderr, "PDFium no puede abrir %s\n", pdf);
        free(data);
        return -1;
    }
    int np = pf.count(doc), done = 0;
    fprintf(stderr, "%s: %d páginas (PDFium)\n", pdf, np);
    for (int r = 0; r < nr; r++) {
        int a = ranges[r][0] > 0 ? ranges[r][0] : 1, b = ranges[r][1] > 0 ? ranges[r][1] : np;
        if (a > np)
            fprintf(stderr, "Aviso: el PDF sólo tiene %d páginas (se pidió la %d)\n", np, a);
        for (int pg = a; pg <= b && pg <= np; pg++) {
            void *page = pf.page(doc, pg - 1);
            if (!page)
                continue;
            int w = (int)(pf.width(page) * dpi / 72.0 + 0.5), h = (int)(pf.height(page) * dpi / 72.0 + 0.5);
            void *bmp = (w > 0 && h > 0 && (long)w * h < 200000000L) ? pf.bcreate(w, h, 0) : NULL;
            if (bmp) {
                pf.bfill(bmp, 0, 0, w, h, 0xFFFFFFFFul);
                pf.render(bmp, page, 0, 0, w, h, 0, 0x01 | 0x800);   /* anotaciones, modo impresión */
                SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(pf.bbuf(bmp), w, h, 32, pf.bstride(bmp),
                                                                    SDL_PIXELFORMAT_BGRX8888 == 0 ? 0 : SDL_PIXELFORMAT_XRGB8888);
                if (s) {
                    char out[700];
                    snprintf(out, sizeof out, "%s/p-%03d.png", tmpdir, pg);
                    fprintf(stderr, "Rasterizando la página %d a %d ppp...\n", pg, dpi);
                    if (IMG_SavePNG(s, out) == 0)
                        done++;
                    SDL_FreeSurface(s);
                }
                pf.bdestroy(bmp);
            }
            pf.closepage(page);
        }
    }
    pf.closedoc(doc);
    free(data);
    return done > 0 ? 0 : -1;
}

/* pdftoppm: junto al programa (poppler/pdftoppm) o en el PATH. */
static void pdftoppm_path(char *out, size_t n)
{
    char *base = SDL_GetBasePath();
    snprintf(out, n, "pdftoppm");
    if (base) {
        char p[1200];
#ifdef _WIN32
        snprintf(p, sizeof p, "%spoppler\\pdftoppm.exe", base);
#else
        snprintf(p, sizeof p, "%spoppler/pdftoppm", base);
#endif
        if (access(p, 0) == 0)
            shell_quote(p, out, n);
        SDL_free(base);
    }
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
    if (make_tmpdir(tmpdir) != 0) {
        perror("carpeta temporal");
        return -1;
    }

    int ok = 0;
    if (pdfium_load()) {
        ok = pdfium_render(pdf, dpi, ranges, nr, tmpdir) == 0;
    } else {
        char qpdf[2200], qprefix[1200], prefix[600], cmd[8192], fl[48], prog[1300];
        snprintf(prefix, sizeof prefix, "%s/p", tmpdir);
        shell_quote(pdf, qpdf, sizeof qpdf);
        shell_quote(prefix, qprefix, sizeof qprefix);
        pdftoppm_path(prog, sizeof prog);
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
#ifdef _WIN32
            /* cmd.exe quita las comillas exteriores: se envuelve todo en otras */
            snprintf(cmd, sizeof cmd, "\"%s -r %d %s-png %s %s\"", prog, dpi, fl, qpdf, qprefix);
#else
            snprintf(cmd, sizeof cmd, "%s -r %d %s-png %s %s", prog, dpi, fl, qpdf, qprefix);
#endif
            if (system(cmd) == 0)
                ok++;
            else
                fprintf(stderr, "Aviso: pdftoppm ha fallado con el rango %d-%d "
                                "(¿fuera del documento? ¿está instalado poppler-utils?)\n", f, l);
        }
    }

    char **fl = NULL;
    int n = list_png(tmpdir, &fl);
    if (!ok || n <= 0) {
        for (int i = 0; i < n; i++)
            free(fl[i]);
        free(fl);
        fprintf(stderr, "El PDF no ha producido ninguna página.\n");
        tmpdir_remove(tmpdir);
        tmpdir[0] = 0;
        return -1;
    }
    *files = fl;
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
