# OpenAL Soft 1.21.1 with a fix for the Haiku capture backend

HaikuSuperMusicThingy's visualizer records what the sound card hears through OpenAL. HaikuPorts' OpenAL has a Haiku capture backend
(`alc/backends/haiku.cpp`, by Gerasim Troeglazov) in which `HaikuCapture::open()` registers the recorder's callback and connects the
recorder **before** it creates the ring buffer (`mRing`) and the converters. If the media server delivers a buffer in between, the callback
dereferences a null `mRing` and the program crashes (`RingBuffer::getWriteVector()`). On RenkuOS this happens on the first capture after a boot.

This folder holds a build of the same OpenAL with one more patch: a ready flag, set at the end of `open()`, that makes the callback
ignore buffers until then. The package installs it in `/boot/system/lib/haikusupermusicthingy/`, and the program is linked with that
folder as its run-path, so no other program uses it and the `openal` package is not touched.

## What is in it
- `libopenal.so.1.21.1` (with the symlinks `libopenal.so.1` and `libopenal.so`): the library.
- `COPYING`: the licence of OpenAL Soft (GNU LGPL v2).
- `haikuports-openal-1.21.1.patchset`: the five Haiku patches of HaikuPorts (`media-libs/openal/patches/openal-1.21.1.patchset`).
- `0001-haiku-capture-ignore-buffers-until-open-has-set-up-t.patch`: the added patch.

## Source and build
- Upstream: openal-soft 1.21.1, https://github.com/kcat/openal-soft/archive/refs/tags/1.21.1.tar.gz
  (sha256 `8ac17e4e3b32c1af3d5508acfffb838640669b4274606b7892aa796ca9d7467f`, the one in HaikuPorts' recipe).
- The upstream source with the HaikuPorts patchset (`git am`) and then the patch above applied.
- Built on RenkuOS hrev60072+1190 (x86_64): `cmake -G Ninja -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release
  -DCMAKE_CXX_FLAGS="-Wno-template-body -fpermissive" -DALSOFT_UTILS=OFF -DALSOFT_EXAMPLES=OFF -DALSOFT_TESTS=OFF -DALSOFT_INSTALL=OFF`,
  then `ninja OpenAL`. RenkuOS has the oldest libbe of the systems this program runs on.

This copy can go away when HaikuPorts' OpenAL has the same fix.
