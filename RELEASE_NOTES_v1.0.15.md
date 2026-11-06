# HaikuSuperMusicThingy v1.0.15

## Fixed
- **The update check reads the version number from its first digit.** The check compares the program's version with the `VERSION` file on GitHub. The old comparison needed a letter in front of the number (the `v` of `v1.0.15`), so a version file that held just the number was read as 0.0.0 and no update was ever announced. It now takes the first number of the form X.Y.Z in either text, with or without the letter. Nothing else changed.

## Install
Install `HaikuSuperMusicThingy-1.0.15-1-x86_64.hpkg` on 64-bit Haiku, or `HaikuSuperMusicThingy-1.0.15-1-x86_gcc2.hpkg` on 32-bit Haiku, for the whole system.
