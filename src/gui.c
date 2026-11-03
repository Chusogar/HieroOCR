/*
 * Interfaz SDL2 para revisar el reconocimiento, corregir signos y enseñar al
 * programa la tipografía de la edición (cada corrección se guarda como
 * plantilla y mejora las siguientes páginas).
 */
#include "hieroocr.h"

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PANEL_H 178
#define NCACHE  4096

typedef struct { char key[CODE_LEN + 4]; SDL_Texture *t; int w, h; } CacheEntry;

typedef struct {
    SDL_Window   *win;
    SDL_Renderer *ren;
    SDL_Texture  *tex;
    int           tex_w, tex_h;
    TTF_Font     *ui, *small, *hiero;
    Page         *pages;
    int           np, cur;
    const Options *o;
    TemplateSet  *ts;
    float         scale, offx, offy;
    int           sel, editing, dragging, learn, running;
    char          buf[CODE_LEN];
    char          status[256];
    CacheEntry    cache[NCACHE];
    int           ncache;
} Gui;

static const char *ui_font_paths[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
    "/System/Library/Fonts/Supplemental/Arial.ttf",
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
    return NULL;
}

static Page *cur_page(Gui *g) { return &g->pages[g->cur]; }

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
            SDL_Surface *sf = TTF_RenderGlyph32_Blended(g->hiero, cp, c);
            if (sf) {
                e->t = SDL_CreateTextureFromSurface(g->ren, sf);
                e->w = sf->w;
                e->h = sf->h;
                SDL_FreeSurface(sf);
            }
        }
    } else {
        e->t = make_text(g, g->small, code, c, &e->w, &e->h);
    }
    if (!e->t)
        return NULL;
    snprintf(e->key, sizeof e->key, "%s", key);
    g->ncache++;
    return e;
}

/* ---------- página ---------- */

static void fit_page(Gui *g)
{
    int ww, wh;
    SDL_GetRendererOutputSize(g->ren, &ww, &wh);
    if (!g->tex_w || !g->tex_h)
        return;
    float sx = (float)ww / g->tex_w, sy = (float)(wh - PANEL_H) / g->tex_h;
    g->scale = sx < sy ? sx : sy;
    g->offx = (ww - g->tex_w * g->scale) / 2;
    g->offy = 0;
}

static int load_page(Gui *g, int i)
{
    Page *p = &g->pages[i];
    if (!p->processed && process_page(p, g->o, g->ts) != 0)
        return -1;
    if (g->tex)
        SDL_DestroyTexture(g->tex);
    g->tex = NULL;
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
        fprintf(stderr, "No se puede crear la textura de la página: %s\n", SDL_GetError());
        return -1;
    }
    g->cur = i;
    g->sel = -1;
    g->editing = 0;
    fit_page(g);
    snprintf(g->status, sizeof g->status, "Página %d cargada: %d renglones, %d signos.",
             i + 1, p->nlines, p->ng);
    return 0;
}

static int next_glyph(Page *p, int from, int dir)
{
    if (p->ng == 0)
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
    float px = (mx - g->offx) / g->scale, py = (my - g->offy) / g->scale;
    int best = -1;
    long barea = 0;
    for (int i = 0; i < p->ng; i++) {
        Glyph *gl = &p->g[i];
        if (!gl->alive)
            continue;
        if (px >= gl->r.x0 - 2 && px <= gl->r.x1 + 2 && py >= gl->r.y0 - 2 && py <= gl->r.y1 + 2) {
            long a = (long)(gl->r.x1 - gl->r.x0 + 1) * (gl->r.y1 - gl->r.y0 + 1);
            if (best < 0 || a < barea) {
                best = i;
                barea = a;
            }
        }
    }
    return best;
}

/* ---------- acciones ---------- */

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
             learned ? "  (plantilla guardada; R para reclasificar)" : "");
    g->sel = next_glyph(p, gi, 1);
}

static void reclassify(Gui *g)
{
    Page *p = cur_page(g);
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
    const char *path = g->o->output ? g->o->output : "salida.mdc";
    Str out = {0};
    for (int i = 0; i < g->np; i++) {
        if (!g->pages[i].processed && process_page(&g->pages[i], g->o, g->ts) != 0)
            continue;
        if (g->np > 1) {
            char hdr[64];
            snprintf(hdr, sizeof hdr, "++página %d+s-!\n", i + 1);
            str_puts(&out, hdr);
        }
        page_to_mdc(&g->pages[i], g->o, &out, i == g->np - 1);
    }
    FILE *f = fopen(path, "w");
    if (!f) {
        snprintf(g->status, sizeof g->status, "No se puede escribir %s", path);
    } else {
        fputs(out.s ? out.s : "", f);
        fclose(f);
        snprintf(g->status, sizeof g->status, "MdC guardado en %s (%zu bytes).", path, out.n);
    }
    free(out.s);
}

static void zoom_at(Gui *g, float f, int mx, int my)
{
    float ns = g->scale * f;
    if (ns < 0.05f || ns > 12.0f)
        return;
    g->offx = mx - (mx - g->offx) * f;
    g->offy = my - (my - g->offy) * f;
    g->scale = ns;
}

static void handle_key(Gui *g, SDL_Keysym ks)
{
    Page *p = cur_page(g);
    int ww, wh;
    SDL_GetRendererOutputSize(g->ren, &ww, &wh);

    if (g->editing) {
        size_t n = strlen(g->buf);
        if (ks.sym == SDLK_RETURN || ks.sym == SDLK_KP_ENTER) {
            if (g->sel >= 0 && valid_code(g->buf))
                assign(g, g->sel, g->buf);
            else if (n)
                snprintf(g->status, sizeof g->status, "«%s» no es un código Gardiner válido.", g->buf);
            g->editing = 0;
            SDL_StopTextInput();
        } else if (ks.sym == SDLK_ESCAPE) {
            g->editing = 0;
            SDL_StopTextInput();
        } else if (ks.sym == SDLK_BACKSPACE && n) {
            g->buf[n - 1] = 0;
        }
        return;
    }

    switch (ks.sym) {
    case SDLK_q:
        g->running = 0;
        break;
    case SDLK_ESCAPE:
        if (g->sel >= 0) g->sel = -1;
        else g->running = 0;
        break;
    case SDLK_TAB:
        g->sel = next_glyph(p, g->sel, (ks.mod & KMOD_SHIFT) ? -1 : 1);
        break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        if (g->sel >= 0) {
            g->editing = 1;
            g->buf[0] = 0;
            SDL_StartTextInput();
        }
        break;
    case SDLK_1: case SDLK_2: case SDLK_3: case SDLK_4: case SDLK_5: {
        int k = ks.sym - SDLK_1;
        if (g->sel >= 0 && k < p->g[g->sel].ncand)
            assign(g, g->sel, g->ts->t[p->g[g->sel].cand[k]].code);
        break;
    }
    case SDLK_DELETE:
    case SDLK_BACKSPACE:
        if (g->sel >= 0) {
            p->g[g->sel].alive = 0;
            snprintf(g->status, sizeof g->status, "Signo %d eliminado.", g->sel);
            g->sel = next_glyph(p, g->sel, 1);
        }
        break;
    case SDLK_m:
        if (g->sel >= 0) {
            if (glyph_merge_next(p, g->sel) >= 0) {
                classify_glyph(p, g->sel, g->ts);
                snprintf(g->status, sizeof g->status, "Unido con el siguiente: ahora %s.", p->g[g->sel].code);
            } else {
                snprintf(g->status, sizeof g->status, "No hay signo siguiente en el renglón.");
            }
        }
        break;
    case SDLK_r: reclassify(g); break;
    case SDLK_s: save_mdc(g); break;
    case SDLK_f: fit_page(g); break;
    case SDLK_l:
        g->learn = !g->learn;
        snprintf(g->status, sizeof g->status, "Aprendizaje %s.", g->learn ? "activado" : "desactivado");
        break;
    case SDLK_PAGEDOWN:
        if (g->cur + 1 < g->np) load_page(g, g->cur + 1);
        break;
    case SDLK_PAGEUP:
        if (g->cur > 0) load_page(g, g->cur - 1);
        break;
    case SDLK_LEFT:  g->offx += 80; break;
    case SDLK_RIGHT: g->offx -= 80; break;
    case SDLK_UP:    g->offy += 80; break;
    case SDLK_DOWN:  g->offy -= 80; break;
    case SDLK_PLUS: case SDLK_KP_PLUS: case SDLK_EQUALS:
        zoom_at(g, 1.25f, ww / 2, (wh - PANEL_H) / 2);
        break;
    case SDLK_MINUS: case SDLK_KP_MINUS:
        zoom_at(g, 0.8f, ww / 2, (wh - PANEL_H) / 2);
        break;
    default:
        break;
    }
}

static void handle_event(Gui *g, SDL_Event *e)
{
    switch (e->type) {
    case SDL_QUIT:
        g->running = 0;
        break;
    case SDL_MOUSEWHEEL: {
        int mx, my;
        SDL_GetMouseState(&mx, &my);
        zoom_at(g, e->wheel.y > 0 ? 1.15f : 1 / 1.15f, mx, my);
        break;
    }
    case SDL_MOUSEBUTTONDOWN:
        if (e->button.button == SDL_BUTTON_LEFT) {
            int hit = hit_test(g, e->button.x, e->button.y);
            if (hit >= 0) {
                g->sel = hit;
                g->editing = 0;
                SDL_StopTextInput();
            }
        } else {
            g->dragging = 1;
        }
        break;
    case SDL_MOUSEBUTTONUP:
        if (e->button.button != SDL_BUTTON_LEFT)
            g->dragging = 0;
        break;
    case SDL_MOUSEMOTION:
        if (g->dragging) {
            g->offx += e->motion.xrel;
            g->offy += e->motion.yrel;
        }
        break;
    case SDL_TEXTINPUT:
        if (g->editing)
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

static void render(Gui *g)
{
    Page *p = cur_page(g);
    int ww, wh;
    SDL_GetRendererOutputSize(g->ren, &ww, &wh);
    SDL_SetRenderDrawColor(g->ren, 70, 70, 74, 255);
    SDL_RenderClear(g->ren);

    SDL_Rect clip = {0, 0, ww, wh - PANEL_H};
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

    for (int i = 0; i < p->ng; i++) {
        Glyph *gl = &p->g[i];
        if (!gl->alive)
            continue;
        SDL_Rect r = {(int)(g->offx + gl->r.x0 * g->scale) - 1, (int)(g->offy + gl->r.y0 * g->scale) - 1,
                      (int)((gl->r.x1 - gl->r.x0 + 1) * g->scale) + 2, (int)((gl->r.y1 - gl->r.y0 + 1) * g->scale) + 2};
        if (r.x > ww || r.y > wh || r.x + r.w < 0 || r.y + r.h < 0)
            continue;
        SDL_Color c = gl->manual ? c_man
                    : gl->score <= g->o->uncertain_thr * 0.75f ? c_ok
                    : gl->score <= g->o->uncertain_thr ? c_mid : c_bad;
        set_color(g, i == g->sel ? c_sel : c);
        SDL_RenderDrawRect(g->ren, &r);
        if (i == g->sel) {
            SDL_Rect r2 = {r.x - 2, r.y - 2, r.w + 4, r.h + 4};
            SDL_RenderDrawRect(g->ren, &r2);
        }
        if (g->scale >= 0.3f && gl->code[0]) {
            CacheEntry *e = cached(g, gl->code, gl->manual ? 'M' : (c.r == c_ok.r ? 'G' : c.r == c_mid.r ? 'Y' : 'R'), c);
            if (e) {
                SDL_Rect d = {r.x, r.y - e->h, e->w, e->h};
                SDL_RenderCopy(g->ren, e->t, NULL, &d);
            }
        }
    }
    SDL_RenderSetClipRect(g->ren, NULL);

    /* Panel inferior */
    SDL_Rect pr = {0, wh - PANEL_H, ww, PANEL_H};
    SDL_SetRenderDrawColor(g->ren, 236, 234, 228, 255);
    SDL_RenderFillRect(g->ren, &pr);
    SDL_SetRenderDrawColor(g->ren, 150, 150, 150, 255);
    SDL_RenderDrawLine(g->ren, 0, wh - PANEL_H, ww, wh - PANEL_H);

    const SDL_Color black = {20, 20, 20, 255}, grey = {90, 90, 90, 255}, blue = {30, 70, 200, 255};
    char line[512];
    int y = wh - PANEL_H + 6;
    snprintf(line, sizeof line, "Página %d/%d  ·  %d signos  ·  aprendizaje %s  ·  %s",
             g->cur + 1, g->np, p->ng, g->learn ? "sí" : "no", g->status);
    draw_text(g, g->small, 10, y, line, grey, ww - 20);
    y += 22;

    if (g->sel >= 0) {
        Glyph *gl = &p->g[g->sel];
        snprintf(line, sizeof line, "Signo %d · renglón %d · código %s · puntuación %.3f%s%s",
                 g->sel, gl->line + 1, gl->code[0] ? gl->code : "?", gl->score,
                 gl->manual ? " · corregido" : "", gl->underlined ? " · subrayado" : "");
        draw_text(g, g->ui, 10, y, line, black, ww - 20);
        y += 26;
        int x = 10;
        for (int k = 0; k < gl->ncand; k++) {
            const char *code = g->ts->t[gl->cand[k]].code;
            snprintf(line, sizeof line, "%d:", k + 1);
            x += draw_text(g, g->ui, x, y + 14, line, grey, 0) + 4;
            CacheEntry *h = cached(g, code, 'H', black);
            if (h) {
                int hh = 44, hw = h->w * hh / (h->h ? h->h : 1);
                SDL_Rect d = {x, y, hw, hh};
                SDL_RenderCopy(g->ren, h->t, NULL, &d);
                x += hw + 4;
            }
            snprintf(line, sizeof line, "%s (%.3f)", code, gl->cand_score[k]);
            x += draw_text(g, g->small, x, y + 16, line, black, 0) + 22;
        }
        y += 50;
    } else {
        draw_text(g, g->ui, 10, y, "Haz clic en un signo (o pulsa Tab) para seleccionarlo.", black, ww - 20);
        y += 76;
    }

    if (g->editing) {
        snprintf(line, sizeof line, "Código Gardiner: %s_   (Intro confirma, Esc cancela)", g->buf);
        draw_text(g, g->ui, 10, y, line, blue, ww - 20);
    } else if (g->sel >= 0) {
        Str s = {0};
        line_to_mdc(p, p->g[g->sel].line, g->o, &s);
        snprintf(line, sizeof line, "MdC: %s!", s.s ? s.s : "");
        free(s.s);
        draw_text(g, g->small, 10, y + 2, line, blue, ww - 20);
    }
    y += 28;
    draw_text(g, g->small, 10, y,
              "Clic: seleccionar · Tab: siguiente · 1-5: candidato · Intro: escribir código · Supr: borrar · "
              "M: unir con el siguiente · R: reclasificar · L: aprendizaje · S: guardar MdC · "
              "AvPág/RePág: página · rueda/+/-: zoom · F: ajustar · botón dcho.: mover · Q: salir",
              grey, ww - 20);

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

int gui_run(Page *pages, int npages, const Options *o, TemplateSet *ts)
{
    static Gui g;   /* la caché es grande */
    memset(&g, 0, sizeof g);
    g.pages = pages;
    g.np = npages;
    g.o = o;
    g.ts = ts;
    g.learn = 1;
    g.sel = -1;
    g.running = 1;

    g.win = SDL_CreateWindow("hieroocr — OCR de jeroglíficos a MdC", SDL_WINDOWPOS_CENTERED,
                             SDL_WINDOWPOS_CENTERED, 1280, 920, SDL_WINDOW_RESIZABLE);
    if (!g.win) {
        fprintf(stderr, "No se puede crear la ventana: %s\n", SDL_GetError());
        return 1;
    }
    g.ren = SDL_CreateRenderer(g.win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g.ren)
        g.ren = SDL_CreateRenderer(g.win, -1, SDL_RENDERER_SOFTWARE);
    if (!g.ren) {
        fprintf(stderr, "No se puede crear el renderizador: %s\n", SDL_GetError());
        SDL_DestroyWindow(g.win);
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    SDL_StopTextInput();

    g.ui = open_ui_font(17);
    g.small = open_ui_font(13);
    if (!g.ui || !g.small)
        fprintf(stderr, "Aviso: no se encontró una fuente para la interfaz (define HIEROOCR_UI_FONT).\n");
    if (o->font)
        g.hiero = TTF_OpenFont(o->font, 40);

    if (load_page(&g, 0) != 0) {
        fprintf(stderr, "No se puede cargar la primera página.\n");
        g.running = 0;
    }

    while (g.running) {
        SDL_Event e;
        if (SDL_WaitEventTimeout(&e, 100)) {
            do
                handle_event(&g, &e);
            while (g.running && SDL_PollEvent(&e));
        }
        if (!g.running)
            break;
        if (o->screenshot && g.sel < 0) {   /* modo de prueba: zoom sobre el primer renglón */
            g.sel = next_glyph(cur_page(&g), 0, 1);
            const char *z = getenv("HIEROOCR_ZOOM");
            if (z && g.sel >= 0) {
                Glyph *s0 = &cur_page(&g)->g[g.sel];
                g.scale = (float)atof(z);
                g.offx = 20 - s0->r.x0 * g.scale;
                g.offy = 40 - s0->r.y0 * g.scale;
            }
        }
        render(&g);
        if (o->screenshot) {
            screenshot(&g, o->screenshot);
            break;
        }
    }

    for (int i = 0; i < g.ncache; i++)
        SDL_DestroyTexture(g.cache[i].t);
    if (g.tex) SDL_DestroyTexture(g.tex);
    if (g.ui) TTF_CloseFont(g.ui);
    if (g.small) TTF_CloseFont(g.small);
    if (g.hiero) TTF_CloseFont(g.hiero);
    SDL_DestroyRenderer(g.ren);
    SDL_DestroyWindow(g.win);
    return 0;
}
