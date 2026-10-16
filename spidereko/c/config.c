/* Settings: defaults, validation, colours, and the JSON config file
 * (~/.config/spidereko/config.json - the same file the Python version used). */
#define _GNU_SOURCE
#include "spidereko.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

const Cfg DEFAULTS = {
    .enabled = 1, .speed = 420.0, .idle = 2.5, .size = 1.0, .lift = 0.0,
    .fg = "#16161c", .bg = "#9a9aae", .accent = "#e45cc0",
    .fps = 30, .tofocus = 0, .shadow = 0,
};

const char *const KEY_NAME[K_COUNT] = {
    "enabled", "speed", "idle", "size", "lift", "fg", "bg", "accent", "fps", "tofocus", "shadow",
};

static const double LIMITS[K_COUNT][2] = {
    [K_SPEED] = {10, 10000}, [K_IDLE] = {0, 3600}, [K_SIZE] = {0.3, 8},
    [K_LIFT] = {-100, 100}, [K_FPS] = {5, 240},
};

int key_kind(int key)
{
    switch (key) {
    case K_ENABLED: case K_TOFOCUS: case K_SHADOW: return KIND_BOOL;
    case K_FG: case K_BG: case K_ACCENT: return KIND_COLOR;
    default: return KIND_NUM;
    }
}

void key_limits(int key, double *lo, double *hi)
{
    *lo = LIMITS[key][0];
    *hi = LIMITS[key][1];
}

double cfg_get_num(const Cfg *c, int key)
{
    switch (key) {
    case K_ENABLED: return c->enabled;
    case K_SPEED: return c->speed;
    case K_IDLE: return c->idle;
    case K_SIZE: return c->size;
    case K_LIFT: return c->lift;
    case K_FPS: return c->fps;
    case K_TOFOCUS: return c->tofocus;
    case K_SHADOW: return c->shadow;
    }
    return 0;
}

static char *color_field(Cfg *c, int key)
{
    return key == K_FG ? c->fg : key == K_BG ? c->bg : c->accent;
}

const char *cfg_get_str(const Cfg *c, int key)
{
    return color_field((Cfg *)c, key);
}

int cfg_set_num(Cfg *c, int key, double v, char *err, size_t en)
{
    if (key_kind(key) == KIND_BOOL) {
        int b = v != 0;
        if (key == K_ENABLED) c->enabled = b;
        else if (key == K_TOFOCUS) c->tofocus = b;
        else c->shadow = b;
        return 0;
    }
    if (key_kind(key) != KIND_NUM) {
        snprintf(err, en, "is not a number setting");
        return -1;
    }
    if (!isfinite(v)) {
        snprintf(err, en, "must be a number");
        return -1;
    }
    if (v < LIMITS[key][0] || v > LIMITS[key][1]) {
        snprintf(err, en, "must be between %g and %g", LIMITS[key][0], LIMITS[key][1]);
        return -1;
    }
    switch (key) {
    case K_SPEED: c->speed = v; break;
    case K_IDLE: c->idle = v; break;
    case K_SIZE: c->size = v; break;
    case K_LIFT: c->lift = v; break;
    case K_FPS: c->fps = (int)v; break;
    }
    return 0;
}

int cfg_set_str(Cfg *c, int key, const char *s, char *err, size_t en)
{
    if (key_kind(key) != KIND_COLOR || !color_valid(s) || strlen(s) >= sizeof c->fg) {
        snprintf(err, en, "is not a valid color (use #rrggbb or a color name)");
        return -1;
    }
    strcpy(color_field(c, key), s);
    return 0;
}

int cfg_key_equal(const Cfg *a, const Cfg *b, int key)
{
    if (key_kind(key) == KIND_COLOR) return strcmp(cfg_get_str(a, key), cfg_get_str(b, key)) == 0;
    return cfg_get_num(a, key) == cfg_get_num(b, key);
}

void cfg_copy_key(Cfg *dst, const Cfg *src, int key)
{
    if (key_kind(key) == KIND_COLOR) {
        strcpy(color_field(dst, key), cfg_get_str(src, key));
        return;
    }
    char e[8];
    cfg_set_num(dst, key, cfg_get_num(src, key), e, sizeof e);
}

/* ------------------------------------------------------------------ colours */
static Display *cdpy;

void color_set_display(Display *d) { cdpy = d; }

static int hexv(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int named_color(const char *s, Rgba *o)
{
    static int tried;
    if (!cdpy && !tried) {
        tried = 1;
        cdpy = XOpenDisplay(NULL);          /* command-line use: peek at the server's colour names */
    }
    if (!cdpy) {
        for (const char *p = s; *p; p++)    /* no X server: accept anything that looks like a name */
            if (!isalnum((unsigned char)*p) && *p != ' ') return 0;
        if (o) o->r = o->g = o->b = 0.5, o->a = 1;
        return *s != 0;
    }
    XColor exact, screen;
    if (!XLookupColor(cdpy, DefaultColormap(cdpy, DefaultScreen(cdpy)), s, &exact, &screen)) return 0;
    if (o) {
        o->r = exact.red / 65535.0;
        o->g = exact.green / 65535.0;
        o->b = exact.blue / 65535.0;
        o->a = 1;
    }
    return 1;
}

static int parse_hex(const char *s, Rgba *o)
{
    size_t n = strlen(s + 1);
    int v[8];
    if (n != 3 && n != 6 && n != 8) return 0;
    for (size_t i = 0; i < n; i++)
        if ((v[i] = hexv(s[1 + i])) < 0) return 0;
    double a = 255, r, g, b;
    if (n == 3) {
        r = v[0] * 17; g = v[1] * 17; b = v[2] * 17;
    } else if (n == 6) {
        r = v[0] * 16 + v[1]; g = v[2] * 16 + v[3]; b = v[4] * 16 + v[5];
    } else {                                 /* #aarrggbb, like Qt */
        a = v[0] * 16 + v[1]; r = v[2] * 16 + v[3]; g = v[4] * 16 + v[5]; b = v[6] * 16 + v[7];
    }
    if (o) o->r = r / 255, o->g = g / 255, o->b = b / 255, o->a = a / 255;
    return 1;
}

int color_parse(const char *s, Rgba *out)
{
    Rgba tmp;
    if (!out) out = &tmp;
    if (s[0] == '#') return parse_hex(s, out);
    return named_color(s, out);
}

int color_valid(const char *s) { return s && *s && color_parse(s, NULL); }

void palette_from_cfg(const Cfg *c, Palette *p)
{
    if (!color_parse(c->fg, &p->fg)) color_parse(DEFAULTS.fg, &p->fg);
    if (!color_parse(c->bg, &p->bg)) color_parse(DEFAULTS.bg, &p->bg);
    if (!color_parse(c->accent, &p->accent)) color_parse(DEFAULTS.accent, &p->accent);
}

/* ------------------------------------------------------------------ file */
const char *config_path(void)
{
    static char path[1024];
    const char *base = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (!home || !*home) {
        struct passwd *pw = getpwuid(getuid());
        home = pw ? pw->pw_dir : "/tmp";
    }
    if (base && *base) snprintf(path, sizeof path, "%s/" APP_NAME "/config.json", base);
    else snprintf(path, sizeof path, "%s/.config/" APP_NAME "/config.json", home);
    return path;
}

enum { JV_NONE, JV_BOOL, JV_NUM, JV_STR };
typedef struct { int type; double num; char str[64]; } JVal;

static void skip_ws(const char **p)
{
    while (**p && (isspace((unsigned char)**p) || **p == ',')) (*p)++;
}

static int read_string(const char **pp, char *out, size_t n)   /* *pp is just after the opening quote */
{
    const char *p = *pp;
    size_t i = 0;
    while (*p && *p != '"') {
        char c = *p++;
        if (c == '\\' && *p) c = *p++;
        if (i + 1 < n) out[i++] = c;
    }
    if (*p != '"') return 0;
    out[i] = 0;
    *pp = p + 1;
    return 1;
}

static void skip_container(const char **pp)                    /* skip a nested {...} or [...] */
{
    const char *p = *pp;
    int depth = 0, in_str = 0;
    for (; *p; p++) {
        if (in_str) {
            if (*p == '\\' && p[1]) p++;
            else if (*p == '"') in_str = 0;
        } else if (*p == '"') in_str = 1;
        else if (*p == '{' || *p == '[') depth++;
        else if ((*p == '}' || *p == ']') && --depth == 0) { p++; break; }
    }
    *pp = p;
}

void cfg_load(Cfg *out)
{
    *out = DEFAULTS;
    FILE *f = fopen(config_path(), "r");
    if (!f) return;
    char *buf = malloc(65536);
    if (!buf) { fclose(f); return; }
    size_t len = fread(buf, 1, 65535, f);
    fclose(f);
    buf[len] = 0;

    JVal vals[K_COUNT];
    memset(vals, 0, sizeof vals);
    const char *p = strchr(buf, '{');
    if (p) p++;
    while (p) {
        skip_ws(&p);
        if (*p != '"') break;
        char key[64];
        p++;
        if (!read_string(&p, key, sizeof key)) break;
        skip_ws(&p);
        if (*p != ':') break;
        p++;
        skip_ws(&p);
        JVal v = {JV_NONE, 0, ""};
        if (*p == '"') {
            p++;
            if (!read_string(&p, v.str, sizeof v.str)) break;
            v.type = JV_STR;
        } else if (!strncmp(p, "true", 4)) { v.type = JV_BOOL; v.num = 1; p += 4; }
        else if (!strncmp(p, "false", 5)) { v.type = JV_BOOL; v.num = 0; p += 5; }
        else if (!strncmp(p, "null", 4)) p += 4;
        else if (*p == '-' || isdigit((unsigned char)*p)) {
            char *end;
            v.num = strtod(p, &end);
            if (end == p) break;
            v.type = JV_NUM;
            p = end;
        } else if (*p == '{' || *p == '[') skip_container(&p);
        else break;
        for (int k = 0; k < K_COUNT; k++)
            if (!strcmp(key, KEY_NAME[k])) vals[k] = v;
    }
    free(buf);

    char err[96];
    for (int k = 0; k < K_COUNT; k++) {            /* a bad value just keeps the default */
        int kind = key_kind(k);
        if (kind == KIND_BOOL && vals[k].type == JV_BOOL) cfg_set_num(out, k, vals[k].num, err, sizeof err);
        else if (kind == KIND_NUM && vals[k].type == JV_NUM) cfg_set_num(out, k, vals[k].num, err, sizeof err);
        else if (kind == KIND_COLOR && vals[k].type == JV_STR) cfg_set_str(out, k, vals[k].str, err, sizeof err);
    }
}

static void fmt_double(char *buf, size_t n, double v)       /* 420 -> 420.0 so Python reads a float too */
{
    snprintf(buf, n, "%.12g", v);
    if (!strpbrk(buf, ".eEn")) strncat(buf, ".0", n - strlen(buf) - 1);
}

static int mkdir_p(const char *path)
{
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (mkdir(tmp, 0755) && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    return mkdir(tmp, 0755) && errno != EEXIST ? -1 : 0;
}

int cfg_save(const Cfg *c)
{
    const char *path = config_path();
    char dir[1024], tmp[1100], a[40], b[40], d[40], e[40];
    snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) { *slash = 0; if (mkdir_p(dir)) return -1; }
    snprintf(tmp, sizeof tmp, "%s.%d.tmp", path, (int)getpid());    /* own temp name: writers never share one */
    FILE *f = fopen(tmp, "w");
    if (!f) return -1;
    fmt_double(a, sizeof a, c->speed);
    fmt_double(b, sizeof b, c->idle);
    fmt_double(d, sizeof d, c->size);
    fmt_double(e, sizeof e, c->lift);
    fprintf(f,
            "{\n"
            "  \"enabled\": %s,\n  \"speed\": %s,\n  \"idle\": %s,\n  \"size\": %s,\n  \"lift\": %s,\n"
            "  \"fg\": \"%s\",\n  \"bg\": \"%s\",\n  \"accent\": \"%s\",\n  \"fps\": %d,\n"
            "  \"tofocus\": %s,\n  \"shadow\": %s\n}\n",
            c->enabled ? "true" : "false", a, b, d, e, c->fg, c->bg, c->accent, c->fps,
            c->tofocus ? "true" : "false", c->shadow ? "true" : "false");
    int bad = ferror(f);
    if (fclose(f)) bad = 1;
    if (bad || rename(tmp, path)) {                         /* atomic: a reader never sees half a file */
        int saved = errno;
        unlink(tmp);
        errno = saved;
        return -1;
    }
    return 0;
}
