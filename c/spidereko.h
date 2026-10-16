/* spidereko - a spider that chases your mouse pointer (C + Xlib + Cairo)
 * Shared declarations. */
#ifndef SPIDEREKO_H
#define SPIDEREKO_H

#include <complex.h>
#include <stddef.h>
#include <stdint.h>
#include <cairo.h>
#include <X11/Xlib.h>

#define APP_NAME "spidereko"

typedef double complex cplx;

/* ------------------------------------------------------------------ settings */
enum { K_ENABLED, K_SPEED, K_IDLE, K_SIZE, K_LIFT, K_FG, K_BG, K_ACCENT,
       K_FPS, K_TOFOCUS, K_SHADOW, K_COUNT };
enum { KIND_BOOL, KIND_NUM, KIND_COLOR };

typedef struct {
    int enabled;
    double speed, idle, size, lift;
    char fg[40], bg[40], accent[40];
    int fps;
    int tofocus, shadow;
} Cfg;

typedef struct { double r, g, b, a; } Rgba;
typedef struct { Rgba fg, bg, accent; } Palette;

extern const Cfg DEFAULTS;
extern const char *const KEY_NAME[K_COUNT];

int         key_kind(int key);
void        key_limits(int key, double *lo, double *hi);
double      cfg_get_num(const Cfg *c, int key);
const char *cfg_get_str(const Cfg *c, int key);
int         cfg_set_num(Cfg *c, int key, double v, char *err, size_t en);   /* 0 ok, -1 + message */
int         cfg_set_str(Cfg *c, int key, const char *s, char *err, size_t en);
int         cfg_key_equal(const Cfg *a, const Cfg *b, int key);
void        cfg_copy_key(Cfg *dst, const Cfg *src, int key);
void        cfg_load(Cfg *out);              /* defaults + whatever the file holds (bad values ignored) */
int         cfg_save(const Cfg *c);          /* 0 ok, -1 error (errno set) */
const char *config_path(void);

void color_set_display(Display *d);          /* lets colour NAMES be looked up in the X server */
int  color_valid(const char *s);
int  color_parse(const char *s, Rgba *out);  /* #rgb #rrggbb #aarrggbb or an X colour name */
void palette_from_cfg(const Cfg *c, Palette *p);

/* ------------------------------------------------------------------ simulation */
enum { CHASE, SLEEP };

#define EDGE_MARGIN 4.0   /* body units: never sit closer than this to the top edge of the screen */
#define CALM_MS     66    /* redraw interval while standing still (only the breathing moves)   */
#define SLEEP_MS    150   /* redraw / pointer-check interval while fast asleep                 */

typedef struct {
    int side, group, stepping;
    double hx, angle, t, dur;
    cplx hip, rest, foot, src, dst;
} Leg;

typedef struct {
    const Cfg *cfg;                  /* LIVE settings (speed, idle, size, lift are read every step) */
    cplx pos, vel, anchor;
    double heading, curl, idle_t, time;
    int state;
    Leg legs[8];
    double u, L1, L2, rest_radius, max_reach, arrive_r, turn_min, look_min, sit_dist;
    int window_size;
} Sim;

void sim_init(Sim *s, const Cfg *cfg, cplx start);
void sim_set_size(Sim *s);
void sim_wake(Sim *s);
cplx sim_anchor_for(const Sim *s, cplx look);
void sim_step(Sim *s, double dt, cplx target, cplx look);
int  sim_settled(const Sim *s);
int  sim_calm(const Sim *s);
void sim_leg_joints(const Sim *s, const Leg *leg, cplx *hip, cplx *knee, cplx *foot);

/* ------------------------------------------------------------------ painting */
void  paint_spider(cairo_t *cr, const Sim *s, double ox, double oy, const Palette *pal, int shadow);
void  draw_icon(cairo_t *cr, double size, int style);     /* style 0 = app tile, 1 = tray */
int   save_icon_png(const char *path, int size, int style);
char *icon_svg(int style);                                  /* malloc'd */

/* ------------------------------------------------------------------ processes, install */
int  try_lock(void);                 /* 1 = we are THE spider now */
int  running_pid(void);              /* pid of the running spider or 0 */
int  stop_instances(void);           /* number of spiders told to stop */
void request_settings(void);
int  take_settings_request(void);
int  install_self(void);
int  uninstall_self(void);

/* ------------------------------------------------------------------ the GUI program */
int  run_gui(Cfg *conf, int show_settings);

/* services gui.c offers to ui.c */
extern Display *g_dpy;
extern int      g_scr;
extern Window   g_root;
extern Cfg      g_cfg;
extern Atom     g_wm_protocols, g_wm_delete, g_net_wm_name, g_net_wm_icon, g_utf8;
void app_commit_num(int key, double v);     /* apply now, save a moment later */
void app_commit_str(int key, const char *s);
void app_toggle_enabled(void);
void app_reset_defaults(void);
void app_quit(void);
int  app_wayland(void);
int  app_monitor_at(int x, int y, int *mx, int *my, int *mw, int *mh);

/* services ui.c offers to gui.c */
void ui_open_settings(void);
void ui_refresh(void);
int  ui_event(XEvent *ev);                  /* 1 = the event was ours */
void ui_menu_open(int x_root, int y_root);

#endif
