/*
 * Segmentación de la página:
 *   1. renglones por perfil de proyección horizontal;
 *   2. eliminación de subrayados (se recuerdan para marcar $r ... $b);
 *   3. componentes conexas con un radio de unión pequeño (une trazos rotos);
 *   4. cada componente que sobrevive a los filtros es un signo.
 */
#include "hieroocr.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

static int cmp_int(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}

static int find_lines(Page *p)
{
    int W = p->bin.w, H = p->bin.h;
    int *prof = calloc((size_t)H, sizeof(int));
    for (int y = 0; y < H; y++) {
        const uint8_t *row = p->bin.px + (size_t)y * W;
        for (int x = 0; x < W; x++)
            prof[y] += row[x];
    }
    int thr = W / 400;
    if (thr < 2)
        thr = 2;

    Band *raw = malloc(sizeof(Band) * (size_t)(H / 2 + 2));
    int nr = 0;
    for (int y = 0; y < H;) {
        if (prof[y] >= thr) {
            int s = y;
            while (y < H && prof[y] >= thr)
                y++;
            if (y - s >= 2)
                raw[nr++] = (Band){s, y - 1};
        } else {
            y++;
        }
    }
    free(prof);
    if (nr == 0) {
        free(raw);
        p->lines = NULL;
        p->nlines = 0;
        p->line_h = 0;
        return 0;
    }

    /* Altura de referencia: percentil 75 de las bandas brutas. */
    int *hs = malloc(sizeof(int) * (size_t)nr);
    for (int i = 0; i < nr; i++)
        hs[i] = raw[i].y1 - raw[i].y0 + 1;
    qsort(hs, (size_t)nr, sizeof(int), cmp_int);
    float ref = (float)hs[(nr * 3) / 4];
    free(hs);

    /* Une fragmentos de un mismo renglón (signos apilados con hueco entre medias). */
    Band *m = malloc(sizeof(Band) * (size_t)nr);
    int nm = 0;
    for (int i = 0; i < nr; i++) {
        if (nm > 0) {
            Band *c = &m[nm - 1];
            int gap = raw[i].y0 - c->y1 - 1;
            int tot = raw[i].y1 - c->y0 + 1;
            if (gap < 0.35f * ref && tot < 1.6f * ref) {
                c->y1 = raw[i].y1;
                continue;
            }
        }
        m[nm++] = raw[i];
    }
    free(raw);

    hs = malloc(sizeof(int) * (size_t)nm);
    int k = 0;
    for (int i = 0; i < nm; i++) {
        int h = m[i].y1 - m[i].y0 + 1;
        if (h >= 0.5f * ref)
            hs[k++] = h;
    }
    qsort(hs, (size_t)k, sizeof(int), cmp_int);
    float lh = k ? (float)hs[k / 2] : ref;
    free(hs);

    /* Descarta bandas muy bajas (números de página, manchas). */
    p->lines = malloc(sizeof(Band) * (size_t)nm);
    p->nlines = 0;
    for (int i = 0; i < nm; i++)
        if (m[i].y1 - m[i].y0 + 1 >= 0.45f * lh)
            p->lines[p->nlines++] = m[i];
    free(m);
    p->line_h = lh;
    return p->nlines;
}

static void add_underline(Page *p, int line, int x0, int x1, int y)
{
    for (int i = 0; i < p->nul; i++) {
        Underline *u = &p->ul[i];
        if (u->line != line || y > u->y1 + 4 || y < u->y0 - 4)
            continue;
        int ov = imin(x1, u->x1) - imax(x0, u->x0);
        if (ov > 0.5f * (x1 - x0)) {
            u->x0 = imin(u->x0, x0);
            u->x1 = imax(u->x1, x1);
            u->y0 = imin(u->y0, y);
            u->y1 = imax(u->y1, y);
            return;
        }
    }
    if (p->nul == p->capul) {
        p->capul = p->capul ? p->capul * 2 : 32;
        p->ul = realloc(p->ul, sizeof(Underline) * (size_t)p->capul);
    }
    p->ul[p->nul++] = (Underline){line, x0, x1, y, y};
}

/* Un trazo horizontal más largo que 1,2 renglones no es un signo: es un subrayado. */
static void remove_underlines(Page *p)
{
    int W = p->bin.w;
    int minrun = (int)(1.2f * p->line_h);
    if (minrun < 20)
        minrun = 20;
    for (int L = 0; L < p->nlines; L++) {
        for (int y = p->lines[L].y0; y <= p->lines[L].y1; y++) {
            uint8_t *row = p->bin.px + (size_t)y * W;
            for (int x = 0; x < W;) {
                if (!row[x]) {
                    x++;
                    continue;
                }
                int s = x;
                while (x < W && row[x])
                    x++;
                if (x - s >= minrun) {
                    /* borra el trazo y sus bordes (filas vecinas más cortas) */
                    for (int yy = y - 2; yy <= y + 2; yy++) {
                        if (yy < 0 || yy >= p->bin.h)
                            continue;
                        uint8_t *rr = p->bin.px + (size_t)yy * W;
                        if (yy == y) {
                            memset(rr + s, 0, (size_t)(x - s));
                            continue;
                        }
                        /* en las vecinas sólo se borran tramos horizontales largos */
                        for (int xx = s; xx < x;) {
                            if (!rr[xx]) { xx++; continue; }
                            int s2 = xx;
                            while (xx < x && rr[xx]) xx++;
                            if (xx - s2 >= minrun / 4)
                                memset(rr + s2, 0, (size_t)(xx - s2));
                        }
                    }
                    add_underline(p, L, s, x - 1, y);
                }
            }
        }
    }
}

static int uf_find(int *par, int x)
{
    while (par[x] != x) {
        par[x] = par[par[x]];
        x = par[x];
    }
    return x;
}

/* Componentes conexas: dos píxeles de tinta a distancia <= R (Chebyshev) quedan unidos. */
static void label_components(Page *p, int R, Rect **rects, int **areas)
{
    int W = p->bin.w, H = p->bin.h;
    size_t N = (size_t)W * H;
    const uint8_t *b = p->bin.px;
    int *par = malloc(N * sizeof(int));
    for (size_t i = 0; i < N; i++)
        par[i] = b[i] ? (int)i : -1;

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            size_t i = (size_t)y * W + x;
            if (!b[i])
                continue;
            for (int dy = 0; dy <= R; dy++) {
                int ny = y + dy;
                if (ny >= H)
                    break;
                for (int dx = -R; dx <= R; dx++) {
                    if (dy == 0 && dx <= 0)
                        continue;
                    int nx = x + dx;
                    if (nx < 0 || nx >= W)
                        continue;
                    size_t j = (size_t)ny * W + nx;
                    if (!b[j])
                        continue;
                    int ra = uf_find(par, (int)i), rb = uf_find(par, (int)j);
                    if (ra != rb) {
                        /* la raíz es siempre el índice menor del conjunto */
                        if (ra < rb) par[rb] = ra;
                        else         par[ra] = rb;
                    }
                }
            }
        }
    }

    int *lab = malloc(N * sizeof(int));
    int nc = 0;
    for (size_t i = 0; i < N; i++) {
        if (par[i] < 0) {
            lab[i] = -1;
            continue;
        }
        int r = uf_find(par, (int)i);
        lab[i] = (r == (int)i) ? nc++ : lab[r];
    }
    free(par);

    Rect *rc = malloc(sizeof(Rect) * (size_t)(nc ? nc : 1));
    int *ar = calloc((size_t)(nc ? nc : 1), sizeof(int));
    for (int c = 0; c < nc; c++)
        rc[c] = (Rect){W, H, -1, -1};
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int c = lab[(size_t)y * W + x];
            if (c < 0)
                continue;
            Rect *r = &rc[c];
            if (x < r->x0) r->x0 = x;
            if (y < r->y0) r->y0 = y;
            if (x > r->x1) r->x1 = x;
            if (y > r->y1) r->y1 = y;
            ar[c]++;
        }
    }
    p->label = lab;
    p->ncomp = nc;
    *rects = rc;
    *areas = ar;
}

typedef struct { int comp, line; float key; } GTmp;

static int cmp_gtmp(const void *a, const void *b)
{
    const GTmp *x = a, *y = b;
    if (x->line != y->line)
        return x->line - y->line;
    return (x->key > y->key) - (x->key < y->key);
}

static void build_glyphs(Page *p, const Rect *rc, const int *ar, int min_area)
{
    float lh = p->line_h;
    GTmp *tmp = malloc(sizeof(GTmp) * (size_t)(p->ncomp ? p->ncomp : 1));
    int nt = 0;
    for (int c = 0; c < p->ncomp; c++) {
        Rect r = rc[c];
        int w = r.x1 - r.x0 + 1, h = r.y1 - r.y0 + 1;
        if (ar[c] < min_area)
            continue;
        if (h > 1.7f * lh || w > 3.0f * lh)       /* marcas de lápiz, viñetas... */
            continue;
        if (w < 0.05f * lh && h < 0.05f * lh)     /* motas */
            continue;
        if (h <= 3 && h < 0.05f * lh && w > 4 * h)  /* restos de subrayado o de pauta */
            continue;
        int cy = (r.y0 + r.y1) / 2, L = -1;
        for (int l = 0; l < p->nlines; l++)
            if (cy >= p->lines[l].y0 && cy <= p->lines[l].y1) {
                L = l;
                break;
            }
        if (L < 0)
            continue;
        tmp[nt++] = (GTmp){c, L, (r.x0 + r.x1) * 0.5f};
    }
    qsort(tmp, (size_t)nt, sizeof(GTmp), cmp_gtmp);

    p->comp_glyph = malloc(sizeof(int) * (size_t)(p->ncomp ? p->ncomp : 1));
    for (int c = 0; c < p->ncomp; c++)
        p->comp_glyph[c] = -1;
    p->capg = nt ? nt : 1;
    p->g = calloc((size_t)p->capg, sizeof(Glyph));
    p->ng = nt;
    for (int i = 0; i < nt; i++) {
        Glyph *g = &p->g[i];
        g->r = rc[tmp[i].comp];
        g->line = tmp[i].line;
        g->alive = 1;
        g->score = 1.0f;
        p->comp_glyph[tmp[i].comp] = i;
    }
    free(tmp);
}

int page_segment(Page *p, const Options *o)
{
    binarize_otsu(&p->gray, &p->bin);
    if (find_lines(p) == 0) {
        fprintf(stderr, "%s: no se han encontrado renglones de texto\n", p->path);
        p->comp_glyph = NULL;
        p->g = NULL;
        p->ng = 0;
        return 0;
    }
    remove_underlines(p);

    int R = o->join > 0 ? o->join : (int)lroundf(p->line_h * 0.02f);
    if (R < 1)
        R = 1;
    int min_area = o->min_area > 0 ? o->min_area : (int)(p->line_h * p->line_h * 0.0008f);
    if (min_area < 4)
        min_area = 4;

    Rect *rc;
    int *ar;
    label_components(p, R, &rc, &ar);
    build_glyphs(p, rc, ar, min_area);
    free(rc);
    free(ar);

    for (int i = 0; i < p->ng; i++) {
        Glyph *g = &p->g[i];
        int cx = (g->r.x0 + g->r.x1) / 2;
        for (int u = 0; u < p->nul; u++)
            if (p->ul[u].line == g->line && cx >= p->ul[u].x0 && cx <= p->ul[u].x1)
                g->underlined = 1;
    }
    return p->ng;
}

void page_free(Page *p)
{
    gray_free(&p->gray);
    free(p->bin.px);
    free(p->label);
    free(p->comp_glyph);
    free(p->lines);
    free(p->ul);
    free(p->g);
    char path[sizeof p->path];
    memcpy(path, p->path, sizeof path);
    memset(p, 0, sizeof *p);
    memcpy(p->path, path, sizeof path);
}

int glyph_mask(const Page *p, int gi, uint8_t **mask, int *w, int *h)
{
    const Glyph *g = &p->g[gi];
    int W = p->bin.w;
    *w = g->r.x1 - g->r.x0 + 1;
    *h = g->r.y1 - g->r.y0 + 1;
    if (*w <= 0 || *h <= 0)
        return -1;
    *mask = calloc((size_t)(*w) * (*h), 1);
    if (!*mask)
        return -1;
    for (int y = 0; y < *h; y++)
        for (int x = 0; x < *w; x++) {
            int l = p->label[(size_t)(g->r.y0 + y) * W + g->r.x0 + x];
            if (l >= 0 && p->comp_glyph[l] == gi)
                (*mask)[(size_t)y * (*w) + x] = 1;
        }
    return 0;
}

/* Une el signo gi con el siguiente del mismo renglón (para corregir signos partidos). */
int glyph_merge_next(Page *p, int gi)
{
    Glyph *a = &p->g[gi];
    int best = -1, bx = 0;
    for (int j = 0; j < p->ng; j++) {
        Glyph *b = &p->g[j];
        if (j == gi || !b->alive || b->line != a->line || b->r.x0 < a->r.x0)
            continue;
        if (best < 0 || b->r.x0 < bx) {
            best = j;
            bx = b->r.x0;
        }
    }
    if (best < 0)
        return -1;
    Glyph *b = &p->g[best];
    a->r.x0 = imin(a->r.x0, b->r.x0);
    a->r.y0 = imin(a->r.y0, b->r.y0);
    a->r.x1 = imax(a->r.x1, b->r.x1);
    a->r.y1 = imax(a->r.y1, b->r.y1);
    a->underlined |= b->underlined;
    for (int c = 0; c < p->ncomp; c++)
        if (p->comp_glyph[c] == best)
            p->comp_glyph[c] = gi;
    b->alive = 0;
    return best;
}
