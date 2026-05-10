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

enum {
    MSG_SHUFFLE = 'shuf',
    MSG_STOP    = 'stop',
    MSG_PLAY    = 'play',
    MSG_PAUSE   = 'paus',
    MSG_VOL_UP  = 'v_up',
    MSG_VOL_DN  = 'v_dn',
    MSG_FAVS    = 'favs',
    MSG_OPEN_URL = 'burl',
    MSG_VOL_CHANGE = 'vchg',
    MSG_UPDATE_BITRATE = 'bitr',
	MSG_PRESET_SELECTED   = 'prsl',
	MSG_REFRESH_PRESETS   = 'prrf',
    MSG_UPDATE_SONG = 'updt', 
    MSG_UPDATE_ART = 'dart',    
    MSG_ADD_FAV     = 'adfv', 
    MSG_DEL_FAV     = 'dlfv',
    MSG_PLAY_FAV    = 'plfv',
    MSG_CFG_AUTO_SHUFFLE = 'c_as',
    MSG_CFG_ICON_SIZE = 'i_sz',
    MSG_TOGGLE_PRESETS = 'e_cv',
    MSG_CFG_AUTO_PresetTimer = 'c_pt',
    MSG_CFG_NOTIFY       = 'c_nt',
    MSG_CFG_QUALITY      = 'c_qu',
    MSG_CFG_THEME        = 'c_th',
    MSG_PLAY_STATION     = 'plst', 
    MSG_TOGGLE_VISUALS   = 'tvis',
    MSG_EQ_CHANGED = 'eqch',
    MSG_TOGGLE_EQ  = 'eqtg',
	MSG_AUDIO_READY = 'AudR',
	MSG_UPDATE_BOUNCE = 'bnce',
	MSG_EQ_RESET 	  = 'eqrs',
    MSG_SHUFFLE_FAVS_CHANGED = 'sfch',   
 
};




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
    BButton*	  fResetEQBtn;
    BStringView*  fStationView;
    BStringView*  fListenersView;
    BStringView*  fquality; 
    BTextView*    fSongView;
    BSlider*      fVolumeSlider;
    BButton*      fShuffleBtn;
    BButton* 	  fApplyEQBtn;
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
