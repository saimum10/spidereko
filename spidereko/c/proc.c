/* One spider at a time: lock file, finding and stopping instances, install / uninstall. */
#define _GNU_SOURCE
#include "spidereko.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static const char *home_dir(void)
{
    const char *h = getenv("HOME");
    if (h && *h) return h;
    struct passwd *pw = getpwuid(getuid());
    return pw ? pw->pw_dir : "/tmp";
}

static void mkdir_p(const char *path)
{
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++)
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    mkdir(tmp, 0755);
}

/* ------------------------------------------------------------------ lock file */
static const char *lock_path(void)
{
    static char path[PATH_MAX];
    const char *base = getenv("XDG_RUNTIME_DIR");
    char dir[PATH_MAX];
    struct stat st;
    if (!base || !*base || stat(base, &st) || !S_ISDIR(st.st_mode)) {
        /* no runtime dir: a private per-user folder, not a guessable file in /tmp */
        const char *cache = getenv("XDG_CACHE_HOME");
        if (cache && *cache) snprintf(dir, sizeof dir, "%s/" APP_NAME, cache);
        else snprintf(dir, sizeof dir, "%s/.cache/" APP_NAME, home_dir());
        mkdir_p(dir);
        base = dir;
    }
    snprintf(path, sizeof path, "%s/" APP_NAME "-%d.pid", base, (int)getuid());
    return path;
}

static const char *marker_path(void)
{
    static char path[PATH_MAX + 16];
    snprintf(path, sizeof path, "%s.settings", lock_path());
    return path;
}

static int lock_fd = -1;                  /* kept open for the whole life of the spider */

int try_lock(void)
{
    int fd = open(lock_path(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0) return 0;
    if (flock(fd, LOCK_EX | LOCK_NB)) {
        close(fd);
        return 0;
    }
    char buf[32];
    int n = snprintf(buf, sizeof buf, "%d\n", (int)getpid());
    if (ftruncate(fd, 0) == 0 && write(fd, buf, n) < 0) { /* the pid is only a convenience */ }
    lock_fd = fd;
    return 1;
}

int running_pid(void)
{
    int fd = open(lock_path(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    int pid = 0;
    for (int tries = 0; tries < 10; tries++) {
        if (flock(fd, LOCK_SH | LOCK_NB)) {           /* somebody holds the exclusive lock -> a spider lives */
            char buf[32] = "";
            ssize_t n = pread(fd, buf, sizeof buf - 1, 0);
            if (n > 0 && (pid = atoi(buf)) > 0) break;
            pid = 0;                                  /* locked but not yet written: look again in a moment */
            struct timespec ts = {0, 20 * 1000 * 1000};
            nanosleep(&ts, NULL);
            continue;
        }
        flock(fd, LOCK_UN);
        break;
    }
    close(fd);
    return pid;
}

void request_settings(void)
{
    FILE *f = fopen(marker_path(), "w");
    if (f) fclose(f);
}

int take_settings_request(void)
{
    return unlink(marker_path()) == 0;
}

/* ------------------------------------------------------------------ finding instances */
static int is_spidereko_cmdline(const char *buf, size_t len)
{
    /* argv[0] is the spider itself (the compiled program) ... */
    char a0[PATH_MAX];
    snprintf(a0, sizeof a0, "%s", buf);
    if (!strcmp(basename(a0), APP_NAME)) return 1;
    /* ... or the old Python version: python3 [-options] spidereko[.py] */
    snprintf(a0, sizeof a0, "%s", buf);
    if (!strstr(basename(a0), "python")) return 0;
    for (const char *p = buf + strlen(buf) + 1; p < buf + len; p += strlen(p) + 1) {
        if (!*p || *p == '-') continue;
        char tmp[PATH_MAX];
        snprintf(tmp, sizeof tmp, "%s", p);
        const char *b = basename(tmp);
        return !strcmp(b, APP_NAME) || !strcmp(b, APP_NAME ".py");
    }
    return 0;
}

int stop_instances(void)
{
    int pids[256], n = 0;
    DIR *d = opendir("/proc");
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) && n < 255) {
            int pid = atoi(e->d_name);
            if (pid <= 0 || pid == (int)getpid()) continue;
            char path[64], buf[4096];
            snprintf(path, sizeof path, "/proc/%d/cmdline", pid);
            struct stat st;
            if (stat(path, &st) || st.st_uid != getuid()) continue;     /* only our own processes */
            int fd = open(path, O_RDONLY);
            if (fd < 0) continue;
            ssize_t len = read(fd, buf, sizeof buf - 1);
            close(fd);
            if (len <= 0) continue;
            buf[len] = 0;
            if (is_spidereko_cmdline(buf, (size_t)len)) pids[n++] = pid;
        }
        closedir(d);
    }
    int lp = running_pid();
    if (lp && lp != (int)getpid()) {
        int seen = 0;
        for (int i = 0; i < n; i++) seen |= pids[i] == lp;
        if (!seen && n < 256) pids[n++] = lp;
    }
    int stopped = 0;
    for (int i = 0; i < n; i++)
        if (kill(pids[i], SIGTERM) == 0) stopped++;
    return stopped;
}

/* ------------------------------------------------------------------ install / uninstall */
static const char *DESKTOP_ENTRY =
    "[Desktop Entry]\n"
    "Type=Application\n"
    "Name=Spidereko\n"
    "Comment=A spider chases your mouse pointer around the desktop\n"
    "Exec=%1$s -settings\n"
    "Icon=%2$s\n"
    "Terminal=false\n"
    "Categories=Utility;Game;\n"
    "Actions=Settings;Start;Stop;\n"
    "\n"
    "[Desktop Action Settings]\n"
    "Name=Settings\n"
    "Exec=%1$s -settings\n"
    "\n"
    "[Desktop Action Start]\n"
    "Name=Start spider\n"
    "Exec=%1$s\n"
    "\n"
    "[Desktop Action Stop]\n"
    "Name=Stop spider\n"
    "Exec=%1$s -stop\n";

static const int PNG_SIZES[3] = {64, 128, 256};

typedef struct { char bin[PATH_MAX], desktop[PATH_MAX], svg[PATH_MAX], png[3][PATH_MAX], icons_root[PATH_MAX], apps[PATH_MAX]; } Paths;

static void install_paths(Paths *p)
{
    const char *home = home_dir(), *xdg = getenv("XDG_DATA_HOME");
    char data[PATH_MAX];
    if (xdg && *xdg) snprintf(data, sizeof data, "%s", xdg);
    else snprintf(data, sizeof data, "%s/.local/share", home);
    snprintf(p->bin, sizeof p->bin, "%s/.local/bin/" APP_NAME, home);
    snprintf(p->apps, sizeof p->apps, "%s/applications", data);
    snprintf(p->desktop, sizeof p->desktop, "%s/" APP_NAME ".desktop", p->apps);
    snprintf(p->icons_root, sizeof p->icons_root, "%s/icons/hicolor", data);
    snprintf(p->svg, sizeof p->svg, "%s/scalable/apps/" APP_NAME ".svg", p->icons_root);
    for (int i = 0; i < 3; i++)
        snprintf(p->png[i], sizeof p->png[i], "%s/%dx%d/apps/" APP_NAME ".png", p->icons_root, PNG_SIZES[i], PNG_SIZES[i]);
}

static int which(const char *prog)
{
    const char *path = getenv("PATH");
    char *copy = strdup(path ? path : "/usr/bin:/bin"), *save = NULL;
    int found = 0;
    for (char *dir = strtok_r(copy, ":", &save); dir && !found; dir = strtok_r(NULL, ":", &save)) {
        char full[PATH_MAX];
        snprintf(full, sizeof full, "%s/%s", dir, prog);
        found = access(full, X_OK) == 0;
    }
    free(copy);
    return found;
}

static void run_quiet(char *const argv[])
{
    pid_t pid = fork();
    if (pid == 0) {
        int nul = open("/dev/null", O_RDWR);
        if (nul >= 0) { dup2(nul, 1); dup2(nul, 2); }
        execvp(argv[0], argv);
        _exit(127);
    }
    if (pid > 0) waitpid(pid, NULL, 0);
}

static void refresh_caches(const Paths *p)
{
    struct stat st;
    if (which("update-desktop-database") && !stat(p->apps, &st)) {
        char *argv[] = {"update-desktop-database", (char *)p->apps, NULL};
        run_quiet(argv);
    }
    if (which("gtk-update-icon-cache") && !stat(p->icons_root, &st)) {
        char *argv[] = {"gtk-update-icon-cache", "-f", "-t", (char *)p->icons_root, NULL};
        run_quiet(argv);
    }
}

static int write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fputs(text, f);
    return fclose(f);
}

static int copy_program(const char *src, const char *dst)
{
    char tmp[PATH_MAX + 16];
    snprintf(tmp, sizeof tmp, "%s.%d.tmp", dst, (int)getpid());
    FILE *in = fopen(src, "rb"), *out = in ? fopen(tmp, "wb") : NULL;
    if (!in || !out) {
        if (in) fclose(in);
        return -1;
    }
    char buf[65536];
    size_t n;
    int bad = 0;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0)
        if (fwrite(buf, 1, n, out) != n) { bad = 1; break; }
    fclose(in);
    if (fclose(out)) bad = 1;
    if (bad || chmod(tmp, 0755) || rename(tmp, dst)) {     /* rename: works even while the old copy runs */
        unlink(tmp);
        return -1;
    }
    return 0;
}

int install_self(void)
{
    Paths p;
    install_paths(&p);
    char dir[PATH_MAX];
    const char *all[] = {p.bin, p.desktop, p.svg, p.png[0], p.png[1], p.png[2]};
    for (int i = 0; i < 6; i++) {
        snprintf(dir, sizeof dir, "%s", all[i]);
        mkdir_p(dirname(dir));
    }

    char src[PATH_MAX], dst[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", src, sizeof src - 1);
    if (n <= 0) { fprintf(stderr, "spidereko: cannot find my own program file\n"); return 1; }
    src[n] = 0;
    if (!realpath(p.bin, dst) || strcmp(src, dst)) {
        if (copy_program(src, p.bin)) {
            fprintf(stderr, "spidereko: cannot write %s: %s\n", p.bin, strerror(errno));
            return 1;
        }
    }
    char *svg = icon_svg(0);
    int bad = write_text(p.svg, svg);
    free(svg);
    if (bad) { fprintf(stderr, "spidereko: cannot write %s: %s\n", p.svg, strerror(errno)); return 1; }
    int png_ok = 1;
    for (int i = 0; i < 3; i++) png_ok &= save_icon_png(p.png[i], PNG_SIZES[i], 0);
    if (!png_ok) printf("Note: PNG icons not written; using the SVG icon only.\n");
    const char *icon_ref = png_ok ? p.png[2] : p.svg;

    char entry[4096];
    snprintf(entry, sizeof entry, DESKTOP_ENTRY, p.bin, icon_ref);
    if (write_text(p.desktop, entry)) { fprintf(stderr, "spidereko: cannot write %s: %s\n", p.desktop, strerror(errno)); return 1; }
    refresh_caches(&p);

    printf("Installed: %s\n", p.bin);
    printf("App icon: %s\n", icon_ref);
    printf("App menu entry: Spidereko   (click = settings window, right-click = Start / Stop)\n");
    printf("If the icon is still blank in the app menu, log out and in once.\n");
    char bindir[PATH_MAX];
    snprintf(bindir, sizeof bindir, "%s", p.bin);
    dirname(bindir);
    const char *path = getenv("PATH");
    int on_path = 0;
    if (path) {
        char *copy = strdup(path), *save = NULL;
        for (char *d = strtok_r(copy, ":", &save); d; d = strtok_r(NULL, ":", &save)) on_path |= !strcmp(d, bindir);
        free(copy);
    }
    if (!on_path) printf("Note: add %s to your PATH to run 'spidereko' in a terminal.\n", bindir);
    printf("Run it:  spidereko        Stop it:  spidereko -stop        Remove it:  spidereko -uninstall\n");
    return 0;
}

int uninstall_self(void)
{
    stop_instances();
    Paths p;
    install_paths(&p);
    const char *all[] = {p.bin, p.desktop, p.svg, p.png[0], p.png[1], p.png[2]};
    int removed = 0;
    for (int i = 0; i < 6; i++) removed += unlink(all[i]) == 0;
    refresh_caches(&p);
    if (removed) printf("Removed %d installed file(s).\n", removed);
    else printf("Nothing was installed.\n");
    return 0;
}
