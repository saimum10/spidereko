# Spidereko 🕷️

A tiny desktop pet in the spirit of `oneko`: a spider chases your mouse pointer around the screen, on top of every window. It walks on eight procedurally animated legs, sits just above the pointer (never covering it, so you can always see where you click), falls asleep when you stop moving, and wakes up when you move again. It never blocks clicks or steals focus.

## Supported OS

| Platform | Status |
|---|---|
| Linux, **X11** session | Supported |
| Linux, Wayland session | Not supported (global pointer tracking only works on X11) |
| Windows / macOS | Not supported |

Requirements: Python 3, [PyQt6](https://pypi.org/project/PyQt6/), and a compositing window manager (GNOME, KDE and most modern desktops have one; without it the spider gets a black box around it). Optional: `xdotool` for `-tofocus`.

## Install

```sh
sudo apt install python3-pyqt6        # Debian / Ubuntu; elsewhere: pip install PyQt6
git clone https://github.com/saimum10/spidereko
python3 spidereko/spidereko.py -install
```

Run it:

```sh
spidereko               # start the spider with your saved settings
spidereko -settings     # open the settings window (starts the spider if needed)
```


## Uninstall

```sh
spidereko -uninstall
```

## Settings

You can change settings in the settings window or on the command line. Both do the same thing: the change is applied **immediately** to the running spider (no second spider is started) and **saved**, so the next start uses it too.

### Command-line options

Options work with one or two dashes (`-speed 600` or `--speed 600`).

| Option | Default | Description |
|---|---|---|
| `-speed N` | `420` | Top speed in pixels per second (10 to 10000) |
| `-idle N` | `2.5` | Seconds sitting by the pointer before it falls asleep (0 to 3600) |
| `-size N` | `1.0` | Spider scale (0.3 to 8) |
| `-lift N` | `0` | Height above the pointer, -100 to 100. `0` = just above the pointer, positive = higher (`100` is three times as high), negative = lower (`-100` = just below the pointer) |
| `-fg COLOR` | `#16161c` | Body color |
| `-bg COLOR` | `#9a9aae` | Outline and eye color |
| `-accent COLOR` | `#e45cc0` | Abdomen mark color |
| `-fps N` | `60` | Animation frame rate (5 to 240) |
| `-tofocus` | off | Chase the focused window's top edge instead of the pointer (needs `xdotool`) |
| `-notofocus` | | Chase the pointer again (turns `-tofocus` off) |
| `-on` | | Show the spider (starts it if it is not running) |
| `-off` | | Hide the spider; the program keeps running. Never starts a spider by itself |
| `-settings` | | Open the settings window (starts the spider if it is not running) |
| `-reset` | | Back to the default settings |
| `-stop` | | Stop the running spider |
| `-toggle` | | Start a spider, or stop it if one is running (handy for a keyboard shortcut) |

Example:

```sh
spidereko -size 1.5 -speed 600 -idle 5 -accent '#00d1b2'
```
## License

[MIT](LICENSE)
