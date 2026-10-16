/* The running spider: overlay window, tray icon, timers and the event loop. */
#define _GNU_SOURCE
#include "spidereko.h"
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/shape.h>
#include <cairo-xlib.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define NEVER INT64_MAX
#define WAYLAND_WARNING \
    "Wayland session detected. Global pointer tracking only works on X11, so the spider will not " \
    "follow the pointer properly. Log in with an X11 session (on KDE: choose 'Plasma (X11)' on the login screen)."

Display *g_dpy;
int      g_scr;
Window   g_root;
Cfg      g_cfg;                       /* the LIVE settings: the simulation reads them every step */
Atom     g_wm_protocols, g_wm_delete, g_net_wm_name, g_net_wm_icon, g_utf8;

static Atom a_active, a_frame_ext, a_tray_sel, a_tray_opcode, a_xembed_info;
static Cfg  last_disk;                /* what the config file held when we last read / wrote it */
static Palette pal;
static Sim  sim;
static int  wayland, quitting, paused = 1, fast_ms = 33;
static int64_t t_tick = NEVER, t_raise = NEVER, t_save = NEVER, t_tray = NEVER, t_focus = NEVER, t_mon = NEVER;
static int64_t last_tick_ms;

static int64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* ------------------------------------------------------------------ signals */
static int sigpipe[2];
static volatile sig_atomic_t flag_reload, flag_quit;

static void on_signal(int sig)
{
    if (sig == SIGUSR1) flag_reload = 1;
    else flag_quit = 1;
    char c = 0;
    if (write(sigpipe[1], &c, 1) < 0) { /* pipe full: the loop wakes up anyway */ }
}

static int on_x_error(Display *d, XErrorEvent *e) { (void)d; (void)e; return 0; }   /* e.g. a window that just vanished */

static void save_now(void);
static int on_io_error(Display *d)             /* the X server went away (logout): leave quietly */
{
    (void)d;
    if (t_save != NEVER) cfg_save(&g_cfg);
    _exit(0);
}

/* ------------------------------------------------------------------ monitors */
typedef struct { int x, y, w, h; } Mon;
static Mon mons[16];
static int nmons;

static void refresh_monitors(void)
{
    int n = 0;
    XRRMonitorInfo *m = XRRGetMonitors(g_dpy, g_root, True, &n);
    nmons = 0;
    for (int i = 0; m && i < n && i < 16; i++)
        mons[nmons++] = (Mon){m[i].x, m[i].y, m[i].width, m[i].height};
    if (m) XRRFreeMonitors(m);
}

int app_monitor_at(int x, int y, int *mx, int *my, int *mw, int *mh)
{
    for (int i = 0; i < nmons; i++)
        if (x >= mons[i].x && x < mons[i].x + mons[i].w && y >= mons[i].y && y < mons[i].y + mons[i].h) {
            *mx = mons[i].x; *my = mons[i].y; *mw = mons[i].w; *mh = mons[i].h;
            return 1;
        }
    *mx = 0; *my = 0;
    *mw = DisplayWidth(g_dpy, g_scr);
    *mh = DisplayHeight(g_dpy, g_scr);
    return 0;
}

int app_wayland(void) { return wayland; }
void app_quit(void) { quitting = 1; }

/* ------------------------------------------------------------------ the overlay window */
static struct {
    Window win;
    int argb, size, mapped, placed, px, py, ox, oy;
    Visual *vis;
    cairo_surface_t *img, *xs;
    cairo_t *icr;
    unsigned char *bits;
} ov;

static void overlay_surfaces(void)
{
    ov.img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, ov.size, ov.size);
    ov.icr = cairo_create(ov.img);
    ov.xs = cairo_xlib_surface_create(g_dpy, ov.win, ov.vis, ov.size, ov.size);
    free(ov.bits);
    ov.bits = ov.argb ? NULL : calloc((size_t)((ov.size + 7) / 8) * ov.size, 1);
}

static void overlay_free_surfaces(void)
{
    cairo_destroy(ov.icr);
    cairo_surface_destroy(ov.img);
    cairo_surface_destroy(ov.xs);
}

static void overlay_create(void)
{
    char name[48];
    snprintf(name, sizeof name, "_NET_WM_CM_S%d", g_scr);
    int comp = XGetSelectionOwner(g_dpy, XInternAtom(g_dpy, name, False)) != None;
    XVisualInfo vi;
    int have32 = XMatchVisualInfo(g_dpy, g_scr, 32, TrueColor, &vi);
    int depth;
    Colormap cmap;
    ov.argb = comp && have32;
    if (ov.argb) {
        ov.vis = vi.visual;
        depth = 32;
        cmap = XCreateColormap(g_dpy, g_root, ov.vis, AllocNone);
    } else {
        ov.vis = DefaultVisual(g_dpy, g_scr);
        depth = DefaultDepth(g_dpy, g_scr);
        cmap = DefaultColormap(g_dpy, g_scr);
        fprintf(stderr, "spidereko: no compositing manager%s - drawing a hard-edged shaped window instead.\n",
                have32 ? "" : " / ARGB visual");
    }
    XSetWindowAttributes a;
    memset(&a, 0, sizeof a);
    a.override_redirect = True;                 /* no window manager: no frame, always on top, no focus */
    a.colormap = cmap;
    a.background_pixel = 0;
    a.border_pixel = 0;
    a.event_mask = ExposureMask;
    ov.size = sim.window_size;
    ov.win = XCreateWindow(g_dpy, g_root, 0, 0, ov.size, ov.size, 0, depth, InputOutput, ov.vis,
                           CWOverrideRedirect | CWColormap | CWBackPixel | CWBorderPixel | CWEventMask, &a);
    XShapeCombineRectangles(g_dpy, ov.win, ShapeInput, 0, 0, NULL, 0, ShapeSet, Unsorted);   /* clicks go through */
    XClassHint ch = {(char *)APP_NAME, (char *)"Spidereko"};
    XSetClassHint(g_dpy, ov.win, &ch);
    overlay_surfaces();
}

static void overlay_resize(int size)
{
    if (size == ov.size) return;
    overlay_free_surfaces();
    ov.size = size;
    XResizeWindow(g_dpy, ov.win, size, size);
    overlay_surfaces();
}

static void overlay_render(void)
{
    if (!ov.mapped) return;
    cairo_t *cr = ov.icr;
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_restore(cr);
    paint_spider(cr, &sim, ov.ox, ov.oy, &pal, g_cfg.shadow);
    cairo_surface_flush(ov.img);

    cairo_t *wc = cairo_create(ov.xs);
    if (ov.argb) {
        cairo_set_operator(wc, CAIRO_OPERATOR_SOURCE);          /* one upload per frame, no flicker */
        cairo_set_source_surface(wc, ov.img, 0, 0);
        cairo_paint(wc);
    } else {
        cairo_set_source_rgb(wc, 0, 0, 0);
        cairo_paint(wc);
        cairo_set_source_surface(wc, ov.img, 0, 0);
        cairo_paint(wc);
    }
    cairo_destroy(wc);
    cairo_surface_flush(ov.xs);

    if (!ov.argb && ov.bits) {                                  /* no compositor: cut the window to the shape */
        int w = ov.size, bpl = (w + 7) / 8, stride = cairo_image_surface_get_stride(ov.img);
        unsigned char *d = cairo_image_surface_get_data(ov.img);
        memset(ov.bits, 0, (size_t)bpl * w);
        for (int y = 0; y < w; y++)
            for (int x = 0; x < w; x++)
                if (d[y * stride + x * 4 + 3] >= 96) ov.bits[y * bpl + (x >> 3)] |= 1 << (x & 7);
        Pixmap pm = XCreateBitmapFromData(g_dpy, ov.win, (char *)ov.bits, w, w);
        XShapeCombineMask(g_dpy, ov.win, ShapeBounding, 0, 0, pm, ShapeSet);
        XFreePixmap(g_dpy, pm);
    }
}

/* ------------------------------------------------------------------ pointer and focused window */
static cplx pointer_pos(void)
{
    static cplx last;
    Window r, c;
    int rx, ry, wx, wy;
    unsigned int m;
    if (XQueryPointer(g_dpy, g_root, &r, &c, &rx, &ry, &wx, &wy, &m)) last = rx + I * ry;
    return last;
}

static int focus_ok, focus_misses;
static cplx focus_target;

static void focus_poll(void)         /* the focused window, natively (EWMH) - no xdotool needed */
{
    Window w = None;
    Atom type;
    int fmt;
    unsigned long n, after;
    unsigned char *data = NULL;
    if (XGetWindowProperty(g_dpy, g_root, a_active, 0, 1, False, XA_WINDOW, &type, &fmt, &n, &after, &data) == Success && data) {
        if (type == XA_WINDOW && n == 1) w = *(Window *)data;
        XFree(data);
    }
    if (!w) {
        int rev;
        XGetInputFocus(g_dpy, &w, &rev);
        if (w == PointerRoot || w == g_root) w = None;
    }
    int ok = 0;
    if (w) {
        Window root, child;
        int x, y, rx, ry;
        unsigned int ww, hh, bw, depth;
        if (XGetGeometry(g_dpy, w, &root, &x, &y, &ww, &hh, &bw, &depth) &&
            XTranslateCoordinates(g_dpy, w, g_root, 0, 0, &rx, &ry, &child) && ww > 40) {
            long top = 0, left = 0, right = 0;
            data = NULL;
            if (XGetWindowProperty(g_dpy, w, a_frame_ext, 0, 4, False, XA_CARDINAL, &type, &fmt, &n, &after, &data) == Success && data) {
                if (n == 4) { left = ((long *)data)[0]; right = ((long *)data)[1]; top = ((long *)data)[2]; }
                XFree(data);
            }
            focus_target = (rx - left + (ww + left + right) / 2.0) + I * (double)(ry - top);   /* top edge, centre */
            ok = 1;
        }
    }
    if (ok) {
        focus_ok = 1;
        focus_misses = 0;
    } else if (++focus_misses >= 3) {      /* a few misses in a row: chase the pointer instead */
        focus_ok = 0;
    }
}

/* ------------------------------------------------------------------ settings -> spider */
static void set_paused(int p)
{
    if (p == paused) return;
    paused = p;
    int64_t now = now_ms();
    if (p) {
        XUnmapWindow(g_dpy, ov.win);
        ov.mapped = 0;
        t_tick = t_raise = t_focus = NEVER;
    } else {
        last_tick_ms = now;
        ov.placed = 0;
        ov.mapped = 1;
        XMapRaised(g_dpy, ov.win);
        t_tick = now;
        t_raise = now + 3000;
        t_focus = g_cfg.tofocus ? now : NEVER;
    }
}

static void apply_settings(const Cfg *n)
{
    Cfg old = g_cfg;
    g_cfg = *n;
    palette_from_cfg(&g_cfg, &pal);
    if (g_cfg.size != old.size) {
        sim_set_size(&sim);
        overlay_resize(sim.window_size);
    }
    if (g_cfg.size != old.size || g_cfg.lift != old.lift) sim_wake(&sim);   /* a sleeper must reach its new spot */
    fast_ms = g_cfg.fps > 0 ? 1000 / g_cfg.fps : 33;
    if (fast_ms < 5) fast_ms = 5;
    set_paused(!g_cfg.enabled);
    if (!paused) {
        if (g_cfg.tofocus && t_focus == NEVER) t_focus = now_ms();
        if (!g_cfg.tofocus) { t_focus = NEVER; focus_ok = 0; }
        t_tick = now_ms();                        /* show the change right away */
    }
    ui_refresh();
}

static void save_now(void)
{
    t_save = NEVER;
    if (cfg_save(&g_cfg) == 0) last_disk = g_cfg;
    else fprintf(stderr, "spidereko: cannot save the settings: %s\n", strerror(errno));   /* never fatal */
}

void app_commit_num(int key, double v)
{
    Cfg n = g_cfg;
    char err[96];
    if (cfg_set_num(&n, key, v, err, sizeof err) == 0) {
        apply_settings(&n);
        t_save = now_ms() + 300;                  /* a slider drag = one write, a moment after the last move */
    }
}

void app_commit_str(int key, const char *s)
{
    Cfg n = g_cfg;
    char err[96];
    if (cfg_set_str(&n, key, s, err, sizeof err) == 0) {
        apply_settings(&n);
        t_save = now_ms() + 300;
    }
}

void app_toggle_enabled(void)
{
    Cfg n = g_cfg;
    n.enabled = !n.enabled;
    apply_settings(&n);
    save_now();
}

void app_reset_defaults(void)
{
    Cfg n = DEFAULTS;
    apply_settings(&n);
    save_now();
}

static void on_reload(void)
{
    /* take over only what CHANGED in the file - an edit still waiting to be saved is not reverted */
    Cfg disk, now = g_cfg;
    cfg_load(&disk);
    for (int k = 0; k < K_COUNT; k++)
        if (!cfg_key_equal(&disk, &last_disk, k)) cfg_copy_key(&now, &disk, k);
    last_disk = disk;
    apply_settings(&now);
    if (take_settings_request()) ui_open_settings();      /* 'spidereko -settings' / the app icon again */
}

/* ------------------------------------------------------------------ tray icon (XEmbed system tray) */
static struct { Window win; int docked, w, h, tries; } tray;

static void tray_draw(void)
{
    if (!tray.win) return;
    int sz = tray.w < tray.h ? tray.w : tray.h;
    if (sz < 8) sz = 22;
    cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sz, sz);
    cairo_t *c = cairo_create(img);
    draw_icon(c, sz, 1);
    cairo_destroy(c);
    XClearWindow(g_dpy, tray.win);                                 /* parent-relative background first */
    cairo_surface_t *xs = cairo_xlib_surface_create(g_dpy, tray.win, DefaultVisual(g_dpy, g_scr), tray.w, tray.h);
    cairo_t *wc = cairo_create(xs);
    cairo_set_source_surface(wc, img, (tray.w - sz) / 2.0, (tray.h - sz) / 2.0);
    cairo_paint(wc);
    cairo_destroy(wc);
    cairo_surface_destroy(xs);
    cairo_surface_destroy(img);
}

static int tray_dock(void)
{
    Window mgr = XGetSelectionOwner(g_dpy, a_tray_sel);
    if (mgr == None) return 0;
    if (!tray.win) {
        tray.w = tray.h = 24;
        tray.win = XCreateSimpleWindow(g_dpy, g_root, 0, 0, 24, 24, 0, 0, 0);
        XSelectInput(g_dpy, tray.win, ExposureMask | ButtonPressMask | StructureNotifyMask);
        XSetWindowBackgroundPixmap(g_dpy, tray.win, ParentRelative);
        XClassHint ch = {(char *)APP_NAME, (char *)"Spidereko"};
        XSetClassHint(g_dpy, tray.win, &ch);
        long info[2] = {0, 1};                                     /* XEmbed version 0, "mapped" */
        XChangeProperty(g_dpy, tray.win, a_xembed_info, a_xembed_info, 32, PropModeReplace, (unsigned char *)info, 2);
    }
    XEvent ev;
    memset(&ev, 0, sizeof ev);
    ev.xclient.type = ClientMessage;
    ev.xclient.window = mgr;
    ev.xclient.message_type = a_tray_opcode;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = CurrentTime;
    ev.xclient.data.l[1] = 0;                                      /* SYSTEM_TRAY_REQUEST_DOCK */
    ev.xclient.data.l[2] = (long)tray.win;
    XSendEvent(g_dpy, mgr, False, NoEventMask, &ev);
    tray.docked = 1;
    return 1;
}

static void tray_lost(void)
{
    if (tray.win) XDestroyWindow(g_dpy, tray.win);
    tray.win = 0;
    tray.docked = 0;
    tray.tries = 0;
    t_tray = now_ms() + 5000;                                      /* the panel restarted: dock again */
}

static void tray_retry(int64_t now)
{
    t_tray = NEVER;
    if (tray_dock()) return;
    if (++tray.tries < 36) t_tray = now + 5000;                    /* at login the panel is often not ready yet */
}

/* ------------------------------------------------------------------ the main tick */
static cplx keep_on_screen(cplx target, cplx look)
{
    int mx, my, mw, mh;                      /* near the top edge the spider slides down instead of leaving the screen */
    if (app_monitor_at((int)creal(look), (int)cimag(look), &mx, &my, &mw, &mh)) {
        double top = my + EDGE_MARGIN * sim.u;
        if (cimag(target) < top) target = creal(target) + I * top;
    }
    return target;
}

static void tick(int64_t now)
{
    double real = (now - last_tick_ms) / 1000.0;
    if (real > 0.25) real = 0.25;
    if (real < 0) real = 0;
    last_tick_ms = now;

    cplx look = (g_cfg.tofocus && focus_ok) ? focus_target : pointer_pos();
    cplx target = keep_on_screen(sim_anchor_for(&sim, look), look);
    int n = (int)ceil(real / 0.05);                  /* a slow tick is simulated in small steps: same behaviour */
    if (n < 1) n = 1;
    for (int i = 0; i < n; i++) sim_step(&sim, real / n, target, look);

    ov.ox = (int)floor(creal(sim.pos) - ov.size / 2.0);
    ov.oy = (int)floor(cimag(sim.pos) - ov.size / 2.0);
    if (!ov.placed || ov.ox != ov.px || ov.oy != ov.py) {    /* a standing spider needs no window move at all */
        XMoveWindow(g_dpy, ov.win, ov.ox, ov.oy);
        ov.px = ov.ox; ov.py = ov.oy; ov.placed = 1;
    }
    overlay_render();

    int settled = sim_settled(&sim), interval;       /* redraw at full rate only while something really moves */
    if (settled) interval = SLEEP_MS;
    else if (sim_calm(&sim)) interval = fast_ms > CALM_MS ? fast_ms : CALM_MS;
    else interval = fast_ms;
    t_tick = now + interval;
}

static void timers_due(void)
{
    int64_t now = now_ms();
    if (now >= t_tick) tick(now);
    if (now >= t_raise) {
        XRaiseWindow(g_dpy, ov.win);
        t_raise = paused ? NEVER : now + 3000;           /* stay above windows that get raised later */
    }
    if (now >= t_focus) {
        focus_poll();
        t_focus = (g_cfg.tofocus && !paused) ? now + (sim_settled(&sim) ? 1000 : 500) : NEVER;
    }
    if (now >= t_mon) {
        refresh_monitors();
        t_mon = now + 5000;
    }
    if (now >= t_tray) tray_retry(now);
    if (now >= t_save) save_now();
}

/* ------------------------------------------------------------------ events */
static void handle_event(XEvent *ev)
{
    if (ui_event(ev)) return;
    switch (ev->type) {
    case Expose:
        if (ev->xexpose.count) break;
        if (ev->xexpose.window == ov.win) overlay_render();
        else if (ev->xexpose.window == tray.win) tray_draw();
        break;
    case ConfigureNotify:
        if (ev->xconfigure.window == tray.win) {
            tray.w = ev->xconfigure.width;
            tray.h = ev->xconfigure.height;
            tray_draw();
        }
        break;
    case ReparentNotify:
        if (ev->xreparent.window == tray.win && ev->xreparent.parent == g_root && tray.docked) tray_lost();
        break;
    case DestroyNotify:
        if (ev->xdestroywindow.window == tray.win) { tray.win = 0; tray.docked = 0; tray.tries = 0; t_tray = now_ms() + 5000; }
        break;
    case ButtonPress:
        if (ev->xbutton.window == tray.win) {
            if (ev->xbutton.button == Button1) ui_open_settings();
            else if (ev->xbutton.button == Button3) ui_menu_open(ev->xbutton.x_root, ev->xbutton.y_root);
        }
        break;
    }
}

static void notify_wayland(void)
{
    pid_t pid = fork();
    if (pid == 0) {                                  /* a desktop notification, if notify-send exists */
        if (fork() == 0) {
            int nul = open("/dev/null", O_RDWR);
            if (nul >= 0) { dup2(nul, 1); dup2(nul, 2); }
            execlp("notify-send", "notify-send", "-a", "Spidereko", "-t", "20000", "Spidereko", WAYLAND_WARNING, (char *)NULL);
        }
        _exit(0);
    }
    if (pid > 0) waitpid(pid, NULL, 0);
}

int run_gui(Cfg *conf, int show_settings)
{
    const char *session = getenv("XDG_SESSION_TYPE");
    wayland = session && !strcasecmp(session, "wayland");
    if (wayland) fprintf(stderr, "spidereko: warning - %s\n", WAYLAND_WARNING);

    XSetErrorHandler(on_x_error);
    XSetIOErrorHandler(on_io_error);
    g_dpy = XOpenDisplay(NULL);
    if (!g_dpy) {
        fprintf(stderr, "spidereko: cannot open the X display (is DISPLAY set? this needs an X11 session)\n");
        return 1;
    }
    g_scr = DefaultScreen(g_dpy);
    g_root = RootWindow(g_dpy, g_scr);
    g_wm_protocols = XInternAtom(g_dpy, "WM_PROTOCOLS", False);
    g_wm_delete = XInternAtom(g_dpy, "WM_DELETE_WINDOW", False);
    g_net_wm_name = XInternAtom(g_dpy, "_NET_WM_NAME", False);
    g_net_wm_icon = XInternAtom(g_dpy, "_NET_WM_ICON", False);
    g_utf8 = XInternAtom(g_dpy, "UTF8_STRING", False);
    a_active = XInternAtom(g_dpy, "_NET_ACTIVE_WINDOW", False);
    a_frame_ext = XInternAtom(g_dpy, "_NET_FRAME_EXTENTS", False);
    a_tray_opcode = XInternAtom(g_dpy, "_NET_SYSTEM_TRAY_OPCODE", False);
    a_xembed_info = XInternAtom(g_dpy, "_XEMBED_INFO", False);
    char sel[48];
    snprintf(sel, sizeof sel, "_NET_SYSTEM_TRAY_S%d", g_scr);
    a_tray_sel = XInternAtom(g_dpy, sel, False);
    color_set_display(g_dpy);

    g_cfg = *conf;
    last_disk = *conf;
    palette_from_cfg(&g_cfg, &pal);
    fast_ms = g_cfg.fps > 0 ? 1000 / g_cfg.fps : 33;
    if (fast_ms < 5) fast_ms = 5;

    if (pipe(sigpipe) == 0) {
        for (int i = 0; i < 2; i++) {
            fcntl(sigpipe[i], F_SETFL, fcntl(sigpipe[i], F_GETFL) | O_NONBLOCK);
            fcntl(sigpipe[i], F_SETFD, FD_CLOEXEC);
        }
    }
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    refresh_monitors();
    t_mon = now_ms() + 5000;
    cplx cur = pointer_pos();
    sim_init(&sim, &g_cfg, creal(cur) + 140 * g_cfg.size + I * cimag(cur));
    overlay_create();
    last_tick_ms = now_ms();
    if (g_cfg.enabled) set_paused(0);

    if (!tray_dock()) t_tray = now_ms() + 5000;                    /* no panel yet: look again every 5 s */

    take_settings_request();                 /* forget any request left over from before we started */
    if (show_settings) ui_open_settings();
    if (wayland) notify_wayland();           /* the terminal warning is invisible when started from the menu */

    struct pollfd fds[2] = {{ConnectionNumber(g_dpy), POLLIN, 0}, {sigpipe[0], POLLIN, 0}};
    while (!quitting) {
        XFlush(g_dpy);
        int64_t next = t_tick, now = now_ms();
        int64_t all[] = {t_raise, t_save, t_tray, t_focus, t_mon};
        for (size_t i = 0; i < sizeof all / sizeof *all; i++) if (all[i] < next) next = all[i];
        int timeout = next == NEVER ? -1 : next <= now ? 0 : (int)(next - now);
        if (XPending(g_dpy)) timeout = 0;
        poll(fds, 2, timeout);
        if (fds[1].revents & POLLIN) { char buf[64]; while (read(sigpipe[0], buf, sizeof buf) > 0) { } }
        if (flag_quit) break;
        if (flag_reload) { flag_reload = 0; on_reload(); }
        while (XPending(g_dpy)) {
            XEvent ev;
            XNextEvent(g_dpy, &ev);
            handle_event(&ev);
        }
        timers_due();
    }
    if (t_save != NEVER) save_now();
    XCloseDisplay(g_dpy);
    return 0;
}
