# HaikuSuperMusicThingy v1.0.16

## Fixed
- **A crash while the projectM visualizer starts no longer stops the program from starting again.** When the visualizer crashed during its start (seen on RenkuOS, in OpenAL's audio capture), `config.txt` still said `"showVisuals": true`, so the program crashed again at every start until the file was edited by hand. The file now says `false` from before the visualizer starts until its first frame has been drawn, then `true` again. After a crash in between, the program starts normally and the visualizer is off; switch it on again in the config window. Nothing changes while the visualizer works. A program that is closed within a second of the visualizer starting also saves the setting as on.

## Not fixed
- The crash itself (OpenAL's capture thread, `RingBuffer::getWriteVector`, when the default audio input is used on RenkuOS) is intermittent and was not reproduced in six later runs, so its cause is not known. With hrecord's mixer sharing the audio input it did not happen.

## Install
Install `HaikuSuperMusicThingy-1.0.16-1-x86_64.hpkg` on 64-bit Haiku, or `HaikuSuperMusicThingy-1.0.16-1-x86_gcc2.hpkg` on 32-bit Haiku, for the whole system.
The 32-bit package was built but not run.
