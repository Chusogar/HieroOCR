/*
 * Generación del texto en Manuel de Codage.
 *
 * Dentro de un renglón los signos se agrupan en "columnas" (cuadrados): los que
 * se solapan horizontalmente van apilados con ':' y, dentro de un mismo piso,
 * los que se solapan verticalmente van juntos con '*'. Las columnas se separan
 * con '-', cada renglón termina en '!' y cada página en '!!'.
 * Los tramos subrayados se envuelven en $r ... $b.
 */
#include "hieroocr.h"

#include <stdlib.h>
#include <string.h>

void str_puts(Str *s, const char *t)
{
    size_t l = strlen(t);
    if (s->n + l + 1 > s->cap) {
        size_t c = s->cap ? s->cap * 2 : 1024;
        while (c < s->n + l + 1)
            c *= 2;
        s->s = realloc(s->s, c);
        s->cap = c;
    }
    memcpy(s->s + s->n, t, l + 1);
    s->n += l;
}

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

typedef struct { int idx; float key; } Item;

static int cmp_item(const void *a, const void *b)
{
    const Item *x = a, *y = b;
    return (x->key > y->key) - (x->key < y->key);
}

static const Page *g_page;   /* para los comparadores de qsort */
static int g_rtl;

static int cmp_y(const void *a, const void *b)
{
    int ya = g_page->g[*(const int *)a].r.y0, yb = g_page->g[*(const int *)b].r.y0;
    return (ya > yb) - (ya < yb);
}

static int cmp_x(const void *a, const void *b)
{
    int xa = g_page->g[*(const int *)a].r.x0, xb = g_page->g[*(const int *)b].r.x0;
    int r = (xa > xb) - (xa < xb);
    return g_rtl ? -r : r;
}

void line_to_mdc(const Page *p, int line, const Options *o, Str *out)
{
    Item *it = malloc(sizeof(Item) * (size_t)(p->ng ? p->ng : 1));
    int n = 0;
    for (int i = 0; i < p->ng; i++) {
        const Glyph *g = &p->g[i];
        if (g->alive && g->line == line)
            it[n++] = (Item){i, (g->r.x0 + g->r.x1) * 0.5f * (o->rtl ? -1.0f : 1.0f)};
    }
    if (n == 0) {
        free(it);
        return;
    }
    qsort(it, (size_t)n, sizeof(Item), cmp_item);

    /* Agrupa en columnas por solape horizontal. */
    int *col_of = malloc(sizeof(int) * (size_t)n);
    int ncol = 0, cx0 = 0, cx1 = 0;
    for (int k = 0; k < n; k++) {
        Rect r = p->g[it[k].idx].r;
        if (ncol > 0) {
            int ov = imin(r.x1, cx1) - imax(r.x0, cx0) + 1;
            int mw = imin(r.x1 - r.x0 + 1, cx1 - cx0 + 1);
            if (ov > 0.45f * mw) {
                col_of[k] = ncol - 1;
                cx0 = imin(cx0, r.x0);
                cx1 = imax(cx1, r.x1);
                continue;
            }
        }
        col_of[k] = ncol++;
        cx0 = r.x0;
        cx1 = r.x1;
    }

    g_page = p;
    g_rtl = o->rtl;
    int *mem = malloc(sizeof(int) * (size_t)n);
    int in_r = 0;
    for (int c = 0; c < ncol; c++) {
        int m = 0;
        for (int k = 0; k < n; k++)
            if (col_of[k] == c)
                mem[m++] = it[k].idx;
        qsort(mem, (size_t)m, sizeof(int), cmp_y);

        Str col = {0};
        int nul = 0, unc = 0, first = 1;
        for (int k = 0; k < m;) {
            int rs = k;
            int ry0 = p->g[mem[k]].r.y0, ry1 = p->g[mem[k]].r.y1;
            k++;
            while (k < m) {
                Rect r = p->g[mem[k]].r;
                int ov = imin(r.y1, ry1) - imax(r.y0, ry0) + 1;
                int mh = imin(r.y1 - r.y0 + 1, ry1 - ry0 + 1);
                if (ov <= 0.3f * mh)
                    break;
                ry0 = imin(ry0, r.y0);
                ry1 = imax(ry1, r.y1);
                k++;
            }
            qsort(mem + rs, (size_t)(k - rs), sizeof(int), cmp_x);
            if (!first)
                str_puts(&col, ":");
            first = 0;
            for (int j = rs; j < k; j++) {
                const Glyph *g = &p->g[mem[j]];
                if (j > rs)
                    str_puts(&col, "*");
                str_puts(&col, g->code[0] ? g->code : "?");
                nul += g->underlined;
                if (!g->manual && g->score > o->uncertain_thr)
                    unc++;
            }
        }
        int ul = nul * 2 > m;
        if (ul && !in_r) {
            str_puts(out, "$r-");
            in_r = 1;
        } else if (!ul && in_r) {
            str_puts(out, "$b-");
            in_r = 0;
        }
        str_puts(out, col.s ? col.s : "");
        str_puts(out, "-");
        free(col.s);
        if (o->mark_uncertain && unc)
            str_puts(out, "++?+s-");
    }
    if (in_r)
        str_puts(out, "$b-");
    free(mem);
    free(col_of);
    free(it);
}

void page_to_mdc(const Page *p, const Options *o, Str *out, int last_page)
{
    (void)last_page;
    Str pg = {0};
    for (int L = 0; L < p->nlines; L++) {
        size_t before = pg.n;
        line_to_mdc(p, L, o, &pg);
        if (pg.n > before)
            str_puts(&pg, "!\n");
    }
    if (pg.n >= 2 && pg.s[pg.n - 2] == '!' && pg.s[pg.n - 1] == '\n') {
        pg.s[--pg.n] = 0;          /* "!\n" -> "!!\n" al final de la página */
        str_puts(&pg, "!\n");
    }
    if (pg.s)
        str_puts(out, pg.s);
    free(pg.s);
}
