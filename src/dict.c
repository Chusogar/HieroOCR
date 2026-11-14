/*
 * Diccionario y corpus de OpenGlyph (ver OpenGlyph/ y OpenGlyph/exportar.py).
 *
 *  - Búsqueda de un signo o de una combinación de signos (códigos Gardiner o
 *    fonéticos de MdC, '?' = cualquier signo) en el diccionario y en el corpus,
 *    y búsqueda por transliteración o traducción.
 *  - Lectura de un renglón reconocido: se parte en palabras del diccionario
 *    (programación dinámica que maximiza los signos cubiertos por palabras
 *    largas y frecuentes) y se busca el renglón del corpus más parecido.
 *  - Modelo de lengua: bigramas de signos del corpus, con los que se reordenan
 *    los candidatos del OCR de cada renglón (Viterbi).
 */
#include "hieroocr.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define MAXTOK 4096

/* ---------- códigos de signo -> enteros ---------- */

#define HSIZE 8192
static char  *g_codes[HSIZE];          /* id -> código */
static int    g_ncodes;
static int    g_hash[HSIZE];           /* posición -> id + 1 */

static unsigned hstr(const char *s)
{
    unsigned h = 2166136261u;
    while (*s)
        h = (h ^ (unsigned char)*s++) * 16777619u;
    return h;
}

int sign_id_find(const char *code)
{
    unsigned h = hstr(code) & (HSIZE - 1);
    while (g_hash[h]) {
        if (strcmp(g_codes[g_hash[h] - 1], code) == 0)
            return g_hash[h] - 1;
        h = (h + 1) & (HSIZE - 1);
    }
    return -1;
}

static int sign_id(const char *code)
{
    int id = sign_id_find(code);
    if (id >= 0 || g_ncodes >= HSIZE / 2)
        return id;
    unsigned h = hstr(code) & (HSIZE - 1);
    while (g_hash[h])
        h = (h + 1) & (HSIZE - 1);
    g_codes[g_ncodes] = strdup(code);
    g_hash[h] = ++g_ncodes;
    return g_ncodes - 1;
}

const char *sign_code(int id) { return id >= 0 && id < g_ncodes ? g_codes[id] : "?"; }

/* ---------- datos ---------- */

static DictEntry  *g_dic;
static int         g_ndic;
static CorpusLine *g_cor;
static int         g_ncor;
static char      **g_text_name, **g_text_file;
static int         g_ntext;
static char      **g_fon_key, **g_fon_val;   /* código fonético MdC -> Gardiner */
static int         g_nfon;

/* bigramas para el modelo de lengua */
#define BSIZE (1 << 20)
static unsigned   *g_bkey;
static int        *g_bcnt;
static int        *g_uni;
static long        g_ntok;
static int         g_lm_ready;

static void *grow(void *p, int n, int *cap, size_t sz)
{
    if (n < *cap)
        return p;
    *cap = *cap ? *cap * 2 : 1024;
    return realloc(p, sz * (size_t)*cap);
}

/* Parte una línea TSV en campos (modifica la línea). */
static int split_tab(char *line, char **f, int max)
{
    int n = 0;
    line[strcspn(line, "\r\n")] = 0;
    char *s = line;
    while (n < max) {
        f[n++] = s;
        char *t = strchr(s, '\t');
        if (!t)
            break;
        *t = 0;
        s = t + 1;
    }
    for (int i = n; i < max; i++)
        f[i] = "";
    return n;
}

static int parse_ids(const char *s, int **out)
{
    int tmp[MAXTOK], n = 0;
    char buf[CODE_LEN + 8];
    while (*s && n < MAXTOK) {
        while (*s == ' ')
            s++;
        int k = 0;
        while (*s && *s != ' ' && k < (int)sizeof buf - 1)
            buf[k++] = *s++;
        buf[k] = 0;
        if (k)
            tmp[n++] = sign_id(buf);
        while (*s && *s != ' ')
            s++;
    }
    *out = malloc(sizeof(int) * (size_t)(n ? n : 1));
    memcpy(*out, tmp, sizeof(int) * (size_t)n);
    return n;
}

static FILE *open_data(const char *dir, const char *name)
{
    char p[1024];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    return fopen(p, "r");
}

/* tabla de secuencias del diccionario -> mejor entrada (para la lectura) */
#define SSIZE (1 << 16)
static int *g_seq;   /* posición -> entrada + 1 */

static unsigned hseq(const int *s, int n)
{
    unsigned h = 2166136261u;
    for (int i = 0; i < n; i++)
        h = (h ^ (unsigned)s[i]) * 16777619u;
    return h;
}

static int entry_better(const DictEntry *a, const DictEntry *b)
{
    int ta = a->trad[0] != 0, tb = b->trad[0] != 0;
    if (ta != tb)
        return ta > tb;
    if (a->is_sign != b->is_sign)
        return a->is_sign < b->is_sign;
    return a->freq > b->freq;
}

static int seq_find(const int *s, int n)
{
    unsigned h = hseq(s, n) & (SSIZE - 1);
    while (g_seq[h]) {
        const DictEntry *e = &g_dic[g_seq[h] - 1];
        if (e->n == n && memcmp(e->s, s, sizeof(int) * (size_t)n) == 0)
            return g_seq[h] - 1;
        h = (h + 1) & (SSIZE - 1);
    }
    return -1;
}

static void seq_add(int idx)
{
    const DictEntry *e = &g_dic[idx];
    unsigned h = hseq(e->s, e->n) & (SSIZE - 1);
    while (g_seq[h]) {
        DictEntry *o = &g_dic[g_seq[h] - 1];
        if (o->n == e->n && memcmp(o->s, e->s, sizeof(int) * (size_t)e->n) == 0) {
            if (entry_better(e, o))
                g_seq[h] = idx + 1;
            return;
        }
        h = (h + 1) & (SSIZE - 1);
    }
    g_seq[h] = idx + 1;
}

static void bigram_add(int a, int b)
{
    unsigned key = ((unsigned)(a + 1) << 16) | (unsigned)(b + 1);
    unsigned h = (key * 2654435761u) & (BSIZE - 1);
    while (g_bkey[h] && g_bkey[h] != key)
        h = (h + 1) & (BSIZE - 1);
    g_bkey[h] = key;
    g_bcnt[h]++;
}

static int bigram(int a, int b)
{
    unsigned key = ((unsigned)(a + 1) << 16) | (unsigned)(b + 1);
    unsigned h = (key * 2654435761u) & (BSIZE - 1);
    while (g_bkey[h]) {
        if (g_bkey[h] == key)
            return g_bcnt[h];
        h = (h + 1) & (BSIZE - 1);
    }
    return 0;
}

int dict_loaded(void) { return g_ndic > 0 || g_ncor > 0; }
int dict_count(void) { return g_ndic; }
int corpus_count(void) { return g_ncor; }
const DictEntry *dict_entry(int i) { return &g_dic[i]; }
const CorpusLine *corpus_line(int i) { return &g_cor[i]; }
const char *corpus_text_name(int t) { return t >= 0 && t < g_ntext ? g_text_name[t] : "?"; }

/* MdC -> transliteración con los signos habituales (ȝ ꞽ ʿ ḥ ḫ ẖ š ḳ ṯ ḏ). */
void translit_unicode(const char *mdc, char *out, size_t n)
{
    size_t k = 0;
    for (const char *s = mdc; *s && k + 4 < n; s++) {
        const char *r = NULL;
        switch (*s) {
        case 'A': r = "ȝ"; break;
        case 'a': r = "ʿ"; break;
        case 'H': r = "ḥ"; break;
        case 'x': r = "ḫ"; break;
        case 'X': r = "ẖ"; break;
        case 'S': r = "š"; break;
        case 'q': r = "ḳ"; break;
        case 'T': r = "ṯ"; break;
        case 'D': r = "ḏ"; break;
        case 'i': r = "ỉ"; break;
        default: break;
        }
        if (r) {
            size_t l = strlen(r);
            if (k + l >= n)
                break;
            memcpy(out + k, r, l);
            k += l;
        } else {
            out[k++] = *s;
        }
    }
    out[k] = 0;
}

int dict_load(const char *dir, const char *exclude)
{
    char line[16384];
    char *f[8];
    FILE *fp;

    if ((fp = open_data(dir, "mdc_fonetico.tsv"))) {
        int cap = 0;
        while (fgets(line, sizeof line, fp)) {
            if (line[0] == '#' || split_tab(line, f, 2) < 2)
                continue;
            g_fon_key = grow(g_fon_key, g_nfon, &cap, sizeof(char *));
            g_fon_val = realloc(g_fon_val, sizeof(char *) * (size_t)cap);
            g_fon_key[g_nfon] = strdup(f[0]);
            g_fon_val[g_nfon] = strdup(f[1]);
            g_nfon++;
        }
        fclose(fp);
    }

    if ((fp = open_data(dir, "openglyph_textos.tsv"))) {
        int cap = 0;
        while (fgets(line, sizeof line, fp)) {
            if (line[0] == '#' || split_tab(line, f, 4) < 3)
                continue;
            int id = atoi(f[0]);
            while (g_ntext <= id) {
                g_text_name = grow(g_text_name, g_ntext, &cap, sizeof(char *));
                g_text_file = realloc(g_text_file, sizeof(char *) * (size_t)cap);
                g_text_name[g_ntext] = strdup(f[2]);
                g_text_file[g_ntext] = strdup(f[1]);
                g_ntext++;
            }
        }
        fclose(fp);
    }

    if ((fp = open_data(dir, "openglyph_corpus.tsv"))) {
        int cap = 0;
        while (fgets(line, sizeof line, fp)) {
            if (line[0] == '#' || split_tab(line, f, 6) < 3)
                continue;
            g_cor = grow(g_cor, g_ncor, &cap, sizeof(CorpusLine));
            CorpusLine *c = &g_cor[g_ncor];
            c->text = atoi(f[0]);
            c->line = atoi(f[1]);
            c->n = parse_ids(f[2], &c->s);
            c->mdc = strdup(f[3]);
            c->tr = strdup(f[4]);
            c->trad = strdup(f[5]);
            g_ncor++;
        }
        fclose(fp);
    }

    if ((fp = open_data(dir, "openglyph_diccionario.tsv"))) {
        int cap = 0;
        while (fgets(line, sizeof line, fp)) {
            if (line[0] == '#' || split_tab(line, f, 5) < 3)
                continue;
            g_dic = grow(g_dic, g_ndic, &cap, sizeof(DictEntry));
            DictEntry *e = &g_dic[g_ndic];
            e->n = parse_ids(f[0], &e->s);
            if (e->n == 0) {
                free(e->s);
                continue;
            }
            e->tr = strdup(f[1]);
            char u[512];
            translit_unicode(f[1], u, sizeof u);
            e->tr_u = strdup(u);
            e->trad = strdup(f[2]);
            e->is_sign = strcmp(f[3], "signo") == 0;
            e->freq = atoi(f[4]);
            g_ndic++;
        }
        fclose(fp);
        g_seq = calloc(SSIZE, sizeof(int));
        for (int i = 0; i < g_ndic; i++)
            if (g_dic[i].n <= 12)
                seq_add(i);
    }

    /* bigramas del corpus (sin los textos excluidos, para evaluar sin trampas) */
    if (g_ncor > 0) {
        g_bkey = calloc(BSIZE, sizeof(unsigned));
        g_bcnt = calloc(BSIZE, sizeof(int));
        g_uni = calloc(HSIZE, sizeof(int));
        int skipped = 0;
        for (int i = 0; i < g_ncor; i++) {
            CorpusLine *c = &g_cor[i];
            if (exclude && *exclude && c->text < g_ntext &&
                (strstr(g_text_file[c->text], exclude) || strstr(g_text_name[c->text], exclude))) {
                skipped++;
                continue;
            }
            for (int k = 0; k < c->n; k++) {
                g_uni[c->s[k]]++;
                g_ntok++;
                bigram_add(k ? c->s[k - 1] : -1, c->s[k]);
            }
        }
        g_lm_ready = g_ntok > 1000;
        if (skipped)
            fprintf(stderr, "Corpus: %d renglones excluidos del modelo de lengua («%s»)\n", skipped, exclude);
    }
    return g_ndic + g_ncor;
}

/* ---------- consultas ---------- */

/* Un token de la consulta -> id de signo (-2 = comodín, -1 = no es un signo). */
static int token_id(const char *t)
{
    if (strcmp(t, "?") == 0 || strcmp(t, "*") == 0)
        return -2;
    char code[CODE_LEN];
    if (code_from_folder(t, code) == 0) {   /* g17 -> G17, aa1 -> Aa1 */
        int id = sign_id_find(code);
        return id >= 0 ? id : sign_id(code);
    }
    for (int i = 0; i < g_nfon; i++)
        if (strcmp(g_fon_key[i], t) == 0)
            return sign_id(g_fon_val[i]);
    return -1;
}

/* "G17 D21-nfr:f" -> ids. Devuelve -1 si algún trozo no es un signo. */
int dict_parse_query(const char *q, int *ids, int max)
{
    int n = 0;
    char tok[64];
    const char *s = q;
    while (*s) {
        while (*s && strchr(" -:*,;!\t", *s))
            s++;
        int k = 0;
        while (*s && !strchr(" -:*,;!\t", *s) && k < (int)sizeof tok - 1)
            tok[k++] = *s++;
        tok[k] = 0;
        if (!k)
            continue;
        int id = token_id(tok);
        if (id == -1)
            return -1;
        if (n < max)
            ids[n++] = id;
    }
    return n;
}

static int match_at(const int *s, int n, const int *q, int m, int pos)
{
    if (pos + m > n)
        return 0;
    for (int k = 0; k < m; k++)
        if (q[k] != -2 && q[k] != s[pos + k])
            return 0;
    return 1;
}

static int find_seq(const int *s, int n, const int *q, int m)
{
    for (int p = 0; p + m <= n; p++)
        if (match_at(s, n, q, m, p))
            return p;
    return -1;
}

static int cmp_hit(const void *a, const void *b)
{
    const DictHit *x = a, *y = b;
    if (x->kind != y->kind)
        return x->kind - y->kind;
    if (x->match != y->match)
        return x->match - y->match;
    if (x->kind == 0) {
        const DictEntry *ea = &g_dic[x->idx], *eb = &g_dic[y->idx];
        int ta = ea->trad[0] != 0, tb = eb->trad[0] != 0;
        if (ta != tb)
            return tb - ta;
        if (ea->freq != eb->freq)
            return eb->freq - ea->freq;
        if (ea->n != eb->n)
            return ea->n - eb->n;
    } else {
        const CorpusLine *ca = &g_cor[x->idx], *cb = &g_cor[y->idx];
        int ta = (ca->trad[0] || ca->tr[0]), tb = (cb->trad[0] || cb->tr[0]);
        if (ta != tb)
            return tb - ta;
    }
    return x->idx - y->idx;
}

/* Diccionario (exacto, empieza por, contiene) y corpus (renglones que contienen la secuencia). */
int dict_search_signs(const int *q, int m, DictHit *hits, int max, int *ntotal)
{
    int n = 0, total = 0;
    if (m <= 0)
        return 0;
    DictHit *all = malloc(sizeof(DictHit) * (size_t)(g_ndic + g_ncor + 1));
    for (int i = 0; i < g_ndic; i++) {
        const DictEntry *e = &g_dic[i];
        int p = find_seq(e->s, e->n, q, m);
        if (p < 0)
            continue;
        int match = (p == 0 && e->n == m) ? 0 : p == 0 ? 1 : 2;
        all[total++] = (DictHit){0, i, match, p};
    }
    for (int i = 0; i < g_ncor; i++) {
        const CorpusLine *c = &g_cor[i];
        int p = find_seq(c->s, c->n, q, m);
        if (p >= 0)
            all[total++] = (DictHit){1, i, 0, p};
    }
    qsort(all, (size_t)total, sizeof(DictHit), cmp_hit);
    /* reparte el espacio: hasta 3/4 para el diccionario y el resto para el corpus */
    int nd = 0, nc = 0, maxd = max * 3 / 4;
    for (int i = 0; i < total; i++)
        if (all[i].kind == 0) nd++; else nc++;
    if (nc < max - maxd)
        maxd = max - nc;
    for (int i = 0, kd = 0; i < total && n < max; i++) {
        if (all[i].kind == 0 && kd++ >= maxd)
            continue;
        hits[n++] = all[i];
    }
    free(all);
    if (ntotal)
        *ntotal = total;
    return n;
}

static int word_match(const char *hay, const char *needle, int icase)
{
    size_t l = strlen(needle);
    for (const char *p = hay; *p; p++) {
        int eq = icase ? strncasecmp(p, needle, l) == 0 : strncmp(p, needle, l) == 0;
        if (!eq)
            continue;
        int b = p == hay || !isalnum((unsigned char)p[-1]);
        int a = !isalnum((unsigned char)p[l]);
        return (a && b) ? 2 : 1;
    }
    return 0;
}

/* Por transliteración (MdC o Unicode, distingue mayúsculas: H ≠ h) o por traducción. */
int dict_search_text(const char *q, DictHit *hits, int max, int *ntotal)
{
    int total = 0, n = 0;
    if (!q[0])
        return 0;
    DictHit *all = malloc(sizeof(DictHit) * (size_t)(g_ndic + g_ncor + 1));
    for (int i = 0; i < g_ndic; i++) {
        const DictEntry *e = &g_dic[i];
        int m;
        if (strcmp(e->tr, q) == 0 || strcmp(e->tr_u, q) == 0)
            m = 0;
        else if ((m = word_match(e->tr, q, 0)) || (m = word_match(e->tr_u, q, 0)))
            m = m == 2 ? 1 : 3;
        else if ((m = word_match(e->trad, q, 1)))
            m = m == 2 ? 2 : 4;
        else
            continue;
        all[total++] = (DictHit){0, i, m, 0};
    }
    for (int i = 0; i < g_ncor; i++) {
        const CorpusLine *c = &g_cor[i];
        int m = word_match(c->tr, q, 0);
        if (!m)
            m = word_match(c->trad, q, 1);
        if (m)
            all[total++] = (DictHit){1, i, m == 2 ? 0 : 1, 0};
    }
    qsort(all, (size_t)total, sizeof(DictHit), cmp_hit);
    int maxd = max * 3 / 4, nc = 0;
    for (int i = 0; i < total; i++)
        nc += all[i].kind == 1;
    if (nc < max - maxd)
        maxd = max - nc;
    for (int i = 0, kd = 0; i < total && n < max; i++) {
        if (all[i].kind == 0 && kd++ >= maxd)
            continue;
        hits[n++] = all[i];
    }
    free(all);
    if (ntotal)
        *ntotal = total;
    return n;
}

/* Signos, transliteración o traducción: decide según la consulta. */
int dict_search(const char *q, DictHit *hits, int max, int *ntotal)
{
    int ids[64];
    int m = dict_parse_query(q, ids, 64);
    if (m > 0) {
        int n = dict_search_signs(ids, m, hits, max, ntotal);
        /* "nfr", "Hr"...: también puede ser una transliteración */
        if (n < max && !strchr(q, ' ') && !strchr(q, '-')) {
            int t2 = 0;
            n += dict_search_text(q, hits + n, max - n, &t2);
            if (ntotal)
                *ntotal += t2;
        }
        return n;
    }
    return dict_search_text(q, hits, max, ntotal);
}

/* ---------- lectura de un renglón ---------- */

/* Parte la secuencia en palabras del diccionario: maximiza Σ (longitud² · peso)
 * de las palabras encontradas; los signos sueltos sin palabra cuentan 0. */
int dict_read(const int *s, int n, DictWord *w, int maxw)
{
    if (!g_seq || n <= 0)
        return 0;
    float *best = calloc((size_t)n + 1, sizeof(float));
    int *from = malloc(sizeof(int) * ((size_t)n + 1)), *ent = malloc(sizeof(int) * ((size_t)n + 1));
    for (int i = 1; i <= n; i++) {
        best[i] = best[i - 1];
        from[i] = i - 1;
        ent[i] = -1;
        for (int l = 2; l <= 10 && l <= i; l++) {   /* palabras de 2 o más signos */
            int e = seq_find(s + i - l, l);
            if (e < 0)
                continue;
            float wgt = (g_dic[e].trad[0] ? 1.0f : 0.6f) * (1.0f + 0.15f * logf(1.0f + g_dic[e].freq));
            float v = best[i - l] + (float)(l * l) * wgt;
            if (v > best[i]) {
                best[i] = v;
                from[i] = i - l;
                ent[i] = e;
            }
        }
    }
    int tmp[MAXTOK][3], nt = 0;
    for (int i = n; i > 0 && nt < MAXTOK; i = from[i])
        if (ent[i] >= 0) {
            tmp[nt][0] = from[i];
            tmp[nt][1] = i - from[i];
            tmp[nt][2] = ent[i];
            nt++;
        }
    int nw = 0;
    for (int k = nt - 1; k >= 0 && nw < maxw; k--)
        w[nw++] = (DictWord){tmp[k][0], tmp[k][1], tmp[k][2]};
    free(best);
    free(from);
    free(ent);
    return nw;
}

/* Renglón del corpus que comparte más bigramas con la secuencia (con traducción
 * o transliteración). Devuelve el índice, o -1; *ratio = fracción de bigramas comunes. */
int corpus_best_line(const int *s, int n, float *ratio)
{
    int best = -1;
    float bv = 0;
    if (n < 3)
        return -1;
    for (int i = 0; i < g_ncor; i++) {
        const CorpusLine *c = &g_cor[i];
        if (c->n < 2 || (!c->tr[0] && !c->trad[0]))
            continue;
        int common = 0;
        for (int a = 0; a + 1 < n; a++)
            for (int b = 0; b + 1 < c->n; b++)
                if (c->s[b] == s[a] && c->s[b + 1] == s[a + 1]) {
                    common++;
                    break;
                }
        float v = (float)common / (float)((n > c->n ? n : c->n) - 1);
        if (v > bv) {
            bv = v;
            best = i;
        }
    }
    if (ratio)
        *ratio = bv;
    return best;
}

/* ---------- modelo de lengua ---------- */

static float g_lambda = 0.012f;   /* peso del modelo frente a la puntuación visual */

int lm_ready(void) { return g_lm_ready; }

/* -log P(b | a), con P(b|a) = (C(a,b) + α·P(b)) / (C(a) + α); a = -1: inicio de renglón. */
static float lm_cost(int a, int b)
{
    const float alpha = 20.0f;
    float pb = (float)((b >= 0 && b < HSIZE ? g_uni[b] : 0) + 0.5) / (float)(g_ntok + 0.5 * g_ncodes);
    int ca = a >= 0 && a < HSIZE ? g_uni[a] : 0;
    float p = ((float)bigram(a, b) + alpha * pb) / ((float)ca + alpha);
    return -logf(p);
}

/* Reordena los candidatos de cada renglón (no toca los corregidos a mano). */
int lm_rerank_page(Page *p, const Options *o, const TemplateSet *ts)
{
    if (!g_lm_ready || p->ng == 0)
        return 0;
    const char *lam = getenv("HIEROOCR_LAMBDA");
    if (lam)
        g_lambda = (float)atof(lam);
    int changed = 0;
    int *ord = malloc(sizeof(int) * (size_t)p->ng);
    float (*cost)[NCAND] = malloc(sizeof(float[NCAND]) * (size_t)p->ng);
    int (*back)[NCAND] = malloc(sizeof(int[NCAND]) * (size_t)p->ng);
    int (*cid)[NCAND] = malloc(sizeof(int[NCAND]) * (size_t)p->ng);
    int *nc = malloc(sizeof(int) * (size_t)p->ng);
    for (int L = 0; L < p->nlines; L++) {
        int m = line_glyph_order(p, L, o, ord);
        if (m == 0)
            continue;
        for (int i = 0; i < m; i++) {
            Glyph *g = &p->g[ord[i]];
            if (g->manual || g->ncand == 0) {
                nc[i] = 1;
                cid[i][0] = sign_id(g->code[0] ? g->code : "?");
            } else {
                nc[i] = g->ncand;
                for (int k = 0; k < nc[i]; k++)
                    cid[i][k] = sign_id(ts->t[g->cand[k]].code);
            }
            for (int k = 0; k < nc[i]; k++) {
                float vis = (g->manual || g->ncand == 0) ? 0 : g->cand_score[k];
                float bestc = 1e30f;
                int bk = 0;
                if (i == 0) {
                    bestc = g_lambda * lm_cost(-1, cid[i][k]);
                } else {
                    for (int j = 0; j < nc[i - 1]; j++) {
                        float c = cost[i - 1][j] + g_lambda * lm_cost(cid[i - 1][j], cid[i][k]);
                        if (c < bestc) {
                            bestc = c;
                            bk = j;
                        }
                    }
                }
                cost[i][k] = bestc + vis;
                back[i][k] = bk;
            }
        }
        int k = 0;
        for (int j = 1; j < nc[m - 1]; j++)
            if (cost[m - 1][j] < cost[m - 1][k])
                k = j;
        for (int i = m - 1; i >= 0; i--) {
            Glyph *g = &p->g[ord[i]];
            if (!g->manual && g->ncand > 0) {
                const char *code = ts->t[g->cand[k]].code;
                if (strcmp(code, g->code) != 0) {
                    snprintf(g->code, CODE_LEN, "%s", code);
                    g->score = g->cand_score[k];
                    changed++;
                }
            }
            k = back[i][k];
        }
    }
    free(ord);
    free(cost);
    free(back);
    free(cid);
    free(nc);
    return changed;
}

/* Secuencia de ids de los signos de un renglón, en orden de lectura. */
int line_sign_ids(const Page *p, int line, const Options *o, int *ids, int *glyphs, int max)
{
    int *ord = malloc(sizeof(int) * (size_t)(p->ng ? p->ng : 1));
    int m = line_glyph_order(p, line, o, ord), n = 0;
    for (int i = 0; i < m && n < max; i++) {
        const Glyph *g = &p->g[ord[i]];
        if (!g->code[0])
            continue;
        if (glyphs)
            glyphs[n] = ord[i];
        ids[n++] = sign_id(g->code);
    }
    free(ord);
    return n;
}

/* "ȝḫ.t «horizonte» · nfr «bueno» ...": lectura de un renglón con el diccionario. */
void line_reading(const Page *p, int line, const Options *o, Str *out)
{
    int ids[MAXTOK];
    DictWord w[512];
    int n = line_sign_ids(p, line, o, ids, NULL, MAXTOK);
    int nw = dict_read(ids, n, w, 512);
    char buf[1024], u[512];
    for (int k = 0; k < nw; k++) {
        const DictEntry *e = &g_dic[w[k].entry];
        translit_unicode(e->tr, u, sizeof u);
        snprintf(buf, sizeof buf, "%s%s%s%.60s%s", k ? " · " : "", u[0] ? u : "?",
                 e->trad[0] ? " «" : "", e->trad, e->trad[0] ? "»" : "");
        str_puts(out, buf);
    }
    float r = 0;
    int c = corpus_best_line(ids, n, &r);
    if (c >= 0 && r >= 0.5f) {
        const CorpusLine *cl = &g_cor[c];
        snprintf(buf, sizeof buf, "%sparecido a %s, %d (%.0f %%)%s%.120s", nw ? " · " : "",
                 corpus_text_name(cl->text), cl->line, 100 * r, cl->trad[0] ? ": " : "", cl->trad);
        str_puts(out, buf);
    }
}
