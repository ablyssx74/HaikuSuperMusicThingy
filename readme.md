
## <p align="center"> <img width="48" height="48" align="top" alt="icon" src="https://github.com/user-attachments/assets/40b6432d-33e5-4946-8eed-9f0508000b23" />HaikuSuperMusicThingy </p>

### <p align="center"> HaikuSuperMusicThingy is a free streaming media client for [SomaFM](https://somafm.com/)<br> Fast, light, and fun! </p>



### Includes
- shuffle stations.
- save/delete/play favorites.
- optional notifications.
- fade in/out on song change. 
- config manager.
- Visualizer window for supported platforms.

### Tested Haiku OS x86_64 and x86




### Build Latest ( No Visualizer Window )
```shell
#Download the source
git clone https://github.com/ablyssx74/HaikuSuperMusicThingy
cd HaikuSuperMusicThingy
make release
```

### Build Latest ( With Visualizer Window )
-	 Visuals require [Haiku Nightly](https://download.haiku-os.org/nightly-images/x86_64/), a Turing+ GPU supported Nvidia card, [libglvnd-1.7.0-4-x86_64.hpkg](https://github.com/X547/nvidia-haiku/releases/download/v0.0.1/libglvnd-1.7.0-4-x86_64.hpkg) and [nebula-0.0.2-1.x86_64.hpkg](https://github.com/X547/nvidia-haiku/releases/download/v0.0.2/nebula-0.0.2-1.x86_64.hpkg). 
```shell
#Download the source
git clone https://github.com/ablyssx74/HaikuSuperMusicThingy
cd HaikuSuperMusicThingy
make release ENABLE_PROJECTM=ON
```
### Presets for Visualizer Window
-   Download from a huge selection of [presets](https://github.com/projectM-visualizer/projectm?tab=readme-ov-file#presets) and copy/move ( no symlinks ) to ~/config/settings/SuperMusicThingy/milk_presets/presets folder. <br> I recommend [projectm_presets](http://spiegelmc.com/pub/projectm_presets.zip). </p>

### Screenshots
<p align="center">
   
<img align="left" width="369" height="228" alt="Image" src="https://github.com/user-attachments/assets/5b59381a-edf2-49a1-999d-247207268ced" />
  <img align="left"  width="122" height="250" alt="Image" src="https://github.com/user-attachments/assets/b42fb56e-d1f5-4d6f-858f-92758c13635b" />
<img align="left" width="122" height="250" alt="Image" src="https://github.com/user-attachments/assets/f4d47b7c-6b66-4697-b404-4f47363dd613" />

</p>


