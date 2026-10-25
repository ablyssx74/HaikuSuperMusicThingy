# HaikuSuperMusicThingy v1.0.14

## Highlights

**Linux**
- **The tray icon is the program's own icon.** It was Qt's stock speaker, which looks the same as a tray's volume icon (hDeskbar and hDesktop both have one), so the two could not be told apart. The crossing-arrows icon the program already uses for its window is now in the tray too.
- **It tells you when there is a newer version** (new in v1.0.13). The check reads the `VERSION` file of this repository on GitHub, and shows an alert when no notification server is running.

**Haiku**
- **The version comes from the `VERSION` file.** The program's version text (the About line and what the update check compares with GitHub's), the resource and the package all take it from there now, as hDesktop and Cricket do. v1.0.13 still said v1.0.12 inside the program, which would have had it offering an update to itself; v1.0.14 does not.
- The resource's version numbers were in the wrong fields (the program read as 1.12.0 to the system); they are right now.

## Install
- **Haiku:** install `HaikuSuperMusicThingy-1.0.14-1-x86_64.hpkg` on 64-bit Haiku (it has the projectM visualizer) or `HaikuSuperMusicThingy-1.0.14-1-x86_gcc2.hpkg` on 32-bit Haiku, for the whole system.
- **Linux:** `cd linux/packaging && makepkg -si`. Restart the program once, so it puts its new icon in the tray.

## Tested on
- Linux (KDE Plasma on Wayland, nested): the new tray icon was looked at in hDeskbar and in hDesktop's dock.
- Haiku R1/beta6: the 64-bit and 32-bit packages were built (on 64-bit Haiku and on 32-bit Haiku) and show v1.0.14 in the program and in the package. They were not run.
