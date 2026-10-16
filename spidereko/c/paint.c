/* Drawing with Cairo: the spider, and the logo (SVG text, PNG files, tray icon). */
#define _GNU_SOURCE
#include "spidereko.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void rgba(cairo_t *cr, Rgba c, double alpha)
{
    cairo_set_source_rgba(cr, c.r, c.g, c.b, alpha);
}

static void ellipse_path(cairo_t *cr, double cx, double cy, double rx, double ry)
{
    cairo_save(cr);
    cairo_translate(cr, cx, cy);
    cairo_scale(cr, rx, ry);
    cairo_new_sub_path(cr);
    cairo_arc(cr, 0, 0, 1, 0, 2 * M_PI);
    cairo_restore(cr);                       /* the path keeps its shape; the pen is not scaled */
}

static void triangle(cairo_t *cr, double x1, double y1, double x2, double y2, double x3, double y3)
{
    cairo_move_to(cr, x1, y1);
    cairo_line_to(cr, x2, y2);
    cairo_line_to(cr, x3, y3);
    cairo_close_path(cr);
}

/* ------------------------------------------------------------------ the spider */
void paint_spider(cairo_t *cr, const Sim *s, double ox, double oy, const Palette *pal, int shadow)
{
    double u = s->u;
    double w_thigh = 0.38 * u, w_shin = 0.28 * u, extra = 0.26 * u;
    cplx J[8][3];
    for (int i = 0; i < 8; i++) sim_leg_joints(s, &s->legs[i], &J[i][0], &J[i][1], &J[i][2]);

    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);

    #define PX(z) (creal(z) - ox)
    #define PY(z) (cimag(z) - oy)

    if (shadow) {                                          /* soft shadow under the legs (optional) */
        cplx sh = 0.35 * u * (0.5 + 1.0 * I);
        cairo_new_path(cr);
        for (int i = 0; i < 8; i++) {
            cairo_move_to(cr, PX(J[i][0] + sh), PY(J[i][0] + sh));
            cairo_line_to(cr, PX(J[i][1] + sh), PY(J[i][1] + sh));
            cairo_line_to(cr, PX(J[i][2] + sh), PY(J[i][2] + sh));
        }
        cairo_set_source_rgba(cr, 0, 0, 0, 55 / 255.0);
        cairo_set_line_width(cr, fmax(w_shin, 1.0));
        cairo_stroke(cr);
    }

    /* all outlines first, then all fills, so neighbouring legs never cut into each other;
       one path per pass = a handful of draw calls per frame */
    for (int pass = 0; pass < 4; pass++) {
        int seg = pass & 1;                                /* 0 = thighs, 1 = shins */
        cairo_new_path(cr);
        for (int i = 0; i < 8; i++) {
            cairo_move_to(cr, PX(J[i][seg]), PY(J[i][seg]));
            cairo_line_to(cr, PX(J[i][seg + 1]), PY(J[i][seg + 1]));
        }
        if (pass < 2) {
            rgba(cr, pal->bg, 210 / 255.0);
            cairo_set_line_width(cr, fmax((seg ? w_shin : w_thigh) + extra, 1.0));
        } else {
            rgba(cr, pal->fg, pal->fg.a);
            cairo_set_line_width(cr, fmax(seg ? w_shin : w_thigh, 1.0));
        }
        cairo_stroke(cr);
    }

    int asleep = s->state == SLEEP;
    double breath = (asleep || cabs(s->vel) < 5.0) ? 1.0 + 0.04 * sin(s->time * 2.4) : 1.0;

    cairo_save(cr);                                        /* body: the spider's own frame, +x = forward */
    cairo_translate(cr, PX(s->pos), PY(s->pos));
    cairo_rotate(cr, s->heading);

    for (int pass = 0; pass < 2; pass++) {                 /* pedipalps */
        if (pass == 0) { rgba(cr, pal->bg, 210 / 255.0); cairo_set_line_width(cr, fmax(w_shin + extra, 1.0)); }
        else { rgba(cr, pal->fg, pal->fg.a); cairo_set_line_width(cr, fmax(w_shin, 1.0)); }
        cairo_new_path(cr);
        for (int sd = -1; sd <= 1; sd += 2) {
            cairo_move_to(cr, 1.9 * u, sd * 0.3 * u);
            cairo_line_to(cr, 2.45 * u, sd * 0.16 * u);
        }
        cairo_stroke(cr);
    }

    for (int part = 0; part < 2; part++) {                 /* abdomen, then head + thorax */
        if (part == 0) ellipse_path(cr, -1.5 * u, 0, 1.9 * u * breath, 1.5 * u * breath);
        else ellipse_path(cr, 0.9 * u, 0, 1.2 * u, 1.0 * u);
        rgba(cr, pal->fg, pal->fg.a);
        cairo_fill_preserve(cr);
        rgba(cr, pal->bg, 210 / 255.0);
        cairo_set_line_width(cr, fmax(0.2 * u, 1.0));
        cairo_stroke(cr);
    }

    double cx = -1.5 * u;                                  /* hourglass mark */
    rgba(cr, pal->accent, pal->accent.a);
    cairo_new_path(cr);
    triangle(cr, cx - 0.6 * u, -0.45 * u, cx - 0.6 * u, 0.45 * u, cx, 0);
    triangle(cr, cx + 0.6 * u, -0.45 * u, cx + 0.6 * u, 0.45 * u, cx, 0);
    cairo_fill(cr);

    if (asleep) {                                          /* closed eyes */
        rgba(cr, pal->bg, pal->bg.a);
        cairo_set_line_width(cr, fmax(0.12 * u, 1.0));
        cairo_new_path(cr);
        for (int sd = -1; sd <= 1; sd += 2) {
            cairo_move_to(cr, 1.5 * u, sd * 0.38 * u - 0.16 * u);
            cairo_line_to(cr, 1.5 * u, sd * 0.38 * u + 0.16 * u);
        }
        cairo_stroke(cr);
    } else {                                               /* open eyes */
        rgba(cr, pal->bg, pal->bg.a);
        cairo_new_path(cr);
        for (int sd = -1; sd <= 1; sd += 2) {
            ellipse_path(cr, 1.55 * u, sd * 0.38 * u, 0.26 * u, 0.26 * u);
            ellipse_path(cr, 1.8 * u, sd * 0.62 * u, 0.13 * u, 0.13 * u);
        }
        cairo_fill(cr);
    }
    cairo_restore(cr);

    if (asleep) {                                          /* floating "z z z" (drawn as lines: no fonts needed) */
        double bx = PX(s->pos), by = PY(s->pos);
        for (int k = 0; k < 3; k++) {
            double ph = fmod(s->time * 0.45 + k / 3.0, 1.0);
            double px = fmax(7.0, (int)(u * (1.0 + ph * 0.9)));
            double x = bx + 1.6 * u + ph * 2.0 * u, y = by - 1.6 * u - ph * 4.0 * u;
            double w = 0.5 * px, h = 0.52 * px;
            rgba(cr, pal->bg, (1.0 - ph) * pal->bg.a);
            cairo_set_line_width(cr, fmax(1.0, 0.16 * px));
            cairo_new_path(cr);
            cairo_move_to(cr, x, y - h);
            cairo_line_to(cr, x + w, y - h);
            cairo_line_to(cr, x, y);
            cairo_line_to(cr, x + w, y);
            cairo_stroke(cr);
        }
    }
    #undef PX
    #undef PY
}

/* ------------------------------------------------------------------ the logo */
/* Left-side legs in a 64x64 box as (hip, knee, foot); the right side is the mirror image. */
static const int ICON_LEGS[4][3][2] = {
    {{30, 26}, {16, 10}, {6, 8}}, {{28, 31}, {12, 26}, {3, 30}},
    {{28, 36}, {12, 42}, {4, 52}}, {{30, 41}, {18, 56}, {10, 60}},
};
#define ICON_ACCENT "#e45cc0"

typedef struct { int tile; const char *body, *edge, *eye; double leg, halo, line; } IconStyle;
static const IconStyle ICON_STYLES[2] = {
    {1, "#f1f1f8", NULL,      "#2a2a44", 3.6, 0.0, 0.0},       /* app: light spider on a dark tile */
    {0, "#f1f1f8", "#16161c", "#16161c", 5.0, 4.6, 3.2},       /* tray: light body, dark outline   */
};

static void hexcolor(cairo_t *cr, const char *hex)
{
    Rgba c = {0, 0, 0, 1};
    color_parse(hex, &c);
    cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a);
}

static void icon_legs_path(cairo_t *cr)
{
    cairo_new_path(cr);
    for (int i = 0; i < 4; i++)
        for (int mirror = 0; mirror < 2; mirror++)
            for (int k = 0; k < 3; k++) {
                double x = mirror ? 64 - ICON_LEGS[i][k][0] : ICON_LEGS[i][k][0], y = ICON_LEGS[i][k][1];
                if (k == 0) cairo_move_to(cr, x, y);
                else cairo_line_to(cr, x, y);
            }
}

void draw_icon(cairo_t *cr, double size, int style)
{
    const IconStyle *st = &ICON_STYLES[style];
    cairo_save(cr);
    cairo_scale(cr, size / 64.0, size / 64.0);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    if (st->tile) {
        cairo_pattern_t *g = cairo_pattern_create_linear(0, 0, 0, 64);
        Rgba a, b;
        color_parse("#3b3a6b", &a);
        color_parse("#1a1a2e", &b);
        cairo_pattern_add_color_stop_rgb(g, 0, a.r, a.g, a.b);
        cairo_pattern_add_color_stop_rgb(g, 1, b.r, b.g, b.b);
        double x = 1, y = 1, w = 62, h = 62, r = 14;
        cairo_new_sub_path(cr);
        cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
        cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
        cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
        cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
        cairo_close_path(cr);
        cairo_set_source(cr, g);
        cairo_fill_preserve(cr);
        cairo_pattern_destroy(g);
        hexcolor(cr, "#5a5a8c");
        cairo_set_line_width(cr, 1.2);
        cairo_stroke(cr);
        cairo_translate(cr, 32, 34);
        cairo_scale(cr, 0.8, 0.8);
        cairo_translate(cr, -32, -34);
    }
    if (st->edge) {
        icon_legs_path(cr);
        hexcolor(cr, st->edge);
        cairo_set_line_width(cr, st->leg + st->halo);
        cairo_stroke(cr);
    }
    icon_legs_path(cr);
    hexcolor(cr, st->body);
    cairo_set_line_width(cr, st->leg);
    cairo_stroke(cr);

    for (int part = 0; part < 2; part++) {                 /* abdomen, head */
        if (part == 0) ellipse_path(cr, 32, 43, 10, 12);
        else ellipse_path(cr, 32, 22, 7, 6.5);
        hexcolor(cr, st->body);
        if (st->edge) {
            cairo_fill_preserve(cr);
            hexcolor(cr, st->edge);
            cairo_set_line_width(cr, st->line);
            cairo_stroke(cr);
        } else {
            cairo_fill(cr);
        }
    }
    hexcolor(cr, ICON_ACCENT);                             /* hourglass mark */
    cairo_new_path(cr);
    triangle(cr, 28, 38, 36, 38, 32, 43);
    triangle(cr, 28, 48, 36, 48, 32, 43);
    cairo_fill(cr);
    hexcolor(cr, st->eye);                                 /* eyes */
    cairo_new_path(cr);
    ellipse_path(cr, 29.5, 19.5, 1.6, 1.6);
    ellipse_path(cr, 34.5, 19.5, 1.6, 1.6);
    cairo_fill(cr);
    cairo_restore(cr);
}

int save_icon_png(const char *path, int size, int style)
{
    cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
    cairo_t *cr = cairo_create(img);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);
    draw_icon(cr, size, style);
    cairo_destroy(cr);
    int ok = cairo_surface_write_to_png(img, path) == CAIRO_STATUS_SUCCESS;
    cairo_surface_destroy(img);
    return ok;
}

char *icon_svg(int style)
{
    const IconStyle *st = &ICON_STYLES[style];
    char *out = malloc(4096), *p = out;
    char legs[1024], *l = legs;
    for (int i = 0; i < 4; i++)
        for (int mirror = 0; mirror < 2; mirror++) {
            int x[3], y[3];
            for (int k = 0; k < 3; k++) {
                x[k] = mirror ? 64 - ICON_LEGS[i][k][0] : ICON_LEGS[i][k][0];
                y[k] = ICON_LEGS[i][k][1];
            }
            l += sprintf(l, "%sM%d %d L%d %d L%d %d", l == legs ? "" : " ", x[0], y[0], x[1], y[1], x[2], y[2]);
        }
    const char *cap = "stroke-linecap=\"round\" stroke-linejoin=\"round\" fill=\"none\"";
    p += sprintf(p, "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 64 64\">\n");
    if (st->tile) {
        p += sprintf(p, "<defs><linearGradient id=\"g\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\">"
                        "<stop offset=\"0\" stop-color=\"#3b3a6b\"/><stop offset=\"1\" stop-color=\"#1a1a2e\"/>"
                        "</linearGradient></defs>\n");
        p += sprintf(p, "<rect x=\"1\" y=\"1\" width=\"62\" height=\"62\" rx=\"14\" fill=\"url(#g)\" "
                        "stroke=\"#5a5a8c\" stroke-width=\"1.2\"/>\n");
        p += sprintf(p, "<g transform=\"translate(32 34) scale(0.8) translate(-32 -34)\">\n");
    } else {
        p += sprintf(p, "<g>\n");
    }
    if (st->edge)
        p += sprintf(p, "<path d=\"%s\" stroke=\"%s\" stroke-width=\"%g\" %s/>\n", legs, st->edge, st->leg + st->halo, cap);
    p += sprintf(p, "<path d=\"%s\" stroke=\"%s\" stroke-width=\"%g\" %s/>\n", legs, st->body, st->leg, cap);
    char outline[96] = "";
    if (st->edge) snprintf(outline, sizeof outline, "stroke=\"%s\" stroke-width=\"%g\"", st->edge, st->line);
    p += sprintf(p, "<ellipse cx=\"32\" cy=\"43\" rx=\"10\" ry=\"12\" fill=\"%s\" %s/>\n", st->body, outline);
    p += sprintf(p, "<ellipse cx=\"32\" cy=\"22\" rx=\"7\" ry=\"6.5\" fill=\"%s\" %s/>\n", st->body, outline);
    p += sprintf(p, "<path d=\"M28 38 L36 38 L32 43 Z M28 48 L36 48 L32 43 Z\" fill=\"%s\"/>\n", ICON_ACCENT);
    p += sprintf(p, "<circle cx=\"29.5\" cy=\"19.5\" r=\"1.6\" fill=\"%s\"/><circle cx=\"34.5\" cy=\"19.5\" r=\"1.6\" fill=\"%s\"/>\n",
                 st->eye, st->eye);
    p += sprintf(p, "</g></svg>\n");
    return out;
}
