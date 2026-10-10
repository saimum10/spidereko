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

This copies the program to `~/.local/bin/spidereko` and adds an icon and an app-menu entry. Clicking the app icon opens the settings window; right-click it for Settings / Start / Stop. If the icon is still blank in the app menu, log out and in once. If `spidereko` is not found in a terminal, add `~/.local/bin` to your `PATH`.

Run it:

```sh
spidereko               # start the spider with your saved settings
spidereko -settings     # open the settings window (starts the spider if needed)
```

Only one spider runs at a time. Running `spidereko` again while it is already running does not start a second one; use `-settings` or one of the options below to change the running spider.

## Uninstall

```sh
spidereko -uninstall
```

Stops the spider and removes the installed files. Your saved settings stay in `~/.config/spidereko/`; delete that folder as well if you want them gone.

## Settings

You can change settings in the settings window or on the command line. Both do the same thing: the change is applied **immediately** to the running spider (no second spider is started) and **saved**, so the next start uses it too.

### Settings window

Open it by clicking the app icon, by choosing **Settings…** in the tray menu, by clicking the tray icon, or with `spidereko -settings`.

| Control | What it does |
|---|---|
| **Spider on** | Shows or hides the spider. The program keeps running, so the tray icon stays and you can switch it back on |
| **Speed** | Top speed, 50 to 2000 pixels per second |
| **Size** | Spider scale, 0.3× to 8× |
| **Height above cursor** | Where the spider sits relative to the pointer. In the middle (default) it sits just above the pointer without touching it. Move the slider right to make it sit higher, left to bring it down; at the far left it sits just below the pointer |
| **Falls asleep after** | Seconds the spider sits by the pointer before it falls asleep, 0 to 60 |
| **Smoothness** | Animation frame rate, 10 to 120 fps |
| **Colors** | Body, outline / eyes and abdomen mark; each button opens a color picker |
| **Chase the focused window** | Follow the focused window's top edge instead of the pointer (greyed out unless `xdotool` is installed) |
| **Reset to defaults** | Puts every setting back to its default |

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

If a spider is already running, it picks these up right away. Invalid values (for example `-lift 150` or an unknown color) are rejected with an error and nothing is changed.

### Where settings are saved

In `~/.config/spidereko/config.json` (or `$XDG_CONFIG_HOME/spidereko/config.json`). If the file is missing or damaged, the defaults are used. `spidereko -reset` restores the defaults.

## Tray icon

The tray icon menu has **Settings…**, **Hide spider** (**Show spider** while it is hidden) and **Quit**. Clicking the icon opens the settings window too, on desktops that report icon clicks; otherwise use the menu entry. The icon is light with a dark outline, so it stays visible on both dark and light panels. On GNOME, tray icons need the AppIndicator extension.

## License

[MIT](LICENSE)
