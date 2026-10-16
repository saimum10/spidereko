<p align="center"><img src="assets/spidereko.svg" width="96" alt="Spidereko logo"></p>

<h1 align="center">Spidereko</h1>

<p align="center">A little spider that chases your mouse pointer around the Linux desktop,<br>
like <i>oneko</i> - but a spider.</p>

<p align="center"><b>English</b> · <a href="README.bn.md">বাংলা</a></p>

![Spidereko: running, running, asleep, and the logo](assets/preview.png)

It walks on eight legs (two-bone inverse kinematics for every leg), turns to face the pointer, sits just
above it, and when you stop moving it curls up and falls asleep ("z z z"). Move the pointer and it wakes up.

**Two versions of the same program live in this repo** - pick the one that suits you (see below).
They use the same options, the same settings file and the same lock, so they behave like one program.

---

## Which version?

| | **C** (`c/`) | **Python** (`python/`) |
|---|---|---|
| Built with | C, Xlib, Cairo | Python 3, PyQt6 |
| Memory (RSS) | **about 6 MB** | about 54 MB |
| CPU, spider asleep | **about 0.4 %** | about 0.8 % |
| Needs | a C compiler + 4 dev packages (once, to build) | `python3-pyqt6` |
| No compositing manager | automatically draws a hard-edged shaped window | black box around the spider |
| `-tofocus` (chase the focused window) | built in | needs `xdotool` |
| Tray icon | classic X11 system tray (XEmbed) | Qt tray icon |
| Settings window | drawn with Cairo (no toolkit) | Qt |
| Good for | lightest possible, no runtime dependencies | nothing to compile, easy to read and change |

*The memory and CPU numbers were measured in a test X server (Xvfb) with default settings - on a real
desktop they differ a little, the ratio is what matters.*

**Install only one of them** - both install to the same place (`~/.local/bin/spidereko`). Installing the
other one later simply replaces it, and your settings stay.

## Requirements (both versions)

* Linux with an **X11 session**. On **Wayland** the spider cannot see the pointer, so it will not follow
  it. KDE: choose **"Plasma (X11)"** on the login screen; GNOME: **"GNOME on Xorg"**.
* For soft, anti-aliased edges, a **compositing manager** (KWin, Mutter, xfwm4, picom, ... - all normal
  desktops have one). Without one, the C version falls back to a hard-edged window; the Python version
  shows a black square.

---

## Install

First get the code:

```bash
git clone https://github.com/saimum10/spidereko.git
cd spidereko
```

### Option 1 - the C version (lightest)

```bash
# Debian / Ubuntu
sudo apt install build-essential pkg-config libx11-dev libxext-dev libxrandr-dev libcairo2-dev

cd c
make
./spidereko -install        # or:  make install
rm -rf spidereko
```

Other distributions - the same four libraries under their usual names:

```bash
sudo dnf install gcc make pkgconf-pkg-config libX11-devel libXext-devel libXrandr-devel cairo-devel   # Fedora
sudo pacman -S base-devel libx11 libxext libxrandr cairo                                               # Arch
```

You can also just try it without installing: `./spidereko`.

### Option 2 - the Python version (nothing to compile)

```bash
# Debian / Ubuntu
sudo apt install python3 python3-pyqt6
sudo apt install xdotool            # optional: only for the -tofocus option

cd python
python3 spidereko.py -install
rm -rf spidereko
```

(`pip install PyQt6` works too if your distribution has no `python3-pyqt6`.)
You can also just try it without installing: `python3 spidereko.py`.

---

## Use

Start it with `spidereko`. A tray icon appears: **click** = settings window, **right-click** = menu
(Settings..., Hide/Show spider, Quit).

| Command | What it does |
|---|---|
| `spidereko` | start the spider |
| `spidereko -settings` | open the settings window (starts the spider if needed) |
| `spidereko -speed 600 -size 2` | change settings - saved, and applied at once to a running spider |
| `spidereko -fg '#202020' -bg tan -accent red` | colors: `#rgb`, `#rrggbb` or a color name |
| `spidereko -off` / `-on` | hide / show the spider (the program keeps running) |
| `spidereko -tofocus` / `-notofocus` | chase the top edge of the focused window / the pointer |
| `spidereko -shadow` / `-noshadow` | soft shadow under the spider (off by default) |
| `spidereko -reset` | back to the default settings |
| `spidereko -toggle` | start it, or stop it if it is running |
| `spidereko -stop` | stop the running spider |
| `spidereko -install` / `-uninstall` | add / remove the per-user install |
| `spidereko -h` | all options |

### Settings

| Option | Meaning | Range | Default |
|---|---|---|---|
| `-speed` | speed in pixels per second | 10 - 10000 | 420 |
| `-idle` | seconds standing still before it falls asleep | 0 - 3600 | 2.5 |
| `-size` | spider scale | 0.3 - 8 | 1 |
| `-lift` | height above the pointer (0 = just above, higher = further up, negative = below) | -100 - 100 | 0 |
| `-fg` `-bg` `-accent` | body / outline and eyes / mark on the back | colors | `#16161c` `#9a9aae` `#e45cc0` |
| `-fps` | animation frame rate - lower = lighter on the CPU | 5 - 240 | 30 |

They are stored in `~/.config/spidereko/config.json` (you may edit it by hand; invalid values are
ignored). A running spider picks up changes from the command line and from the settings window at once.

### Start at login

```bash
mkdir -p ~/.config/autostart
cat > ~/.config/autostart/spidereko.desktop <<DESKTOP
[Desktop Entry]
Type=Application
Name=Spidereko
Exec=$HOME/.local/bin/spidereko
Terminal=false
DESKTOP
```

(On KDE you can also add it under *System Settings -> Autostart*.)

---

## Uninstall

```bash
spidereko -uninstall                           # stops the spider, removes the program, icons and menu entry
rm -rf ~/.config/spidereko                     # optional: also delete the saved settings
rm -f ~/.config/autostart/spidereko.desktop    # only if you set up autostart
```

This is the same for both versions. Afterwards you can delete the cloned `spidereko` folder.
(If `spidereko` is no longer in your `PATH`, run the uninstall from the folder instead:
`./c/spidereko -uninstall` or `python3 python/spidereko.py -uninstall`.)

**Switching between the versions:** just run `-install` of the other one; it replaces the first. Your
settings are kept.

---

## Troubleshooting

* **The spider does not follow the pointer / sits still.** You are probably on Wayland - see
  *Requirements*. Check with `echo $XDG_SESSION_TYPE` (it should say `x11`).
* **A black square around the spider** (Python version). No compositing manager is running; turn the
  compositor on (KDE: *System Settings -> Display and Monitor -> Compositor*).
* **No tray icon.** Some panels have no tray. Use the app-menu entry or `spidereko -settings` instead.
* **`-tofocus` does nothing.** Python version: install `xdotool`. C version: your window manager must
  support `_NET_ACTIVE_WINDOW` (all common ones do).
* **"spidereko is already running".** That is intended - only one spider runs. `spidereko -stop` stops it.
* **Build error in `c/`.** One of the four `-dev` packages is missing; install them as shown above.


## License

[MIT](LICENSE)
