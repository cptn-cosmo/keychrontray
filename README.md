# keychrontray

A KDE Plasma 6 system tray app (Wayland and X11) for Keychron keyboards:

- battery level in the tray icon and tooltip, with low-battery notifications
- RGB lighting profiles as multi-color gradients across the keyboard (static, or slowly scrolling left to
  right), switched from the tray menu, the CLI or D-Bus, or automatically when a game starts
- shipped profiles for FFXIV (one per expansion), WoW, Baldur's Gate 3, Control, Cyberpunk 2077, Elden Ring, Hades,
  Remnant II, Halo MCC, Half-Life: Alyx, Beat Saber, TaleSpire, VRChat, Fellowship, Destiny 2, HELLDIVERS 2, and a
  generic "Steam game" fallback

C++20, Qt 6, KDE Frameworks 6 (KStatusNotifierItem, KNotifications, KConfig), hidapi, libudev and QtDBus. No Electron, web view or GTK.

## What works in which connection mode

Developed and tested on a **Keychron K10 HE** (firmware v1.1.1). Details and evidence are in [`docs/PROTOCOL.md`](docs/PROTOCOL.md).

| Mode | Lighting profiles | Battery |
|---|---|---|
| USB cable (also while the keyboard types over 2.4 GHz) | ✓ verified on hardware | ✗ firmware has no query → "unavailable (USB cable)" |
| 2.4 GHz Keychron Link receiver | ✗ the receiver does not forward VIA commands (verified) | ✗ not exposed → "unavailable (2.4 GHz)" |
| Bluetooth | ✗ same radio-module limitation | ✓ expected via BlueZ/UPower (**untested**) |

The app never shows a made-up battery value. If the keyboard can't be reached, the tray still runs and says why.
Profile switches made while the keyboard is unreachable are applied once it becomes reachable.

## Install

### Arch / CachyOS / Manjaro: package

The repo ships a PKGBUILD that builds the latest commit of this repository:

```sh
git clone https://github.com/cptn-cosmo/keychrontray.git
cd keychrontray/packaging/arch
makepkg -si
```

This installs the binary, the application menu entry, the notification config and the udev rule
(`/usr/lib/udev/rules.d/70-keychrontray.rules`). Replug the keyboard once afterwards. To update later, run
`makepkg -si` again in the same directory. It always builds the newest commit.

## Build from source

### Arch / CachyOS / Manjaro
```sh
sudo pacman -S --needed base-devel cmake ninja extra-cmake-modules qt6-base \
    kstatusnotifieritem knotifications kconfig kcoreaddons hidapi systemd-libs
```

### Fedora
```sh
sudo dnf install gcc-c++ cmake ninja-build extra-cmake-modules qt6-qtbase-devel \
    kf6-kstatusnotifieritem-devel kf6-knotifications-devel kf6-kconfig-devel kf6-kcoreaddons-devel \
    hidapi-devel systemd-devel
```

### Debian 13+ / Ubuntu 24.10+ / KDE neon
```sh
sudo apt install g++ cmake ninja-build extra-cmake-modules qt6-base-dev \
    libkf6statusnotifieritem-dev libkf6notifications-dev libkf6config-dev libkf6coreaddons-dev \
    libhidapi-dev libudev-dev
```

### Compile, test, install
```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$HOME/.local
cmake --build build
ctest --test-dir build           # unit tests + UI smoke test, no hardware needed
cmake --install build            # binary, .desktop, notification config, udev rule template
# packagers: -DKEYCHRONTRAY_UDEV_RULES_DIR=/usr/lib/udev/rules.d installs the udev rule directly
```
Installing matters for notifications: KNotifications needs `keychrontray.notifyrc` in
`<prefix>/share/knotifications6/`. With a `~/.local` prefix, make sure `~/.local/bin` is in `PATH`.

## udev: access without root

The app never needs root. It needs read/write access to the keyboard's `hidraw` node. Many distributions already
ship a rule (e.g. the AUR packages `keychron-link-udev` / `keychron-k10-he-udev`). Check with:

```sh
ls -l /dev/hidraw*            # Keychron nodes should show a "+" (ACL) or rw for your user
keychrontray status           # while the app runs: "Profile: …" instead of "Lighting: unavailable"
```

If not, install the bundled rule:

```sh
sudo cp data/70-keychrontray.rules /etc/udev/rules.d/
sudo udevadm control --reload && sudo udevadm trigger
```

Then replug the keyboard. The rule uses `TAG+="uaccess"`, so only the user logged in at the seat gets access. It
does not use `MODE="0666"`.

## Usage

Start `keychrontray`, or pick *Keychron Tray* in the application menu.

### Start automatically at login

Any of these works and they all do the same thing:

- tray menu → **Start automatically at login**
- `keychrontray autostart on` (`off` / `status`), which works whether or not the app is running
- Settings › General → *Start with the Plasma session*

This writes a standard XDG autostart entry, `~/.config/autostart/io.github.keychrontray.desktop`. It also appears
in *System Settings › Autostart*, where you can remove it too.

**Tray menu:** battery status, profiles (grouped, e.g. *Final Fantasy XIV › Endwalker*), *Auto-switch for games*,
*Restore default*, *Save current lighting to keyboard…*, *Start automatically at login*, *Open settings…*, *Quit*. Left-click opens settings.

**CLI** (talks to the running app over D-Bus, so it can be bound to KDE global shortcuts):
```sh
keychrontray set-profile ffxiv-endwalker
keychrontray restore-default
keychrontray auto-switch off
keychrontray list-profiles
keychrontray status
keychrontray save-to-keyboard --yes          # EEPROM write, see below
keychrontray add-game 1174180                # new rule + profile (name from your Steam library)
keychrontray add-game 1174180 --profile desk # map to an existing profile
keychrontray list-steam-games 40             # local library sorted by playtime
keychrontray autostart on                    # start at every login
keychrontray reset-profile wow-forever       # take the shipped version of a profile (discards your edits)
```
Raw D-Bus: `qdbus6 io.github.keychrontray /KeychronTray io.github.keychrontray.Control.SetProfile bg3`.

KDE shortcut: *System Settings › Keyboard › Shortcuts › Add New › Command* → `keychrontray set-profile ffxiv-dawntrail`.

### Which profile is active

1. live preview from the settings window
2. a profile you picked manually (stays until a game starts or exits, or *Restore default*)
3. the detected game's profile
4. the default profile (Settings › Games; `keyboard` = the lighting the keyboard had when the app started)

Several games at once: a configured game beats the generic Steam fallback, then the higher `priority` wins, then
the most recently started game.

### Keyboard memory (EEPROM) is never written implicitly

Profile changes only change the keyboard's RAM state. On quit (tray, `keychrontray quit`, SIGTERM as sent at logout) the
lighting from startup is restored. If the app crashes, the next start still knows the real original, because it is
kept in `~/.local/state/keychrontray/original.json`. Only *Save to keyboard…* or `save-to-keyboard --yes` writes
EEPROM.

## Configuration files

Everything is plain text and safe to edit by hand. Changes are picked up live.

| File | Content |
|---|---|
| `~/.config/keychrontray/profiles/<id>.json` | one lighting profile each |
| `~/.config/keychrontray/games.json` | game → profile rules, fallback, ignored Steam tool ids |
| `~/.config/keychrontrayrc` | default profile, auto-switch, scan interval, battery thresholds, optional Steam API key |

Profile example (gradient):
```json
{
    "name": "Endwalker",
    "group": "Final Fantasy XIV",
    "gradient": {
        "colors": ["#e0b84a", "#ffffff", "#4aa3e0"],
        "motion": "static",
        "cycleSeconds": 16
    },
    "brightness": 80
}
```
- `colors`: 2–16 stops from the left edge to the right edge.
- `motion`: `"static"` spreads the stops across the keyboard. `"scroll"` repeats them in a loop (A → B → … → A) and
  moves the pattern slowly from left to right, one keyboard width every `cycleSeconds` (2–600).
- Colors blend in HSV along the shorter way round the hue wheel. To steer a blend, add a stop. Pure white
  (`#ffffff`) has no hue, so gold → white → blue fades through white instead of through green.
- Only hue and saturation are used. Brightness is one value for the whole keyboard (firmware limitation), so a
  gradient can fade to white but not to black.
- Gradients are rendered by this app into the keyboard's per-key color table (RAM only) and need the USB cable.
  Scrolling keeps running while the app runs, at roughly 7–10 frames per second and about 2% of one CPU core. On
  keyboards without Keychron's per-key protocol, the first color is shown as a solid color.

A firmware effect instead of a gradient (this is how the Desk profile is defined):
```json
{ "name": "Desk", "effect": "Solid Color", "color": "#ffffff", "brightness": 60, "speed": 128 }
```
`effect` takes a name or the number from `docs/PROTOCOL.md`. `color` is `#rrggbb` (only hue and saturation are
used) or `{"hue": 0-255, "saturation": 0-255}`. `brightness` is a percentage and `speed` is 0–255.

**Updates to the shipped themes.** When a new version changes a shipped profile, it replaces your copy only if you
never edited it (tracked in `profiles/.shipped.json`). Edited and deleted profiles are left alone. To take a
shipped version anyway, run `keychrontray reset-profile <id>` (or `--all`).

Game rule example (`games.json`):
```json
{ "id": "ffxiv", "name": "FINAL FANTASY XIV Online", "steamAppIds": [39210],
  "processNames": ["ffxiv_dx11.exe"], "profile": "ffxiv-dawntrail", "priority": 0 }
```
Steam games are matched by app id: the `reaper … AppId=<id>` launcher process, or `SteamAppId` in a Proton process's
environment. Non-Steam games (Lutris, Heroic, Battle.net…) are matched by process name, which is case-insensitive
and allows `*` wildcards. Non-Steam shortcuts in Steam (ids ≥ 2³¹) never trigger the generic Steam profile. Map
them explicitly if you want to. The game can't report which FFXIV expansion zone you're in, so the FFXIV rule uses
one "default expansion" profile (Dawntrail as shipped), and you switch expansions from the tray or CLI.

The shipped process names for the Windows executables are best-effort. Steam app ids are the primary match.

### Steam library

`add-game`, `list-steam-games` and *Add from Steam library…* read your local Steam data offline: `libraryfolders.vdf`,
`appmanifest_*.acf`, playtime from `userdata/*/config/localconfig.vdf`, and names from `appcache/appinfo.vdf`. To
list owned games that Steam has never cached locally, put a Web API key (from
<https://steamcommunity.com/dev/apikey>) and your SteamID64 or vanity name under Settings › General. The key is
stored in `keychrontrayrc` with mode 600 and is never logged.

## Development without hardware

```sh
./build/bin/keychrontray --mock        # or KEYCHRONTRAY_MOCK=1
KEYCHRONTRAY_SCREENSHOT_DIR=/tmp/shots QT_QPA_PLATFORM=offscreen ./build/bin/test_settingsui
```
Tests cover VIA packet encoding, profile parsing and validation (including every shipped default), the game
matcher and priority rules, `/proc` parsing, battery alert thresholds, the lighting controller (precedence, restore,
crash recovery, no implicit EEPROM writes) against the mock backend, the VDF/appinfo parsers and the settings window.

## Troubleshooting

**Logs.** By default they go to the journal: `journalctl --user -t keychrontray -f`. They also appear in
Settings › Diagnostics, which has a *Copy report* button. For more detail run
`QT_FORCE_STDERR_LOGGING=1 QT_LOGGING_RULES="keychrontray.*.debug=true" keychrontray`.

**"Lighting needs the USB cable".** That's expected on 2.4 GHz and Bluetooth. Plug in the cable. The keyboard can
stay in 2.4 GHz mode. If it still says so with the cable connected, check Diagnostics: "cannot open … (permissions?)"
in the log means the udev rule is missing.

**Colors don't change, or change partially.** Another tool (VIA, Keychron Launcher in a browser) can have the
device open at the same time. The app copes with that, but the other tool may overwrite its colors. Firmware other than the K10 HE's may number effects differently. Use effect numbers
from your board's VIA JSON.

**Battery shows "unavailable".** That's correct for cable and 2.4 GHz (see the table above). Over Bluetooth, check
`upower -d`. The keyboard has to appear there with a model containing "Keychron".

**A game doesn't switch.** Run `keychrontray status` while the game runs. If nothing is detected, find the process
with `ps -eo comm,args | grep -i <game>` and add its executable name or Steam app id under Settings › Games. Launchers
can be ignored by leaving them out of the rules.

**Notifications don't appear.** Install the project (`cmake --install build`) so `keychrontray.notifyrc` is found.

**Already running.** Only one instance can own the D-Bus name. Use `keychrontray quit`.

## License

GPL-3.0-or-later, see [LICENSE](LICENSE).
