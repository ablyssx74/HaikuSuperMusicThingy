[![CMake on multiple platforms](https://github.com/ablyssx74/music_thingy/actions/workflows/cmake-multi-platform.yml/badge.svg)](https://github.com/ablyssx74/music_thingy/actions/workflows/cmake-multi-platform.yml)

## <p align="center"> <img width="48" height="48" align="top" alt="icon" src="https://github.com/user-attachments/assets/40b6432d-33e5-4946-8eed-9f0508000b23" />SuperMusicThingy </p>
### <p align="center"> SuperMusicThingy is a free streaming terminal media client for [SomaFM](https://somafm.com/)<br> Fast, light, and fun! </p>
#### <p align="center">Screenshots
<p align="center"><img width="200" height="180" align="center" alt="Screenshot1" src="https://github.com/user-attachments/assets/cfdaf54e-b502-4317-89b9-365cdd11dd6c" /><img width="200" height="180" align="center" alt="Screenshot2" src="https://github.com/user-attachments/assets/8d00ca64-6a20-4193-9841-005beab8e61b" /><img width="200" height="180" align="center" alt="Screenshot3" src="https://github.com/user-attachments/assets/c1575020-3bb6-45f4-a6b3-4fe4c3f495dc" /></p></p>

### Includes


-	 projectM milkdrop visualizer.
-    shuffle stations.
-	 save/delete/play favorites.
-	 optional notifications.
-	 fade in/out on song change. 
-	 config manger.
-	 a CLI API backend for sending keyboard shortcuts like vol_up, shuffle, status.
### Tested on CachyOS, Haiku OS x86_64 and x86

### Presets 
-   Download from a huge selection of [presets](https://github.com/projectM-visualizer/projectm?tab=readme-ov-file#presets) and install in SuperMusicThingy config presets folder.<br> I recommend [projectm_presets](http://spiegelmc.com/pub/projectm_presets.zip).
  


### Build Latest SuperMusicThingy with projectm enabled
```shell
#Download the source
git clone https://github.com/ablyssx74/SuperMusicThingy.git
cd SuperMusicThingy
```
```shell

# Build wtih projectm
# Default install prefix: /usr/local/bin
cmake -B build
cmake --build build
sudo cmake --install build
```

### Build without projectm using cmake
```shell
# Easy Build without projectm using make
make
# If Haiku:
make package
```
```
cmake -B build -DENABLE_PROJECTM=OFF -DENABLE_SDL2=OFF -DENABLE_GL=OFF
cmake --build build
sudo cmake --install build
```
### Preferred Methods For Haiku
-    The preferred method is just to use ```make package```<br>
-    Alternatively, you can use HaikuHpkg.sh <br>
-	 HaikuHpkg.sh will walk you some questions that the simple ```make package``` does not provide eventually creating SuperMusicThingy.hpkg on the Desktop<br>

-	 Download: [HaikuHpkg.zip](https://github.com/ablyssx74/SuperMusicThingy/releases/download/v1.0.0/HaikuHpkg.zip) <br>
-	 For exampe, If building without projectm visuals select option 2 after running ./HaikuHpkg.sh <br>
-	 If building with projectm select option 1. <br>
-	 Visuals require [Haiku Nightly](https://download.haiku-os.org/nightly-images/x86_64/), a Turing+ GPU supported Nvidia card, [libglvnd-1.7.0-4-x86_64.hpkg](https://github.com/X547/nvidia-haiku/releases/download/v0.0.1/libglvnd-1.7.0-4-x86_64.hpkg) and [nebula-0.0.2-1.x86_64.hpkg](https://github.com/X547/nvidia-haiku/releases/download/v0.0.2/nebula-0.0.2-1.x86_64.hpkg). 
```shell
# How to run:
bash ./HaikuHpkg.sh
```

