/* The windows people click on, drawn with Cairo straight onto X windows:
 * the settings window, the colour picker and the tray popup menu. */
#define _GNU_SOURCE
#include "spidereko.h"
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <cairo-xlib.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ look */
static const Rgba C_BG = {0.937, 0.937, 0.949, 1}, C_TEXT = {0.11, 0.11, 0.13, 1},
                  C_TRACK = {0.77, 0.77, 0.80, 1}, C_FILL = {0.36, 0.43, 0.78, 1},
                  C_BTN = {0.86, 0.86, 0.89, 1}, C_EDGE = {0.62, 0.62, 0.68, 1},
                  C_RED = {0.75, 0.10, 0.10, 1}, C_WHITE = {1, 1, 1, 1};

static void col(cairo_t *cr, Rgba c) { cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a); }

static void rrect(cairo_t *cr, double x, double y, double w, double h, double r)
{
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}

enum { AL_LEFT, AL_CENTER, AL_RIGHT };

static void text(cairo_t *cr, double x, double y, const char *s, double size, int bold, Rgba c, int align)
{
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, size);
    cairo_text_extents_t e;
    cairo_text_extents(cr, s, &e);
    if (align == AL_CENTER) x -= e.x_advance / 2;
    else if (align == AL_RIGHT) x -= e.x_advance;
    col(cr, c);
    cairo_move_to(cr, x, y);
    cairo_show_text(cr, s);
}

/* ------------------------------------------------------------------ a top-level window drawn through an image */
typedef struct { Window win; int w, h; cairo_surface_t *img, *xs; } Win;

static void win_props(Win *w, const char *title)
{
    XChangeProperty(g_dpy, w->win, g_net_wm_name, g_utf8, 8, PropModeReplace, (unsigned char *)title, (int)strlen(title));
    XStoreName(g_dpy, w->win, title);
    XClassHint ch = {(char *)APP_NAME, (char *)"Spidereko"};
    XSetClassHint(g_dpy, w->win, &ch);
    Atom proto[1] = {g_wm_delete};
    XSetWMProtocols(g_dpy, w->win, proto, 1);
    XSizeHints *sh = XAllocSizeHints();                         /* fixed size */
    sh->flags = PMinSize | PMaxSize;
    sh->min_width = sh->max_width = w->w;
    sh->min_height = sh->max_height = w->h;
    XSetWMNormalHints(g_dpy, w->win, sh);
    XFree(sh);

    int n = 64;                                                  /* _NET_WM_ICON: the app logo */
    cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, n, n);
    cairo_t *cr = cairo_create(img);
    draw_icon(cr, n, 0);
    cairo_destroy(cr);
    cairo_surface_flush(img);
    uint32_t *px = (uint32_t *)cairo_image_surface_get_data(img);
    long *data = malloc((size_t)(2 + n * n) * sizeof *data);
    data[0] = data[1] = n;
    for (int i = 0; i < n * n; i++) {                            /* premultiplied -> plain ARGB */
        uint32_t p = px[i], a = p >> 24, r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
        if (a && a < 255) { r = r * 255 / a; g = g * 255 / a; b = b * 255 / a; }
        data[2 + i] = (long)((a << 24) | (r << 16) | (g << 8) | b);
    }
    XChangeProperty(g_dpy, w->win, g_net_wm_icon, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)data, 2 + n * n);
    free(data);
    cairo_surface_destroy(img);
}

static void pointer_xy(int *x, int *y)
{
    Window r, c;
    int wx, wy;
    unsigned int m;
    *x = *y = 0;
    XQueryPointer(g_dpy, g_root, &r, &c, x, y, &wx, &wy, &m);
}

static void win_open(Win *w, int width, int height, const char *title)
{
    int px, py, mx, my, mw, mh;
    pointer_xy(&px, &py);
    app_monitor_at(px, py, &mx, &my, &mw, &mh);
    w->w = width;
    w->h = height;
    XSetWindowAttributes a;
    memset(&a, 0, sizeof a);
    a.background_pixel = WhitePixel(g_dpy, g_scr);
    a.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | KeyPressMask;
    w->win = XCreateWindow(g_dpy, g_root, mx + (mw - width) / 2, my + (mh - height) / 2, width, height, 0,
                           CopyFromParent, InputOutput, CopyFromParent, CWBackPixel | CWEventMask, &a);
    win_props(w, title);
    w->img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    w->xs = cairo_xlib_surface_create(g_dpy, w->win, DefaultVisual(g_dpy, g_scr), width, height);
    XMapRaised(g_dpy, w->win);
}

static void win_flush(Win *w)
{
    cairo_surface_flush(w->img);
    cairo_t *wc = cairo_create(w->xs);
    cairo_set_operator(wc, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_surface(wc, w->img, 0, 0);
    cairo_paint(wc);
    cairo_destroy(wc);
    cairo_surface_flush(w->xs);
}

static void win_close(Win *w)
{
    if (!w->win) return;
    cairo_surface_destroy(w->img);                              /* the idle spider keeps no window around */
    cairo_surface_destroy(w->xs);
    XDestroyWindow(g_dpy, w->win);
    memset(w, 0, sizeof *w);
}

static int is_delete(const XEvent *ev)
{
    return ev->type == ClientMessage && ev->xclient.message_type == g_wm_protocols &&
           (Atom)ev->xclient.data.l[0] == g_wm_delete;
}

/* ================================================================== settings window */
enum { WK_LABEL, WK_CHECK, WK_SLIDER, WK_COLOR, WK_BUTTON };
typedef struct { int kind, key, x, y, w, h; const char *label; } Wd;

#define SW_W 480
static Win SW;
static Wd wd[24];
static int nwd, sw_drag = -1;

static const struct { int key; const char *label; double lo, hi, step; const char *fmt; } SL[] = {
    {K_SPEED, "Speed", 50, 2000, 1, "%.0f px/s"},
    {K_SIZE, "Size", 0.3, 8, 0.1, "%.1f\xc3\x97"},
    {K_LIFT, "Height above cursor", -100, 100, 1, "%.0f"},
    {K_IDLE, "Falls asleep after", 0, 60, 0.1, "%.1f s"},
    {K_FPS, "Smoothness", 10, 120, 1, "%.0f fps"},
};
#define NSL ((int)(sizeof SL / sizeof *SL))

static int sl_index(int key)
{
    for (int i = 0; i < NSL; i++) if (SL[i].key == key) return i;
    return 0;
}

static void wd_add(int kind, int key, int x, int y, int w, int h, const char *label)
{
    if (nwd < 24) wd[nwd++] = (Wd){kind, key, x, y, w, h, label};
}

static int sw_layout(void)
{
    nwd = 0;
    int y = 14;
    if (app_wayland()) y += 28;                                  /* room for the Wayland banner */
    wd_add(WK_CHECK, K_ENABLED, 16, y, SW_W - 32, 24, "Spider on");
    y += 38;
    for (int i = 0; i < NSL; i++) {
        wd_add(WK_SLIDER, SL[i].key, 170, y, 210, 24, SL[i].label);
        y += 34;
    }
    y += 4;
    wd_add(WK_LABEL, -1, 16, y, 140, 28, "Colors");
    wd_add(WK_COLOR, K_FG, 170, y, 92, 28, "Body");
    wd_add(WK_COLOR, K_BG, 268, y, 92, 28, "Outline / eyes");
    wd_add(WK_COLOR, K_ACCENT, 366, y, 92, 28, "Mark");
    y += 44;
    wd_add(WK_CHECK, K_SHADOW, 16, y, SW_W - 32, 24, "Soft shadow under the spider");
    y += 32;
    wd_add(WK_CHECK, K_TOFOCUS, 16, y, SW_W - 32, 24, "Chase the focused window instead of the pointer");
    y += 46;
    wd_add(WK_BUTTON, 0, 16, y, 150, 30, "Reset to defaults");
    wd_add(WK_BUTTON, 1, SW_W - 16 - 100, y, 100, 30, "Close");
    return y + 30 + 16;
}

static double lightness(Rgba c)
{
    double hi = fmax(c.r, fmax(c.g, c.b)), lo = fmin(c.r, fmin(c.g, c.b));
    return (hi + lo) / 2;
}

static void sw_draw(void)
{
    if (!SW.win) return;
    cairo_t *cr = cairo_create(SW.img);
    col(cr, C_BG);
    cairo_paint(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_GRAY);
    if (app_wayland())
        text(cr, 16, 28, "Wayland session: the spider cannot follow the pointer here - use an X11 session.", 11, 1, C_RED, AL_LEFT);
    for (int i = 0; i < nwd; i++) {
        const Wd *d = &wd[i];
        switch (d->kind) {
        case WK_LABEL:
            text(cr, d->x, d->y + 19, d->label, 13, 0, C_TEXT, AL_LEFT);
            break;
        case WK_CHECK: {
            int on = cfg_get_num(&g_cfg, d->key) != 0;
            rrect(cr, d->x, d->y + 4, 16, 16, 3);
            col(cr, on ? C_FILL : C_WHITE);
            cairo_fill_preserve(cr);
            col(cr, C_EDGE);
            cairo_set_line_width(cr, 1);
            cairo_stroke(cr);
            if (on) {
                col(cr, C_WHITE);
                cairo_set_line_width(cr, 2);
                cairo_move_to(cr, d->x + 3.5, d->y + 12.5);
                cairo_line_to(cr, d->x + 7, d->y + 16);
                cairo_line_to(cr, d->x + 12.5, d->y + 8);
                cairo_stroke(cr);
            }
            text(cr, d->x + 26, d->y + 18, d->label, 13, d->key == K_ENABLED, C_TEXT, AL_LEFT);
            break;
        }
        case WK_SLIDER: {
            int si = sl_index(d->key);
            double v = cfg_get_num(&g_cfg, d->key), lo = fmin(SL[si].lo, v), hi = fmax(SL[si].hi, v);
            double f = hi > lo ? (v - lo) / (hi - lo) : 0, tx = d->x + f * d->w, ty = d->y + d->h / 2.0;
            char buf[48];
            text(cr, 16, d->y + 17, d->label, 13, 0, C_TEXT, AL_LEFT);
            rrect(cr, d->x, ty - 2, d->w, 4, 2);
            col(cr, C_TRACK);
            cairo_fill(cr);
            rrect(cr, d->x, ty - 2, tx - d->x, 4, 2);
            col(cr, C_FILL);
            cairo_fill(cr);
            cairo_new_path(cr);
            cairo_arc(cr, tx, ty, 8, 0, 2 * M_PI);
            col(cr, C_WHITE);
            cairo_fill_preserve(cr);
            col(cr, C_EDGE);
            cairo_set_line_width(cr, 1.2);
            cairo_stroke(cr);
            snprintf(buf, sizeof buf, SL[si].fmt, v);
            text(cr, d->x + d->w + 14, d->y + 17, buf, 13, 0, C_TEXT, AL_LEFT);
            break;
        }
        case WK_COLOR: {
            Rgba c = {0.5, 0.5, 0.5, 1};
            color_parse(cfg_get_str(&g_cfg, d->key), &c);
            c.a = 1;
            rrect(cr, d->x, d->y, d->w, d->h, 5);
            col(cr, c);
            cairo_fill_preserve(cr);
            col(cr, C_EDGE);
            cairo_set_line_width(cr, 1);
            cairo_stroke(cr);
            text(cr, d->x + d->w / 2.0, d->y + 19, d->label, 12, 0, lightness(c) < 0.5 ? C_WHITE : (Rgba){0, 0, 0, 1}, AL_CENTER);
            break;
        }
        case WK_BUTTON:
            rrect(cr, d->x, d->y, d->w, d->h, 5);
            col(cr, C_BTN);
            cairo_fill_preserve(cr);
            col(cr, C_EDGE);
            cairo_set_line_width(cr, 1);
            cairo_stroke(cr);
            text(cr, d->x + d->w / 2.0, d->y + 20, d->label, 13, 0, C_TEXT, AL_CENTER);
            break;
        }
    }
    cairo_destroy(cr);
    win_flush(&SW);
}

static void slider_set(const Wd *d, int mx)
{
    int si = sl_index(d->key);
    double cur = cfg_get_num(&g_cfg, d->key), lo = fmin(SL[si].lo, cur), hi = fmax(SL[si].hi, cur);
    double f = (mx - d->x) / (double)d->w;
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    double v = lo + f * (hi - lo);
    v = SL[si].lo + round((v - SL[si].lo) / SL[si].step) * SL[si].step;      /* snap to the slider's step */
    v = round(v * 1000) / 1000;
    if (fabs(v - cur) > 1e-9) app_commit_num(d->key, v);
}

static void cd_open(int key);

void ui_open_settings(void)
{
    if (SW.win) {
        XMapRaised(g_dpy, SW.win);
        sw_draw();
        return;
    }
    int h = sw_layout();
    win_open(&SW, SW_W, h, "Spidereko settings");
    sw_draw();
}

void ui_refresh(void)
{
    sw_draw();
}

static void sw_event(XEvent *ev)
{
    if (is_delete(ev)) { win_close(&SW); sw_drag = -1; return; }
    switch (ev->type) {
    case Expose:
        if (!ev->xexpose.count) sw_draw();
        break;
    case KeyPress:
        if (XLookupKeysym(&ev->xkey, 0) == XK_Escape) { win_close(&SW); sw_drag = -1; }
        break;
    case ButtonPress:
        if (ev->xbutton.button != Button1) break;
        for (int i = 0; i < nwd; i++) {
            const Wd *d = &wd[i];
            int x = ev->xbutton.x, y = ev->xbutton.y;
            int hit = x >= d->x - (d->kind == WK_SLIDER ? 10 : 0) && x < d->x + d->w + (d->kind == WK_SLIDER ? 10 : 0) &&
                      y >= d->y && y < d->y + d->h;
            if (!hit) continue;
            if (d->kind == WK_CHECK) {
                if (d->key == K_ENABLED) app_toggle_enabled();
                else app_commit_num(d->key, cfg_get_num(&g_cfg, d->key) ? 0 : 1);
            } else if (d->kind == WK_SLIDER) {
                sw_drag = i;
                slider_set(d, x);
            } else if (d->kind == WK_COLOR) {
                cd_open(d->key);
            } else if (d->kind == WK_BUTTON) {
                if (d->key == 0) app_reset_defaults();
                else { win_close(&SW); sw_drag = -1; return; }
            }
            break;
        }
        break;
    case MotionNotify:
        if (sw_drag >= 0) slider_set(&wd[sw_drag], ev->xmotion.x);
        break;
    case ButtonRelease:
        sw_drag = -1;
        break;
    }
}

/* ================================================================== colour picker */
#define SV 200
#define HUE_W 26
static struct {
    Win w;
    int key, drag, valid;
    double h, s, v;
    char hex[12];
    Rgba old;
    cairo_surface_t *svimg, *huimg;
    double sv_hue;
} CD;

static void hsv2rgb(double h, double s, double v, double *r, double *g, double *b)
{
    h = fmod(h, 360);
    if (h < 0) h += 360;
    double c = v * s, x = c * (1 - fabs(fmod(h / 60, 2) - 1)), m = v - c, rr = 0, gg = 0, bb = 0;
    int sector = (int)(h / 60);
    switch (sector) {
    case 0: rr = c; gg = x; break;
    case 1: rr = x; gg = c; break;
    case 2: gg = c; bb = x; break;
    case 3: gg = x; bb = c; break;
    case 4: rr = x; bb = c; break;
    default: rr = c; bb = x; break;
    }
    *r = rr + m; *g = gg + m; *b = bb + m;
}

static void rgb2hsv(double r, double g, double b, double *h, double *s, double *v)
{
    double mx = fmax(r, fmax(g, b)), mn = fmin(r, fmin(g, b)), d = mx - mn;
    *v = mx;
    *s = mx > 0 ? d / mx : 0;
    if (d <= 0) *h = 0;
    else if (mx == r) *h = 60 * fmod((g - b) / d, 6);
    else if (mx == g) *h = 60 * ((b - r) / d + 2);
    else *h = 60 * ((r - g) / d + 4);
    if (*h < 0) *h += 360;
}

static void cd_sync_hex(void)
{
    double r, g, b;
    hsv2rgb(CD.h, CD.s, CD.v, &r, &g, &b);
    snprintf(CD.hex, sizeof CD.hex, "#%02x%02x%02x", (int)lround(r * 255), (int)lround(g * 255), (int)lround(b * 255));
    CD.valid = 1;
}

static void put_px(unsigned char *p, double r, double g, double b)
{
    p[0] = (unsigned char)lround(b * 255); p[1] = (unsigned char)lround(g * 255);
    p[2] = (unsigned char)lround(r * 255); p[3] = 255;
}

static void cd_build_images(void)
{
    if (!CD.huimg) {
        CD.huimg = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, HUE_W, SV);
        unsigned char *d = cairo_image_surface_get_data(CD.huimg);
        int stride = cairo_image_surface_get_stride(CD.huimg);
        for (int y = 0; y < SV; y++) {
            double r, g, b;
            hsv2rgb(360.0 * y / SV, 1, 1, &r, &g, &b);
            for (int x = 0; x < HUE_W; x++) put_px(d + y * stride + x * 4, r, g, b);
        }
        cairo_surface_mark_dirty(CD.huimg);
    }
    if (!CD.svimg) CD.svimg = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, SV, SV);
    if (CD.sv_hue != CD.h || CD.sv_hue < 0) {
        unsigned char *d = cairo_image_surface_get_data(CD.svimg);
        int stride = cairo_image_surface_get_stride(CD.svimg);
        for (int y = 0; y < SV; y++)
            for (int x = 0; x < SV; x++) {
                double r, g, b;
                hsv2rgb(CD.h, x / (double)(SV - 1), 1.0 - y / (double)(SV - 1), &r, &g, &b);
                put_px(d + y * stride + x * 4, r, g, b);
            }
        cairo_surface_mark_dirty(CD.svimg);
        CD.sv_hue = CD.h;
    }
}

#define CD_W 340
#define CD_H 300
static const int CD_HEX[4] = {56, 226, 112, 28};          /* x y w h of the hex field */
static const int CD_OK[4] = {140, 256, 88, 30}, CD_CANCEL[4] = {236, 256, 88, 30};

static void cd_draw(void)
{
    if (!CD.w.win) return;
    cd_build_images();
    cairo_t *cr = cairo_create(CD.w.img);
    col(cr, C_BG);
    cairo_paint(cr);
    cairo_set_source_surface(cr, CD.svimg, 16, 16);
    cairo_paint(cr);
    cairo_set_source_surface(cr, CD.huimg, 228, 16);
    cairo_paint(cr);
    col(cr, C_EDGE);                                           /* frames */
    cairo_set_line_width(cr, 1);
    cairo_rectangle(cr, 15.5, 15.5, SV + 1, SV + 1);
    cairo_rectangle(cr, 227.5, 15.5, HUE_W + 1, SV + 1);
    cairo_stroke(cr);
    double mx = 16 + CD.s * (SV - 1), my = 16 + (1 - CD.v) * (SV - 1);     /* marker in the square */
    cairo_new_path(cr);
    cairo_arc(cr, mx, my, 6, 0, 2 * M_PI);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_set_line_width(cr, 2);
    cairo_stroke_preserve(cr);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
    double hy = 16 + CD.h / 360.0 * (SV - 1);                  /* marker on the hue bar */
    cairo_rectangle(cr, 226, hy - 2, HUE_W + 4, 4);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_fill_preserve(cr);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);

    double r, g, b;                                            /* new / old swatches */
    hsv2rgb(CD.h, CD.s, CD.v, &r, &g, &b);
    cairo_set_source_rgb(cr, r, g, b);
    cairo_rectangle(cr, 268, 16, 56, 44);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, CD.old.r, CD.old.g, CD.old.b);
    cairo_rectangle(cr, 268, 60, 56, 44);
    cairo_fill(cr);
    col(cr, C_EDGE);
    cairo_rectangle(cr, 268.5, 16.5, 55, 87);
    cairo_stroke(cr);
    text(cr, 296, 116, "new / old", 10, 0, C_TEXT, AL_CENTER);

    text(cr, 16, 245, "Hex", 13, 0, C_TEXT, AL_LEFT);
    rrect(cr, CD_HEX[0], CD_HEX[1], CD_HEX[2], CD_HEX[3], 4);
    col(cr, C_WHITE);
    cairo_fill_preserve(cr);
    col(cr, CD.valid ? C_EDGE : C_RED);
    cairo_set_line_width(cr, 1.2);
    cairo_stroke(cr);
    text(cr, CD_HEX[0] + 8, CD_HEX[1] + 19, CD.hex, 13, 0, C_TEXT, AL_LEFT);

    const int *rects[2] = {CD_OK, CD_CANCEL};
    const char *names[2] = {"OK", "Cancel"};
    for (int i = 0; i < 2; i++) {
        rrect(cr, rects[i][0], rects[i][1], rects[i][2], rects[i][3], 5);
        col(cr, C_BTN);
        cairo_fill_preserve(cr);
        col(cr, C_EDGE);
        cairo_stroke(cr);
        text(cr, rects[i][0] + rects[i][2] / 2.0, rects[i][1] + 20, names[i], 13, 0, C_TEXT, AL_CENTER);
    }
    cairo_destroy(cr);
    win_flush(&CD.w);
}

static void cd_close(void)
{
    win_close(&CD.w);
    if (CD.svimg) cairo_surface_destroy(CD.svimg);
    if (CD.huimg) cairo_surface_destroy(CD.huimg);
    CD.svimg = CD.huimg = NULL;
}

static void cd_open(int key)
{
    if (CD.w.win) { XMapRaised(g_dpy, CD.w.win); return; }
    memset(&CD, 0, sizeof CD);
    CD.key = key;
    CD.old = (Rgba){0.5, 0.5, 0.5, 1};
    color_parse(cfg_get_str(&g_cfg, key), &CD.old);
    rgb2hsv(CD.old.r, CD.old.g, CD.old.b, &CD.h, &CD.s, &CD.v);
    CD.sv_hue = -1;
    cd_sync_hex();
    win_open(&CD.w, CD_W, CD_H, "Choose a color");
    cd_draw();
}

static void cd_pick(int x, int y)
{
    if (CD.drag == 1) {
        CD.s = fmin(1, fmax(0, (x - 16) / (double)(SV - 1)));
        CD.v = 1 - fmin(1, fmax(0, (y - 16) / (double)(SV - 1)));
    } else if (CD.drag == 2) {
        CD.h = 360.0 * fmin(1, fmax(0, (y - 16) / (double)(SV - 1)));
        if (CD.h >= 360) CD.h = 359.999;
    }
    cd_sync_hex();
    cd_draw();
}

static int inside(const int *r, int x, int y) { return x >= r[0] && x < r[0] + r[2] && y >= r[1] && y < r[1] + r[3]; }

static void cd_accept(void)
{
    if (!CD.valid) return;                                      /* a half-typed colour: stay open (the field is red) */
    app_commit_str(CD.key, CD.hex);
    cd_close();
}

static void cd_event(XEvent *ev)
{
    if (is_delete(ev)) { cd_close(); return; }
    switch (ev->type) {
    case Expose:
        if (!ev->xexpose.count) cd_draw();
        break;
    case ButtonPress: {
        if (ev->xbutton.button != Button1) break;
        int x = ev->xbutton.x, y = ev->xbutton.y;
        if (x >= 16 && x < 16 + SV && y >= 16 && y < 16 + SV) { CD.drag = 1; cd_pick(x, y); }
        else if (x >= 228 && x < 228 + HUE_W && y >= 16 && y < 16 + SV) { CD.drag = 2; cd_pick(x, y); }
        else if (inside(CD_OK, x, y)) cd_accept();
        else if (inside(CD_CANCEL, x, y)) cd_close();
        break;
    }
    case MotionNotify:
        if (CD.drag) cd_pick(ev->xmotion.x, ev->xmotion.y);
        break;
    case ButtonRelease:
        CD.drag = 0;
        break;
    case KeyPress: {
        char buf[16];
        KeySym ks;
        int n = XLookupString(&ev->xkey, buf, sizeof buf - 1, &ks, NULL);
        size_t len = strlen(CD.hex);
        if (ks == XK_Escape) { cd_close(); return; }
        if (ks == XK_Return || ks == XK_KP_Enter) { cd_accept(); return; }
        if (ks == XK_BackSpace) { if (len > 1) CD.hex[len - 1] = 0; }
        else if (n == 1 && len < 7 && (buf[0] == '#' || strchr("0123456789abcdefABCDEF", buf[0]))) {
            if (len == 0 && buf[0] != '#') { CD.hex[0] = '#'; len = 1; }
            if (buf[0] != '#') { CD.hex[len] = buf[0]; CD.hex[len + 1] = 0; }
        } else break;
        Rgba c;
        size_t hl = strlen(CD.hex);
        CD.valid = (hl == 4 || hl == 7) && color_parse(CD.hex, &c);       /* only #rgb / #rrggbb here */
        if (CD.valid) rgb2hsv(c.r, c.g, c.b, &CD.h, &CD.s, &CD.v);
        cd_draw();
        break;
    }
    }
}

/* ================================================================== tray popup menu */
static struct { Window win; cairo_surface_t *img, *xs; int w, h, hot; int64_t opened; } MN;
#define MN_W 170
#define MN_ITEM_H 28
static const int MN_Y[3] = {4, 32, 68};                         /* y of the three items (a gap before Quit) */

static void mn_labels(const char *out[3])
{
    out[0] = "Settings\xe2\x80\xa6";
    out[1] = g_cfg.enabled ? "Hide spider" : "Show spider";
    out[2] = "Quit";
}

static int64_t mono_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void mn_draw(void)
{
    if (!MN.win) return;
    cairo_t *cr = cairo_create(MN.img);
    col(cr, C_BG);
    cairo_paint(cr);
    col(cr, C_EDGE);
    cairo_set_line_width(cr, 1);
    cairo_rectangle(cr, 0.5, 0.5, MN.w - 1, MN.h - 1);
    cairo_stroke(cr);
    const char *lab[3];
    mn_labels(lab);
    for (int i = 0; i < 3; i++) {
        if (MN.hot == i) {
            col(cr, C_FILL);
            cairo_rectangle(cr, 3, MN_Y[i], MN.w - 6, MN_ITEM_H);
            cairo_fill(cr);
        }
        text(cr, 14, MN_Y[i] + 19, lab[i], 13, 0, MN.hot == i ? C_WHITE : C_TEXT, AL_LEFT);
    }
    col(cr, C_EDGE);                                            /* separator above Quit */
    cairo_move_to(cr, 6, 64.5);
    cairo_line_to(cr, MN.w - 6, 64.5);
    cairo_stroke(cr);
    cairo_destroy(cr);
    cairo_surface_flush(MN.img);
    cairo_t *wc = cairo_create(MN.xs);
    cairo_set_source_surface(wc, MN.img, 0, 0);
    cairo_paint(wc);
    cairo_destroy(wc);
    cairo_surface_flush(MN.xs);
}

static void mn_close(void)
{
    if (!MN.win) return;
    XUngrabPointer(g_dpy, CurrentTime);
    XUngrabKeyboard(g_dpy, CurrentTime);
    cairo_surface_destroy(MN.img);
    cairo_surface_destroy(MN.xs);
    XDestroyWindow(g_dpy, MN.win);
    memset(&MN, 0, sizeof MN);
}

void ui_menu_open(int xr, int yr)
{
    mn_close();
    int mx, my, mw, mh;
    app_monitor_at(xr, yr, &mx, &my, &mw, &mh);
    MN.w = MN_W;
    MN.h = MN_Y[2] + MN_ITEM_H + 4;
    MN.hot = -1;
    int x = xr, y = yr;
    if (x + MN.w > mx + mw) x = mx + mw - MN.w;                 /* keep it on the screen */
    if (y + MN.h > my + mh) y = yr - MN.h;                      /* near the bottom (a bottom panel): open upwards */
    if (y < my) y = my;
    XSetWindowAttributes a;
    memset(&a, 0, sizeof a);
    a.override_redirect = True;
    a.background_pixel = WhitePixel(g_dpy, g_scr);
    a.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | KeyPressMask;
    MN.win = XCreateWindow(g_dpy, g_root, x, y, MN.w, MN.h, 0, CopyFromParent, InputOutput, CopyFromParent,
                           CWOverrideRedirect | CWBackPixel | CWEventMask, &a);
    MN.img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, MN.w, MN.h);
    MN.xs = cairo_xlib_surface_create(g_dpy, MN.win, DefaultVisual(g_dpy, g_scr), MN.w, MN.h);
    MN.opened = mono_ms();
    XMapRaised(g_dpy, MN.win);
    if (XGrabPointer(g_dpy, MN.win, False, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                     GrabModeAsync, GrabModeAsync, None, None, CurrentTime) != GrabSuccess) {
        mn_close();                                             /* somebody else holds the pointer */
        return;
    }
    XGrabKeyboard(g_dpy, MN.win, False, GrabModeAsync, GrabModeAsync, CurrentTime);
    mn_draw();
}

static int mn_item_at(int x, int y)
{
    if (x < 0 || x >= MN.w) return -1;
    for (int i = 0; i < 3; i++)
        if (y >= MN_Y[i] && y < MN_Y[i] + MN_ITEM_H) return i;
    return -1;
}

static void mn_event(XEvent *ev)
{
    switch (ev->type) {
    case Expose:
        if (!ev->xexpose.count) mn_draw();
        break;
    case MotionNotify: {
        int hot = mn_item_at(ev->xmotion.x, ev->xmotion.y);
        if (hot != MN.hot) { MN.hot = hot; mn_draw(); }
        break;
    }
    case ButtonPress: {                                          /* a click outside closes the menu */
        int x = ev->xbutton.x, y = ev->xbutton.y;
        if (x < 0 || y < 0 || x >= MN.w || y >= MN.h) mn_close();
        break;
    }
    case ButtonRelease: {
        if (mono_ms() - MN.opened < 300) break;                  /* the release of the click that opened it */
        int item = mn_item_at(ev->xbutton.x, ev->xbutton.y);
        if (item < 0) { if (ev->xbutton.x < 0 || ev->xbutton.y < 0 || ev->xbutton.x >= MN.w || ev->xbutton.y >= MN.h) mn_close(); break; }
        mn_close();
        if (item == 0) ui_open_settings();
        else if (item == 1) app_toggle_enabled();
        else app_quit();
        break;
    }
    case KeyPress:
        if (XLookupKeysym(&ev->xkey, 0) == XK_Escape) mn_close();
        break;
    }
}

/* ================================================================== dispatch */
int ui_event(XEvent *ev)
{
    Window w = ev->xany.window;
    if (MN.win && w == MN.win) { mn_event(ev); return 1; }      /* (the menu holds the pointer grab) */
    if (SW.win && w == SW.win) { sw_event(ev); return 1; }
    if (CD.w.win && w == CD.w.win) { cd_event(ev); return 1; }
    return 0;
}
