/*
 * Interfaz SDL2 para revisar el reconocimiento, corregir signos y enseñar al
 * programa la tipografía de la edición (cada corrección se guarda como
 * plantilla y mejora las siguientes páginas).
 *
 * Funciona con ratón y teclado (escritorio) y con pantalla táctil (Android):
 * toque = seleccionar, arrastrar = mover la página, pellizcar = zoom, y todas
 * las órdenes tienen botón. El tamaño de letra y botones se ajusta a la
 * densidad de la pantalla.
 */
#include "hieroocr.h"

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define NCACHE  4096
#define MAXBTN  160
#define MAXHITS 120

enum {
    A_NONE, A_OPEN, A_CAMERA, A_PREV_PAGE, A_NEXT_PAGE, A_FIT, A_LEARN, A_RECLASS, A_SAVE,
    A_PREV_SIGN, A_NEXT_SIGN, A_EDIT, A_DELETE, A_MERGE, A_EDIT_OK, A_EDIT_CANCEL,
    A_DICT, A_DICT_CLOSE, A_DICT_EDIT, A_DICT_GO, A_EXTEND, A_SEARCH_SEL,
    A_CAND,              /* A_CAND + k: candidato k */
    A_HIT = A_CAND + 16  /* A_HIT + i: resultado i del diccionario */
};
enum { B_NORMAL, B_PRIMARY, B_ON, B_LABEL };

typedef struct { char key[CODE_LEN + 4]; SDL_Texture *t; int w, h; SDL_Rect src; } CacheEntry;
typedef struct { SDL_Rect r; int action; } Button;
typedef struct { const char *label; int action; int style; } BtnSpec;
typedef struct { SDL_FingerID id; float x, y; } Finger;

typedef struct {
    SDL_Window   *win;
    SDL_Renderer *ren;
    SDL_Texture  *tex;
    int           tex_w, tex_h;
    TTF_Font     *ui, *small, *hiero;
    Doc          *doc;
    int           cur;
    Options      *o;
    TemplateSet  *ts;
    float         u;                      /* escala de la interfaz (1 = 96 ppp) */
    int           mobile;
    float         scale, offx, offy;
    int           sel, editing, dragging, learn, running;
    int           top_h, panel_h;         /* alto de la barra superior y del panel inferior */
    char          buf[CODE_LEN];
    char          status[256];
    Button        btn[MAXBTN];
    int           nbtn;
    /* gestos */
    int           press, moved, press_action, press_in_page;
    int           press_x, press_y;
    Finger        fing[2];
    int           nfing, ntouch, pinch;
    /* OpenGlyph: selección de varios signos, buscador y lectura */
    int           sel_n;                  /* signos seleccionados a partir de sel, en orden de lectura */
    int           overlay;                /* buscador abierto */
    int           sedit;                  /* escribiendo en el buscador */
    char          query[160];
    int           q_from_sel;             /* la consulta viene de la selección */
    DictHit       hits[MAXHITS];
    int           nhits, ntotal;
    float         ov_scroll;
    int           ov_y0, ov_y1;
    unsigned      rd_key;                 /* lectura del renglón en caché */
    char          rd_text[1200], rd_word[400];
    CacheEntry    cache[NCACHE];
    int           ncache;
} Gui;

static Uint32 g_event_type = (Uint32)-1;

uint32_t gui_event_type(void)
{
    if (g_event_type == (Uint32)-1)
        g_event_type = SDL_RegisterEvents(1);
    return g_event_type;
}

void request_free(Request *r)
{
    if (!r)
        return;
    for (int i = 0; i < r->n; i++)
        free(r->files[i]);
    free(r->files);
    free(r);
}

static const char *ui_font_paths[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
    "/system/fonts/Roboto-Regular.ttf",          /* Android */
    "/system/fonts/NotoSans-Regular.ttf",
    "/system/fonts/DroidSans.ttf",
    "/System/Library/Fonts/Supplemental/Arial.ttf",
    "C:/Windows/Fonts/segoeui.ttf",
    "C:/Windows/Fonts/arial.ttf",
    NULL
};

static TTF_Font *open_ui_font(int size)
{
    const char *env = getenv("HIEROOCR_UI_FONT");
    if (env) {
        TTF_Font *f = TTF_OpenFont(env, size);
        if (f)
            return f;
    }
    for (const char **p = ui_font_paths; *p; p++) {
        TTF_Font *f = TTF_OpenFont(*p, size);
        if (f)
            return f;
    }
    const char *win = getenv("WINDIR");   /* Windows instalado fuera de C: */
    if (win) {
        char p[600];
        snprintf(p, sizeof p, "%s\\Fonts\\segoeui.ttf", win);
        TTF_Font *f = TTF_OpenFont(p, size);
        if (!f) {
            snprintf(p, sizeof p, "%s\\Fonts\\arial.ttf", win);
            f = TTF_OpenFont(p, size);
        }
        return f;
    }
    return NULL;
}

static float ui_scale(void)
{
    float u = 1.0f;
    const char *e = getenv("HIEROOCR_UI_SCALE");
    if (e) {
        u = (float)atof(e);
    } else {
#ifdef __ANDROID__
        float ddpi = 0;
        if (SDL_GetDisplayDPI(0, &ddpi, NULL, NULL) == 0 && ddpi > 0)
            u = ddpi / 160.0f;
        else
            u = 2.0f;
#endif
    }
    if (u < 0.75f) u = 0.75f;
    if (u > 4.0f) u = 4.0f;
    return u;
}

#define U(v) ((int)lroundf((v) * g->u))

static int has_doc(Gui *g) { return g->doc && g->doc->np > 0; }
static Page *cur_page(Gui *g) { return has_doc(g) ? &g->doc->pages[g->cur] : NULL; }

/* ---------- texto ---------- */

static SDL_Texture *make_text(Gui *g, TTF_Font *f, const char *s, SDL_Color c, int *w, int *h)
{
    if (!f || !s || !*s)
        return NULL;
    SDL_Surface *sf = TTF_RenderUTF8_Blended(f, s, c);
    if (!sf)
        return NULL;
    SDL_Texture *t = SDL_CreateTextureFromSurface(g->ren, sf);
    *w = sf->w;
    *h = sf->h;
    SDL_FreeSurface(sf);
    return t;
}

static int text_w(TTF_Font *f, const char *s)
{
    int w = 0, h = 0;
    if (f && s && *s)
        TTF_SizeUTF8(f, s, &w, &h);
    return w;
}

static int line_h(TTF_Font *f) { return f ? TTF_FontLineSkip(f) : 16; }

static int draw_text(Gui *g, TTF_Font *f, int x, int y, const char *s, SDL_Color c, int maxw)
{
    int w, h;
    SDL_Texture *t = make_text(g, f, s, c, &w, &h);
    if (!t)
        return 0;
    SDL_Rect src = {0, 0, (maxw > 0 && w > maxw) ? maxw : w, h};
    SDL_Rect dst = {x, y, src.w, h};
    SDL_RenderCopy(g->ren, t, &src, &dst);
    SDL_DestroyTexture(t);
    return src.w;
}

/* Texto partido en renglones por los espacios; devuelve el alto ocupado. */
static int draw_wrapped(Gui *g, TTF_Font *f, int x, int y, const char *s, SDL_Color c,
                        int maxw, int maxlines, int draw)
{
    if (!f || !s || !*s)
        return 0;
    int lh = line_h(f), nl = 0;
    char line[1024];
    const char *p = s;
    while (*p && nl < maxlines) {
        size_t best = 0, i = 0;
        for (;;) {
            size_t j = i;
            while (p[j] && p[j] != ' ')
                j++;
            if (j >= sizeof line)
                break;
            memcpy(line, p, j);
            line[j] = 0;
            int w = text_w(f, line);
            if (w > maxw && best > 0)
                break;
            best = j;
            if (!p[j] || w > maxw)
                break;
            i = j + 1;
        }
        size_t take = (nl == maxlines - 1) ? strlen(p) : best;
        if (take == 0)
            take = strlen(p);
        if (take >= sizeof line)
            take = sizeof line - 1;
        memcpy(line, p, take);
        line[take] = 0;
        if (draw)
            draw_text(g, f, x, y + nl * lh, line, c, maxw);
        nl++;
        p += take;
        while (*p == ' ')
            p++;
    }
    return nl * lh;
}

/* Etiquetas de código y dibujos de jeroglíficos, en caché (hay cientos por página). */
static CacheEntry *cached(Gui *g, const char *code, char kind, SDL_Color c)
{
    char key[CODE_LEN + 4];
    snprintf(key, sizeof key, "%c%s", kind, code);
    for (int i = 0; i < g->ncache; i++)
        if (strcmp(g->cache[i].key, key) == 0)
            return &g->cache[i];
    if (g->ncache >= NCACHE)
        return NULL;
    CacheEntry *e = &g->cache[g->ncache];
    e->t = NULL;
    if (kind == 'H') {
        uint32_t cp = ts_codepoint(g->ts, code);
        if (cp && g->hiero) {
            SDL_Surface *s0 = TTF_RenderGlyph32_Blended(g->hiero, cp, c);
            SDL_Surface *sf = s0 ? SDL_ConvertSurfaceFormat(s0, SDL_PIXELFORMAT_RGBA32, 0) : NULL;
            if (s0)
                SDL_FreeSurface(s0);
            if (sf) {
                /* recorta el margen que deja la fuente alrededor del signo */
                int x0 = sf->w, y0 = sf->h, x1 = -1, y1 = -1;
                SDL_LockSurface(sf);
                for (int y = 0; y < sf->h; y++) {
                    const uint8_t *row = (const uint8_t *)sf->pixels + (size_t)y * sf->pitch;
                    for (int x = 0; x < sf->w; x++)
                        if (row[4 * x + 3] > 24) {
                            if (x < x0) x0 = x;
                            if (x > x1) x1 = x;
                            if (y < y0) y0 = y;
                            if (y > y1) y1 = y;
                        }
                }
                SDL_UnlockSurface(sf);
                e->t = SDL_CreateTextureFromSurface(g->ren, sf);
                if (x1 >= x0)
                    e->src = (SDL_Rect){x0, y0, x1 - x0 + 1, y1 - y0 + 1};
                else
                    e->src = (SDL_Rect){0, 0, sf->w, sf->h};
                e->w = e->src.w;
                e->h = e->src.h;
                SDL_FreeSurface(sf);
            }
        }
    } else {
        e->t = make_text(g, g->small, code, c, &e->w, &e->h);
        e->src = (SDL_Rect){0, 0, e->w, e->h};
    }
    if (!e->t)
        return NULL;
    snprintf(e->key, sizeof e->key, "%s", key);
    g->ncache++;
    return e;
}

static int selection(Gui *g, int *out, int max);
static void update_reading(Gui *g);

/* ---------- botones ---------- */

static void set_rgb(Gui *g, int r, int gg, int b) { SDL_SetRenderDrawColor(g->ren, (Uint8)r, (Uint8)gg, (Uint8)b, 255); }

static void draw_button(Gui *g, SDL_Rect r, const char *label, int style)
{
    SDL_Color fg = {25, 25, 25, 255};
    if (style == B_PRIMARY) {
        set_rgb(g, 38, 84, 160);
        fg = (SDL_Color){255, 255, 255, 255};
    } else if (style == B_ON) {
        set_rgb(g, 205, 232, 205);
    } else if (style == B_LABEL) {
        set_rgb(g, 236, 234, 228);
    } else {
        set_rgb(g, 252, 251, 248);
    }
    SDL_RenderFillRect(g->ren, &r);
    if (style != B_LABEL) {
        set_rgb(g, 150, 150, 150);
        SDL_RenderDrawRect(g->ren, &r);
    }
    int tw = text_w(g->ui, label), th = line_h(g->ui);
    draw_text(g, g->ui, r.x + (r.w - tw) / 2, r.y + (r.h - th) / 2, label, fg, r.w - 4);
}

static void add_button(Gui *g, SDL_Rect r, int action)
{
    if (action != A_NONE && g->nbtn < MAXBTN)
        g->btn[g->nbtn++] = (Button){r, action};
}

static int button_at(Gui *g, int x, int y)
{
    for (int i = g->nbtn - 1; i >= 0; i--) {
        SDL_Rect *r = &g->btn[i].r;
        if (x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h)
            return g->btn[i].action;
    }
    return A_NONE;
}

/* Coloca botones de izquierda a derecha, pasando a otra fila si no caben. */
static int flow_buttons(Gui *g, int x0, int y0, int maxw, const BtnSpec *b, int n, int draw)
{
    int bh = U(34), gap = U(6), padx = U(12);
    int x = x0, y = y0;
    for (int i = 0; i < n; i++) {
        int w = text_w(g->ui, b[i].label) + 2 * padx;
        if (w > maxw)
            w = maxw;
        if (x > x0 && x + w > x0 + maxw) {
            x = x0;
            y += bh + gap;
        }
        SDL_Rect r = {x, y, w, bh};
        if (draw) {
            draw_button(g, r, b[i].label, b[i].style);
            add_button(g, r, b[i].action);
        }
        x += w + gap;
    }
    return n ? y + bh - y0 : 0;
}

/* ---------- disposición ---------- */

static int toolbar(Gui *g, int ww, int draw)
{
    BtnSpec b[14];
    int n = 0;
    char pg[48];
    if (g->mobile) {
        b[n++] = (BtnSpec){"Abrir", A_OPEN, B_PRIMARY};
        b[n++] = (BtnSpec){"Cámara", A_CAMERA, B_PRIMARY};
    }
    if (dict_loaded())
        b[n++] = (BtnSpec){"Diccionario", g->overlay ? A_DICT_CLOSE : A_DICT, g->overlay ? B_ON : B_NORMAL};
    if (has_doc(g)) {
        if (g->doc->np > 1) {
            snprintf(pg, sizeof pg, "Pág. %d (%d/%d)", cur_page(g)->pageno, g->cur + 1, g->doc->np);
            b[n++] = (BtnSpec){"<", A_PREV_PAGE, B_NORMAL};
            b[n++] = (BtnSpec){pg, A_NONE, B_LABEL};
            b[n++] = (BtnSpec){">", A_NEXT_PAGE, B_NORMAL};
        }
        b[n++] = (BtnSpec){"Ajustar", A_FIT, B_NORMAL};
        b[n++] = (BtnSpec){g->learn ? "Aprender: sí" : "Aprender: no", A_LEARN, g->learn ? B_ON : B_NORMAL};
        b[n++] = (BtnSpec){"Reclasificar", A_RECLASS, B_NORMAL};
        b[n++] = (BtnSpec){g->mobile ? "Compartir MdC" : "Guardar MdC", A_SAVE, B_NORMAL};
    }
    int pad = U(6);
    int h = n ? flow_buttons(g, pad, pad, ww - 2 * pad, b, n, 0) + 2 * pad : 0;
    if (draw && n) {
        SDL_Rect bar = {0, 0, ww, h};
        set_rgb(g, 236, 234, 228);
        SDL_RenderFillRect(g->ren, &bar);
        set_rgb(g, 150, 150, 150);
        SDL_RenderDrawLine(g->ren, 0, h - 1, ww, h - 1);
        flow_buttons(g, pad, pad, ww - 2 * pad, b, n, 1);
    }
    return h;
}

static int panel(Gui *g, int ww, int y0, int draw)
{
    Page *p = cur_page(g);
    const SDL_Color black = {20, 20, 20, 255}, grey = {90, 90, 90, 255}, blue = {30, 70, 200, 255};
    int pad = U(8), maxw = ww - 2 * pad, y = y0 + U(6);
    char line[512];

    if (!p) {
        if (!g->status[0])
            return 0;
        y += draw_wrapped(g, g->small, pad, y, g->status, grey, maxw, 2, draw);
        return y + U(6) - y0;
    }

    snprintf(line, sizeof line, "Página %d · %d signos · %s", p->pageno, p->ng, g->status);
    y += draw_wrapped(g, g->small, pad, y, line, grey, maxw, 2, draw) + U(4);

    if (g->sel >= 0) {
        Glyph *gl = &p->g[g->sel];
        const SignInfo *si = gl->code[0] ? sign_find(gl->code) : NULL;
        char info[220] = "";
        if (si && (si->phon[0] || si->desc[0]))
            snprintf(info, sizeof info, " · %s%s%s%s", si->desc,
                     si->phon[0] ? " [" : "", si->phon, si->phon[0] ? "]" : "");
        snprintf(line, sizeof line, "Signo %d · renglón %d · %s%s · puntuación %.3f%s%s",
                 g->sel, gl->line + 1, gl->code[0] ? gl->code : "?", info, gl->score,
                 gl->manual ? " · corregido" : "", gl->underlined ? " · subrayado" : "");
        y += draw_wrapped(g, g->ui, pad, y, line, black, maxw, 2, draw) + U(4);

        if (dict_loaded()) {   /* OpenGlyph: palabra del signo y lectura del renglón */
            const SDL_Color brown = {120, 60, 10, 255};
            update_reading(g);
            if (g->sel_n > 1) {
                int sl[64], ns = selection(g, sl, 64);
                char q[200] = "";
                for (int i = 0; i < ns && strlen(q) < 180; i++) {
                    strcat(q, i ? "-" : "");
                    strcat(q, p->g[sl[i]].code[0] ? p->g[sl[i]].code : "?");
                }
                snprintf(line, sizeof line, "Selección: %s (%d signos) · «Buscar» la consulta en OpenGlyph", q, ns);
                y += draw_wrapped(g, g->ui, pad, y, line, brown, maxw, 2, draw) + U(2);
            } else if (g->rd_word[0]) {
                snprintf(line, sizeof line, "Palabra: %s", g->rd_word);
                y += draw_wrapped(g, g->ui, pad, y, line, brown, maxw, 2, draw) + U(2);
            }
            if (g->rd_text[0]) {
                snprintf(line, sizeof line, "Lectura: %s", g->rd_text);
                y += draw_wrapped(g, g->small, pad, y, line, brown, maxw, 2, draw) + U(4);
            }
        }

        /* candidatos: casillas que se pueden tocar */
        int gap = U(6), cw = (maxw - (NCAND - 1) * gap) / NCAND;
        if (cw > U(190))
            cw = U(190);
        int hh = U(44), sl = line_h(g->small), ch = U(6) + hh + 2 * sl + U(4);
        if (draw) {
            for (int k = 0; k < gl->ncand; k++) {
                const char *code = g->ts->t[gl->cand[k]].code;
                SDL_Rect r = {pad + k * (cw + gap), y, cw, ch};
                int chosen = strcmp(code, gl->code) == 0;
                if (chosen) set_rgb(g, 222, 232, 250); else set_rgb(g, 252, 251, 248);
                SDL_RenderFillRect(g->ren, &r);
                set_rgb(g, 150, 150, 150);
                SDL_RenderDrawRect(g->ren, &r);
                add_button(g, r, A_CAND + k);
                CacheEntry *h = cached(g, code, 'H', black);
                if (h) {   /* el signo, tan grande como quepa en la casilla */
                    float k1 = (float)hh / (h->h ? h->h : 1), k2 = (float)(cw - U(8)) / (h->w ? h->w : 1);
                    float kk = k1 < k2 ? k1 : k2;
                    int hw = (int)(h->w * kk), hgt = (int)(h->h * kk);
                    SDL_Rect d = {r.x + (cw - hw) / 2, r.y + U(4) + (hh - hgt) / 2, hw, hgt};
                    SDL_RenderCopy(g->ren, h->t, &h->src, &d);
                }
                snprintf(line, sizeof line, "%d · %s", k + 1, code);
                int tw = text_w(g->small, line);
                draw_text(g, g->small, r.x + (tw < cw ? (cw - tw) / 2 : 2), r.y + U(6) + hh, line, black, cw - 4);
                const SignInfo *ci = sign_find(code);
                char ph[24] = "";
                if (ci && ci->phon[0]) {
                    snprintf(ph, sizeof ph, "%.23s", ci->phon);
                    char *semi = strchr(ph, ';');
                    if (semi) *semi = 0;   /* sólo el primer valor fonético */
                }
                snprintf(line, sizeof line, "%s%s%s %.3f", ph[0] ? "[" : "", ph, ph[0] ? "]" : "", gl->cand_score[k]);
                tw = text_w(g->small, line);
                draw_text(g, g->small, r.x + (tw < cw ? (cw - tw) / 2 : 2), r.y + U(6) + hh + sl, line, grey, cw - 4);
            }
        }
        y += ch + U(6);

        if (!g->editing) {
            Str s = {0};
            line_to_mdc(p, gl->line, g->o, &s);
            snprintf(line, sizeof line, "MdC: %s!", s.s ? s.s : "");
            free(s.s);
            y += draw_wrapped(g, g->small, pad, y, line, blue, maxw, 2, draw) + U(4);
        }
        BtnSpec b[] = {
            {"< Anterior", A_PREV_SIGN, B_NORMAL}, {"Siguiente >", A_NEXT_SIGN, B_NORMAL},
            {"Escribir código", A_EDIT, B_NORMAL}, {"Borrar", A_DELETE, B_NORMAL},
            {"Unir con el siguiente", A_MERGE, B_NORMAL},
            {"Ampliar selección >", A_EXTEND, B_NORMAL}, {"Buscar en OpenGlyph", A_SEARCH_SEL, B_NORMAL},
        };
        y += flow_buttons(g, pad, y, maxw, b, dict_loaded() ? 7 : 5, draw) + U(6);
    } else {
        y += draw_wrapped(g, g->ui, pad, y,
                          g->mobile ? "Toca un signo para corregirlo. Arrastra para mover la página y pellizca para ampliarla."
                                    : "Haz clic en un signo (o pulsa Tab) para seleccionarlo.",
                          black, maxw, 2, draw) + U(6);
    }
    if (!g->mobile)
        y += draw_wrapped(g, g->small, pad, y,
                          "Clic: seleccionar · Tab: siguiente · 1-5: candidato · Intro: escribir código · Supr: borrar · "
                          "M: unir con el siguiente · R: reclasificar · L: aprendizaje · S: guardar MdC · "
                          "E: ampliar selección · B: buscar la selección · D: diccionario · "
                          "AvPág/RePág: página · rueda/+/-: zoom · F: ajustar · arrastrar: mover · Q: salir",
                          grey, maxw, 3, draw) + U(4);
    return y - y0;
}

static void measure(Gui *g, int *ww, int *wh)
{
    SDL_GetRendererOutputSize(g->ren, ww, wh);
    g->top_h = toolbar(g, *ww, 0);
    g->panel_h = g->overlay ? 0 : panel(g, *ww, 0, 0);
    if (g->panel_h > *wh / 2)
        g->panel_h = *wh / 2;
}

/* ---------- página ---------- */

static void fit_page(Gui *g)
{
    int ww, wh;
    measure(g, &ww, &wh);
    if (!g->tex_w || !g->tex_h)
        return;
    int ah = wh - g->top_h - g->panel_h;
    float sx = (float)ww / g->tex_w, sy = (float)ah / g->tex_h;
    g->scale = sx < sy ? sx : sy;
    g->offx = (ww - g->tex_w * g->scale) / 2;
    g->offy = g->top_h + (ah - g->tex_h * g->scale) / 2;
    if (g->offy > g->top_h && sx < sy)
        g->offy = (float)g->top_h;   /* página alargada: empezar por arriba */
}

static void render(Gui *g, const char *busy);

static int load_page(Gui *g, int i)
{
    if (!g->doc || i < 0 || i >= g->doc->np)
        return -1;
    Page *p = &g->doc->pages[i];
    if (!p->processed) {
        char msg[96];
        snprintf(msg, sizeof msg, "Reconociendo la página %d...", p->pageno);
        render(g, msg);
        if (process_page(p, g->o, g->ts) != 0) {
            snprintf(g->status, sizeof g->status, "No se puede leer la página %d.", p->pageno);
            return -1;
        }
    }
    if (g->tex)
        SDL_DestroyTexture(g->tex);
    g->tex = NULL;
    g->tex_w = g->tex_h = 0;
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, p->gray.w, p->gray.h, 24, SDL_PIXELFORMAT_RGB24);
    if (!s)
        return -1;
    SDL_LockSurface(s);
    for (int y = 0; y < p->gray.h; y++) {
        uint8_t *row = (uint8_t *)s->pixels + (size_t)y * s->pitch;
        const uint8_t *src = p->gray.px + (size_t)y * p->gray.w;
        for (int x = 0; x < p->gray.w; x++)
            row[3 * x] = row[3 * x + 1] = row[3 * x + 2] = src[x];
    }
    SDL_UnlockSurface(s);
    g->tex = SDL_CreateTextureFromSurface(g->ren, s);
    g->tex_w = s->w;
    g->tex_h = s->h;
    SDL_FreeSurface(s);
    if (!g->tex) {
        snprintf(g->status, sizeof g->status, "No se puede mostrar la página: %s", SDL_GetError());
        g->tex_w = g->tex_h = 0;
        return -1;
    }
    g->cur = i;
    g->sel = -1;
    g->editing = 0;
    snprintf(g->status, sizeof g->status, "%d renglones reconocidos.", p->nlines);
    fit_page(g);
    return 0;
}

/* Sustituye el documento abierto por otro. */
static void replace_doc(Gui *g, Doc *nd)
{
    doc_free(g->doc);
    *g->doc = *nd;
    g->cur = 0;
    g->sel = -1;
    g->editing = 0;
    SDL_StopTextInput();
    if (g->tex)
        SDL_DestroyTexture(g->tex);
    g->tex = NULL;
    g->tex_w = g->tex_h = 0;
    if (has_doc(g))
        load_page(g, 0);
}

static int next_glyph(Page *p, int from, int dir)
{
    if (!p || p->ng == 0)
        return -1;
    int i = from;
    for (int k = 0; k < p->ng; k++) {
        i = (i + dir + p->ng) % p->ng;
        if (from < 0 && k == 0)
            i = dir > 0 ? 0 : p->ng - 1;
        if (p->g[i].alive)
            return i;
    }
    return -1;
}

static int hit_test(Gui *g, int mx, int my)
{
    Page *p = cur_page(g);
    if (!p)
        return -1;
    float px = (mx - g->offx) / g->scale, py = (my - g->offy) / g->scale;
    float tol = U(g->mobile ? 10 : 2) / g->scale;   /* el dedo es menos preciso que el ratón */
    int best = -1;
    float bd = 0;
    for (int i = 0; i < p->ng; i++) {
        Glyph *gl = &p->g[i];
        if (!gl->alive)
            continue;
        if (px >= gl->r.x0 - tol && px <= gl->r.x1 + tol && py >= gl->r.y0 - tol && py <= gl->r.y1 + tol) {
            float cx = (gl->r.x0 + gl->r.x1) / 2.0f, cy = (gl->r.y0 + gl->r.y1) / 2.0f;
            float area = (float)(gl->r.x1 - gl->r.x0 + 1) * (gl->r.y1 - gl->r.y0 + 1);
            float d = (px - cx) * (px - cx) + (py - cy) * (py - cy) + area * 0.05f;
            if (best < 0 || d < bd) {
                best = i;
                bd = d;
            }
        }
    }
    return best;
}

/* Signos seleccionados (sel y los sel_n-1 siguientes del renglón), en orden de lectura. */
static int selection(Gui *g, int *out, int max)
{
    Page *p = cur_page(g);
    if (!p || g->sel < 0)
        return 0;
    int *ord = malloc(sizeof(int) * (size_t)p->ng);
    int m = line_glyph_order(p, p->g[g->sel].line, g->o, ord), n = 0, k = 0;
    while (k < m && ord[k] != g->sel)
        k++;
    for (; k < m && n < g->sel_n && n < max; k++)
        out[n++] = ord[k];
    free(ord);
    return n;
}

static int in_selection(Gui *g, int gi)
{
    int s[64], n = selection(g, s, 64);
    for (int i = 0; i < n; i++)
        if (s[i] == gi)
            return 1;
    return 0;
}

/* Lectura del renglón del signo seleccionado (se recalcula sólo si cambian los signos). */
static void update_reading(Gui *g)
{
    Page *p = cur_page(g);
    g->rd_text[0] = g->rd_word[0] = 0;
    if (!p || g->sel < 0 || !dict_loaded())
        return;
    int ids[1024], gl[1024];
    int n = line_sign_ids(p, p->g[g->sel].line, g->o, ids, gl, 1024);
    unsigned key = 2166136261u ^ (unsigned)g->sel;
    for (int i = 0; i < n; i++)
        key = (key ^ (unsigned)ids[i]) * 16777619u;
    if (key == g->rd_key && g->rd_text[0])
        return;
    g->rd_key = key;
    DictWord w[256];
    int nw = dict_read(ids, n, w, 256);
    size_t len = 0;
    char u[256];
    for (int k = 0; k < nw; k++) {
        const DictEntry *e = dict_entry(w[k].entry);
        translit_unicode(e->tr, u, sizeof u);
        int here = 0;
        for (int j = w[k].start; j < w[k].start + w[k].len; j++)
            here |= gl[j] == g->sel;
        if (here)
            snprintf(g->rd_word, sizeof g->rd_word, "%.200s%s%.150s%s", u, e->trad[0] ? " «" : "", e->trad,
                     e->trad[0] ? "»" : "");
        if (len < sizeof g->rd_text - 1)
            len += (size_t)snprintf(g->rd_text + len, sizeof g->rd_text - len, "%s%s%s%s", k ? " · " : "",
                                    here ? "[" : "", u, here ? "]" : "");
    }
    float r = 0;
    int c = corpus_best_line(ids, n, &r);
    if (c >= 0 && r >= 0.5f && len < sizeof g->rd_text - 1) {
        const CorpusLine *cl = corpus_line(c);
        snprintf(g->rd_text + len, sizeof g->rd_text - len, "%sparecido a %s, %d (%.0f %%)%s%.200s",
                 len ? " · " : "", corpus_text_name(cl->text), cl->line, 100 * r, cl->trad[0] ? ": " : "", cl->trad);
    }
}

static void run_query(Gui *g)
{
    g->nhits = g->ntotal = 0;
    g->ov_scroll = 0;
    if (g->query[0])
        g->nhits = dict_search(g->query, g->hits, MAXHITS, &g->ntotal);
}

static void open_search(Gui *g, int from_sel)
{
    g->overlay = 1;
    g->q_from_sel = 0;
    if (from_sel) {
        int s[64], n = selection(g, s, 64);
        Page *p = cur_page(g);
        g->query[0] = 0;
        for (int i = 0; i < n; i++) {
            const char *c = p->g[s[i]].code[0] ? p->g[s[i]].code : "?";
            if (strlen(g->query) + strlen(c) + 2 < sizeof g->query) {
                if (i)
                    strcat(g->query, " ");
                strcat(g->query, c);
            }
        }
        g->q_from_sel = n;
        g->sedit = 0;
    } else {
        g->sedit = 1;
        SDL_StartTextInput();
    }
    run_query(g);
}

static void close_search(Gui *g)
{
    g->overlay = 0;
    g->sedit = 0;
    SDL_StopTextInput();
}

/* Desplaza la vista para que el signo seleccionado quede a la vista. */
static void ensure_visible(Gui *g)
{
    Page *p = cur_page(g);
    if (!p || g->sel < 0)
        return;
    int ww, wh;
    measure(g, &ww, &wh);
    Glyph *gl = &p->g[g->sel];
    float x0 = g->offx + gl->r.x0 * g->scale, x1 = g->offx + gl->r.x1 * g->scale;
    float y0 = g->offy + gl->r.y0 * g->scale, y1 = g->offy + gl->r.y1 * g->scale;
    int top = g->top_h, bot = wh - g->panel_h;
    if (x0 < 0 || x1 > ww || y0 < top || y1 > bot) {
        g->offx += ww / 2.0f - (x0 + x1) / 2;
        g->offy += (top + bot) / 2.0f - (y0 + y1) / 2;
    }
}

/* ---------- acciones ---------- */

static void select_glyph(Gui *g, int i)
{
    g->sel = i;
    g->sel_n = 1;
    ensure_visible(g);
}

/* Aplica a la selección los signos de una palabra del diccionario. */
static void assign(Gui *g, int gi, const char *code);
static void apply_hit(Gui *g, int i)
{
    if (i < 0 || i >= g->nhits || g->hits[i].kind != 0)
        return;
    const DictEntry *e = dict_entry(g->hits[i].idx);
    int s[64], n = selection(g, s, 64);
    if (n != e->n || n != g->q_from_sel) {
        snprintf(g->status, sizeof g->status, "Para aplicar una palabra, selecciona tantos signos como tiene (%d).", e->n);
        return;
    }
    int learn = g->learn;
    for (int k = 0; k < n; k++)
        assign(g, s[k], sign_code(e->s[k]));
    g->learn = learn;
    char u[256];
    translit_unicode(e->tr, u, sizeof u);
    snprintf(g->status, sizeof g->status, "Aplicada la palabra %.200s (%d signos).", u, n);
    close_search(g);
    g->sel = s[0];
    g->sel_n = n;
}

static void assign(Gui *g, int gi, const char *code)
{
    Page *p = cur_page(g);
    Glyph *gl = &p->g[gi];
    snprintf(gl->code, CODE_LEN, "%s", code);
    gl->manual = 1;
    int learned = 0;
    if (g->learn) {
        uint8_t *m;
        int w, h;
        if (glyph_mask(p, gi, &m, &w, &h) == 0) {
            learned = ts_learn(g->ts, g->o->tmpl_dir, code, m, w, h,
                               p->line_h > 0 ? h / p->line_h : -1.0f) == 0;
            free(m);
        }
    }
    snprintf(g->status, sizeof g->status, "Signo %d = %s%s", gi, code,
             learned ? " (plantilla guardada; «Reclasificar» la aplica al resto)" : "");
    select_glyph(g, next_glyph(p, gi, 1));
}

static void reclassify(Gui *g)
{
    Page *p = cur_page(g);
    if (!p)
        return;
    int changed = 0;
    for (int i = 0; i < p->ng; i++) {
        if (!p->g[i].alive || p->g[i].manual)
            continue;
        char old[CODE_LEN];
        memcpy(old, p->g[i].code, CODE_LEN);
        classify_glyph(p, i, g->ts);
        changed += strcmp(old, p->g[i].code) != 0;
    }
    snprintf(g->status, sizeof g->status, "Reclasificado: %d signos han cambiado.", changed);
}

static void save_mdc(Gui *g)
{
    if (!has_doc(g))
        return;
    const char *path = platform_mdc_path(g->o);
    render(g, "Generando el MdC...");
    Str out = {0};
    doc_to_mdc(g->doc, g->o, g->ts, &out);
    FILE *f = fopen(path, "w");
    if (!f) {
        snprintf(g->status, sizeof g->status, "No se puede escribir %s", path);
    } else {
        fputs(out.s ? out.s : "", f);
        fclose(f);
        snprintf(g->status, sizeof g->status, "MdC guardado en %s (%zu bytes).", path, out.n);
        platform_share(path, out.s ? out.s : "");
    }
    free(out.s);
}

/* Tras cambiar de modelo los índices de los candidatos ya no valen: se reclasifica
 * todo, conservando los códigos que el usuario haya corregido. */
static void reload_model(Gui *g)
{
    render(g, "Cargando el modelo...");
    ts_free(g->ts);
    app_load_templates(g->o, g->ts, 1);
    for (int pi = 0; g->doc && pi < g->doc->np; pi++) {
        Page *p = &g->doc->pages[pi];
        if (!p->processed)
            continue;
        for (int i = 0; i < p->ng; i++) {
            char keep[CODE_LEN];
            memcpy(keep, p->g[i].code, CODE_LEN);
            classify_glyph(p, i, g->ts);
            if (p->g[i].manual)
                memcpy(p->g[i].code, keep, CODE_LEN);
        }
    }
    snprintf(g->status, sizeof g->status, "Modelo %s cargado: %d plantillas.",
             g->o->model ? g->o->model : "(fuente)", g->ts->n);
}

static void start_edit(Gui *g)
{
    if (g->sel < 0)
        return;
    g->editing = 1;
    g->buf[0] = 0;
    SDL_StartTextInput();
}

static void finish_edit(Gui *g, int accept)
{
    if (accept && g->buf[0]) {
        char code[CODE_LEN];
        if (code_from_folder(g->buf, code) == 0)      /* "g17" -> "G17", "aa15" -> "Aa15" */
            assign(g, g->sel, code);
        else if (valid_code(g->buf))
            assign(g, g->sel, g->buf);
        else
            snprintf(g->status, sizeof g->status, "«%s» no es un código Gardiner válido.", g->buf);
    }
    g->editing = 0;
    SDL_StopTextInput();
}

static void zoom_at(Gui *g, float f, float mx, float my)
{
    float ns = g->scale * f;
    if (ns < 0.03f || ns > 16.0f)
        return;
    g->offx = mx - (mx - g->offx) * f;
    g->offy = my - (my - g->offy) * f;
    g->scale = ns;
}

static void do_action(Gui *g, int a)
{
    Page *p = cur_page(g);
    switch (a) {
    case A_OPEN: platform_pick_file(); break;
    case A_CAMERA: platform_take_photo(); break;
    case A_DICT: open_search(g, 0); break;
    case A_DICT_CLOSE: close_search(g); break;
    case A_DICT_EDIT: g->sedit = 1; SDL_StartTextInput(); break;
    case A_DICT_GO: run_query(g); g->sedit = 0; SDL_StopTextInput(); break;
    case A_SEARCH_SEL: if (g->sel >= 0) open_search(g, 1); break;
    case A_EXTEND:
        if (p && g->sel >= 0) {
            int s[64], n = selection(g, s, 64);
            g->sel_n = n + 1;
            if (selection(g, s, 64) == n) {
                g->sel_n = n;
                snprintf(g->status, sizeof g->status, "La selección ya llega al final del renglón.");
            }
        }
        break;
    case A_PREV_PAGE: if (g->cur > 0) load_page(g, g->cur - 1); break;
    case A_NEXT_PAGE: if (g->doc && g->cur + 1 < g->doc->np) load_page(g, g->cur + 1); break;
    case A_FIT: fit_page(g); break;
    case A_LEARN:
        g->learn = !g->learn;
        snprintf(g->status, sizeof g->status, "Aprendizaje %s.", g->learn ? "activado" : "desactivado");
        break;
    case A_RECLASS: reclassify(g); break;
    case A_SAVE: save_mdc(g); break;
    case A_PREV_SIGN: select_glyph(g, next_glyph(p, g->sel, -1)); break;
    case A_NEXT_SIGN: select_glyph(g, next_glyph(p, g->sel, 1)); break;
    case A_EDIT: start_edit(g); break;
    case A_EDIT_OK: finish_edit(g, 1); break;
    case A_EDIT_CANCEL: finish_edit(g, 0); break;
    case A_DELETE:
        if (p && g->sel >= 0) {
            p->g[g->sel].alive = 0;
            snprintf(g->status, sizeof g->status, "Signo %d eliminado.", g->sel);
            select_glyph(g, next_glyph(p, g->sel, 1));
        }
        break;
    case A_MERGE:
        if (p && g->sel >= 0) {
            if (glyph_merge_next(p, g->sel) >= 0) {
                classify_glyph(p, g->sel, g->ts);
                snprintf(g->status, sizeof g->status, "Unido con el siguiente: ahora %s.", p->g[g->sel].code);
            } else {
                snprintf(g->status, sizeof g->status, "No hay signo siguiente en el renglón.");
            }
        }
        break;
    default:
        if (a >= A_HIT) {
            apply_hit(g, a - A_HIT);
        } else if (a >= A_CAND && a < A_CAND + NCAND && p && g->sel >= 0) {
            int k = a - A_CAND;
            if (k < p->g[g->sel].ncand)
                assign(g, g->sel, g->ts->t[p->g[g->sel].cand[k]].code);
        }
        break;
    }
}

static void handle_key(Gui *g, SDL_Keysym ks)
{
    int ww, wh;
    measure(g, &ww, &wh);
    float cy = (g->top_h + wh - g->panel_h) / 2.0f;

    if (g->overlay) {
        size_t n = strlen(g->query);
        if (ks.sym == SDLK_RETURN || ks.sym == SDLK_KP_ENTER) {
            do_action(g, A_DICT_GO);
        } else if (ks.sym == SDLK_ESCAPE || ks.sym == SDLK_AC_BACK) {
            if (g->sedit && g->mobile) { g->sedit = 0; SDL_StopTextInput(); }
            else close_search(g);
        } else if (ks.sym == SDLK_BACKSPACE && n && g->sedit) {
            while (n > 0 && ((unsigned char)g->query[n - 1] & 0xC0) == 0x80)
                n--;                       /* borra un carácter UTF-8 entero */
            g->query[n ? n - 1 : 0] = 0;
            run_query(g);
        } else if (ks.sym == SDLK_UP) {
            g->ov_scroll -= U(60);
        } else if (ks.sym == SDLK_DOWN) {
            g->ov_scroll += U(60);
        } else if (!g->sedit && ks.sym == SDLK_d) {
            close_search(g);
        }
        return;
    }
    if (g->editing) {
        size_t n = strlen(g->buf);
        if (ks.sym == SDLK_RETURN || ks.sym == SDLK_KP_ENTER)
            finish_edit(g, 1);
        else if (ks.sym == SDLK_ESCAPE || ks.sym == SDLK_AC_BACK)
            finish_edit(g, 0);
        else if (ks.sym == SDLK_BACKSPACE && n)
            g->buf[n - 1] = 0;
        return;
    }

    switch (ks.sym) {
    case SDLK_q:
        g->running = 0;
        break;
    case SDLK_ESCAPE:
    case SDLK_AC_BACK:
        if (g->sel >= 0) { g->sel = -1; g->sel_n = 1; }
        else g->running = 0;
        break;
    case SDLK_TAB: select_glyph(g, next_glyph(cur_page(g), g->sel, (ks.mod & KMOD_SHIFT) ? -1 : 1)); break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: start_edit(g); break;
    case SDLK_1: case SDLK_2: case SDLK_3: case SDLK_4: case SDLK_5:
        do_action(g, A_CAND + (ks.sym - SDLK_1));
        break;
    case SDLK_DELETE:
    case SDLK_BACKSPACE: do_action(g, A_DELETE); break;
    case SDLK_m: do_action(g, A_MERGE); break;
    case SDLK_d: if (dict_loaded()) do_action(g, A_DICT); break;
    case SDLK_e: do_action(g, A_EXTEND); break;
    case SDLK_b: if (dict_loaded()) do_action(g, A_SEARCH_SEL); break;
    case SDLK_r: do_action(g, A_RECLASS); break;
    case SDLK_s: do_action(g, A_SAVE); break;
    case SDLK_f: do_action(g, A_FIT); break;
    case SDLK_l: do_action(g, A_LEARN); break;
    case SDLK_o: if (g->mobile) do_action(g, A_OPEN); break;
    case SDLK_PAGEDOWN: do_action(g, A_NEXT_PAGE); break;
    case SDLK_PAGEUP: do_action(g, A_PREV_PAGE); break;
    case SDLK_LEFT:  g->offx += U(80); break;
    case SDLK_RIGHT: g->offx -= U(80); break;
    case SDLK_UP:    g->offy += U(80); break;
    case SDLK_DOWN:  g->offy -= U(80); break;
    case SDLK_PLUS: case SDLK_KP_PLUS: case SDLK_EQUALS:
        zoom_at(g, 1.25f, ww / 2.0f, cy);
        break;
    case SDLK_MINUS: case SDLK_KP_MINUS:
        zoom_at(g, 0.8f, ww / 2.0f, cy);
        break;
    default:
        break;
    }
}

static void handle_request(Gui *g, Request *r)
{
    switch (r->kind) {
    case REQ_DOCUMENT:
    case REQ_PHOTO: {
        Doc nd;
        int numbered = r->kind == REQ_DOCUMENT && r->n > 0 && strstr(r->files[0], "/p-") != NULL;
        if (doc_from_files(&nd, r->files, r->n, r->kind == REQ_PHOTO, numbered) == 0)
            replace_doc(g, &nd);
        else
            snprintf(g->status, sizeof g->status, "No se ha podido abrir el documento.");
        break;
    }
    case REQ_MODEL:
        reload_model(g);
        break;
    case REQ_STATUS:
        snprintf(g->status, sizeof g->status, "%s", r->msg);
        break;
    }
}

/* Dedos sobre la pantalla: con dos, pellizco (zoom) y desplazamiento. */
static int finger_index(Gui *g, SDL_FingerID id)
{
    for (int i = 0; i < g->nfing; i++)
        if (g->fing[i].id == id)
            return i;
    return -1;
}

static void handle_finger(Gui *g, SDL_Event *e)
{
    int ww, wh;
    SDL_GetRendererOutputSize(g->ren, &ww, &wh);
    float x = e->tfinger.x * ww, y = e->tfinger.y * wh;
    int i = finger_index(g, e->tfinger.fingerId);
    if (e->type == SDL_FINGERDOWN) {
        g->ntouch++;
        if (i < 0 && g->nfing < 2) {
            g->fing[g->nfing++] = (Finger){e->tfinger.fingerId, x, y};
        }
        if (g->ntouch >= 2) {
            g->pinch = 1;
            g->moved = 1;   /* ya no es un toque */
        }
    } else if (e->type == SDL_FINGERUP) {
        if (g->ntouch > 0)
            g->ntouch--;
        if (i >= 0) {
            g->fing[i] = g->fing[g->nfing - 1];
            g->nfing--;
        }
        if (g->ntouch == 0) {
            g->pinch = 0;
            g->nfing = 0;
        }
    } else if (i >= 0) {
        Finger old[2] = {g->fing[0], g->fing[1]};
        g->fing[i].x = x;
        g->fing[i].y = y;
        if (g->pinch && g->nfing == 2 && !g->overlay) {
            float d0 = hypotf(old[0].x - old[1].x, old[0].y - old[1].y);
            float d1 = hypotf(g->fing[0].x - g->fing[1].x, g->fing[0].y - g->fing[1].y);
            float c0x = (old[0].x + old[1].x) / 2, c0y = (old[0].y + old[1].y) / 2;
            float c1x = (g->fing[0].x + g->fing[1].x) / 2, c1y = (g->fing[0].y + g->fing[1].y) / 2;
            if (d0 > 4 && d1 > 4)
                zoom_at(g, d1 / d0, c1x, c1y);
            g->offx += c1x - c0x;
            g->offy += c1y - c0y;
        }
    }
}

static void handle_event(Gui *g, SDL_Event *e)
{
    if (e->type == g_event_type) {
        Request *r = e->user.data1;
        if (r)
            handle_request(g, r);
        request_free(r);
        return;
    }
    switch (e->type) {
    case SDL_QUIT:
        g->running = 0;
        break;
    case SDL_WINDOWEVENT:
        if (e->window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
            fit_page(g);
        break;
#ifndef __ANDROID__   /* en Android los archivos llegan como Request desde HieroActivity */
    case SDL_DROPFILE: {   /* escritorio: soltar un archivo en la ventana */
        Doc nd;
        char msg[160];
        snprintf(msg, sizeof msg, "Abriendo %s...", e->drop.file);
        render(g, msg);
        if (doc_open(&nd, e->drop.file, g->o) == 0)
            replace_doc(g, &nd);
        else
            snprintf(g->status, sizeof g->status, "No se puede abrir %s", e->drop.file);
        SDL_free(e->drop.file);
        break;
    }
#endif
    case SDL_MOUSEWHEEL: {
        int mx, my;
        SDL_GetMouseState(&mx, &my);
        if (g->overlay)
            g->ov_scroll -= e->wheel.y * U(60);
        else
            zoom_at(g, e->wheel.y > 0 ? 1.15f : 1 / 1.15f, (float)mx, (float)my);
        break;
    }
    case SDL_FINGERDOWN:
    case SDL_FINGERUP:
    case SDL_FINGERMOTION:
        handle_finger(g, e);
        break;
    case SDL_MOUSEBUTTONDOWN:
        if (e->button.button == SDL_BUTTON_LEFT) {
            int ww, wh;
            SDL_GetRendererOutputSize(g->ren, &ww, &wh);
            g->press = 1;
            g->moved = g->ntouch >= 2;
            g->press_x = e->button.x;
            g->press_y = e->button.y;
            g->press_action = button_at(g, e->button.x, e->button.y);
            g->press_in_page = g->press_action == A_NONE && e->button.y >= g->top_h &&
                               e->button.y < wh - g->panel_h;
        } else {
            g->dragging = 1;
        }
        break;
    case SDL_MOUSEBUTTONUP:
        if (e->button.button != SDL_BUTTON_LEFT) {
            g->dragging = 0;
        } else if (g->press) {
            g->press = 0;
            if (g->press_action != A_NONE) {
                if (button_at(g, e->button.x, e->button.y) == g->press_action)
                    do_action(g, g->press_action);
            } else if (g->press_in_page && !g->moved && !g->editing && !g->overlay) {
                g->sel = hit_test(g, e->button.x, e->button.y);
                g->sel_n = 1;
            }
        }
        break;
    case SDL_MOUSEMOTION:
        if (g->pinch || g->ntouch >= 2)
            break;
        if (g->press && g->press_in_page) {
            int dx = e->motion.x - g->press_x, dy = e->motion.y - g->press_y;
            if (!g->moved && dx * dx + dy * dy > U(8) * U(8))
                g->moved = 1;
            if (g->moved && g->overlay) {
                g->ov_scroll -= e->motion.yrel;
            } else if (g->moved) {
                g->offx += e->motion.xrel;
                g->offy += e->motion.yrel;
            }
        } else if (g->dragging) {
            g->offx += e->motion.xrel;
            g->offy += e->motion.yrel;
        }
        break;
    case SDL_TEXTINPUT:
        if (g->overlay && g->sedit) {
            size_t n = strlen(g->query), l = strlen(e->text.text);
            if (n + l + 1 < sizeof g->query) {
                memcpy(g->query + n, e->text.text, l + 1);
                run_query(g);
            }
        } else if (g->editing)
            for (const char *c = e->text.text; *c; c++) {
                size_t n = strlen(g->buf);
                if (isalnum((unsigned char)*c) && n + 1 < CODE_LEN) {
                    g->buf[n] = *c;
                    g->buf[n + 1] = 0;
                }
            }
        break;
    case SDL_KEYDOWN:
        handle_key(g, e->key.keysym);
        break;
    default:
        break;
    }
}

/* ---------- dibujo ---------- */

static void set_color(Gui *g, SDL_Color c) { SDL_SetRenderDrawColor(g->ren, c.r, c.g, c.b, c.a); }

static void draw_page(Gui *g, int ww, int y0, int y1)
{
    Page *p = cur_page(g);
    SDL_Rect clip = {0, y0, ww, y1 - y0};
    SDL_RenderSetClipRect(g->ren, &clip);
    if (g->tex) {
        SDL_Rect dst = {(int)g->offx, (int)g->offy, (int)(g->tex_w * g->scale), (int)(g->tex_h * g->scale)};
        SDL_RenderCopy(g->ren, g->tex, NULL, &dst);
    }
    const SDL_Color c_ok = {0, 150, 60, 255}, c_mid = {200, 140, 0, 255}, c_bad = {210, 30, 30, 255},
                    c_man = {40, 90, 230, 255}, c_ul = {200, 0, 200, 255}, c_sel = {255, 0, 255, 255};
    set_color(g, c_ul);
    for (int u = 0; u < p->nul; u++) {
        int y = (int)(g->offy + p->ul[u].y1 * g->scale) + 1;
        SDL_RenderDrawLine(g->ren, (int)(g->offx + p->ul[u].x0 * g->scale), y,
                           (int)(g->offx + p->ul[u].x1 * g->scale), y);
    }
    int lab_h = line_h(g->small);
    for (int i = 0; i < p->ng; i++) {
        Glyph *gl = &p->g[i];
        if (!gl->alive)
            continue;
        SDL_Rect r = {(int)(g->offx + gl->r.x0 * g->scale) - 1, (int)(g->offy + gl->r.y0 * g->scale) - 1,
                      (int)((gl->r.x1 - gl->r.x0 + 1) * g->scale) + 2, (int)((gl->r.y1 - gl->r.y0 + 1) * g->scale) + 2};
        if (r.x > ww || r.y > y1 || r.x + r.w < 0 || r.y + r.h < y0)
            continue;
        SDL_Color c = gl->manual ? c_man
                    : gl->score <= g->o->uncertain_thr * 0.75f ? c_ok
                    : gl->score <= g->o->uncertain_thr ? c_mid : c_bad;
        int insel = i == g->sel || (g->sel_n > 1 && in_selection(g, i));
        set_color(g, insel ? c_sel : c);
        SDL_RenderDrawRect(g->ren, &r);
        if (insel) {
            for (int k = 1; k <= U(2) + 1; k++) {
                SDL_Rect r2 = {r.x - k, r.y - k, r.w + 2 * k, r.h + 2 * k};
                SDL_RenderDrawRect(g->ren, &r2);
            }
        }
        /* etiquetas sólo si hay sitio (con la página reducida tapan el texto) */
        if (gl->code[0] && p->line_h * g->scale >= 1.6f * lab_h) {
            CacheEntry *e = cached(g, gl->code, gl->manual ? 'M' : (c.r == c_ok.r ? 'G' : c.r == c_mid.r ? 'Y' : 'R'), c);
            if (e) {
                SDL_Rect d = {r.x, r.y - e->h, e->w, e->h};
                SDL_RenderCopy(g->ren, e->t, &e->src, &d);
            }
        }
    }
    SDL_RenderSetClipRect(g->ren, NULL);
}

static void draw_empty(Gui *g, int ww, int y0, int y1)
{
    const SDL_Color white = {235, 235, 235, 255};
    int maxw = ww - U(40);
    const char *t1 = "HieroOCR · jeroglíficos egipcios a MdC";
    const char *t2 = g->mobile
        ? "Abre una imagen o un PDF, o haz una foto de la página con la cámara."
        : "Arrastra a esta ventana una imagen o un PDF (o pásalo como argumento).";
    int h = line_h(g->ui) * 2 + U(16) + draw_wrapped(g, g->ui, 0, 0, t2, white, maxw, 4, 0);
    int y = y0 + (y1 - y0 - h) / 2 - (g->mobile ? U(40) : 0);
    int tw = text_w(g->ui, t1);
    draw_text(g, g->ui, (ww - (tw < maxw ? tw : maxw)) / 2, y, t1, white, maxw);
    y += line_h(g->ui) * 2;
    draw_wrapped(g, g->ui, U(20), y, t2, white, maxw, 4, 1);
    y += draw_wrapped(g, g->ui, U(20), y, t2, white, maxw, 4, 0) + U(24);
    if (g->mobile) {
        int bw = ww - U(80) < U(360) ? ww - U(80) : U(360), bh = U(52);
        SDL_Rect r1 = {(ww - bw) / 2, y, bw, bh}, r2 = {(ww - bw) / 2, y + bh + U(14), bw, bh};
        draw_button(g, r1, "Abrir imagen o PDF", B_PRIMARY);
        add_button(g, r1, A_OPEN);
        draw_button(g, r2, "Hacer una foto", B_PRIMARY);
        add_button(g, r2, A_CAMERA);
    }
}

static void draw_editor(Gui *g, int ww, int y0)
{
    int pad = U(10), bh = U(40);
    char line[96];
    snprintf(line, sizeof line, "Código Gardiner: %s_", g->buf);
    SDL_Rect box = {pad, y0 + pad, ww - 2 * pad, line_h(g->ui) + bh + 3 * pad};
    set_rgb(g, 255, 255, 240);
    SDL_RenderFillRect(g->ren, &box);
    set_rgb(g, 30, 70, 200);
    SDL_RenderDrawRect(g->ren, &box);
    const SDL_Color blue = {30, 70, 200, 255};
    draw_text(g, g->ui, box.x + pad, box.y + pad, line, blue, box.w - 2 * pad);
    BtnSpec b[] = {{"Aceptar", A_EDIT_OK, B_PRIMARY}, {"Cancelar", A_EDIT_CANCEL, B_NORMAL}};
    flow_buttons(g, box.x + pad, box.y + 2 * pad + line_h(g->ui), box.w - 2 * pad, b, 2, 1);
}

/* Fila de jeroglíficos de alto h; devuelve el ancho usado. */
static int draw_signs(Gui *g, int x, int y, int h, const int *ids, int n, int maxw, int draw)
{
    const SDL_Color black = {20, 20, 20, 255};
    int x0 = x, gap = U(3);
    for (int k = 0; k < n; k++) {
        CacheEntry *e = cached(g, sign_code(ids[k]), 'H', black);
        int w, hh = h;
        if (e) {
            float kk = (float)h / (e->h > 0 ? e->h : 1);
            if (e->w * kk > 1.6f * h)
                kk = 1.6f * h / e->w;
            w = (int)(e->w * kk);
            hh = (int)(e->h * kk);
        } else {
            w = text_w(g->small, sign_code(ids[k]));
        }
        if (x + w - x0 > maxw) {
            if (draw)
                draw_text(g, g->small, x, y + h / 3, "…", black, 0);
            return x - x0 + U(12);
        }
        if (draw) {
            if (e) {
                SDL_Rect d = {x, y + (h - hh), w, hh};
                SDL_RenderCopy(g->ren, e->t, &e->src, &d);
            } else {
                draw_text(g, g->small, x, y + h / 3, sign_code(ids[k]), black, 0);
            }
        }
        x += w + gap;
    }
    return x - x0;
}

/* Buscador de OpenGlyph: diccionario y corpus. */
static void draw_overlay(Gui *g, int ww, int y0, int y1)
{
    const SDL_Color black = {20, 20, 20, 255}, grey = {95, 95, 95, 255}, blue = {30, 70, 200, 255},
                    brown = {120, 60, 10, 255};
    int pad = U(10), maxw = ww - 2 * pad;
    SDL_Rect bg = {0, y0, ww, y1 - y0};
    set_rgb(g, 250, 248, 242);
    SDL_RenderFillRect(g->ren, &bg);

    /* cabecera: consulta y botones */
    char line[400];
    snprintf(line, sizeof line, "Buscar: %s%s", g->query, g->sedit ? "_" : "");
    int y = y0 + pad;
    int qh = line_h(g->ui) + U(14);
    SDL_Rect qb = {pad, y, maxw, qh};
    set_rgb(g, 255, 255, 255);
    SDL_RenderFillRect(g->ren, &qb);
    set_rgb(g, g->sedit ? 30 : 150, g->sedit ? 70 : 150, g->sedit ? 200 : 150);
    SDL_RenderDrawRect(g->ren, &qb);
    draw_text(g, g->ui, qb.x + U(8), qb.y + U(7), line, blue, qb.w - U(16));
    add_button(g, qb, A_DICT_EDIT);
    y += qh + U(6);
    BtnSpec b[] = {{"Buscar", A_DICT_GO, B_PRIMARY}, {"Cerrar", A_DICT_CLOSE, B_NORMAL}};
    y += flow_buttons(g, pad, y, maxw, b, 2, 1) + U(6);
    if (g->q_from_sel)
        snprintf(line, sizeof line, "%d resultados. Toca una palabra de %d signos para aplicarla a la selección.",
                 g->ntotal, g->q_from_sel);
    else if (g->query[0])
        snprintf(line, sizeof line, "%d resultados%s", g->ntotal, g->ntotal > g->nhits ? " (se muestran los primeros)" : "");
    else
        snprintf(line, sizeof line, "Escribe códigos Gardiner o MdC (G17 D21, nfr-f:r; ? = cualquier signo), "
                 "una transliteración (nfr, Htp) o una palabra de la traducción (horizon).");
    y += draw_wrapped(g, g->small, pad, y, line, grey, maxw, 3, 1) + U(6);
    set_rgb(g, 190, 190, 190);
    SDL_RenderDrawLine(g->ren, 0, y, ww, y);
    int top = y + 1;

    /* resultados con desplazamiento */
    SDL_Rect clip = {0, top, ww, y1 - top};
    SDL_RenderSetClipRect(g->ren, &clip);
    int sh = U(34), sl = line_h(g->small), uh = line_h(g->ui);
    int total_h = 0;
    for (int pass = 0; pass < 2; pass++) {   /* 0: medir, 1: dibujar */
        if (pass == 1) {
            float maxs = (float)(total_h - (y1 - top) + U(20));
            if (g->ov_scroll > maxs) g->ov_scroll = maxs;
            if (g->ov_scroll < 0) g->ov_scroll = 0;
        }
        int yy = top + U(6) - (pass ? (int)g->ov_scroll : 0), last = -1;
        for (int i = 0; i < g->nhits; i++) {
            DictHit *h = &g->hits[i];
            int vis = pass && yy < y1 && yy + 4 * sl + sh > top;
            if (h->kind != last) {
                if (vis)
                    draw_text(g, g->ui, pad, yy, h->kind == 0 ? "Diccionario de OpenGlyph" : "Corpus de OpenGlyph", brown, maxw);
                yy += uh + U(4);
                last = h->kind;
            }
            int y_start = yy;
            char u[600];
            if (h->kind == 0) {
                const DictEntry *e = dict_entry(h->idx);
                int wsig = draw_signs(g, pad, yy, sh, e->s, e->n, maxw / 2, vis);
                translit_unicode(e->tr, u, sizeof u);
                if (vis)
                    draw_text(g, g->ui, pad + wsig + U(10), yy + sh - uh, u[0] ? u : "—", blue, maxw - wsig - U(10));
                yy += sh + U(4);
                char t[600];
                snprintf(t, sizeof t, "%s%s", e->trad[0] ? e->trad : "(sin traducción)",
                         e->freq ? "" : "");
                yy += draw_wrapped(g, g->small, pad + U(6), yy, t, black, maxw - U(6), 2, vis);
                if (e->freq) {
                    snprintf(t, sizeof t, "%d apariciones en el corpus", e->freq);
                    yy += draw_wrapped(g, g->small, pad + U(6), yy, t, grey, maxw - U(6), 1, vis);
                }
                if (pass && g->q_from_sel == e->n) {
                    SDL_Rect r = {0, y_start - U(3), ww, yy - y_start + U(6)};
                    if (r.y + r.h > top && r.y < y1)
                        add_button(g, r, A_HIT + i);
                }
            } else {
                const CorpusLine *c = corpus_line(h->idx);
                snprintf(u, sizeof u, "%s, renglón %d", corpus_text_name(c->text), c->line);
                yy += draw_wrapped(g, g->small, pad, yy, u, grey, maxw, 1, vis) + U(2);
                /* los signos alrededor de la coincidencia */
                int from = h->pos > 3 ? h->pos - 3 : 0, cnt = c->n - from;
                draw_signs(g, pad, yy, sh, c->s + from, cnt, maxw, vis);
                yy += sh + U(4);
                if (c->tr[0]) {
                    translit_unicode(c->tr, u, sizeof u);
                    yy += draw_wrapped(g, g->small, pad + U(6), yy, u, blue, maxw - U(6), 2, vis);
                }
                if (c->trad[0])
                    yy += draw_wrapped(g, g->small, pad + U(6), yy, c->trad, black, maxw - U(6), 2, vis);
            }
            yy += U(10);
            if (vis) {
                set_rgb(g, 225, 222, 214);
                SDL_RenderDrawLine(g->ren, pad, yy - U(5), ww - pad, yy - U(5));
            }
        }
        total_h = yy - (top + U(6)) + (pass ? (int)g->ov_scroll : 0);
    }
    SDL_RenderSetClipRect(g->ren, NULL);
}

static void render(Gui *g, const char *busy)
{
    int ww, wh;
    measure(g, &ww, &wh);
    g->nbtn = 0;
    SDL_SetRenderDrawColor(g->ren, 70, 70, 74, 255);
    SDL_RenderClear(g->ren);

    int y0 = g->top_h, y1 = wh - g->panel_h;
    if (has_doc(g) && g->tex)
        draw_page(g, ww, y0, y1);
    else if (!busy)
        draw_empty(g, ww, y0, y1);

    if (g->overlay)
        draw_overlay(g, ww, y0, wh);
    toolbar(g, ww, 1);
    if (g->panel_h > 0) {
        SDL_Rect pr = {0, y1, ww, g->panel_h};
        set_rgb(g, 236, 234, 228);
        SDL_RenderFillRect(g->ren, &pr);
        set_rgb(g, 150, 150, 150);
        SDL_RenderDrawLine(g->ren, 0, y1, ww, y1);
        SDL_RenderSetClipRect(g->ren, &pr);
        panel(g, ww, y1, 1);
        SDL_RenderSetClipRect(g->ren, NULL);
    }
    if (g->editing)
        draw_editor(g, ww, y0);

    if (busy) {   /* aviso mientras se trabaja (el bucle de eventos está parado) */
        int tw = text_w(g->ui, busy), bw = tw + U(40), bh = line_h(g->ui) + U(30);
        if (bw > ww - U(20)) bw = ww - U(20);
        SDL_Rect r = {(ww - bw) / 2, (y0 + y1 - bh) / 2, bw, bh};
        set_rgb(g, 30, 30, 34);
        SDL_RenderFillRect(g->ren, &r);
        const SDL_Color white = {255, 255, 255, 255};
        draw_text(g, g->ui, r.x + U(20), r.y + U(15), busy, white, bw - U(40));
    }
    SDL_RenderPresent(g->ren);
}

static void screenshot(Gui *g, const char *path)
{
    int ww, wh;
    SDL_GetRendererOutputSize(g->ren, &ww, &wh);
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, ww, wh, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!s)
        return;
    if (SDL_RenderReadPixels(g->ren, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) == 0)
        IMG_SavePNG(s, path);
    SDL_FreeSurface(s);
}

int gui_run(Doc *doc, Options *o, TemplateSet *ts)
{
    static Gui gs;   /* la caché es grande */
    Gui *g = &gs;
    memset(g, 0, sizeof *g);
    g->doc = doc;
    g->o = o;
    g->ts = ts;
    g->learn = 1;
    g->sel = -1;
    g->running = 1;
    g->mobile = platform_is_mobile();
    gui_event_type();

    int W = 1280, H = 920;
    const char *ven = getenv("HIEROOCR_VENTANA");   /* p. ej. 1080x2200, para probar */
    if (ven)
        sscanf(ven, "%dx%d", &W, &H);
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "Portrait PortraitUpsideDown LandscapeLeft LandscapeRight");
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
    g->win = SDL_CreateWindow("hieroocr — OCR de jeroglíficos a MdC", SDL_WINDOWPOS_CENTERED,
                             SDL_WINDOWPOS_CENTERED, W, H, SDL_WINDOW_RESIZABLE);
    if (!g->win) {
        fprintf(stderr, "No se puede crear la ventana: %s\n", SDL_GetError());
        return 1;
    }
    g->ren = SDL_CreateRenderer(g->win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g->ren)
        g->ren = SDL_CreateRenderer(g->win, -1, SDL_RENDERER_SOFTWARE);
    if (!g->ren) {
        fprintf(stderr, "No se puede crear el renderizador: %s\n", SDL_GetError());
        SDL_DestroyWindow(g->win);
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    SDL_StopTextInput();

    g->u = ui_scale();
    g->ui = open_ui_font(U(16));
    g->small = open_ui_font(U(13));
    if (!g->ui || !g->small)
        fprintf(stderr, "Aviso: no se encontró una fuente para la interfaz (define HIEROOCR_UI_FONT).\n");
    if (o->font)
        g->hiero = TTF_OpenFont(o->font, U(44));

    if (has_doc(g) && load_page(g, 0) != 0)
        fprintf(stderr, "No se puede cargar la primera página.\n");

    while (g->running) {
        SDL_Event e;
        if (SDL_WaitEventTimeout(&e, 100)) {
            do
                handle_event(g, &e);
            while (g->running && SDL_PollEvent(&e));
        }
        if (!g->running)
            break;
        if (o->screenshot && has_doc(g) && g->sel < 0 && !getenv("HIEROOCR_SIN_SELECCION")) {
            g->sel = next_glyph(cur_page(g), 0, 1);   /* modo de prueba: zoom sobre el primer renglón */
            const char *z = getenv("HIEROOCR_ZOOM");
            if (z && g->sel >= 0) {
                Glyph *s0 = &cur_page(g)->g[g->sel];
                measure(g, &W, &H);
                g->scale = (float)atof(z);
                g->offx = U(20) - s0->r.x0 * g->scale;
                g->offy = g->top_h + U(40) - s0->r.y0 * g->scale;
            }
        }
        if (o->screenshot) {   /* modo de prueba del buscador y de la selección múltiple */
            const char *ns = getenv("HIEROOCR_PRUEBA_SELECCION"), *q = getenv("HIEROOCR_PRUEBA_BUSCAR");
            if (ns && g->sel >= 0)
                g->sel_n = atoi(ns);
            if (q && !g->overlay) {
                if (strcmp(q, "seleccion") == 0) {
                    open_search(g, 1);
                } else {
                    open_search(g, 0);
                    snprintf(g->query, sizeof g->query, "%s", q);
                    run_query(g);
                    g->sedit = 0;
                }
            }
        }
        render(g, NULL);
        if (o->screenshot) {
            render(g, NULL);   /* el segundo fotograma ya tiene la disposición definitiva */
            screenshot(g, o->screenshot);
            break;
        }
    }

    for (int i = 0; i < g->ncache; i++)
        SDL_DestroyTexture(g->cache[i].t);
    if (g->tex) SDL_DestroyTexture(g->tex);
    if (g->ui) TTF_CloseFont(g->ui);
    if (g->small) TTF_CloseFont(g->small);
    if (g->hiero) TTF_CloseFont(g->hiero);
    SDL_DestroyRenderer(g->ren);
    SDL_DestroyWindow(g->win);
    return 0;
}
