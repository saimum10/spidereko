/* spidereko - command line, and what to do with it.
 * A spider chases your mouse pointer around the desktop. */
#define _GNU_SOURCE
#include "spidereko.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *USAGE =
    "spidereko - a spider chases your mouse pointer around the desktop.\n"
    "Every setting you give is SAVED, and applied at once to a spider that is already running.\n"
    "\n"
    "  spidereko                      start the spider (tray icon: click = settings window)\n"
    "  spidereko -settings            open the settings window (starts the spider if needed)\n"
    "  spidereko -speed 600 -size 2   change settings of the running spider (and save them)\n"
    "  spidereko -fg '#202020' -bg tan -accent red     colors: #rgb, #rrggbb or a color name\n"
    "  spidereko -off / -on           hide / show the spider (the program keeps running)\n"
    "  spidereko -tofocus             chase the focused window's top edge instead of the pointer\n"
    "  spidereko -reset               back to the default settings\n"
    "  spidereko -toggle              start a spider, or stop it if one is running\n"
    "  spidereko -stop                stop the running spider\n"
    "  spidereko -install / -uninstall   ~/.local/bin, icon and app-menu entry (per user)\n"
    "\n"
    "Options:\n"
    "  -speed N     speed in pixels/second (default %g)\n"
    "  -idle N      seconds before it falls asleep (default %g)\n"
    "  -size N      spider scale (default %g)\n"
    "  -lift N      height above the pointer, -100..100: 0 = just above it, higher = up, lower = down (default %g)\n"
    "  -fg C        body color (default %s)\n"
    "  -bg C        outline/eye color (default %s)\n"
    "  -accent C    abdomen mark color (default %s)\n"
    "  -fps N       animation frame rate (default %d; the lower, the lighter)\n"
    "  -tofocus / -notofocus    chase the focused window / the pointer\n"
    "  -shadow / -noshadow      soft shadow under the spider (default off)\n"
    "  -settings -on -off -reset -stop -toggle -install -uninstall -h\n"
    "\n"
    "Needs an X11 session (on KDE: 'Plasma (X11)') and, for transparency, a compositing manager.\n"
    "Settings file: ~/.config/spidereko/config.json\n";

static void usage(FILE *out)
{
    fprintf(out, USAGE, DEFAULTS.speed, DEFAULTS.idle, DEFAULTS.size, DEFAULTS.lift, DEFAULTS.fg, DEFAULTS.bg,
            DEFAULTS.accent, DEFAULTS.fps);
}

static void die_usage(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));
#include <stdarg.h>
static void die_usage(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "spidereko: error: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\nTry 'spidereko -h'.\n");
    va_end(ap);
    exit(2);
}

typedef struct {
    int install, uninstall, stop, toggle, settings, reset, on, off, tofocus, notofocus, shadow, noshadow;
    int has[K_COUNT];               /* settings the user explicitly asked for ... */
    Cfg vals;                       /* ... and their (already checked) values */
} Args;

static const struct { const char *name; int key; } VALUE_OPTS[] = {
    {"speed", K_SPEED}, {"idle", K_IDLE}, {"size", K_SIZE}, {"lift", K_LIFT},
    {"fg", K_FG}, {"bg", K_BG}, {"accent", K_ACCENT}, {"fps", K_FPS},
};

static void parse_args(int argc, char **argv, Args *a)
{
    memset(a, 0, sizeof *a);
    a->vals = DEFAULTS;
    struct { const char *name; int *flag; } flags[] = {
        {"install", &a->install}, {"uninstall", &a->uninstall}, {"stop", &a->stop}, {"toggle", &a->toggle},
        {"settings", &a->settings}, {"reset", &a->reset}, {"on", &a->on}, {"off", &a->off},
        {"tofocus", &a->tofocus}, {"notofocus", &a->notofocus}, {"shadow", &a->shadow}, {"noshadow", &a->noshadow},
    };
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (arg[0] != '-' || !arg[1]) die_usage("unrecognized argument: %s", arg);
        const char *name = arg + (arg[1] == '-' ? 2 : 1);
        char nbuf[32];
        const char *eq = strchr(name, '='), *inline_val = NULL;
        if (eq) {
            snprintf(nbuf, sizeof nbuf, "%.*s", (int)(eq - name), name);
            name = nbuf;
            inline_val = eq + 1;
        }
        if (!strcmp(name, "h") || !strcmp(name, "help")) { usage(stdout); exit(0); }
        int matched = 0;
        for (size_t f = 0; f < sizeof flags / sizeof *flags && !matched; f++)
            if (!strcmp(name, flags[f].name)) { *flags[f].flag = 1; matched = 1; }
        if (matched) continue;
        for (size_t v = 0; v < sizeof VALUE_OPTS / sizeof *VALUE_OPTS && !matched; v++) {
            if (strcmp(name, VALUE_OPTS[v].name)) continue;
            matched = 1;
            int key = VALUE_OPTS[v].key;
            const char *val = inline_val;
            if (!val) {
                if (i + 1 >= argc) die_usage("argument -%s: expected one argument", name);
                val = argv[++i];
            }
            char err[96];
            int bad;
            if (key_kind(key) == KIND_COLOR) {
                bad = cfg_set_str(&a->vals, key, val, err, sizeof err);
                if (bad) die_usage("argument -%s: '%s' %s", name, val, err);
            } else {
                char *end;
                errno = 0;
                double num = key == K_FPS ? (double)strtol(val, &end, 10) : strtod(val, &end);
                if (end == val || *end || errno) die_usage("argument -%s: invalid %s value: '%s'", name, key == K_FPS ? "int" : "float", val);
                bad = cfg_set_num(&a->vals, key, num, err, sizeof err);
                if (bad) die_usage("-%s %s", name, err);
            }
            a->has[key] = 1;
        }
        if (!matched) die_usage("unrecognized argument: %s", arg);
    }
    if (a->tofocus && a->notofocus) die_usage("-tofocus and -notofocus cannot be used together");
    if (a->shadow && a->noshadow) die_usage("-shadow and -noshadow cannot be used together");
    if (a->on && a->off) die_usage("-on and -off cannot be used together");
    if (a->tofocus || a->notofocus) { a->has[K_TOFOCUS] = 1; a->vals.tofocus = a->tofocus; }
    if (a->shadow || a->noshadow) { a->has[K_SHADOW] = 1; a->vals.shadow = a->shadow; }
    if (a->on || a->off) { a->has[K_ENABLED] = 1; a->vals.enabled = a->on; }
}

static void describe(char *out, size_t n, const Args *a, int reset)
{
    size_t len = 0;
    out[0] = 0;
    if (reset) len += snprintf(out + len, n - len, "defaults restored");
    for (int k = 0; k < K_COUNT; k++) {
        if (!a->has[k]) continue;
        const char *sep = len ? ", " : "";
        if (key_kind(k) == KIND_COLOR) len += snprintf(out + len, n - len, "%s%s=%s", sep, KEY_NAME[k], cfg_get_str(&a->vals, k));
        else if (key_kind(k) == KIND_BOOL) len += snprintf(out + len, n - len, "%s%s=%s", sep, KEY_NAME[k], cfg_get_num(&a->vals, k) ? "true" : "false");
        else len += snprintf(out + len, n - len, "%s%s=%g", sep, KEY_NAME[k], cfg_get_num(&a->vals, k));
    }
}

int main(int argc, char **argv)
{
    signal(SIGUSR1, SIG_IGN);                   /* a stray "settings changed" signal must never kill us */
    Args a;
    parse_args(argc, argv, &a);

    if (a.install) return install_self();
    if (a.uninstall) return uninstall_self();
    if (a.stop) {
        int n = stop_instances();
        if (n) printf("stopped %d spider(s)\n", n);
        else printf("no spider is running\n");
        return 0;
    }
    if (a.toggle && stop_instances()) return 0;
    if (a.toggle && !a.has[K_ENABLED]) { a.has[K_ENABLED] = 1; a.vals.enabled = 1; }   /* toggled on = you want to see it */

    Cfg conf;
    cfg_load(&conf);
    if (a.reset) conf = DEFAULTS;
    int changed = a.reset;
    for (int k = 0; k < K_COUNT; k++)
        if (a.has[k]) { cfg_copy_key(&conf, &a.vals, k); changed = 1; }
    if (changed) {
        if (cfg_save(&conf)) fprintf(stderr, "spidereko: cannot save the settings: %s\n", strerror(errno));
        char said[512];
        describe(said, sizeof said, &a, a.reset);
        printf("saved: %s\n", said);
    }

    /* hand the saved settings (and a settings-window request) to a spider that is already running */
    int pid = running_pid();
    for (int attempt = 0; attempt < 2; attempt++) {
        if (pid) {
            if (!changed && !a.settings) {
                printf("spidereko is already running (pid %d). Open the settings with 'spidereko -settings', "
                       "change one, e.g. 'spidereko -speed 600', or stop it with 'spidereko -stop'.\n", pid);
                return 0;
            }
            if (a.settings) request_settings();
            if (kill(pid, SIGUSR1) == 0) {         /* the running spider loads the file again (and opens the window) */
                printf("%s%s%s\n", changed ? "applied to the running spider" : "", changed && a.settings ? ", " : "",
                       a.settings ? "opening the settings window" : "");
                return 0;
            }                                      /* it ended just now - start a fresh one below */
        }
        if (a.off && !a.settings) return 0;        /* -off never starts anything */
        if (try_lock()) break;
        pid = running_pid();                       /* somebody started a spider in this very moment: tell that one */
        if (!pid) { printf("spidereko is already running.\n"); return 0; }
        if (attempt == 1) { printf("spidereko is already running.\n"); return 0; }
    }
    if (!conf.enabled && !a.settings) printf("The spider is switched off. Turn it on with 'spidereko -on' (or the tray menu).\n");
    fflush(stdout);
    return run_gui(&conf, a.settings);
}
