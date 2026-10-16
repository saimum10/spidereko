<p align="center"><img src="assets/spidereko.svg" width="96" alt="Spidereko logo"></p>

<h1 align="center">Spidereko</h1>

<p align="center">লিনাক্স ডেস্কটপে মাউস পয়েন্টারের পিছু নেওয়া একটা ছোট্ট মাকড়সা,<br>
<i>oneko</i>-র মতো - তবে বিড়ালের বদলে মাকড়সা।</p>

<p align="center"><a href="README.md">English</a> · <b>বাংলা</b></p>

![Spidereko: দৌড়াচ্ছে, দৌড়াচ্ছে, ঘুমিয়ে আছে, আর লোগো](assets/preview.png)

মাকড়সাটা আটটা পায়ে হাঁটে (প্রতিটা পায়ের জন্য দুই-হাড়ের inverse kinematics), পয়েন্টারের দিকে ঘুরে তাকায়,
ঠিক তার ওপরে এসে বসে। পয়েন্টার থামিয়ে রাখলে গুটিসুটি মেরে ঘুমিয়ে পড়ে ("z z z"), আবার নাড়ালেই জেগে ওঠে।

**এই রিপোজিটরিতে একই প্রোগ্রামের দুটো সংস্করণ আছে** - যেটা সুবিধামতো বেছে নাও (নিচে দেখো)।
দুটোর অপশন, সেটিংস ফাইল আর লক একই, তাই এরা একটা প্রোগ্রামের মতোই আচরণ করে।

---

## কোন সংস্করণ?

| | **C** (`c/`) | **Python** (`python/`) |
|---|---|---|
| যা দিয়ে বানানো | C, Xlib, Cairo | Python 3, PyQt6 |
| মেমরি (RSS) | **প্রায় ৬ MB** | প্রায় ৫৪ MB |
| CPU, মাকড়সা ঘুমন্ত | **প্রায় ০.৪%** | প্রায় ০.৮% |
| যা লাগে | একটা C কম্পাইলার + ৪টা dev প্যাকেজ (শুধু বিল্ডের সময়) | `python3-pyqt6` |
| কম্পোজিটর না থাকলে | নিজে থেকে শক্ত ধারের shaped উইন্ডো আঁকে | মাকড়সার চারপাশে কালো বাক্স |
| `-tofocus` (ফোকাস করা উইন্ডো ধরে দৌড়ানো) | ভেতরেই আছে | `xdotool` লাগে |
| ট্রে আইকন | ক্লাসিক X11 সিস্টেম ট্রে (XEmbed) | Qt ট্রে আইকন |
| সেটিংস উইন্ডো | Cairo দিয়ে আঁকা (কোনো টুলকিট ছাড়া) | Qt |
| কার জন্য ভালো | সবচেয়ে হালকা, রানটাইমে কোনো ডিপেন্ডেন্সি নেই | কম্পাইল করতে হয় না, পড়া ও বদলানো সহজ |

*মেমরি আর CPU-র সংখ্যাগুলো টেস্ট X সার্ভারে (Xvfb) ডিফল্ট সেটিংসে মাপা - আসল ডেস্কটপে কিছুটা আলাদা হবে,
গুরুত্বপূর্ণ হলো অনুপাতটা।*

**দুটোর মধ্যে একটাই ইনস্টল করো** - দুটোই একই জায়গায় (`~/.local/bin/spidereko`) বসে। পরে অন্যটা ইনস্টল
করলে সেটা আগেরটাকে বদলে দেয়, সেটিংস থেকে যায়।

## প্রয়োজনীয়তা (দুই সংস্করণেই)

* **X11 সেশন** সহ লিনাক্স। **Wayland**-এ মাকড়সা পয়েন্টার দেখতে পায় না, তাই পিছু নেয় না।
  KDE-তে লগইন স্ক্রিনে **"Plasma (X11)"** বেছে নাও; GNOME-এ **"GNOME on Xorg"**।
* নরম, মসৃণ ধারের জন্য একটা **কম্পোজিটিং ম্যানেজার** (KWin, Mutter, xfwm4, picom ... - সাধারণ ডেস্কটপে
  থাকেই)। না থাকলে C সংস্করণ শক্ত ধারের উইন্ডোতে ফিরে যায়, Python সংস্করণে কালো বর্গ দেখা যায়।

---

## ইনস্টল

আগে কোড নামাও:

```bash
git clone https://github.com/saimum10/spidereko.git
cd spidereko
```

### অপশন ১ - C সংস্করণ (সবচেয়ে হালকা)

```bash
# Debian / Ubuntu
sudo apt install build-essential pkg-config libx11-dev libxext-dev libxrandr-dev libcairo2-dev

cd c
make
./spidereko -install        # অথবা:  make install
cd ../..
rm -rf spidereko
```

অন্য ডিস্ট্রিবিউশনে একই চারটা লাইব্রেরি, তাদের চলতি নামে:

```bash
sudo dnf install gcc make pkgconf-pkg-config libX11-devel libXext-devel libXrandr-devel cairo-devel   # Fedora
sudo pacman -S base-devel libx11 libxext libxrandr cairo                                               # Arch
```

ইনস্টল না করে শুধু চালিয়ে দেখতে চাইলে: `./spidereko`।

### অপশন ২ - Python সংস্করণ (কম্পাইল করতে হয় না)

```bash
# Debian / Ubuntu
sudo apt install python3 python3-pyqt6
sudo apt install xdotool            # ঐচ্ছিক: শুধু -tofocus অপশনের জন্য

cd python
python3 spidereko.py -install
cd ../..
rm -rf spidereko
```

(তোমার ডিস্ট্রিবিউশনে `python3-pyqt6` না থাকলে `pip install PyQt6`-ও চলবে।)
ইনস্টল না করে চালিয়ে দেখতে চাইলে: `python3 spidereko.py`।

### `-install` কী করে

প্রোগ্রামটা `~/.local/bin/spidereko`-তে কপি করে, লোগো আর অ্যাপ মেনুতে **"Spidereko"** নামে একটা এন্ট্রি
বসায় (ক্লিক করলে সেটিংস উইন্ডো খোলে, আর মাকড়সা না চললে চালু করে দেয়)। সবকিছু তোমার হোম ডিরেক্টরিতেই
যায় - `sudo` লাগে না, `~`-র বাইরে কিছু বদলায় না। এরপর টার্মিনাল যদি `spidereko: command not found` বলে,
`~/.local/bin` তোমার `PATH`-এ যোগ করো (অথবা একবার লগ আউট করে ঢোকো)।

---

## ব্যবহার

`spidereko` লিখে চালু করো। ট্রে আইকন আসবে: **ক্লিক** = সেটিংস উইন্ডো, **রাইট-ক্লিক** = মেনু
(Settings..., Hide/Show spider, Quit)।

| কমান্ড | কী করে |
|---|---|
| `spidereko` | মাকড়সা চালু করে |
| `spidereko -settings` | সেটিংস উইন্ডো খোলে (দরকার হলে মাকড়সাও চালু করে) |
| `spidereko -speed 600 -size 2` | সেটিংস বদলায় - সেভ হয়, আর চলমান মাকড়সায় সঙ্গে সঙ্গে প্রয়োগ হয় |
| `spidereko -fg '#202020' -bg tan -accent red` | রঙ: `#rgb`, `#rrggbb` বা রঙের নাম |
| `spidereko -off` / `-on` | মাকড়সা লুকায় / দেখায় (প্রোগ্রাম চলতে থাকে) |
| `spidereko -tofocus` / `-notofocus` | ফোকাস করা উইন্ডোর ওপরের কিনারার দিকে / পয়েন্টারের দিকে দৌড়ায় |
| `spidereko -shadow` / `-noshadow` | মাকড়সার নিচে নরম ছায়া (ডিফল্টে বন্ধ) |
| `spidereko -reset` | ডিফল্ট সেটিংসে ফেরে |
| `spidereko -toggle` | চালু করে, আর চলতে থাকলে থামায় |
| `spidereko -stop` | চলমান মাকড়সা থামায় |
| `spidereko -install` / `-uninstall` | ইউজারের জন্য ইনস্টল যোগ / মুছে ফেলা |
| `spidereko -h` | সব অপশন |

### সেটিংস

| অপশন | মানে | সীমা | ডিফল্ট |
|---|---|---|---|
| `-speed` | গতি (পিক্সেল/সেকেন্ডে) | ১০ - ১০০০০ | ৪২০ |
| `-idle` | কত সেকেন্ড স্থির থাকলে ঘুমিয়ে পড়ে | ০ - ৩৬০০ | ২.৫ |
| `-size` | মাকড়সার আকারের গুণক | ০.৩ - ৮ | ১ |
| `-lift` | পয়েন্টারের কত ওপরে (০ = ঠিক ওপরে, বেশি = আরও ওপরে, ঋণাত্মক = নিচে) | -১০০ - ১০০ | ০ |
| `-fg` `-bg` `-accent` | শরীর / ধার ও চোখ / পিঠের চিহ্ন | রঙ | `#16161c` `#9a9aae` `#e45cc0` |
| `-fps` | অ্যানিমেশনের ফ্রেম রেট - কম মানে CPU-তে হালকা | ৫ - ২৪০ | ৩০ |

এগুলো `~/.config/spidereko/config.json`-এ জমা থাকে (হাতেও বদলাতে পারো, ভুল মান উপেক্ষা করা হয়)। চলমান
মাকড়সা কমান্ড লাইন আর সেটিংস উইন্ডোর পরিবর্তন সঙ্গে সঙ্গে নিয়ে নেয়।

### লগইনের সময় নিজে থেকে চালু

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

(KDE-তে *System Settings -> Autostart* থেকেও যোগ করা যায়।)

---

## আনইনস্টল

```bash
spidereko -uninstall                           # মাকড়সা থামায়, প্রোগ্রাম, আইকন আর মেনু এন্ট্রি মুছে দেয়
rm -rf ~/.config/spidereko                     # ঐচ্ছিক: সেভ করা সেটিংসও মুছতে
rm -f ~/.config/autostart/spidereko.desktop    # শুধু autostart বানিয়ে থাকলে
```

দুই সংস্করণের জন্যই একই নিয়ম। এরপর ক্লোন করা `spidereko` ফোল্ডারটাও মুছে ফেলতে পারো।
(`spidereko` আর `PATH`-এ না থাকলে ফোল্ডার থেকেই আনইনস্টল চালাও:
`./c/spidereko -uninstall` অথবা `python3 python/spidereko.py -uninstall`।)

**এক সংস্করণ থেকে অন্যটায় যেতে:** অন্যটার `-install` চালালেই হয়, সেটা প্রথমটাকে বদলে দেয়। সেটিংস থেকে যায়।

---

## সমস্যা হলে

* **মাকড়সা পয়েন্টারের পিছু নেয় না / এক জায়গায় বসে থাকে।** সম্ভবত Wayland-এ আছ - *প্রয়োজনীয়তা* দেখো।
  `echo $XDG_SESSION_TYPE` দিয়ে দেখো (`x11` আসা উচিত)।
* **মাকড়সার চারপাশে কালো বর্গ** (Python সংস্করণ)। কোনো কম্পোজিটিং ম্যানেজার চলছে না; কম্পোজিটর চালু করো
  (KDE: *System Settings -> Display and Monitor -> Compositor*)।
* **ট্রে আইকন নেই।** কিছু প্যানেলে ট্রে থাকে না। অ্যাপ মেনুর এন্ট্রি বা `spidereko -settings` ব্যবহার করো।
* **`-tofocus` কাজ করছে না।** Python সংস্করণে `xdotool` ইনস্টল করো। C সংস্করণে তোমার উইন্ডো ম্যানেজারকে
  `_NET_ACTIVE_WINDOW` মানতে হবে (সাধারণগুলো সবই মানে)।
* **"spidereko is already running"।** এটাই নিয়ম - একসঙ্গে একটাই মাকড়সা চলে। থামাতে `spidereko -stop`।
* **`c/`-তে বিল্ড error।** চারটা `-dev` প্যাকেজের একটা নেই; ওপরে দেখানো মতো ইনস্টল করো।


## লাইসেন্স

[MIT](LICENSE)
