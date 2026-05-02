/*
 * Copyright 2026, Kris Beazley jb@epluribusunix.net
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef SUPER_MUSIC_WINDOW_H
#define SUPER_MUSIC_WINDOW_H

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

class SuperMusicWindow : public BWindow {
public:
    SuperMusicWindow();
    virtual ~SuperMusicWindow(); 
    virtual void MessageReceived(BMessage* message);
    void UpdateStatus(const char* station, const char* song);
    void RefreshFavorites();
    void UpdateFavButtons(); 
    void SendNotification(const char* songTitle); 
    void ApplyTheme(); 
    void StartVisuals();
    void StopVisuals();
    virtual bool QuitRequested();
    
    BBitmap*     fAlbumArt;
    BView*       fArtView;

private:

    BTabView*    fTabView;
    BListView*   fFavList;
    
    BButton*     fBtnAddFav;
    BButton*     fBtnDelFav;

    BStringView* fStationView;
    BStringView* fListenersView;
    BStringView* fquality; 
    BTextView*   fSongView;
    BSlider*     fVolumeSlider;
    BButton*     fShuffleBtn;
    BCheckBox*   fVisualsCheckbox; 
};



#endif
