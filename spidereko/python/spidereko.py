#!/usr/bin/env python3
"""
spidereko - a spider chases your mouse pointer around the desktop (like oneko).

Runs on top of every window (X11), never steals clicks or focus.
The spider walks on eight inverse-kinematics legs, runs up to the pointer and sits
just above it (never covering it), falls asleep when the pointer stays still, and
wakes up when it moves again.

Usage (oneko-style options; single or double dash both work):
    spidereko                      start the spider with your saved settings
    spidereko -settings            open the settings window (starts the spider if needed)
    spidereko -speed 600           faster (pixels per second, default 420)
    spidereko -idle 5              seconds before it falls asleep (default 2.5)
    spidereko -size 1.5            bigger spider (default 1.0)
    spidereko -fg '#222' -bg '#bbb' -accent '#e45cc0'   colors
    spidereko -lift 30             sit higher above the pointer (-100..100, default 0 = just above it)
    spidereko -tofocus             chase the focused window instead (needs xdotool)
    spidereko -notofocus           chase the pointer again
    spidereko -off / -on           hide / show the spider (the program keeps running)
    spidereko -shadow / -noshadow  soft shadow under the spider (default off - it costs a little CPU)
    spidereko -fps 30              animation frame rate (default 30; the lower, the lighter)
    spidereko -reset               back to the default settings
    spidereko -stop                stop the running spider
    spidereko -toggle              start if none is running, otherwise stop

Every option you give is SAVED (~/.config/spidereko/config.json) and, if a spider is
already running, applied to it at once - no second spider is started.

Install / remove (per user, no root needed), from the folder that holds this file:
    python3 spidereko.py -install      copy to ~/.local/bin, add icon + app-menu entry
    spidereko -uninstall               remove everything again (your saved settings stay)

Needs: python3-pyqt6, an X11 session and a compositing window manager
(GNOME, KDE, XFCE with compositing, ... all have one).
"""

import argparse
import fcntl
import json
import math
import os
import re
import shutil
import signal
import subprocess
import sys
import time
import traceback
from cmath import rect as polar

APP_NAME = "spidereko"


# --------------------------------------------------------------------------- #
#  Process helpers (-stop / -toggle) - no Qt needed, so they start instantly  #
# --------------------------------------------------------------------------- #

def find_instances():
    """PIDs of other running spidereko processes of this user (never the calling process itself)."""
    pids = []
    me, uid = os.getpid(), os.getuid()
    for entry in os.listdir("/proc"):
        if not entry.isdigit() or int(entry) == me:
            continue
        try:
            if os.stat("/proc/" + entry).st_uid != uid:
                continue
            with open("/proc/%s/cmdline" % entry, "rb") as fh:
                argv = fh.read().decode(errors="ignore").split("\0")
        except OSError:
            continue
        if not argv or "python" not in os.path.basename(argv[0]):
            continue
        # the script is the first argument that is not an interpreter option (-u, -B, ...)
        script = next((a for a in argv[1:] if a and not a.startswith("-")), "")
        if os.path.basename(script) in (APP_NAME, APP_NAME + ".py"):
            pids.append(int(entry))
    return pids


def stop_instances():
    pids = set(find_instances())
    pid = running_pid()
    if pid and pid != os.getpid():
        pids.add(pid)
    stopped = 0
    for pid in pids:
        try:
            os.kill(pid, signal.SIGTERM)
            stopped += 1
        except OSError:
            pass
    return stopped


# --------------------------------------------------------------------------- #
#  One spider at a time (lock file) + "settings changed" signal               #
# --------------------------------------------------------------------------- #

_LOCK_FH = None                   # kept open for the whole life of the spider process
_FLAGS = {"reload": False}        # SIGUSR1 sets this: "settings changed, load them again"


def _on_usr1(*_):
    _FLAGS["reload"] = True


def _lock_path():
    base = os.environ.get("XDG_RUNTIME_DIR")
    if not base or not os.path.isdir(base):
        # no runtime dir: use a private per-user folder, not a guessable file in /tmp
        cache = os.environ.get("XDG_CACHE_HOME") or os.path.join(os.path.expanduser("~"), ".cache")
        base = os.path.join(cache, APP_NAME)
        os.makedirs(base, exist_ok=True)
    return os.path.join(base, "%s-%d.pid" % (APP_NAME, os.getuid()))


def _settings_marker():
    return _lock_path()[:-len(".pid")] + ".settings"


def request_settings():
    """Ask the running spider to open its settings window (it sees this when it gets the reload signal)."""
    open(_settings_marker(), "w").close()


def take_settings_request():
    """True (once) if somebody asked for the settings window."""
    try:
        os.remove(_settings_marker())
        return True
    except OSError:
        return False


def try_lock():
    """Become THE running spider. False when another one already holds the lock."""
    global _LOCK_FH
    fh = open(_lock_path(), "a+")
    try:
        fcntl.flock(fh, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        fh.close()
        return False
    fh.seek(0)
    fh.truncate()
    fh.write(str(os.getpid()))
    fh.flush()
    _LOCK_FH = fh                 # the lock is released automatically when the process ends
    return True


def running_pid():
    """PID of the running spider (it holds the lock while it lives), or None."""
    try:
        fh = open(_lock_path(), "r")
    except OSError:
        return None
    with fh:
        for _ in range(10):
            try:
                fcntl.flock(fh, fcntl.LOCK_SH | fcntl.LOCK_NB)
            except OSError:           # somebody holds the exclusive lock -> a spider is alive
                fh.seek(0)
                try:
                    return int(fh.read().strip())
                except ValueError:    # it has locked but not written its pid yet: look again in a moment
                    time.sleep(0.02)
                    continue
            fcntl.flock(fh, fcntl.LOCK_UN)
            return None
    return None


# --------------------------------------------------------------------------- #
#  Settings (saved in ~/.config/spidereko/config.json)                        #
# --------------------------------------------------------------------------- #

DEFAULTS = {"enabled": True, "speed": 420.0, "idle": 2.5, "size": 1.0, "lift": 0.0,
            "fg": "#16161c", "bg": "#9a9aae", "accent": "#e45cc0", "fps": 30, "tofocus": False,
            "shadow": False}
LIMITS = {"speed": (10.0, 10000.0), "idle": (0.0, 3600.0), "size": (0.3, 8.0), "lift": (-100.0, 100.0),
          "fps": (5, 240)}
_HEX_COLOR = re.compile(r"^#(?:[0-9a-fA-F]{3}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$")


def valid_color(value):
    if not isinstance(value, str):
        return False
    if _HEX_COLOR.match(value):
        return True
    try:                          # colour names ("red", "steelblue") are checked by Qt
        from PyQt6.QtGui import QColor
    except ImportError:
        return False
    return QColor(value).isValid()


def clean_value(key, value):
    """Return `value` checked/converted for setting `key`; ValueError says what is wrong."""
    default = DEFAULTS[key]
    if isinstance(default, bool):
        if isinstance(value, bool):
            return value
        raise ValueError("must be true or false")
    if isinstance(default, (int, float)):
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
            raise ValueError("must be a number")
        lo, hi = LIMITS[key]
        if not lo <= value <= hi:
            raise ValueError("must be between %g and %g" % (lo, hi))
        return int(value) if key == "fps" else float(value)
    if not valid_color(value):
        raise ValueError("is not a valid color (use #rrggbb or a color name)")
    return str(value)


def sanitize(raw):
    """A complete, valid settings dict: anything missing or broken in `raw` falls back to its default."""
    out = dict(DEFAULTS)
    if isinstance(raw, dict):
        for key in DEFAULTS:
            if key in raw:
                try:
                    out[key] = clean_value(key, raw[key])
                except ValueError:
                    pass
    return out


def config_path():
    base = os.environ.get("XDG_CONFIG_HOME") or os.path.join(os.path.expanduser("~"), ".config")
    return os.path.join(base, APP_NAME, "config.json")


def load_config():
    try:
        with open(config_path()) as fh:
            raw = json.load(fh)
    except (OSError, ValueError):
        raw = {}
    return sanitize(raw)


def save_config(conf):
    path = config_path()
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = "%s.%d.tmp" % (path, os.getpid())     # own temp name: two writers never share one
    try:
        with open(tmp, "w") as fh:
            json.dump({k: conf.get(k, DEFAULTS[k]) for k in DEFAULTS}, fh, indent=2)
            fh.write("\n")
        os.replace(tmp, path)     # atomic: a reader never sees a half-written file
    except OSError:
        try:
            os.remove(tmp)
        except OSError:
            pass
        raise


# --------------------------------------------------------------------------- #
#  Install / uninstall (-install / -uninstall)                                #
# --------------------------------------------------------------------------- #

# One description of the logo, used for the SVG file, the PNG files and the tray icon.
# Left-side legs in a 64x64 box as (hip, knee, foot); the right side is the mirror image.
ICON_LEGS = [((30, 26), (16, 10), (6, 8)), ((28, 31), (12, 26), (3, 30)),
             ((28, 36), (12, 42), (4, 52)), ((30, 41), (18, 56), (10, 60))]
ICON_ACCENT = "#e45cc0"
ICON_PNG_SIZES = (64, 128, 256)

# "app":  light spider on a dark rounded tile - readable on any launcher background.
# "tray": light body with a dark outline, no tile - readable on dark AND light panels
#         (the old all-black spider vanished on dark themes).
ICON_STYLES = {
    "app":  {"tile": True,  "body": "#f1f1f8", "edge": None,      "eye": "#2a2a44",
             "leg": 3.6, "halo": 0.0, "line": 0.0},
    "tray": {"tile": False, "body": "#f1f1f8", "edge": "#16161c", "eye": "#16161c",
             "leg": 5.0, "halo": 4.6, "line": 3.2},
}


def icon_svg(style="app"):
    st = ICON_STYLES[style]
    d = []
    for a, b, c in ICON_LEGS:
        for mirror in (False, True):
            pts = [(64 - x if mirror else x, y) for x, y in (a, b, c)]
            d.append("M%d %d L%d %d L%d %d" % (pts[0] + pts[1] + pts[2]))
    legs = " ".join(d)
    cap = 'stroke-linecap="round" stroke-linejoin="round" fill="none"'
    out = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64">']
    if st["tile"]:
        out.append('<defs><linearGradient id="g" x1="0" y1="0" x2="0" y2="1">'
                   '<stop offset="0" stop-color="#3b3a6b"/><stop offset="1" stop-color="#1a1a2e"/>'
                   '</linearGradient></defs>')
        out.append('<rect x="1" y="1" width="62" height="62" rx="14" fill="url(#g)" '
                   'stroke="#5a5a8c" stroke-width="1.2"/>')
        out.append('<g transform="translate(32 34) scale(0.8) translate(-32 -34)">')
    else:
        out.append("<g>")
    if st["edge"]:
        out.append('<path d="%s" stroke="%s" stroke-width="%g" %s/>' % (legs, st["edge"], st["leg"] + st["halo"], cap))
    out.append('<path d="%s" stroke="%s" stroke-width="%g" %s/>' % (legs, st["body"], st["leg"], cap))
    outline = 'stroke="%s" stroke-width="%g"' % (st["edge"], st["line"]) if st["edge"] else ""
    out.append('<ellipse cx="32" cy="43" rx="10" ry="12" fill="%s" %s/>' % (st["body"], outline))
    out.append('<ellipse cx="32" cy="22" rx="7" ry="6.5" fill="%s" %s/>' % (st["body"], outline))
    out.append('<path d="M28 38 L36 38 L32 43 Z M28 48 L36 48 L32 43 Z" fill="%s"/>' % ICON_ACCENT)
    out.append('<circle cx="29.5" cy="19.5" r="1.6" fill="%s"/><circle cx="34.5" cy="19.5" r="1.6" fill="%s"/>'
               % (st["eye"], st["eye"]))
    out.append("</g></svg>")
    return "\n".join(out) + "\n"


DESKTOP_ENTRY = """[Desktop Entry]
Type=Application
Name=Spidereko
Comment=A spider chases your mouse pointer around the desktop
Exec={exe} -settings
Icon={icon}
Terminal=false
Categories=Utility;Game;
Actions=Settings;Start;Stop;

[Desktop Action Settings]
Name=Settings
Exec={exe} -settings

[Desktop Action Start]
Name=Start spider
Exec={exe}

[Desktop Action Stop]
Name=Stop spider
Exec={exe} -stop
"""


def _install_paths():
    home = os.path.expanduser("~")
    data = os.environ.get("XDG_DATA_HOME") or os.path.join(home, ".local", "share")
    icons = os.path.join(data, "icons", "hicolor")
    paths = {
        "bin": os.path.join(home, ".local", "bin", APP_NAME),
        "desktop": os.path.join(data, "applications", APP_NAME + ".desktop"),
        "icon": os.path.join(icons, "scalable", "apps", APP_NAME + ".svg"),
    }
    for s in ICON_PNG_SIZES:
        paths["png%d" % s] = os.path.join(icons, "%dx%d" % (s, s), "apps", APP_NAME + ".png")
    return paths


def _refresh_caches(paths):
    """Tell the desktop that menu entries / icons changed (silently skipped when the tools are missing)."""
    icons_root = os.path.dirname(os.path.dirname(os.path.dirname(paths["icon"])))
    for cmd in (["update-desktop-database", os.path.dirname(paths["desktop"])],
                ["gtk-update-icon-cache", "-f", "-t", icons_root]):
        if shutil.which(cmd[0]) and os.path.isdir(cmd[-1]):
            subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def install_self():
    paths = _install_paths()
    for path in paths.values():
        os.makedirs(os.path.dirname(path), exist_ok=True)

    src = os.path.realpath(__file__)
    if src != os.path.realpath(paths["bin"]):
        shutil.copyfile(src, paths["bin"])
    os.chmod(paths["bin"], 0o755)
    with open(paths["icon"], "w") as fh:
        fh.write(icon_svg("app"))
    try:                                            # PNGs as well: every desktop can show those
        for s in ICON_PNG_SIZES:
            if not save_icon_png(paths["png%d" % s], s, "app"):
                raise OSError("cannot write " + paths["png%d" % s])
    except Exception as exc:
        print(f"Note: PNG icons not written ({exc}); using the SVG icon only.")
    icon_ref = paths["png256"] if os.path.exists(paths["png256"]) else paths["icon"]
    with open(paths["desktop"], "w") as fh:
        fh.write(DESKTOP_ENTRY.format(exe=paths["bin"], icon=icon_ref))

    _refresh_caches(paths)

    print(f"Installed: {paths['bin']}")
    print(f"App icon: {icon_ref}")
    print("App menu entry: Spidereko   (click = settings window, right-click = Start / Stop)")
    print("If the icon is still blank in the app menu, log out and in once.")
    if os.path.dirname(paths["bin"]) not in os.environ.get("PATH", "").split(":"):
        print(f"Note: add {os.path.dirname(paths['bin'])} to your PATH to run 'spidereko' in a terminal.")
    try:
        import PyQt6  # noqa: F401
    except ImportError:
        print("Missing dependency:  sudo apt install python3-pyqt6")
    if not shutil.which("xdotool"):
        print("Optional (for -tofocus):  sudo apt install xdotool")
    print("Run it:  spidereko        Stop it:  spidereko -stop        Remove it:  spidereko -uninstall")


def uninstall_self():
    stop_instances()
    removed = 0
    for path in _install_paths().values():
        try:
            os.remove(path)
            removed += 1
        except FileNotFoundError:
            pass
    _refresh_caches(_install_paths())
    print(f"Removed {removed} installed file(s)." if removed else "Nothing was installed.")


# --------------------------------------------------------------------------- #
#  Spider simulation (pure Python, no Qt)                                     #
# --------------------------------------------------------------------------- #
# All positions are complex numbers: x = real, y = imag, in screen pixels.

CHASE, SLEEP = 0, 1
WAKE_DIST = 12.0        # pointer must move this far from where the spider fell asleep to wake it
SIT_EXTENT = 6.6        # measured: furthest any drawn part reaches from the body centre towards the pointer (body units)
SIT_GAP = 1.2           # clear space (body units) kept between the spider and the pointer
EDGE_MARGIN = 4.0       # body units: the spider never sits closer than this to the top edge of the screen
CALM_MS = 66            # redraw interval while the spider stands still (only the breathing moves)
SLEEP_MS = 150          # redraw / pointer-check interval while it is fast asleep (only the z-z-z moves)

# (hip offset along the body axis, resting angle from "forward") for one side, in body units
LEG_LAYOUT = [(0.90, 35.0), (0.35, 75.0), (-0.25, 112.0), (-0.85, 150.0)]
HIP_SIDE_OFFSET = 0.55  # hips sit this far left/right of the body axis


def wrap_angle(a):
    return (a + math.pi) % (2 * math.pi) - math.pi


def smoothstep(t):
    return t * t * (3 - 2 * t)


class Leg:
    __slots__ = ("side", "angle", "hx", "hx_unit", "group", "hip", "rest", "foot",
                 "src", "dst", "t", "dur", "stepping")

    def __init__(self, side, hx_unit, angle, group):
        self.side = side
        self.hx_unit = hx_unit            # hip position along the body in body units
        self.hx = hx_unit                 # ... in pixels (SpiderSim._apply_size scales it)
        self.angle = math.radians(angle)
        self.group = group
        self.hip = self.rest = self.foot = self.src = self.dst = 0j
        self.t = 0.0
        self.dur = 0.1
        self.stepping = False


class SpiderSim:
    def __init__(self, cfg, start):
        self.cfg = cfg

        self.pos = complex(start)
        self.vel = 0j
        self.heading = math.pi                # facing left, toward where the pointer usually is
        self.state = CHASE
        self.curl = 1.0                       # 1 = legs spread out, ~0.45 = curled up asleep
        self.idle_t = 0.0
        self.time = 0.0
        self.anchor = 0j                      # pointer position when the spider fell asleep

        self.legs = []
        for side in (-1, 1):
            for j, (hx, ang) in enumerate(LEG_LAYOUT):
                group = (j + (0 if side < 0 else 1)) % 2   # alternating tetrapod gait
                self.legs.append(Leg(side, hx, ang, group))
        self._apply_size()
        self._place_hips()
        for leg in self.legs:
            leg.foot = leg.rest

    def _apply_size(self):
        """All lengths follow cfg.size (read again here, so the size can change while running)."""
        u = self.u = 5.0 * self.cfg.size      # one body unit in pixels
        self.L1, self.L2 = 3.0 * u, 4.4 * u   # thigh and shin length
        self.rest_radius = 5.2 * u
        self.max_reach = (self.L1 + self.L2) * 0.995
        self.arrive_r = 0.6 * u               # body centre this close to its spot counts as arrived
        self.turn_min = 2.0 * u               # closer than this to its spot it stops steering towards it
        self.look_min = 2.0 * u               # pointer closer than this: it keeps its heading (no spinning)
        self.sit_dist = (SIT_EXTENT + SIT_GAP) * u   # body centre -> pointer when sitting just above it
        self.window_size = int(19 * u) + 8    # square window that follows the spider
        for leg in self.legs:
            leg.hx = leg.hx_unit * u

    def set_size(self):
        """cfg.size changed: rescale the spider in place (position, heading and mood stay)."""
        self._apply_size()
        self._place_hips()
        for leg in self.legs:
            leg.foot = leg.rest
            leg.stepping = False
            leg.t = 0.0

    # -- geometry ----------------------------------------------------------- #
    def _place_hips(self):
        f = polar(1, self.heading)
        n = f * 1j
        for leg in self.legs:
            leg.hip = self.pos + f * leg.hx + n * (leg.side * HIP_SIDE_OFFSET * self.u)
            leg.rest = leg.hip + polar(self.rest_radius * self.curl,
                                       self.heading + leg.side * leg.angle)

    def leg_joints(self, leg):
        """Two-bone IK: returns (hip, knee, foot) for drawing."""
        hip = leg.hip
        v = leg.foot - hip
        d = abs(v)
        lo, hi = abs(self.L1 - self.L2) + 1e-3, self.max_reach
        if d < 1e-6:
            v, d = polar(1, self.heading), 1.0
        v = v / d * min(max(d, lo), hi)
        d = abs(v)
        foot = hip + v
        a = (self.L1 ** 2 - self.L2 ** 2 + d * d) / (2 * d)
        h = math.sqrt(max(self.L1 ** 2 - a * a, 0.0))
        if leg.stepping:                      # raise the knee while the foot is in the air
            h *= 1 + 0.35 * math.sin(math.pi * leg.t)
        direction = v / d
        base = hip + direction * a
        k1 = base + direction * 1j * h
        k2 = base - direction * 1j * h
        knee = k1 if abs(k1 - self.pos) > abs(k2 - self.pos) else k2   # knees point away from the body
        return hip, knee, foot

    # -- behaviour ---------------------------------------------------------- #
    def anchor_for(self, look):
        """Where the body sits for a pointer at `look`: straight above it, just clear of every part.
        cfg.lift moves that spot: 0 = just above, +100 = three times as far up, -100 = just below."""
        return look - 1j * self.sit_dist * (1.0 + 2.0 * self.cfg.lift / 100.0)

    def wake(self):
        if self.state == SLEEP:
            self.state = CHASE
            self.idle_t = 0.0

    def step(self, dt, target, look=None):
        """target = where the body should sit; look = the pointer (what it faces once it has arrived)."""
        cfg = self.cfg
        self.time += dt
        to_t = target - self.pos
        dist = abs(to_t)

        if self.state == SLEEP and abs(target - self.anchor) > WAKE_DIST:
            self.state = CHASE
            self.idle_t = 0.0
        awake = self.state == CHASE

        # which way to face: the way it runs, and once it has arrived: at the pointer
        want_h = None
        if dist > self.turn_min:
            want_h = math.atan2(to_t.imag, to_t.real)
        elif look is not None and abs(look - self.pos) > self.look_min:
            to_look = look - self.pos
            want_h = math.atan2(to_look.imag, to_look.real)
        facing = want_h is None or abs(wrap_angle(want_h - self.heading)) < 0.25

        # movement
        want = 0j
        if awake and dist > self.arrive_r:
            gap = dist - self.arrive_r
            want = to_t / dist * min(cfg.speed, gap * 5.0 + 20.0)   # ease in; the +20 floor makes sure it arrives
        self.vel += (want - self.vel) * min(1.0, (10.0 if awake else 14.0) * dt)
        self.pos += self.vel * dt
        speed = abs(self.vel)

        # catch the pointer -> wait a bit -> fall asleep
        if awake:
            if dist <= self.arrive_r + 2 and speed < 25 and facing:
                self.idle_t += dt
                if self.idle_t >= cfg.idle:
                    self.state = SLEEP
                    self.anchor = target
            else:
                self.idle_t = 0.0

        # turn (limited turn rate)
        if awake and want_h is not None:
            max_turn = 7.0 * dt
            self.heading += max(-max_turn, min(max_turn, wrap_angle(want_h - self.heading)))
            self.heading = wrap_angle(self.heading)

        # curl up when asleep
        goal = 0.45 if self.state == SLEEP else 1.0
        self.curl += (goal - self.curl) * min(1.0, 3.0 * dt)

        self._place_hips()
        self._walk_legs(dt, speed)

    def _landing(self, leg, remaining, trigger, forward):
        """Where a foot that lands in `remaining` seconds should go: the spot its rest position
        will have moved to by then, plus a little extra in the walking direction."""
        landing = leg.rest + self.vel * remaining + forward * trigger * 0.6
        future_hip = leg.hip + self.vel * remaining
        off = landing - future_hip
        limit = self.max_reach * 0.92
        if abs(off) > limit:
            landing = future_hip + off / abs(off) * limit
        return landing

    def _walk_legs(self, dt, speed):
        moving = speed > 30
        radius = self.rest_radius
        trigger = radius * (0.45 if moving else 0.12)
        if self.state == CHASE:
            # the faster the body runs, the quicker each foot has to swing
            dur = max(0.045, min(0.12, 0.9 * radius / max(speed, 1.0)))
        else:
            dur = 0.25
        forward = self.vel / speed if moving else 0j
        airborne = {leg.group for leg in self.legs if leg.stepping}

        for leg in self.legs:
            if leg.stepping:
                leg.t += dt / leg.dur
                if leg.t >= 1.0:
                    leg.t, leg.stepping, leg.foot = 1.0, False, leg.dst
                else:
                    # keep re-aiming: the body may speed up or turn while the foot is in the air
                    leg.dst = self._landing(leg, (1.0 - leg.t) * leg.dur, trigger, forward)
                    leg.foot = leg.src + (leg.dst - leg.src) * smoothstep(leg.t)
                continue
            too_far = abs(leg.foot - leg.hip) > self.max_reach * 0.97
            if (abs(leg.foot - leg.rest) > trigger or too_far) and \
                    (too_far or (1 - leg.group) not in airborne):
                leg.src, leg.dst = leg.foot, self._landing(leg, dur, trigger, forward)
                leg.t, leg.dur, leg.stepping = 0.0, dur, True
                airborne.add(leg.group)

    def calm(self):
        """Standing still with no foot in the air: nothing but the breathing animation changes."""
        return abs(self.vel) < 5.0 and not any(leg.stepping for leg in self.legs)

    def settled(self):
        """Asleep, curled up and motionless: the window can refresh slowly."""
        return (self.state == SLEEP and self.curl < 0.47 and abs(self.vel) < 1.0
                and not any(leg.stepping for leg in self.legs))


# --------------------------------------------------------------------------- #
#  Qt part                                                                    #
# --------------------------------------------------------------------------- #

_QT = None


def _qt():
    """The PyQt6 names used while painting - imported once (painting runs many times a second)."""
    global _QT
    if _QT is None:
        from PyQt6 import QtCore, QtGui
        _QT = argparse.Namespace(
            QPointF=QtCore.QPointF, Qt=QtCore.Qt, QColor=QtGui.QColor, QFont=QtGui.QFont,
            QPainter=QtGui.QPainter, QPainterPath=QtGui.QPainterPath, QPen=QtGui.QPen,
            QPolygonF=QtGui.QPolygonF)
    return _QT


def _paint_cache(sim, colors, cache):
    """Pens, colours and the font are built once and reused until the size or a colour changes."""
    q = _qt()
    u = sim.u
    key = (u, tuple(c.rgba() for c in colors))
    if cache.get("key") == key:
        return cache
    fg, bg, accent = colors
    w_thigh, w_shin, extra = 0.38 * u, 0.28 * u, 0.26 * u

    def pen(color, w):
        return q.QPen(color, max(w, 1.0), q.Qt.PenStyle.SolidLine, q.Qt.PenCapStyle.RoundCap,
                      q.Qt.PenJoinStyle.RoundJoin)

    outline = q.QColor(bg)
    outline.setAlpha(210)
    font = q.QFont()
    font.setBold(True)
    cache.clear()
    cache.update(key=key, fg=fg, bg=bg, accent=accent, font=font,
                 shadow=pen(q.QColor(0, 0, 0, 55), w_shin),
                 thigh_out=pen(outline, w_thigh + extra), shin_out=pen(outline, w_shin + extra),
                 thigh=pen(fg, w_thigh), shin=pen(fg, w_shin),
                 body=pen(outline, 0.2 * u), eye_closed=pen(bg, 0.12 * u))
    return cache


def paint_spider(p, sim, origin, colors, cache):
    """Draw the spider onto QPainter p; origin = world position of the canvas top-left.
    All legs of one kind are drawn as ONE path, so a frame is only a handful of draw calls."""
    q = _qt()
    c = _paint_cache(sim, colors, cache)
    u = sim.u
    ox, oy = origin
    p.setRenderHint(q.QPainter.RenderHint.Antialiasing)
    p.setBrush(q.Qt.BrushStyle.NoBrush)

    def pt(z):
        return q.QPointF(z.real - ox, z.imag - oy)

    joints = [sim.leg_joints(leg) for leg in sim.legs]

    if sim.cfg.shadow:                                      # soft shadow under the legs (optional)
        sh = 0.35 * u * complex(0.5, 1.0)
        path = q.QPainterPath()
        for hip, knee, foot in joints:
            path.moveTo(pt(hip + sh))
            path.lineTo(pt(knee + sh))
            path.lineTo(pt(foot + sh))
        p.setPen(c["shadow"])
        p.drawPath(path)

    def segments(i, j):
        path = q.QPainterPath()
        for joint in joints:
            path.moveTo(pt(joint[i]))
            path.lineTo(pt(joint[j]))
        return path

    thighs, shins = segments(0, 1), segments(1, 2)
    # all outlines first, then all fills, so neighbouring legs never cut into each other
    for pen_, path in ((c["thigh_out"], thighs), (c["shin_out"], shins),
                       (c["thigh"], thighs), (c["shin"], shins)):
        p.setPen(pen_)
        p.drawPath(path)

    # body (drawn in the spider's own frame: +x is forward)
    asleep = sim.state == SLEEP
    breath = 1.0 + 0.04 * math.sin(sim.time * 2.4) if (asleep or abs(sim.vel) < 5) else 1.0
    p.save()
    p.translate(pt(sim.pos))
    p.rotate(math.degrees(sim.heading))

    for pen_ in (c["shin_out"], c["shin"]):                 # pedipalps
        p.setPen(pen_)
        for s in (-1, 1):
            p.drawLine(q.QPointF(1.9 * u, s * 0.3 * u), q.QPointF(2.45 * u, s * 0.16 * u))

    p.setPen(c["body"])
    p.setBrush(c["fg"])
    p.drawEllipse(q.QPointF(-1.5 * u, 0), 1.9 * u * breath, 1.5 * u * breath)   # abdomen
    p.drawEllipse(q.QPointF(0.9 * u, 0), 1.2 * u, 1.0 * u)                      # head + thorax

    p.setPen(q.Qt.PenStyle.NoPen)                                               # hourglass mark
    p.setBrush(c["accent"])
    cx = -1.5 * u
    p.drawPolygon(q.QPolygonF([q.QPointF(cx - 0.6 * u, -0.45 * u), q.QPointF(cx - 0.6 * u, 0.45 * u),
                               q.QPointF(cx, 0)]))
    p.drawPolygon(q.QPolygonF([q.QPointF(cx + 0.6 * u, -0.45 * u), q.QPointF(cx + 0.6 * u, 0.45 * u),
                               q.QPointF(cx, 0)]))

    if asleep:                                                                  # closed eyes
        p.setPen(c["eye_closed"])
        for s in (-1, 1):
            p.drawLine(q.QPointF(1.5 * u, s * 0.38 * u - 0.16 * u),
                       q.QPointF(1.5 * u, s * 0.38 * u + 0.16 * u))
    else:                                                                       # open eyes
        p.setPen(q.Qt.PenStyle.NoPen)
        p.setBrush(c["bg"])
        for s in (-1, 1):
            p.drawEllipse(q.QPointF(1.55 * u, s * 0.38 * u), 0.26 * u, 0.26 * u)
            p.drawEllipse(q.QPointF(1.8 * u, s * 0.62 * u), 0.13 * u, 0.13 * u)
    p.restore()

    # floating "z z z" while asleep
    if asleep:
        base = pt(sim.pos)
        font = c["font"]
        for k in range(3):
            ph = (sim.time * 0.45 + k / 3.0) % 1.0
            col = q.QColor(c["bg"])
            col.setAlpha(int(255 * (1 - ph)))
            font.setPixelSize(max(7, int(u * (1.0 + ph * 0.9))))
            p.setFont(font)
            p.setPen(col)
            p.drawText(q.QPointF(base.x() + 1.6 * u + ph * 2.0 * u, base.y() - 1.6 * u - ph * 4.0 * u), "z")


def draw_icon(p, size, style="app"):
    """Paint the logo (same geometry as icon_svg) onto QPainter p inside a size x size square."""
    from PyQt6.QtCore import QPointF, QRectF, Qt
    from PyQt6.QtGui import QBrush, QColor, QLinearGradient, QPainter, QPainterPath, QPen, QPolygonF

    st = ICON_STYLES[style]

    def pen(color, w):
        return QPen(QColor(color), w, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap,
                    Qt.PenJoinStyle.RoundJoin)

    p.save()
    p.setRenderHint(QPainter.RenderHint.Antialiasing)
    p.scale(size / 64.0, size / 64.0)
    if st["tile"]:
        grad = QLinearGradient(0, 0, 0, 64)
        grad.setColorAt(0, QColor("#3b3a6b"))
        grad.setColorAt(1, QColor("#1a1a2e"))
        p.setPen(pen("#5a5a8c", 1.2))
        p.setBrush(QBrush(grad))
        p.drawRoundedRect(QRectF(1, 1, 62, 62), 14, 14)
        p.translate(32, 34)
        p.scale(0.8, 0.8)
        p.translate(-32, -34)

    paths = []
    for a, b, c in ICON_LEGS:
        for mirror in (False, True):
            pts = [QPointF(64 - x if mirror else x, y) for x, y in (a, b, c)]
            path = QPainterPath(pts[0])
            path.lineTo(pts[1])
            path.lineTo(pts[2])
            paths.append(path)
    p.setBrush(Qt.BrushStyle.NoBrush)
    if st["edge"]:
        p.setPen(pen(st["edge"], st["leg"] + st["halo"]))
        for path in paths:
            p.drawPath(path)
    p.setPen(pen(st["body"], st["leg"]))
    for path in paths:
        p.drawPath(path)

    p.setPen(pen(st["edge"], st["line"]) if st["edge"] else Qt.PenStyle.NoPen)
    p.setBrush(QColor(st["body"]))
    p.drawEllipse(QPointF(32, 43), 10, 12)                      # abdomen
    p.drawEllipse(QPointF(32, 22), 7, 6.5)                      # head
    p.setPen(Qt.PenStyle.NoPen)
    p.setBrush(QColor(ICON_ACCENT))                             # hourglass mark
    p.drawPolygon(QPolygonF([QPointF(28, 38), QPointF(36, 38), QPointF(32, 43)]))
    p.drawPolygon(QPolygonF([QPointF(28, 48), QPointF(36, 48), QPointF(32, 43)]))
    p.setBrush(QColor(st["eye"]))
    p.drawEllipse(QPointF(29.5, 19.5), 1.6, 1.6)
    p.drawEllipse(QPointF(34.5, 19.5), 1.6, 1.6)
    p.restore()


def make_qicon(style):
    """QIcon with the logo drawn separately at every size (needs a running QApplication)."""
    from PyQt6.QtCore import Qt
    from PyQt6.QtGui import QIcon, QPainter, QPixmap

    icon = QIcon()
    for s in (16, 22, 24, 32, 48, 64, 128):
        pm = QPixmap(s, s)
        pm.fill(Qt.GlobalColor.transparent)
        p = QPainter(pm)
        draw_icon(p, s, style)
        p.end()
        icon.addPixmap(pm)
    return icon


def save_icon_png(path, size, style="app"):
    from PyQt6.QtCore import Qt
    from PyQt6.QtGui import QImage, QPainter

    img = QImage(size, size, QImage.Format.Format_ARGB32)
    img.fill(Qt.GlobalColor.transparent)
    p = QPainter(img)
    draw_icon(p, size, style)
    p.end()
    return img.save(path)


# (key, label, minimum, maximum, slider steps per unit, label format)
SLIDER_ROWS = [
    ("speed", "Speed", 50, 2000, 1, "%d px/s"),
    ("size", "Size", 0.3, 8, 10, "%.1f\u00d7"),
    ("lift", "Height above cursor", -100, 100, 1, "%d"),
    ("idle", "Falls asleep after", 0, 60, 10, "%.1f s"),
    ("fps", "Smoothness", 10, 120, 1, "%d fps"),
]
COLOR_ROWS = [("fg", "Body"), ("bg", "Outline / eyes"), ("accent", "Mark")]


def build_settings_window(cfg, on_change, on_reset, icon, on_closed):
    """The settings window. on_change(key, value) is called for every edit, on_reset() for the
    defaults button; on_closed(window) when it is closed; window.refresh() copies the live settings
    (cfg) into the controls."""
    from PyQt6.QtCore import Qt
    from PyQt6.QtGui import QColor
    from PyQt6.QtWidgets import (QCheckBox, QColorDialog, QFormLayout, QHBoxLayout, QLabel,
                                 QPushButton, QSlider, QVBoxLayout, QWidget)

    class SettingsWindow(QWidget):
        def __init__(self):
            super().__init__()
            self.setWindowTitle("Spidereko settings")
            self.setWindowIcon(icon)
            self._busy = False                  # True while refresh() fills the controls: not an edit

            root = QVBoxLayout(self)
            self.chk_on = QCheckBox("Spider on")
            self.chk_on.setStyleSheet("font-weight: bold;")
            self.chk_on.toggled.connect(lambda on: self._edit("enabled", on))
            root.addWidget(self.chk_on)

            form = QFormLayout()
            self.sliders = {}
            self.base_range = {}
            for key, label, lo, hi, scale, fmt in SLIDER_ROWS:
                slider = QSlider(Qt.Orientation.Horizontal)
                slider.setRange(int(round(lo * scale)), int(round(hi * scale)))
                slider.setMinimumWidth(220)
                shown = QLabel()
                shown.setMinimumWidth(80)
                slider.valueChanged.connect(lambda v, k=key: self._slider(k, v))
                row = QHBoxLayout()
                row.addWidget(slider, 1)
                row.addWidget(shown)
                form.addRow(label, row)
                self.sliders[key] = (slider, shown, scale, fmt)
                self.base_range[key] = (slider.minimum(), slider.maximum())

            self.color_buttons = {}
            colors = QHBoxLayout()
            for key, label in COLOR_ROWS:
                button = QPushButton(label)
                button.clicked.connect(lambda _=False, k=key, t=label: self._pick_color(k, t))
                colors.addWidget(button)
                self.color_buttons[key] = button
            form.addRow("Colors", colors)
            root.addLayout(form)

            self.chk_shadow = QCheckBox("Soft shadow under the spider")
            self.chk_shadow.toggled.connect(lambda on: self._edit("shadow", on))
            root.addWidget(self.chk_shadow)

            self.chk_focus = QCheckBox("Chase the focused window instead of the pointer")
            if not shutil.which("xdotool"):
                self.chk_focus.setEnabled(False)
                self.chk_focus.setToolTip("Needs xdotool:  sudo apt install xdotool")
            self.chk_focus.toggled.connect(lambda on: self._edit("tofocus", on))
            root.addWidget(self.chk_focus)

            buttons = QHBoxLayout()
            reset = QPushButton("Reset to defaults")
            reset.clicked.connect(lambda _=False: on_reset())
            close = QPushButton("Close")
            close.clicked.connect(lambda _=False: self.close())
            buttons.addWidget(reset)
            buttons.addStretch(1)
            buttons.addWidget(close)
            root.addLayout(buttons)

        def closeEvent(self, event):
            super().closeEvent(event)
            on_closed(self)             # the owner drops and frees this window (it is rebuilt when needed)

        # -- from the controls to the spider ---------------------------- #
        def _edit(self, key, value):
            if not self._busy:
                on_change(key, value)

        def _slider(self, key, raw):
            slider, shown, scale, fmt = self.sliders[key]
            value = raw / scale
            value = int(round(value)) if key == "fps" else float(value)
            shown.setText(fmt % value)
            self._edit(key, value)

        def _pick_color(self, key, title):
            chosen = QColorDialog.getColor(QColor(getattr(cfg, key)), self, title)
            if chosen.isValid():
                on_change(key, chosen.name())
                self._paint_button(key)

        def _paint_button(self, key):
            color = QColor(getattr(cfg, key))
            text = "#ffffff" if color.lightness() < 128 else "#000000"
            self.color_buttons[key].setStyleSheet(
                "background-color: %s; color: %s; padding: 4px 10px;" % (color.name(), text))

        # -- from the spider to the controls ----------------------------- #
        def refresh(self):
            self._busy = True
            self.chk_on.setChecked(bool(cfg.enabled))
            for key, (slider, shown, scale, fmt) in self.sliders.items():
                value = getattr(cfg, key)
                raw = int(round(value * scale))
                lo, hi = self.base_range[key]
                slider.setRange(min(lo, raw), max(hi, raw))     # a value set from the command line may lie outside
                slider.setValue(raw)
                shown.setText(fmt % value)
            self.chk_shadow.setChecked(bool(cfg.shadow))
            self.chk_focus.setChecked(bool(cfg.tofocus))
            for key in self.color_buttons:
                self._paint_button(key)
            self._busy = False

    return SettingsWindow()


WAYLAND_WARNING = ("Wayland session detected. Global pointer tracking only works on X11, so the spider "
                   "will not follow the pointer properly. Log in with an X11 session "
                   "(on KDE: choose 'Plasma (X11)' on the login screen).")

_SEEN_ERRORS = set()


def _report_once(etype, value, tb):
    """sys.excepthook while the GUI runs: PyQt would abort the whole program on an uncaught error inside
    a Qt callback. Print each distinct error once instead and keep the spider alive."""
    key = (etype.__name__, str(value))
    if key not in _SEEN_ERRORS:
        _SEEN_ERRORS.add(key)
        traceback.print_exception(etype, value, tb)


def run_gui(conf, show_settings=False):
    wayland = os.environ.get("XDG_SESSION_TYPE", "").lower() == "wayland"
    if wayland:
        print("spidereko: warning - " + WAYLAND_WARNING, file=sys.stderr)
    os.environ.setdefault("QT_QPA_PLATFORM", "xcb")

    from PyQt6.QtCore import QElapsedTimer, QPoint, QProcess, Qt, QTimer
    from PyQt6.QtGui import QAction, QColor, QCursor, QGuiApplication, QPainter
    from PyQt6.QtWidgets import QApplication, QMenu, QSystemTrayIcon, QWidget

    sys.excepthook = _report_once
    cfg = argparse.Namespace(**conf)    # the LIVE settings: the simulation and the overlay read from this object

    def make_colors():
        return tuple(QColor(c) for c in (cfg.fg, cfg.bg, cfg.accent))

    app = QApplication(sys.argv[:1])
    app.setApplicationName(APP_NAME)
    app.setDesktopFileName(APP_NAME)             # lets the desktop pair our windows with the app-menu icon
    app.setQuitOnLastWindowClosed(False)

    class Overlay(QWidget):
        def __init__(self):
            super().__init__()
            self.setWindowFlags(Qt.WindowType.FramelessWindowHint
                                | Qt.WindowType.WindowStaysOnTopHint
                                | Qt.WindowType.Tool
                                | Qt.WindowType.WindowTransparentForInput
                                | Qt.WindowType.WindowDoesNotAcceptFocus
                                | Qt.WindowType.X11BypassWindowManagerHint)
            self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
            self.setAttribute(Qt.WidgetAttribute.WA_ShowWithoutActivating)
            self.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents)

            cur = QCursor.pos()
            self.sim = SpiderSim(cfg, complex(cur.x() + 140 * cfg.size, cur.y()))
            self.colors = make_colors()
            self.paint_cache = {}
            self.size_px = self.sim.window_size
            self.resize(self.size_px, self.size_px)
            self.origin = (0, 0)                # world position of the canvas (used when painting)
            self.moved_to = None                # where the window was last moved to (skip identical moves)
            self.focus_target = None
            self.focus_misses = 0
            self.focus_proc = self.focus_timer = None
            self.xdotool_warned = False
            self.paused = False

            self.clock = QElapsedTimer()
            self.clock.start()
            self.timer = QTimer(self)
            self.timer.setTimerType(Qt.TimerType.CoarseTimer)    # +-5 % is plenty and lets the CPU sleep longer
            self.timer.timeout.connect(self.tick)
            self.fast_ms = max(5, int(1000 / cfg.fps))
            self.timer.start(self.fast_ms)

            self.raise_timer = QTimer(self)             # stay above windows that get raised later
            self.raise_timer.timeout.connect(self.raise_)
            self.raise_timer.start(3000)

            self.sync_focus_tracking()

        # -- live settings ------------------------------------------------- #
        def apply(self, new):
            """Take over new settings (a complete, validated dict) while the spider keeps running."""
            old_size, old_lift = cfg.size, cfg.lift
            vars(cfg).update(new)
            self.colors = make_colors()
            if cfg.size != old_size:
                self.sim.set_size()
                self.size_px = self.sim.window_size
                self.resize(self.size_px, self.size_px)
            if cfg.size != old_size or cfg.lift != old_lift:
                self.sim.wake()                 # a sleeping spider has to move to its new spot
            self.fast_ms = max(5, int(1000 / cfg.fps))
            self.sync_focus_tracking()
            self.set_paused(not cfg.enabled)
            self.update()

        # -- window chasing (-tofocus) ----------------------------------- #
        def sync_focus_tracking(self):
            """Start / stop following the focused window so it matches cfg.tofocus."""
            want = bool(cfg.tofocus)
            if want and not shutil.which("xdotool"):
                if not self.xdotool_warned:
                    print("spidereko: -tofocus needs xdotool (sudo apt install xdotool); "
                          "chasing the pointer instead.", file=sys.stderr)
                    self.xdotool_warned = True
                want = False
            if want and self.focus_proc is None:
                self.focus_proc = QProcess(self)
                self.focus_proc.finished.connect(self._focus_done)
                self.focus_timer = QTimer(self)
                self.focus_timer.timeout.connect(self._focus_poll)
                if not self.paused:             # no polling while the spider is hidden
                    self.focus_timer.start(500)
            elif not want and self.focus_proc is not None:
                self.focus_timer.stop()
                self.focus_proc.finished.disconnect()
                self.focus_proc.kill()
                self.focus_proc.deleteLater()
                self.focus_timer.deleteLater()
                self.focus_proc = self.focus_timer = None
                self.focus_target = None

        def _focus_poll(self):
            if self.focus_proc.state() == QProcess.ProcessState.NotRunning:
                self.focus_proc.start("xdotool", ["getwindowfocus", "getwindowgeometry", "--shell"])

        def _focus_done(self, *_):
            if self.focus_proc is None:
                return
            text = bytes(self.focus_proc.readAllStandardOutput()).decode(errors="ignore")
            vals = dict(line.split("=", 1) for line in text.split() if "=" in line)
            try:
                x, y, w = int(vals["X"]), int(vals["Y"]), int(vals["WIDTH"])
            except (KeyError, ValueError):
                self.focus_misses += 1          # no focused window (a few misses in a row: chase the pointer)
                if self.focus_misses >= 3:
                    self.focus_target = None
                return
            self.focus_misses = 0
            if w > 40:
                self.focus_target = complex(x + w / 2, y)       # top edge, centre of the window

        # -- main loop ---------------------------------------------------- #
        def keep_on_screen(self, target, look):
            """The spider sits above the pointer; near the top of the screen that would be off screen,
            so there it slides down instead (smoothly - no jumping)."""
            screen = QGuiApplication.screenAt(QPoint(int(look.real), int(look.imag)))
            if screen is not None:
                top = screen.geometry().top() + EDGE_MARGIN * self.sim.u
                if target.imag < top:
                    target = complex(target.real, top)
            return target

        def tick(self):
            real = min(self.clock.restart() / 1000.0, 0.25)
            sim = self.sim
            if self.focus_target is not None and cfg.tofocus:
                look = self.focus_target
            else:
                cur = QCursor.pos()
                look = complex(cur.x(), cur.y())
            target = self.keep_on_screen(sim.anchor_for(look), look)
            # a slow tick (calm / asleep) is simulated in small steps, so it behaves like a fast one
            n = max(1, math.ceil(real / 0.05))
            for _ in range(n):
                sim.step(real / n, target, look)

            half = self.size_px / 2
            self.origin = (math.floor(sim.pos.real - half), math.floor(sim.pos.imag - half))
            if self.origin != self.moved_to:    # a standing spider needs no window move at all
                self.moved_to = self.origin
                self.move(*self.origin)

            # how often to redraw: full rate only while something really moves
            settled = sim.settled()
            if settled:
                interval = SLEEP_MS
            elif sim.calm():
                interval = max(self.fast_ms, CALM_MS)
            else:
                interval = self.fast_ms
            if self.timer.interval() != interval:
                self.timer.setInterval(interval)
            if self.focus_timer is not None:
                poll = 1000 if settled else 500
                if self.focus_timer.interval() != poll:
                    self.focus_timer.setInterval(poll)
            self.update()

        def paintEvent(self, _event):
            p = QPainter(self)
            paint_spider(p, self.sim, self.origin, self.colors, self.paint_cache)
            p.end()

        def set_paused(self, paused):
            if paused == self.paused:
                return
            self.paused = paused
            if paused:
                self.timer.stop()
                self.raise_timer.stop()
                if self.focus_timer is not None:
                    self.focus_timer.stop()
                self.hide()
            else:
                self.clock.restart()
                self.moved_to = None
                self.show()
                self.timer.start(self.fast_ms)
                self.raise_timer.start(3000)
                if self.focus_timer is not None:
                    self.focus_timer.start(500)

    overlay = Overlay()
    if cfg.enabled:
        overlay.show()
    else:
        overlay.set_paused(True)

    act_pause = None
    settings_win = None                          # built when asked for, freed again when closed
    last_disk = dict(conf)                       # what the config file held when we last read / wrote it

    def current_settings():
        return {k: getattr(cfg, k) for k in DEFAULTS}

    def save_now(settings):
        """Write the config; a failure (read-only disk, ...) is reported but must never kill the spider."""
        nonlocal last_disk
        try:
            save_config(settings)
        except OSError as exc:
            print("spidereko: cannot save the settings: %s" % exc, file=sys.stderr)
        else:
            last_disk = dict(settings)

    def sync_tray():
        """Make the tray's Hide/Show entry match cfg.enabled (also after a change from outside)."""
        if act_pause is None:
            return
        hidden = not cfg.enabled
        act_pause.blockSignals(True)
        act_pause.setChecked(hidden)
        act_pause.setText("Show spider" if hidden else "Hide spider")
        act_pause.blockSignals(False)

    def apply_settings(new, refresh=True):
        overlay.apply(new)
        sync_tray()
        if refresh and settings_win is not None:
            settings_win.refresh()

    # edits are applied to the spider at once; the file is written a moment later (a slider drag = one write)
    save_timer = QTimer()
    save_timer.setSingleShot(True)
    save_timer.setInterval(300)
    save_timer.timeout.connect(lambda: save_now(current_settings()))

    def commit(changes, immediate=False, refresh=False):
        now = current_settings()
        for key, value in changes.items():
            try:
                now[key] = clean_value(key, value)
            except ValueError:
                pass
        apply_settings(now, refresh=refresh)
        if immediate:
            save_timer.stop()
            save_now(now)
        else:
            save_timer.start()

    def flush_settings():
        if save_timer.isActive():
            save_timer.stop()
            save_now(current_settings())

    app.aboutToQuit.connect(flush_settings)

    def settings_closed(win):
        nonlocal settings_win
        if settings_win is win:
            settings_win = None
        win.deleteLater()                        # the idle spider does not carry a hidden settings window around

    def open_settings():
        nonlocal settings_win
        if settings_win is None:
            settings_win = build_settings_window(
                cfg, lambda key, value: commit({key: value}),
                lambda: commit(dict(DEFAULTS), immediate=True, refresh=True),
                make_qicon("app"), settings_closed)
        settings_win.refresh()
        settings_win.show()
        settings_win.raise_()
        settings_win.activateWindow()

    # -- tray icon: settings, hide / show, quit --------------------------- #
    tray = None

    def make_tray():
        nonlocal tray, act_pause
        tray = QSystemTrayIcon(make_qicon("tray"), app)
        tray.setToolTip("Spidereko")
        menu = QMenu()
        act_settings = QAction("Settings\u2026", menu)
        act_settings.triggered.connect(lambda *_: open_settings())
        act_pause = QAction("Hide spider", menu)
        act_pause.setCheckable(True)
        act_pause.toggled.connect(lambda hidden: commit({"enabled": not hidden}, immediate=True, refresh=True))
        act_quit = QAction("Quit", menu)
        act_quit.triggered.connect(app.quit)
        menu.addAction(act_settings)
        menu.addAction(act_pause)
        menu.addSeparator()
        menu.addAction(act_quit)
        tray.setContextMenu(menu)

        def on_tray_click(reason):               # a click on the icon itself (not every desktop sends this)
            if reason in (QSystemTrayIcon.ActivationReason.Trigger,
                          QSystemTrayIcon.ActivationReason.DoubleClick):
                open_settings()

        tray.activated.connect(on_tray_click)
        sync_tray()
        tray.show()
        app._tray, app._menu = tray, menu      # keep references alive
        if wayland:                              # the terminal warning is invisible when started from the menu
            tray.showMessage("Spidereko", WAYLAND_WARNING, QSystemTrayIcon.MessageIcon.Warning, 20000)

    tray_retry = QTimer()
    tray_tries = [0]

    def tray_retry_tick():                       # at login the panel is often not ready yet: keep looking a while
        tray_tries[0] += 1
        if QSystemTrayIcon.isSystemTrayAvailable():
            tray_retry.stop()
            make_tray()
        elif tray_tries[0] >= 36:
            tray_retry.stop()

    if QSystemTrayIcon.isSystemTrayAvailable():
        make_tray()
    else:
        tray_retry.timeout.connect(tray_retry_tick)
        tray_retry.start(5000)

    # let Ctrl+C / SIGTERM (spidereko -stop) end the program cleanly
    signal.signal(signal.SIGINT, lambda *_: app.quit())
    signal.signal(signal.SIGTERM, lambda *_: app.quit())

    def on_wake():      # every 300 ms Python gets a turn to notice signals, e.g. "settings changed"
        nonlocal last_disk
        if _FLAGS["reload"]:
            _FLAGS["reload"] = False
            # take over only what CHANGED in the file - an edit still waiting to be saved is not reverted
            disk = load_config()
            outside = {k: v for k, v in disk.items() if v != last_disk.get(k)}
            last_disk = disk
            now = current_settings()
            now.update(outside)
            apply_settings(now)
            if take_settings_request():          # 'spidereko -settings' / the app icon was clicked again
                open_settings()

    wake = QTimer()
    wake.timeout.connect(on_wake)
    wake.start(300)

    take_settings_request()                      # forget any request left over from before we started
    if show_settings:
        open_settings()
    return app.exec()


# --------------------------------------------------------------------------- #

def parse_args(argv):
    """Returns (args, edits): edits = the settings the user explicitly asked for, already checked."""
    ap = argparse.ArgumentParser(
        prog=APP_NAME, allow_abbrev=False,
        description="A spider chases your mouse pointer around the desktop. Every setting you give is "
                    "saved, and applied at once to a spider that is already running.")

    def opt(*names, **kw):
        ap.add_argument(*names, **kw)

    d = DEFAULTS
    opt("-speed", "--speed", type=float, help="speed in pixels/second (default %g)" % d["speed"])
    opt("-idle", "--idle", type=float, help="seconds before it falls asleep (default %g)" % d["idle"])
    opt("-size", "--size", type=float, help="spider scale (default %g)" % d["size"])
    opt("-lift", "--lift", type=float,
        help="height above the pointer, -100..100: 0 = just above it, higher = up, lower = down (default %g)" % d["lift"])
    opt("-fg", "--fg", help="body color: #rrggbb or a color name (default %s)" % d["fg"])
    opt("-bg", "--bg", help="outline/eye color (default %s)" % d["bg"])
    opt("-accent", "--accent", help="abdomen mark color (default %s)" % d["accent"])
    opt("-fps", "--fps", type=int, help="animation frame rate (default %d)" % d["fps"])
    opt("-tofocus", "--tofocus", action="store_true",
        help="chase the focused window's top edge instead of the pointer (needs xdotool)")
    opt("-notofocus", "--notofocus", action="store_true", help="chase the pointer again")
    opt("-shadow", "--shadow", action="store_true", help="draw a soft shadow under the spider (default: off)")
    opt("-noshadow", "--noshadow", action="store_true", help="no shadow (lighter on the CPU)")
    opt("-settings", "--settings", action="store_true",
        help="open the settings window (starts the spider if it is not running)")
    opt("-on", "--on", action="store_true", help="show the spider (starts it if it is not running)")
    opt("-off", "--off", action="store_true", help="hide the spider (the program keeps running)")
    opt("-reset", "--reset", action="store_true", help="back to the default settings")
    opt("-stop", "--stop", action="store_true", help="stop the running spider and exit")
    opt("-toggle", "--toggle", action="store_true", help="start a spider, or stop it if one is running")
    opt("-install", "--install", action="store_true",
        help="install for this user: ~/.local/bin, icon and app-menu entry")
    opt("-uninstall", "--uninstall", action="store_true", help="remove the installed files and stop the spider")
    args = ap.parse_args(argv)

    if args.tofocus and args.notofocus:
        ap.error("-tofocus and -notofocus cannot be used together")
    if args.shadow and args.noshadow:
        ap.error("-shadow and -noshadow cannot be used together")
    if args.on and args.off:
        ap.error("-on and -off cannot be used together")
    edits = {}
    for key in ("speed", "idle", "size", "lift", "fg", "bg", "accent", "fps"):
        value = getattr(args, key)
        if value is not None:
            try:
                edits[key] = clean_value(key, value)
            except ValueError as exc:
                ap.error("-%s %s" % (key, exc))
    if args.tofocus or args.notofocus:
        edits["tofocus"] = bool(args.tofocus)
    if args.shadow or args.noshadow:
        edits["shadow"] = bool(args.shadow)
    if args.on or args.off:
        edits["enabled"] = bool(args.on)
    return args, edits


def main():
    signal.signal(signal.SIGUSR1, _on_usr1)     # a stray "settings changed" signal must never kill us
    args, edits = parse_args(sys.argv[1:])
    if args.install:
        install_self()
        return 0
    if args.uninstall:
        uninstall_self()
        return 0
    if args.stop:
        n = stop_instances()
        print(f"stopped {n} spider(s)" if n else "no spider is running")
        return 0
    if args.toggle and stop_instances():
        return 0
    if args.toggle:
        edits.setdefault("enabled", True)       # toggling a spider on means you want to see it

    conf = load_config()
    if args.reset:
        conf = dict(DEFAULTS)
    conf.update(edits)
    changed = bool(edits) or args.reset
    if changed:
        save_config(conf)
        said = (["defaults restored"] if args.reset else []) + ["%s=%s" % kv for kv in edits.items()]
        print("saved: " + ", ".join(said))

    def tell_running(pid):
        """Hand the saved settings (and a settings-window request) to a running spider. False if it is gone."""
        try:
            if args.settings:
                request_settings()
            os.kill(pid, signal.SIGUSR1)        # the running spider loads the file again (and opens the window)
        except OSError:
            return False
        print(", ".join((["applied to the running spider"] if changed else [])
                        + (["opening the settings window"] if args.settings else [])))
        return True

    pid = running_pid()
    if pid:
        if not changed and not args.settings:
            print("spidereko is already running (pid %d). Open the settings with 'spidereko -settings', "
                  "change one, e.g. 'spidereko -speed 600', or stop it with 'spidereko -stop'." % pid)
            return 0
        if tell_running(pid):
            return 0                            # (else it ended just now - start a fresh one below)
    if args.off and not args.settings:
        return 0                                # -off never starts anything
    if not try_lock():                          # somebody else started a spider in this very moment
        pid = running_pid()
        if pid and (changed or args.settings) and tell_running(pid):
            return 0
        print("spidereko is already running.")
        return 0
    if not conf["enabled"] and not args.settings:
        print("The spider is switched off. Turn it on with 'spidereko -on' (or the tray menu).")
    return run_gui(conf, show_settings=args.settings)


if __name__ == "__main__":
    sys.exit(main())
