/*
 * Copyright 2026, Kris Beazley jb@epluribusunix.net
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef SUPER_MUSIC_WINDOW_H
#define SUPER_MUSIC_WINDOW_H

#ifdef USE_PROJECTM
#include <projectM-4/projectM.h>
#endif



#include <Application.h>
#include <Window.h>
#include <StatusBar.h>
#include <StringView.h>
#include <Slider.h>
#include <Button.h>
#include <Application.h>
#include <LayoutBuilder.h>
#include <SupportDefs.h>
#include <InterfaceDefs.h>
#include <LayoutBuilder.h>
#include <TabView.h>    
#include <ListView.h>   
#include <ScrollView.h> 
#include <TextView.h> 
#include <CheckBox.h>
#include <map>
#include <string>
#include <ListView.h>
#include <Bitmap.h>
#include <vector>
#include <set>

struct Channel {
    std::string title;
    std::string id;
    std::string desc;
    std::string listeners;
    std::string largeimage;
    std::string image;
    std::string url; 
    std::set<std::string> supported_bitrates; 
    std::map<std::string, std::string> quality_map; 
};

class SongLabel; 
class SpectrumView;

class SuperMusicWindow : public BWindow {
public:
    SuperMusicWindow();
    
    virtual 	 ~SuperMusicWindow(); 
    virtual void MessageReceived(BMessage* message);
    void 		 UpdateStatus(const char* station, const char* song);
    void 		 RefreshFavorites();
    void 		 UpdateFavButtons(); 
    void 		 SendNotification(const char* songTitle); 
    void 		 ApplyTheme(); 
    void 		 StartVisuals();
    void 		 StopVisuals();
    virtual bool QuitRequested();
    void 		 PlayStation(const Channel& chan);
    void 		 PopulateStationList(); 
    void 		 DownloadStationIcons(); 
    bool 		 shuffleFavsOnly;    
    BBitmap*     fAlbumArt;
    BView*       fArtView;
    std::map<std::string, BBitmap*> fArtCache; 
    void UpdateMPVFilters(); 	

private:

    BTabView*     fTabView;
    BListView*    fFavList;    
    BListView*    fPresetList;
    BScrollView*  fPresetScroll;
    BCheckBox*    fPresetToggle;  
    BGroupView*   fSizeContainer; 
    BCheckBox*    chkShuffle; 
    BButton*      fBtnAddFav;
    BButton*      fBtnDelFav;
    BStringView*  fStationView;
    BStringView*  fListenersView;
    BStringView*  fquality; 
    BTextView*    fSongView;
    BSlider*      fVolumeSlider;
    BButton*      fShuffleBtn;
    BCheckBox*    fVisualsCheckbox; 
    BCheckBox*    fShuffleFavsCheckbox; 
    BListView*    fStationList; 
    SongLabel*    fDescView;
    void 		  UpdateUI(); 
	BSlider* 	  fEQSliders[10]; 
    BSlider 	 *fLimitInput, *fLimitLimit, *fLimitRelease;
    BCheckBox* 	  fEQToggle;
    BGroupView*   fEQContainer;
    SpectrumView* fSpectrum;
    std::map<std::string, BBitmap*> fIconCache;
    #ifdef USE_PROJECTM
    projectm_handle fProjectM; 
	#else
    void*          fProjectM; 
	#endif 
};



#endif
