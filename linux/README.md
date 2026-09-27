# HaikuSuperMusicThingy — Linux Port

This is a Linux/Qt6 port of the Haiku OS `HaikuSuperMusicThingy` SomaFM
streaming client. It targets KDE Plasma / Wayland (e.g. CachyOS), but should
run fine on any desktop with Qt6 and a StatusNotifierItem-capable tray
(GNOME needs an extension for the tray icon; everything else works either
way).

## Why this isn't a 1:1 port

The original code (`../haiku-supermusicthingy.cpp`) is built directly on
Haiku/BeOS's native GUI toolkit — `BApplication`, `BWindow`, `BView`,
`BMessage`, etc. — which only exists on Haiku. There's no compatibility
layer for that API on Linux, so porting it means rebuilding the UI on a
Linux-native toolkit rather than recompiling with a few `#ifdef`s. This
port rebuilds the UI in Qt6 Widgets and reuses the same cross-platform
backend pieces the Haiku build already relied on: **libmpv** for playback
and **SomaFM's `channels.json`** for the station list (fetched here via Qt
Network instead of libcurl).

## Scope of this first pass

Ported:
- Playback (play/pause/stop/mute), volume with fade-in/out on song change
- Station list (from SomaFM's `channels.json`) and Favorites (add/remove/play/shuffle)
- Quality selection (32k/64k/256k/320k or the station's default)
- 15-band equalizer + limiter (via mpv's built-in `equalizer`/`alimiter`
  audio filters instead of the Haiku build's optional LADSPA path, whose
  plugin paths are Haiku-specific)
- Config (persisted under `~/.config/SuperMusicThingy/`), notifications,
  system tray, sleep timer, auto-shuffle-stations timer
- `.desktop` file + hicolor SVG icon for menu integration
- The primary 64-bar spectrum (`SpectrumWidget`) from the Player tab's
  `MODE_BARS` view: a single overall audio level (read from mpv's `astats`
  filter metadata) driving 64 bars through the same per-bar frequency-scale
  curves and spring physics as the Haiku build, colored from a palette
  sampled directly off the current station's album art (falls back to the
  Haiku build's default cyan-to-blue gradient when there's no art yet)

Not yet ported (candidates for a follow-up pass):
- The projectM visualizer window
- The other custom spectrum-game views (pong balls, raindrops, acid melt,
  the moto rider and neon-sign animations, etc.) — only the primary bar
  spectrum was ported
- Deskbar-tray-specific code (irrelevant on Linux; replaced by
  `QSystemTrayIcon`, which uses the StatusNotifierItem protocol so it works
  under Plasma on both X11 and Wayland)

## Dependencies

- Qt6 (Widgets, Network, Svg)
- libmpv
- CMake, a C++17 compiler, pkg-config

On Arch/CachyOS:

```shell
sudo pacman -S --needed qt6-base qt6-svg mpv cmake pkgconf
```

## Building manually

```shell
cd linux
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/haikusupermusicthingy
```

## Building/installing as a package (`makepkg -si`)

```shell
cd linux/packaging
makepkg -si
```

The `PKGBUILD` here builds directly from your working tree (whatever is
currently checked out in this repo, not a downloaded tarball), so it always
packages your current local changes — convenient for active development.
Bump the `VERSION` file at the repo root when you cut a release; `pkgver`
is read from it automatically.

## Config & data locations

- Settings: `~/.config/SuperMusicThingy/config.ini`
- Favorites: `~/.config/SuperMusicThingy/favorites.ini`

(These are separate from the Haiku build's
`~/config/settings/SuperMusicThingy/` files — the two builds don't share
state.)
