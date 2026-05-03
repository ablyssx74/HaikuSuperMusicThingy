

## <p align="center"> <img width="48" height="48" align="top" alt="icon" src="https://github.com/user-attachments/assets/40b6432d-33e5-4946-8eed-9f0508000b23" />HaikuSuperMusicThingy </p>
### <p align="center"> HaikuSuperMusicThingy is a free streaming media client for [SomaFM](https://somafm.com/)<br> Fast, light, and fun! </p>
#### <p align="center">Screenshots
<p align="center">
  <img width="303" height="592" alt="Image" src="https://github.com/user-attachments/assets/329e7ab8-21c7-47ef-9f63-a5a94a090b6e" />
<img width="304" height="601" alt="Image" src="https://github.com/user-attachments/assets/a4883213-ed79-45ef-9eea-fcdc1bb265fd" />
<img width="296" height="601" alt="Image" src="https://github.com/user-attachments/assets/7fe28553-f0bd-4c62-8897-233b92ce40ba" /></p>

### Includes
-    shuffle stations.
-	 save/delete/play favorites.
-	 optional notifications.
-	 fade in/out on song change. 
-	 config manger.
-	 Visualizer window for supported platforms.
### Tested  Haiku OS x86_64  and x86



### Build Latest Haiku SuperMusicThingy 
```shell
#Download the source
git clone https://github.com/ablyssx74/HaikuSuperMusicThingy
cd HaikuSuperMusicThingy
make release
```

### Build Latest Haiku SuperMusicThingy with projectm Visualizer Window
### Presets for Visualizer Window
-   Download from a huge selection of [presets](https://github.com/projectM-visualizer/projectm?tab=readme-ov-file#presets) and install in SuperMusicThingy config presets folder.<br> I recommend [projectm_presets](http://spiegelmc.com/pub/projectm_presets.zip). Then move to ~/config/settings/SuperMusicThingy/milk_presets/presets folder.
-	 Visuals require [Haiku Nightly](https://download.haiku-os.org/nightly-images/x86_64/), a Turing+ GPU supported Nvidia card, [libglvnd-1.7.0-4-x86_64.hpkg](https://github.com/X547/nvidia-haiku/releases/download/v0.0.1/libglvnd-1.7.0-4-x86_64.hpkg) and [nebula-0.0.2-1.x86_64.hpkg](https://github.com/X547/nvidia-haiku/releases/download/v0.0.2/nebula-0.0.2-1.x86_64.hpkg). 
```shell
#Download the source
git clone https://github.com/ablyssx74/HaikuSuperMusicThingy
cd HaikuSuperMusicThingy
make release ENABLE_PROJECTM=ON
```


