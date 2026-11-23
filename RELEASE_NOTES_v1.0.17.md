# HaikuSuperMusicThingy v1.0.17

## Fixed
- **The projectM visualizer no longer crashes when it starts listening.** On RenkuOS the program could crash the first time the visualizer recorded audio after a boot (a segment violation in OpenAL's `RingBuffer::getWriteVector()`). The cause is in the Haiku capture backend of OpenAL: it registers the recorder's callback and connects the recorder before it creates its ring buffer, so a buffer that arrives in between meets a null pointer. The program now carries its own copy of OpenAL with one more patch: the callback ignores buffers until the backend is ready.
- The copy is installed in a folder of its own, `/boot/system/lib/haikusupermusicthingy/`, and only this program is linked to look there first. The `openal` package and other programs are not touched. Its licence (GNU LGPL v2), the source details and the patches are in `data/HaikuSuperMusicThingy/openal/`.
- Tested on RenkuOS: four starts of the visualizer after a reboot with the fixed OpenAL, no crash; before, the same test crashed every time. With the installed 1.0.17 package, a start after a boot (the first audio capture since the boot) also ran without a crash, and the running program had loaded one OpenAL, the copy in `/boot/system/lib/haikusupermusicthingy/` (`libmpv` uses it too).

## Notes
- The fixed OpenAL is in the 64-bit package only. The 32-bit package has no visualizer.
- On regular Haiku the visualizer's capture may not receive any audio at all with the stock media kit on some hardware (the test machine's HD Audio input delivered nothing); that is not changed by this release.

## Install
Install `HaikuSuperMusicThingy-1.0.17-1-x86_64.hpkg` on 64-bit Haiku, or `HaikuSuperMusicThingy-1.0.17-1-x86_gcc2.hpkg` on 32-bit Haiku, for the whole system.
