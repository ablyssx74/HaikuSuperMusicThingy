/*
 * Copyright 2026, Kris Beazley jb@epluribusunix.net
 * All rights reserved. Distributed under the terms of the MIT license.
 */

// --- Haiku Interface Kit ---
#include <Application.h>
#include <Window.h>
#include <View.h>
#include <Message.h>
#include <Bitmap.h>
#include <TranslationUtils.h>
#include <Notification.h>
#include <PopUpMenu.h>
#include <MenuItem.h>
#include <MenuField.h>
#include <CheckBox.h>
#include <TextView.h>
#include <map>
#include <string>
#include <ListView.h>
#include <IconUtils.h>
#include <Roster.h>
#include <StringView.h>


// --- Haiku Storage Kit ---
#include <Path.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <Directory.h> 
#include <storage/Entry.h>
#include <storage/Path.h>

// --- Third Party Libraries ---
#include <curl/curl.h>
#include <mpv/client.h>
#include "nlohmann/json.hpp"

// --- C++ Standard Library ---
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <ctime>
#include <cstdlib>    // for rand, getenv
#include <algorithm>  // for std::find
#include <cstring>
#include <random>


#ifdef USE_SDL2
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#endif

#ifdef USE_GL
#include <GL/gl.h>
#endif

#ifdef USE_PROJECTM
#include <projectM-4/projectM.h>
#endif

#include "haiku-supermusicthingy.h"


namespace fs = std::filesystem;

const std::string BASE_URL = "https://somafm.com/";

#ifdef USE_PROJECTM
#include <image.h>
#include <OS.h>
#include <AL/al.h>
#include <AL/alc.h>

// Milkdrop auto-shuffle timer
const uint32_t PRESET_DURATION = 30000;  // 30 Sec
uint32_t lastPresetChange = 0;
std::string currentPresetName = "None";
void update_visuals_logic();
void HandleSDLEvents(SDL_Event& e); 

ALCdevice *alcCaptureDevice = nullptr;
projectm_handle pm = nullptr;
bool visualsRunning = false;

void cleanup_capture_device() {
    if (alcCaptureDevice) {
        alcCaptureCloseDevice(alcCaptureDevice);
        alcCaptureDevice = nullptr;
    }
}
std::string gPendingPresetPath = "";

#endif

#ifdef USE_SDL2
SDL_Window* visualWin = nullptr;
SDL_GLContext glContext = nullptr;
#endif



std::string statusMsg = "";
std::time_t statusExpiry = 0;
bool mpvthread_running = true;
SuperMusicWindow* gGuiWindow = nullptr; 
int32 mpv_loop_thread(void* data);
using json = nlohmann::json;

class SuperMusicWindow; 

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
    MSG_SHUFFLE_FAVS_CHANGED = 'sfch'
 
};


void ensure_config_dir() {
    BPath path;

    if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
        path.Append("SuperMusicThingy");
        if (create_directory(path.Path(), 0755) == B_OK) {
        } else {            
        }
    }
}



BBitmap* GetVectorIcon(const unsigned char* data, size_t size, float dimensions) {
    BBitmap* icon = new BBitmap(BRect(0, 0, dimensions - 1, dimensions - 1), B_RGBA32);
    if (BIconUtils::GetVectorIcon(data, size, icon) != B_OK) {
        delete icon;
        return nullptr;
    }
    return icon;
}

class IconButton : public BButton {
public:
    IconButton(const char* name, BBitmap* icon, BMessage* msg)
        : BButton(name, "", msg), fIcon(icon), fIsFavorite(false) 
    {
        SetViewColor(B_TRANSPARENT_COLOR);
    }

    void SetFavorite(bool fav) {
        if (fIsFavorite != fav) {
            fIsFavorite = fav;
            Invalidate(); 
        }
    }

void Draw(BRect updateRect) override {
    if (fIcon) {
        BRect b = Bounds();
        float x = (b.Width() - fIcon->Bounds().Width()) / 2.0f;
        float y = (b.Height() - fIcon->Bounds().Height()) / 2.0f;

        if (Value() == B_CONTROL_ON) {
            x += 1.0f;
            y += 1.0f;
        }

        bool isFavBtn = (strcmp(Name(), "btn_add_fav") == 0);

        if (isFavBtn) {
            if (fIsFavorite) {
                SetDrawingMode(B_OP_ALPHA);
                SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
            } else {
                SetDrawingMode(B_OP_BLEND);
            }
        } else {
            SetDrawingMode(B_OP_ALPHA);
            SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
            
            if (!IsEnabled()) {
                SetDrawingMode(B_OP_BLEND);
            }
        }
        
        DrawBitmap(fIcon, BPoint(x, y));
        SetDrawingMode(B_OP_COPY);
    }
}


private:
    BBitmap* fIcon;
    bool fIsFavorite;
};



const unsigned char kIconShuffle[] = {
	0x6e, 0x63, 0x69, 0x66, 0x03, 0x04, 0x00, 0x66, 0x05, 0x00, 0x02, 0x00, 0x16, 0x02, 0x00, 0x00,
	0x00, 0x3c, 0x60, 0x00, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4c, 0x00, 0x00, 0x48, 0xa0, 0x00,
	0x00, 0x80, 0xff, 0x28, 0x04, 0x04, 0x04, 0x3e, 0x40, 0x40, 0x38, 0x40, 0x3e, 0x40, 0x32, 0x40,
	0x2c, 0x2a, 0x32, 0x2a, 0x26, 0x2a, 0x24, 0x04, 0x04, 0x3e, 0x40, 0x2a, 0x38, 0x2a, 0x3e, 0x2a,
	0x32, 0x2a, 0x2c, 0x40, 0x32, 0x40, 0x26, 0x40, 0x24, 0x0a, 0x03, 0x40, 0x22, 0x40, 0x32, 0x48,
	0x2a, 0x0a, 0x03, 0x40, 0x38, 0x40, 0x48, 0x48, 0x40, 0x09, 0x0a, 0x00, 0x01, 0x01, 0x12, 0x40,
	0xaa, 0xaa, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0xaa, 0xaa, 0x44, 0xaa, 0xaa, 0x44, 0xaa,
	0xaa, 0x01, 0x17, 0x88, 0x22, 0x04, 0x0a, 0x00, 0x01, 0x00, 0x12, 0x40, 0xaa, 0xaa, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x40, 0xaa, 0xaa, 0x44, 0xaa, 0xaa, 0x44, 0xaa, 0xaa, 0x01, 0x17, 0x88,
	0x22, 0x04, 0x0a, 0x00, 0x02, 0x02, 0x03, 0x12, 0x40, 0xaa, 0xaa, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x40, 0xaa, 0xaa, 0x44, 0xaa, 0xaa, 0x44, 0xaa, 0xaa, 0x01, 0x17, 0x84, 0x02, 0x04, 0x0a,
	0x01, 0x02, 0x02, 0x03, 0x12, 0x40, 0xaa, 0xaa, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0xaa,
	0xaa, 0x42, 0xaa, 0xaa, 0x42, 0xaa, 0xaa, 0x01, 0x17, 0x84, 0x02, 0x04, 0x0a, 0x01, 0x01, 0x00,
	0x12, 0x40, 0xaa, 0xaa, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0xaa, 0xaa, 0x42, 0xaa, 0xaa,
	0x42, 0xaa, 0xaa, 0x01, 0x17, 0x88, 0x22, 0x04, 0x0a, 0x02, 0x01, 0x00, 0x12, 0x40, 0xaa, 0xaa,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0xaa, 0xaa, 0x42, 0xaa, 0xaa, 0x42, 0xaa, 0xaa, 0x01,
	0x17, 0x84, 0x22, 0x04, 0x0a, 0x01, 0x01, 0x01, 0x12, 0x40, 0xaa, 0xaa, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x40, 0xaa, 0xaa, 0x42, 0xaa, 0xaa, 0x42, 0xaa, 0xaa, 0x01, 0x17, 0x88, 0x22, 0x04,
	0x0a, 0x02, 0x01, 0x01, 0x12, 0x40, 0xaa, 0xaa, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0xaa,
	0xaa, 0x42, 0xaa, 0xaa, 0x42, 0xaa, 0xaa, 0x01, 0x17, 0x84, 0x22, 0x04, 0x0a, 0x02, 0x02, 0x02,
	0x03, 0x02, 0x40, 0xaa, 0xaa, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0xaa, 0xaa, 0x42, 0xaa,
	0xaa, 0x42, 0xaa, 0xaa
};

const size_t kIconShuffleSize = 340;

const unsigned char kIconFav[] = {
	0x6e, 0x63, 0x69, 0x66, 0x07, 0x02, 0x00, 0x16, 0x02, 0xba, 0x75, 0x56, 0x34, 0xef, 0xed, 0xb5,
	0x1a, 0xee, 0xba, 0x53, 0x4a, 0x4b, 0x2e, 0xca, 0x4a, 0xd8, 0x00, 0x00, 0xff, 0xff, 0x00, 0x05,
	0x00, 0x02, 0x00, 0x06, 0x02, 0xbc, 0xd3, 0x35, 0xba, 0x45, 0xf3, 0x00, 0x00, 0x00, 0x3d, 0x53,
	0xcd, 0x49, 0xf1, 0xe7, 0x4c, 0xb7, 0x0e, 0x00, 0xff, 0x00, 0x00, 0xff, 0xff, 0x66, 0x66, 0x02,
	0x00, 0x06, 0x02, 0xbc, 0xd3, 0x34, 0xba, 0x45, 0xf3, 0x00, 0x00, 0x00, 0x3d, 0x53, 0xcd, 0x49,
	0x11, 0xb3, 0x4c, 0xa0, 0x81, 0x00, 0xff, 0x00, 0x00, 0xff, 0xff, 0x66, 0x66, 0x02, 0x00, 0x06,
	0x02, 0xb6, 0x2f, 0x3c, 0xb7, 0x71, 0xa3, 0x37, 0xa4, 0x0f, 0xb6, 0x10, 0xfb, 0x48, 0xc8, 0xbc,
	0x47, 0x16, 0xd8, 0x00, 0xff, 0x00, 0x00, 0xff, 0xff, 0x66, 0x66, 0x02, 0x00, 0x06, 0x02, 0x37,
	0x6c, 0x19, 0x3a, 0x32, 0x54, 0xba, 0x52, 0x7d, 0x37, 0x3c, 0xb4, 0x4a, 0x0b, 0x89, 0x4a, 0x6e,
	0x10, 0x00, 0xff, 0x00, 0x00, 0xff, 0xbd, 0x00, 0x00, 0x02, 0x00, 0x06, 0x02, 0x00, 0x00, 0x00,
	0xba, 0x47, 0x16, 0x3a, 0x68, 0x6f, 0x00, 0x00, 0x00, 0x4a, 0x22, 0xd2, 0x49, 0xd5, 0xd6, 0x00,
	0xff, 0x00, 0x00, 0xff, 0xff, 0x66, 0x66, 0x07, 0x06, 0x04, 0xba, 0xbc, 0x85, 0xce, 0x13, 0xca,
	0x40, 0xcc, 0xf8, 0xcc, 0x55, 0xc7, 0xa6, 0xcc, 0xc0, 0xcb, 0x97, 0xcc, 0x2f, 0xc6, 0x3c, 0xc6,
	0xea, 0xc4, 0x7f, 0x06, 0x10, 0xef, 0xfb, 0xff, 0xfb, 0xb8, 0xb3, 0xb4, 0xb4, 0xb9, 0xfd, 0xb4,
	0x25, 0xb8, 0x39, 0xb4, 0xe9, 0xb7, 0x69, 0xb5, 0x7c, 0xb7, 0xca, 0xb5, 0x2c, 0xb7, 0x69, 0xb5,
	0x7c, 0xb7, 0x7b, 0xb5, 0x6e, 0xb4, 0xe1, 0xb7, 0x2f, 0xb4, 0xe1, 0xb7, 0x2f, 0xae, 0x67, 0xbb,
	0xde, 0xbc, 0x1a, 0xcd, 0x82, 0xb9, 0xc3, 0xca, 0x9a, 0xbc, 0x1a, 0xcd, 0x82, 0xbc, 0x8f, 0xce,
	0x13, 0xbd, 0x3e, 0xcd, 0xc6, 0xbd, 0x3e, 0xcd, 0xc6, 0xbd, 0xe2, 0xcd, 0x7f, 0xc2, 0x47, 0xca,
	0xb9, 0xc0, 0x21, 0xcc, 0x73, 0xc4, 0x39, 0xc9, 0x98, 0xc9, 0xaf, 0xc2, 0x0f, 0xc8, 0xb4, 0xc6,
	0x8f, 0xc9, 0xcf, 0xc1, 0x7e, 0xc9, 0xde, 0xc0, 0x5b, 0xc9, 0xde, 0xc0, 0xec, 0xc9, 0xde, 0xbe,
	0x72, 0xc7, 0xc6, 0xba, 0xb0, 0xc9, 0x2b, 0xbc, 0x8b, 0xc6, 0x43, 0xb8, 0xad, 0xc3, 0x71, 0xb7,
	0xfd, 0xc4, 0x95, 0xb8, 0x1c, 0xc2, 0x41, 0xb7, 0xdc, 0xc0, 0x4e, 0xb8, 0xd4, 0xc1, 0x15, 0xb8,
	0x2d, 0xc0, 0x4e, 0xb8, 0xd4, 0xc0, 0x62, 0xb8, 0xc5, 0xc0, 0x10, 0xb8, 0xfd, 0xc0, 0x2c, 0xb8,
	0xea, 0xbf, 0x79, 0xb7, 0xa2, 0xbc, 0xc5, 0xb5, 0x22, 0xbe, 0x40, 0xb6, 0x0b, 0xbb, 0x65, 0xb4,
	0x4b, 0x06, 0x03, 0x2e, 0xbf, 0xba, 0xcb, 0x23, 0xbf, 0x93, 0xca, 0xf2, 0xbf, 0xac, 0xcb, 0x12,
	0xbf, 0x79, 0xca, 0xfd, 0xbf, 0x6b, 0xcb, 0x03, 0x06, 0x03, 0x2e, 0xbf, 0xba, 0xcb, 0x23, 0xbf,
	0x93, 0xca, 0xf2, 0xbf, 0xac, 0xcb, 0x12, 0xbf, 0x79, 0xca, 0xfd, 0xbf, 0x6b, 0xcb, 0x03, 0x06,
	0x06, 0xef, 0x0e, 0xb9, 0x07, 0xb5, 0x63, 0xbc, 0x5d, 0xb3, 0xf0, 0xb8, 0x9d, 0xb5, 0x91, 0xb7,
	0xf6, 0xb6, 0x08, 0xb8, 0x43, 0xb5, 0xc9, 0xb7, 0xf6, 0xb6, 0x08, 0xb5, 0x5b, 0xb7, 0xc9, 0xb8,
	0xc8, 0x2b, 0xb8, 0xc8, 0x2b, 0xbb, 0x3e, 0xb8, 0xa4, 0xbc, 0xc8, 0xbc, 0xe0, 0xbf, 0x69, 0xba,
	0x8f, 0xbd, 0xc3, 0xba, 0xf7, 0xbf, 0xc9, 0xb9, 0x40, 0x06, 0x07, 0xaf, 0x3f, 0xc6, 0xf1, 0xbb,
	0x14, 0xcc, 0x2a, 0xc2, 0x08, 0xc4, 0xc7, 0xb8, 0x32, 0xc0, 0xdd, 0xb9, 0x5f, 0xc2, 0x02, 0xb8,
	0x68, 0xc0, 0xdd, 0xb9, 0x5f, 0xbe, 0x56, 0xbb, 0x15, 0xc1, 0x36, 0xbc, 0x09, 0xc1, 0xe2, 0xbb,
	0x27, 0xc1, 0xe2, 0xbb, 0x27, 0xc2, 0x58, 0xbb, 0x7a, 0xc3, 0x54, 0xbc, 0x8f, 0xc2, 0xd3, 0xbb,
	0xef, 0xc8, 0x28, 0xc2, 0x8c, 0xc1, 0x41, 0xca, 0x3f, 0xc4, 0xaa, 0xc7, 0x7b, 0xc5, 0x16, 0xc8,
	0x0a, 0x06, 0x05, 0xfb, 0x03, 0xc3, 0x54, 0xbc, 0x8f, 0xcb, 0x9c, 0xc6, 0xd4, 0xbf, 0x6c, 0xb7,
	0xb8, 0xbc, 0xc8, 0xbc, 0xe0, 0xb6, 0x3d, 0xb7, 0x4a, 0xba, 0x24, 0xb5, 0x98, 0xae, 0x79, 0xba,
	0xaa, 0xbc, 0xc5, 0xcc, 0xd5, 0xbb, 0x24, 0xca, 0xd0, 0xbc, 0xc7, 0xcc, 0xd8, 0xbc, 0xcc, 0xcc,
	0xdd, 0xbc, 0xc9, 0xcc, 0xdb, 0xbc, 0xcc, 0xcc, 0xdd, 0x06, 0x0a, 0x01, 0x01, 0x01, 0x02, 0x3f,
	0xbf, 0xfb, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3f, 0xc0, 0x86, 0x44, 0x7d, 0xf0, 0xc1, 0x93,
	0x71, 0x0a, 0x02, 0x01, 0x02, 0x02, 0x3f, 0xbf, 0xfb, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3f,
	0xc0, 0x86, 0x44, 0x7d, 0xf0, 0xc1, 0x93, 0x71, 0x0a, 0x03, 0x01, 0x03, 0x02, 0x3f, 0xbf, 0xfb,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3f, 0xc0, 0x86, 0x44, 0x7d, 0xf0, 0xc1, 0x93, 0x71, 0x0a,
	0x04, 0x01, 0x04, 0x02, 0x3f, 0xbf, 0xfb, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3f, 0xc0, 0x86,
	0x44, 0x7d, 0xf0, 0xc1, 0x93, 0x71, 0x0a, 0x05, 0x01, 0x05, 0x02, 0x3f, 0xbf, 0xfb, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x3f, 0xc0, 0x86, 0x44, 0x7d, 0xf0, 0xc1, 0x93, 0x71, 0x0a, 0x06, 0x01,
	0x06, 0x02, 0x3f, 0xbf, 0xfb, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3f, 0xc0, 0x86, 0x44, 0x7d,
	0xf0, 0xc1, 0x93, 0x71
};

const size_t kIconFavSize = 756;

const unsigned char kIconStop[] = {
	0x6e, 0x63, 0x69, 0x66, 0x03, 0x04, 0x00, 0x66, 0x05, 0x00, 0x02, 0x00, 0x16, 0x02, 0x00, 0x00,
	0x00, 0x3c, 0x60, 0x00, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4c, 0x00, 0x00, 0x48, 0xa0, 0x00,
	0x00, 0x80, 0xff, 0x28, 0x01, 0x0a, 0x04, 0x48, 0x48, 0x48, 0x22, 0x22, 0x22, 0x22, 0x48, 0x03,
	0x0a, 0x00, 0x01, 0x00, 0x12, 0x40, 0xaa, 0xaa, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0xaa,
	0xaa, 0x44, 0xaa, 0xaa, 0x44, 0xaa, 0xaa, 0x01, 0x17, 0x84, 0x22, 0x04, 0x0a, 0x01, 0x01, 0x00,
	0x12, 0x40, 0xaa, 0xaa, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0xaa, 0xaa, 0x42, 0xaa, 0xaa,
	0x42, 0xaa, 0xaa, 0x01, 0x17, 0x84, 0x22, 0x04, 0x0a, 0x02, 0x01, 0x00, 0x02, 0x40, 0xaa, 0xaa,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0xaa, 0xaa, 0x42, 0xaa, 0xaa, 0x42, 0xaa, 0xaa
};

const size_t kIconStopSize = 127;

const unsigned char kIconPause[] = {
	0x6e, 0x63, 0x69, 0x66, 0x03, 0x04, 0x00, 0x66, 0x05, 0x00, 0x02, 0x00, 0x16, 0x02, 0x00, 0x00,
	0x00, 0x3c, 0x60, 0x00, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4c, 0x00, 0x00, 0x48, 0xa0, 0x00,
	0x00, 0x80, 0xff, 0x28, 0x01, 0x0a, 0x04, 0x30, 0x22, 0x24, 0x22, 0x24, 0x48, 0x30, 0x48, 0x06,
	0x0a, 0x00, 0x01, 0x00, 0x12, 0x40, 0xaa, 0xaa, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0xaa,
	0xaa, 0x44, 0xaa, 0xaa, 0x44, 0xaa, 0xaa, 0x01, 0x17, 0x84, 0x22, 0x04, 0x0a, 0x01, 0x01, 0x00,
	0x12, 0x40, 0xaa, 0xaa, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0xaa, 0xaa, 0x42, 0xaa, 0xaa,
	0x42, 0xaa, 0xaa, 0x01, 0x17, 0x84, 0x22, 0x04, 0x0a, 0x02, 0x01, 0x00, 0x02, 0x40, 0xaa, 0xaa,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0xaa, 0xaa, 0x42, 0xaa, 0xaa, 0x42, 0xaa, 0xaa, 0x0a,
	0x00, 0x01, 0x00, 0x12, 0x40, 0xaa, 0xaa, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0xaa, 0xaa,
	0x4a, 0x2a, 0xaa, 0x44, 0xaa, 0xaa, 0x01, 0x17, 0x84, 0x22, 0x04, 0x0a, 0x01, 0x01, 0x00, 0x12,
	0x40, 0xaa, 0xaa, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0xaa, 0xaa, 0x4a, 0x00, 0x00, 0x42,
	0xaa, 0xaa, 0x01, 0x17, 0x84, 0x22, 0x04, 0x0a, 0x02, 0x01, 0x00, 0x02, 0x40, 0xaa, 0xaa, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0xaa, 0xaa, 0x4a, 0x00, 0x00, 0x42, 0xaa, 0xaa
};

const size_t kIconPauseSize = 206;

const unsigned char kIconPlay[] = {
	0x6e, 0x63, 0x69, 0x66, 0x03, 0x04, 0x00, 0x66, 0x05, 0x00, 0x02, 0x00, 0x16, 0x02, 0x00, 0x00,
	0x00, 0x3c, 0x60, 0x00, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4c, 0x00, 0x00, 0x48, 0xa0, 0x00,
	0x00, 0x80, 0xff, 0x28, 0x01, 0x0a, 0x03, 0x46, 0x35, 0x24, 0xb3, 0xcb, 0x24, 0x48, 0x03, 0x0a,
	0x00, 0x01, 0x00, 0x12, 0x40, 0xaa, 0xaa, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0xaa, 0xaa,
	0x44, 0xaa, 0xaa, 0x44, 0xaa, 0xaa, 0x01, 0x17, 0x84, 0x22, 0x04, 0x0a, 0x01, 0x01, 0x00, 0x12,
	0x40, 0xaa, 0xaa, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0xaa, 0xaa, 0x42, 0xaa, 0xaa, 0x42,
	0xaa, 0xaa, 0x01, 0x17, 0x84, 0x22, 0x04, 0x0a, 0x02, 0x01, 0x00, 0x02, 0x40, 0xaa, 0xaa, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0xaa, 0xaa, 0x42, 0xaa, 0xaa, 0x42, 0xaa, 0xaa
};

const size_t kIconPlaySize = 126;

mpv_handle *mpv = nullptr;
std::vector<Channel> channels;
std::string pendingSong = "";
std::time_t notifyTimer = 0;
std::string currentSong = "None";
std::string currentDesc = "None";
std::string currentStation = "";
std::string currentStationID = ""; 
std::string currentListeners = "";
std::string currentAlbumArtUrl = "";


static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}


void download_art(const std::string& url) {
    if (url.empty()) return;
    
    CURL* curl = curl_easy_init();
    if(curl) {
        std::string buffer;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "SuperMusicThingy/1.0");

        if(curl_easy_perform(curl) == CURLE_OK) {
            BMemoryIO memIO(buffer.data(), buffer.size());
            BBitmap* newBitmap = BTranslationUtils::GetBitmap(&memIO);

            if (newBitmap && gGuiWindow) {
                BMessage* msg = new BMessage(MSG_UPDATE_ART);
                msg->AddPointer("bitmap", newBitmap);
                gGuiWindow->PostMessage(msg);
            }
        }
        curl_easy_cleanup(curl);
    }
}



void fetch_channels() {
    channels.clear();
    CURL* curl = curl_easy_init();
    std::string buffer;
    
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, (BASE_URL + "channels.json").c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "SuperMusicThingy/1.0");

        if(curl_easy_perform(curl) == CURLE_OK) {
            try {
                auto data = json::parse(buffer);
                for (auto& ch : data["channels"]) {
                    // 1. Create the object and fill basic info
                    Channel channel;
                    channel.title      = ch.value("title", "");
                    channel.id         = ch.value("id", "");
                    channel.desc       = ch.value("description", "");
                    channel.listeners  = ch.value("listeners", "0");
                    channel.largeimage = ch.value("largeimage", "");
                    channel.image      = ch.value("image", "");
                    
		if (ch.contains("playlists") && ch["playlists"].is_array()) {
    		for (auto& pl : ch["playlists"]) {
        		std::string url = pl.value("url", "");
        		size_t lastDot = url.find_last_of('.');
        		if (lastDot != std::string::npos) {

            		if (url.find("320.pls") != std::string::npos) channel.supported_bitrates.insert("320k");
            		if (url.find("256.pls") != std::string::npos) channel.supported_bitrates.insert("256k");
            		if (url.find("64.pls") != std::string::npos)  channel.supported_bitrates.insert("64k");
            		if (url.find("32.pls") != std::string::npos)  channel.supported_bitrates.insert("32k");
        		}
    		}
		}

        channels.push_back(channel);
                	}
            	} catch(...) {
            }
        }
        curl_easy_cleanup(curl);
    }
}




class StationItem : public BListItem {
public:
    StationItem(Channel chan) : BListItem(), fChannel(chan), fIcon(nullptr) {}

    virtual ~StationItem() {
        delete fIcon;
    }

    void SetIcon(BBitmap* icon) { fIcon = icon; }
    Channel GetChannel() { return fChannel; }

	virtual void DrawItem(BView* owner, BRect frame, bool complete) override {
    	rgb_color bgColor = owner->ViewColor(); 
    	rgb_color textColor = owner->HighColor();
    
    	if ((bgColor.red + bgColor.green + bgColor.blue) / 3 < 128 && 
        	(textColor.red + textColor.green + textColor.blue) / 3 < 128) {
        	textColor = {255, 255, 255, 255}; 
    	}

    	if (IsSelected()) {
        	bgColor = ui_color(B_LIST_SELECTED_BACKGROUND_COLOR);
        	textColor = ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR);
    	}

    	owner->SetLowColor(bgColor);
    	owner->FillRect(frame, B_SOLID_LOW); 
    
    	float textOffset = 45.0;     
    	if (fIcon && fIcon->IsValid()) {
        	owner->SetDrawingMode(B_OP_ALPHA);
        	owner->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
        	owner->DrawBitmap(fIcon, BRect(frame.left + 4, frame.top + 4, 
                                     frame.left + 36, frame.top + 36));
        	owner->SetDrawingMode(B_OP_COPY); // Reset after bitmap
    	}

    owner->SetDrawingMode(B_OP_OVER);
    owner->SetHighColor(textColor);  
    owner->MovePenTo(frame.left + textOffset, frame.top + 18);
    owner->DrawString(fChannel.title.c_str());
    
    float scale = be_plain_font->Size() / 12.0f;
    BFont font;
    owner->GetFont(&font);
    font.SetSize(10.0 * scale);
    owner->SetFont(&font);

    rgb_color descColor = textColor;
    if (!IsSelected()) {
        descColor.alpha = 180;
        owner->SetDrawingMode(B_OP_ALPHA);
    } else {
        owner->SetDrawingMode(B_OP_OVER);
    }
    
    owner->SetHighColor(descColor);
    owner->MovePenTo(frame.left + textOffset, frame.top + 32);

    // Define the BString and truncate it
    BString truncatedDesc(fChannel.desc.c_str());
    float maxWidth = frame.Width() - textOffset - 10; 
    owner->TruncateString(&truncatedDesc, B_TRUNCATE_END, maxWidth);
    owner->DrawString(truncatedDesc.String());
    
    // Clean up: Reset drawing mode and font for the next item
    owner->SetDrawingMode(B_OP_COPY);
    owner->SetFont(be_plain_font); 
}

virtual void Update(BView* owner, const BFont* font) override {

    SetHeight(48.0);
}


private:
    Channel fChannel;
    BBitmap* fIcon; 
};




class SongLabel : public BTextView {
public:
    SongLabel(const char* name) : BTextView(name) {
        MakeEditable(false);
        MakeSelectable(false);
        SetWordWrap(true);
        SetAlignment(B_ALIGN_CENTER);
        
   
        SetInsets(2, 2, 2, 2); 
        SetExplicitMinSize(BSize(B_SIZE_UNSET, 50));
    }


    void AttachedToWindow() override {
        BTextView::AttachedToWindow();
        SetViewColor(Parent()->ViewColor());
        BRect r = Bounds();
        r.InsetBy(2, 2); 
        SetTextRect(r);
    }


    void FrameResized(float width, float height) override {
        BTextView::FrameResized(width, height);
        BRect r = Bounds();
        r.InsetBy(2, 2);
        SetTextRect(r);
    }
    
    void SetCustomFont(const BFont* font) {
        SetFontAndColor(font); 
        Invalidate();
    }
};



class AlbumArtView : public BView {
public:
    AlbumArtView() : BView("art_view", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE) {
        fCurrentBitmap = nullptr;        
    }

    void SetBitmap(BBitmap* bitmap) {
        fCurrentBitmap = bitmap;
        Invalidate(); 
    }

	virtual void Draw(BRect updateRect) {
    if (fCurrentBitmap) {
        SetHighColor(30, 30, 30);
        FillRect(Bounds());
        SetDrawingMode(B_OP_ALPHA);
        DrawBitmap(fCurrentBitmap, fCurrentBitmap->Bounds(), Bounds(), B_FILTER_BITMAP_BILINEAR);        
        SetDrawingMode(B_OP_COPY); 
    } else {
        	SetHighColor(30, 30, 30);
        	FillRect(Bounds());
        	SetHighColor(200, 200, 200);
        
        	const char* text = "Press Play or Shuffle";

        	font_height fh;
        	GetFontHeight(&fh);
        	float textWidth = StringWidth(text);

        	float x = (Bounds().Width() - textWidth) / 2;
        	float y = (Bounds().Height() / 2) + (fh.ascent / 2) - (fh.descent / 2);

        	DrawString(text, BPoint(x, y));
    	}
	}


private:
    BBitmap* fCurrentBitmap;
};


void SuperMusicWindow::PopulateStationList() {
    fStationList->MakeEmpty(); 
    for (const auto& chan : channels) {
        fStationList->AddItem(new StationItem(chan));
    }
}

class FavListView : public BListView {
public:
    FavListView(const char* name) 
        : BListView(name) {} 

    virtual void MouseDown(BPoint where) override {
        BMessage* msg = Window()->CurrentMessage();
        int32 buttons = msg->GetInt32("buttons", 0);
        
        int32 index = IndexOf(where);

        if ((buttons & B_SECONDARY_MOUSE_BUTTON) != 0 && index >= 0) {
            Select(index); 
            
            BPopUpMenu* menu = new BPopUpMenu("fav_context_menu");
            BMenuItem* deleteItem = new BMenuItem("Remove from Favorites", new BMessage(MSG_DEL_FAV));
            
            menu->AddItem(deleteItem);
            menu->SetTargetForItems(Window());

            BPoint screenPoint = ConvertToScreen(where);
            menu->Go(screenPoint, true, true, true);
        } else {
            BListView::MouseDown(where);
        }
    }
};

class PresetListView : public BListView {
public:
    PresetListView(const char* name) 
        : BListView(name) {} 

	virtual void MouseDown(BPoint where) override {
    BMessage* msg = Window()->CurrentMessage();
    int32 buttons = msg->GetInt32("buttons", 0);
    int32 index = IndexOf(where);

    if ((buttons & B_SECONDARY_MOUSE_BUTTON) != 0 && index >= 0) {
        Select(index);         
        BPopUpMenu* menu = new BPopUpMenu("preset_context_menu", false, false);        
        menu->AddItem(new BMenuItem("Load Preset", new BMessage(MSG_PRESET_SELECTED)));
        menu->AddSeparatorItem();
        menu->AddItem(new BMenuItem("Refresh List", new BMessage(MSG_REFRESH_PRESETS)));        
        menu->SetTargetForItems(Window());
        BPoint screenPoint = ConvertToScreen(where);
        BMenuItem* selected = menu->Go(screenPoint);
        	if (selected) {
            	Window()->PostMessage(selected->Message());
        	}
        	delete menu; 
    	} else {
        	BListView::MouseDown(where);
    	}
	}

};



struct Config {
    bool showNotifications = true;
    bool showVisuals = false;
    bool autoShuffle = false;
    bool autoShuffleVisuals = false;
    bool showSpectrumVisuals = false;
    bool autoVsync = false;
    bool shuffleFavsOnly = false;
    int notifyIconSize = 64; 
    std::string updateTheme = "Dark";
    std::string quality = "128k";
     bool eqEnabled = false;
    float eqBands[10] = {0.0f}; 
    float limitIn = 0.0f;
    float limitLmt = 0.0f;
    float limitRel = 100.0f; 
} cfg;

int selectedConfig = 0;


void save_config() {
    json j;
    j["quality"] = cfg.quality;
    j["notifyIconSize"] = cfg.notifyIconSize;
    j["updateTheme"] = cfg.updateTheme;
    j["showNotifications"] = cfg.showNotifications;
    j["autoShuffle"] = cfg.autoShuffle;
    j["autoShuffleVisuals"] = cfg.autoShuffleVisuals;
    j["showSpectrumVisuals"] = cfg.showSpectrumVisuals;
    j["shuffleFavsOnly"] = cfg.shuffleFavsOnly;
    j["autoVsync"] = cfg.autoVsync;
    j["showVisuals"] = cfg.showVisuals;
    j["eqEnabled"] = cfg.eqEnabled;
    json eqArray = json::array();
    for (int i = 0; i < 10; i++) {
        eqArray.push_back(cfg.eqBands[i]);
    }
    j["eqBands"] = eqArray;
    j["limitIn"] = cfg.limitIn;
    j["limitLmt"] = cfg.limitLmt;
    j["limitRel"] = cfg.limitRel;

    BPath path;
    if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
        path.Append("SuperMusicThingy/config.txt");
        std::ofstream outfile(path.Path());
        if (outfile.is_open()) {
            outfile << j.dump(4);
            outfile.close();
        }
    }
}


void load_config() {
    BPath path;

    if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
        path.Append("SuperMusicThingy/config.txt");
        std::ifstream infile(path.Path());        
        if (infile.is_open()) {
            try {
                json j = json::parse(infile);                
                cfg.quality = j.value("quality", "128k");
                int val = j.value("notifyIconSize", 64);
                if (val == 32 || val == 40 || val == 64 || val == 96 || val == 128) {
                    cfg.notifyIconSize = val;
                } else {
                    cfg.notifyIconSize = 64; 
                }
                cfg.updateTheme = j.value("updateTheme", "Dark");
                cfg.showNotifications = j.value("showNotifications", true);
                cfg.autoShuffle = j.value("autoShuffle", false);
                cfg.autoShuffleVisuals = j.value("autoShuffleVisuals", false);
                cfg.autoVsync = j.value("autoVsync", false);
                cfg.showVisuals = j.value("showVisuals", false);   
                cfg.showSpectrumVisuals = j.value("showSpectrumVisuals", false);   
                cfg.shuffleFavsOnly = j.value("shuffleFavsOnly", false); 
                cfg.eqEnabled = j.value("eqEnabled", false);                
                if (j.contains("eqBands") && j["eqBands"].is_array()) {
                     for (size_t i = 0; i < 10 && i < j["eqBands"].size(); i++) {
                        cfg.eqBands[i] = j["eqBands"][i].get<float>();
                    }
                }
                cfg.limitIn = j.value("limitIn", 0.0f);
                cfg.limitLmt = j.value("limitLmt", 0.0f);
                cfg.limitRel = j.value("limitRel", 100.0f);                                    
            } catch(...) {
            }
        }
    }
}



void SuperMusicWindow::DownloadStationIcons() {
    std::thread([this]() {
        snooze(100000); 

        int32 mainCount = 0;
        if (Lock()) {
            mainCount = fStationList->CountItems();
            Unlock();
        }

        for (int32 i = 0; i < mainCount; i++) {
            StationItem* mainItem = nullptr;
            if (Lock()) {
                mainItem = (StationItem*)fStationList->ItemAt(i);
                Unlock();
            }
            if (!mainItem) continue;
            
            Channel chan = mainItem->GetChannel();
            if (chan.image.empty()) continue;

            if (fIconCache.count(chan.id) > 0) {
                if (Lock()) {
                    mainItem->SetIcon(new BBitmap(fIconCache[chan.id]));
                    fStationList->InvalidateItem(i);
                    
                    for (int32 j = 0; j < fFavList->CountItems(); j++) {
                        StationItem* favItem = (StationItem*)fFavList->ItemAt(j);
                        if (favItem && favItem->GetChannel().id == chan.id) {
                            favItem->SetIcon(new BBitmap(fIconCache[chan.id]));
                            fFavList->InvalidateItem(j);
                        }
                    }
                    Unlock();
                }
                continue;
            }

            CURL* curl = curl_easy_init();
            std::string buffer;
            if (curl) {
                curl_easy_setopt(curl, CURLOPT_URL, chan.image.c_str());
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
                curl_easy_setopt(curl, CURLOPT_USERAGENT, "SuperMusicThingy/1.0");
                curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

                if (curl_easy_perform(curl) == CURLE_OK && !buffer.empty()) {
                    BMemoryIO mem(buffer.data(), buffer.size());
                    BBitmap* icon = BTranslationUtils::GetBitmap(&mem);
                    
                    if (icon && Lock()) {
                        fIconCache[chan.id] = new BBitmap(icon);                         
                        mainItem->SetIcon(icon);
                        fStationList->InvalidateItem(i); 

                        for (int32 j = 0; j < fFavList->CountItems(); j++) {
                            StationItem* favItem = (StationItem*)fFavList->ItemAt(j);
                            if (favItem && favItem->GetChannel().id == chan.id) {
                                favItem->SetIcon(new BBitmap(icon));
                                fFavList->InvalidateItem(j);
                            }
                        }
                        Unlock();
                    }
                }
                curl_easy_cleanup(curl);
            }
        }
    }).detach();
}


void init_mpv() {
        mpv = mpv_create();
        if (!mpv) exit(1);
        mpv_set_option_string(mpv, "ao", "openal");
        mpv_set_option_string(mpv, "input-default-bindings", "yes");
        mpv_set_option_string(mpv, "terminal", "no");
        if (mpv_initialize(mpv) < 0) exit(1);
        mpv_observe_property(mpv, 0, "media-title", MPV_FORMAT_STRING);
        mpv_observe_property(mpv, 0, "paused-for-cache", MPV_FORMAT_FLAG);
        mpv_observe_property(mpv, 0, "audio-bitrate", MPV_FORMAT_DOUBLE);  
   		mpv_observe_property(mpv, 0, "audio-params", MPV_FORMAT_NODE);
		mpv_observe_property(mpv, 0, "af-metadata/bouncy", MPV_FORMAT_NODE);

}

    
std::string get_quality_url(const Channel& ch) {
    if (ch.supported_bitrates.count(cfg.quality)) {
        if (cfg.quality == "320k") return BASE_URL + ch.id + "320.pls";
        if (cfg.quality == "256k") return BASE_URL + ch.id + "256.pls";
        if (cfg.quality == "64k")  return BASE_URL + ch.id + "64.pls";
        if (cfg.quality == "32k")  return BASE_URL + ch.id + "32.pls";
    }
    return BASE_URL + ch.id + ".pls";
}

        
void fade_volume(mpv_handle *mpv, double target_vol, double duration_ms) {
    double current_vol = 0;
    mpv_get_property(mpv, "volume", MPV_FORMAT_DOUBLE, &current_vol);

    const int steps = 50; 
    double step_size = (target_vol - current_vol) / steps;
    // (ms * 1000) / steps gives us delay per step in microseconds
    useconds_t step_duration = (useconds_t)((duration_ms * 1000) / steps);

    for (int i = 0; i < steps; ++i) {
        current_vol += step_size;
        mpv_set_property(mpv, "volume", MPV_FORMAT_DOUBLE, &current_vol);
        usleep(step_duration);
    }

    mpv_set_property(mpv, "volume", MPV_FORMAT_DOUBLE, &target_vol);
}

std::string get_bitrate_text() {
    if (cfg.quality.empty()) return "128k";
    return cfg.quality;
}


// Save Station to favorites
void save_favorite() {
        std::string home = getenv("HOME") ? getenv("HOME") : ".";
        std::string dir = home + "/config/settings/SuperMusicThingy";
        std::string path = dir + "/favorites.txt";
        mkdir(dir.c_str(), 0755);
        std::string currentUrl = "";
        for(const auto& ch : channels) {
            if(ch.title == currentStation) {
                currentUrl = BASE_URL + ch.id + ".pls";
                break;
            }
        }

        if (currentUrl.empty()) {
            statusMsg = "Cannot save: No station selected.";
            statusExpiry = std::time(nullptr) + 2;
            return;
        }

        std::ifstream infile(path);
        std::string line;
        bool isDuplicate = false;
        while (std::getline(infile, line)) {
            if (line == currentUrl) {
                isDuplicate = true;
                break;
            }
        }
        infile.close();

        if (isDuplicate) {
            statusMsg = "Already in favorites!";
        } else {
            std::ofstream outfile(path, std::ios_base::app);
            if (outfile.is_open()) {
                outfile << currentUrl << std::endl;
                statusMsg = "URL saved to favorites!";
                outfile.close();
            } else {
                statusMsg = "Error opening file!";
            }
        }
        statusExpiry = std::time(nullptr) + 2;
}
    
void play_favorite() {
    std::string home = getenv("HOME") ? getenv("HOME") : ".";
    std::string path = home + "/config/settings/SuperMusicThingy/favorites.txt";
    std::ifstream infile(path);
    std::vector<std::string> favs;
    std::string line;
    while (std::getline(infile, line)) if (!line.empty()) favs.push_back(line);

    if (favs.empty()) return;

    std::string favUrl = favs[rand() % favs.size()];
    
    size_t lastSlash = favUrl.find_last_of('/');
    size_t lastDot = favUrl.find_last_of('.');
    if (lastSlash == std::string::npos || lastDot == std::string::npos) return;
    std::string id = favUrl.substr(lastSlash + 1, lastDot - lastSlash - 1);

    std::string finalUrl = favUrl; 
    for (const auto& ch : channels) {
        if (ch.id == id) {
            currentStation = ch.title;
            currentStationID = ch.id; 
            currentDesc = ch.desc;
            currentListeners = ch.listeners;
            currentAlbumArtUrl = ch.largeimage;

            finalUrl = get_quality_url(ch); 

            if (!currentAlbumArtUrl.empty()) {
                if (gGuiWindow && gGuiWindow->fArtCache.count(currentStationID) > 0) {
                    if (gGuiWindow->Lock()) {
                        gGuiWindow->fAlbumArt = gGuiWindow->fArtCache[currentStationID];
                        if (gGuiWindow->fArtView)
                            ((AlbumArtView*)gGuiWindow->fArtView)->SetBitmap(gGuiWindow->fAlbumArt);
                        gGuiWindow->Unlock();
                    }
                } else {
                    std::thread([artUrl = currentAlbumArtUrl]() {
                        download_art(artUrl);
                    }).detach();
                }
            }
            break;
        }
    }

    double original_vol;
    mpv_get_property(mpv, "volume", MPV_FORMAT_DOUBLE, &original_vol);
    fade_volume(mpv, 0, 300);

    currentSong = "Loading Favorite...";
    if (gGuiWindow && gGuiWindow->Lock()) {
        gGuiWindow->UpdateStatus(currentStation.c_str(), currentSong.c_str());
        gGuiWindow->Unlock();
    }

    const char *cmd[] = {"loadfile", finalUrl.c_str(), NULL};
    mpv_command(mpv, cmd);
    
    fade_volume(mpv, original_vol, 500);
}



void SuperMusicWindow::PlayStation(const Channel& chan) {
    currentStation = chan.title;
    currentStationID = chan.id; 
    currentDesc = chan.desc;
    currentListeners = chan.listeners;
    currentAlbumArtUrl = chan.largeimage;
    
    if (Lock()) {
       fDescView->SetText(currentDesc.c_str());
        Unlock();
    }
    
    if (!currentAlbumArtUrl.empty()) {
        if (fArtCache.count(currentStationID) > 0) {
            if (Lock()) {
                fAlbumArt = fArtCache[currentStationID];
                if (fArtView) 
                    ((AlbumArtView*)fArtView)->SetBitmap(fAlbumArt);
                Unlock();
            }
        } else {
            if (Lock()) {            	
                fAlbumArt = nullptr;
                if (fArtView) ((AlbumArtView*)fArtView)->SetBitmap(nullptr);
                Unlock();
            }
            std::thread([url = currentAlbumArtUrl]() {
                download_art(url);
            }).detach();
        }
    }

    double original_vol;
    mpv_get_property(mpv, "volume", MPV_FORMAT_DOUBLE, &original_vol);
    fade_volume(mpv, 0, 200); 

    currentSong = "Buffering...";
    UpdateStatus(currentStation.c_str(), currentSong.c_str());

    std::string url = get_quality_url(chan); 
    const char *cmd[] = {"loadfile", url.c_str(), NULL};
    mpv_command(mpv, cmd);    
    fade_volume(mpv, original_vol, 500);
    if (Lock()) {
        fTabView->Select(0);
        Unlock();
    }
}


// Delete Station from favorites
void delete_favorite() {
        std::string home = getenv("HOME") ? getenv("HOME") : ".";
        std::string path = home + "/config/settings/SuperMusicThingy/favorites.txt";
        std::string currentUrl = "";
        for(const auto& ch : channels) {
            if(ch.title == currentStation) {
                currentUrl = BASE_URL + ch.id + ".pls";
                break;
            }
        }

        if (currentUrl.empty()) return;
        std::ifstream infile(path);
        std::vector<std::string> remaining;
        std::string line;
        bool removed = false;

        while (std::getline(infile, line)) {
            if (line != currentUrl && !line.empty()) remaining.push_back(line);
            else removed = true;
        }
        infile.close();

        if (removed) {
            std::ofstream outfile(path, std::ios::trunc);
            for (const auto& f : remaining) outfile << f << "\n";
            statusMsg = "Deleted from favorites.";
        } else {
            statusMsg = "Not in favorites.";
        }
        statusExpiry = std::time(nullptr) + 2;
}
    
void play_random() {
    if (channels.empty()) return;

    double original_vol;        
    mpv_get_property(mpv, "volume", MPV_FORMAT_DOUBLE, &original_vol);
    fade_volume(mpv, 0, 300);

    int idx = rand() % channels.size();
    Channel& chan = channels[idx];
    currentStation = chan.title;
    currentStationID = chan.id; 
    currentDesc = chan.desc;
    currentListeners = chan.listeners;
    currentSong = "Buffering...";
    currentAlbumArtUrl = chan.largeimage;

    if (!currentAlbumArtUrl.empty()) {
        if (gGuiWindow && gGuiWindow->fArtCache.count(currentStationID) > 0) {
            gGuiWindow->fAlbumArt = gGuiWindow->fArtCache[currentStationID];
            if (gGuiWindow->fArtView) {
                gGuiWindow->fArtView->Invalidate();
            }
        } else {
            std::thread([url = currentAlbumArtUrl]() {
                download_art(url);
            }).detach();
        }
    }
    
    std::string url = get_quality_url(chan); 
    
    const char *cmd[] = {"loadfile", url.c_str(), NULL};
    mpv_command(mpv, cmd);
    
    fade_volume(mpv, original_vol, 500);
}


    
bool is_favorite() {
    BPath path;
    // 1. Get the standard settings path
    if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK) return false;
    path.Append("SuperMusicThingy/favorites.txt");    
    std::ifstream infile(path.Path());
    std::string currentUrl = "";
    for(const auto& ch : channels) {
        if(ch.title == currentStation) {
            currentUrl = BASE_URL + ch.id + ".pls";
            break;
        }
    }

    if (currentUrl.empty()) return false;
    if (infile.is_open()) {
        std::string line;
        while (std::getline(infile, line)) {
            if (line == currentUrl) return true;
        }
    }
    
 return false;
}

void set_volume(char direction) {
    double vol;
    mpv_get_property(mpv, "volume", MPV_FORMAT_DOUBLE, &vol);    
    if (direction == '+') vol += 5;
    else if (direction == '-') vol -= 5;
    if (vol > 100) vol = 100;
    if (vol < 0) vol = 0;
    mpv_set_property(mpv, "volume", MPV_FORMAT_DOUBLE, &vol);
}


void toggle_mute() {
        int mute;
        mpv_get_property(mpv, "mute", MPV_FORMAT_FLAG, &mute);
        mute = !mute;
        mpv_set_property(mpv, "mute", MPV_FORMAT_FLAG, &mute);
 }



void PopulatePresetList(BListView* list, const char* folderPath) {
    list->MakeEmpty();
    if (!std::filesystem::exists(folderPath)) return;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(folderPath)) {
        if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().string();
            if (ext == ".milk" || ext == ".milk2") {
                list->AddItem(new BStringItem(entry.path().filename().string().c_str()));
            }
        }
    }
}



#ifdef USE_PROJECTM
void load_random_preset(projectm_handle pm) {
    const char* home = getenv("HOME");
    if (!home) return;
    std::string configPath = std::string(home) + "/config/settings/SuperMusicThingy/milk_presets/";
    std::vector<std::string> presets;
    try {

        if (!std::filesystem::exists(configPath)) {
            std::filesystem::create_directories(configPath);
            return;
        }

        for (const auto& entry : std::filesystem::recursive_directory_iterator(configPath)) {
            if (entry.is_regular_file() && entry.path().extension() == ".milk") {
                presets.push_back(entry.path().string());
            }
        }

        if (presets.empty()) {
            std::cerr << "No presets found in: " << configPath << std::endl;
            return;
        }

        static std::mt19937 rng(static_cast<unsigned int>(std::time(nullptr)));
        std::uniform_int_distribution<int> dist(0, presets.size() - 1);
        std::string selected = presets[dist(rng)];

        projectm_load_preset_file(pm, selected.c_str(), true);
        std::string name = std::filesystem::path(selected).stem().string();
        if (name.length() > 46) {
            currentPresetName = name.substr(0, 43) + "...";
        } else {
            currentPresetName = name;
        }

    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "FS Error: " << e.what() << std::endl;
    }
}
#endif


#ifdef USE_PROJECTM
void init_visuals() {
    if (visualWin) return;
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {           
            return;
        }
 
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);   

        visualWin = SDL_CreateWindow("SuperMusicThingy Visualizer",
                                     SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                     800, 600, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
        if (!visualWin) return;

        glContext = SDL_GL_CreateContext(visualWin);
        SDL_GL_MakeCurrent(visualWin, NULL); 

        if (cfg.autoVsync) {
         if (SDL_GL_SetSwapInterval(-1) < 0) {
            SDL_GL_SetSwapInterval(1);
         }
        }

        // Disable VSync for better responsiveness on Haiku
        SDL_GL_SetSwapInterval(0);        
        

        alcCaptureDevice = alcCaptureOpenDevice(NULL, 48000, AL_FORMAT_STEREO16, 8192);
        if (!alcCaptureDevice) {
            alcCaptureDevice = alcCaptureOpenDevice("null", 48000, AL_FORMAT_STEREO16, 8192);
        }

        if (alcCaptureDevice) {
            alcCaptureStart(alcCaptureDevice);        
            }

        // Initialize projectM
        visualsRunning = true;

    }
    #endif



int32 VisualsThread(void* data) {
	#ifdef USE_PROJECTM

    if (visualWin && glContext) {
        if (SDL_GL_MakeCurrent(visualWin, glContext) < 0) {
            std::cerr << "GL Context Error: " << SDL_GetError() << std::endl;
            return -1;
        }
    }

    if (!pm) {
        pm = projectm_create();
        if (pm) {
            projectm_set_window_size(pm, 800, 600); 
            load_random_preset(pm);
            lastPresetChange = SDL_GetTicks();
        }
    }

    // 3. RENDER LOOP
    while (visualsRunning && pm) { // added '&& pm' safety check
    
    
    if (!gPendingPresetPath.empty()) {
        projectm_load_preset_file(pm, gPendingPresetPath.c_str(), true);
        gPendingPresetPath = "";
        lastPresetChange = SDL_GetTicks(); 
    }
        
        // --- Audio Capture ---
        if (alcCaptureDevice) {
            ALCint samples = 0;
            alcGetIntegerv(alcCaptureDevice, ALC_CAPTURE_SAMPLES, 1, &samples);
            if (samples > 1024) {
                // Use a static or vector to avoid stack overflow on Haiku threads
                static short buffer[2048]; 
                alcCaptureSamples(alcCaptureDevice, (ALCvoid*)buffer, 1024);
                
                static float floatBuffer[2048];
                for (int i = 0; i < 2048; ++i) floatBuffer[i] = buffer[i] / 32768.0f;
                projectm_pcm_add_float(pm, floatBuffer, 1024, PROJECTM_STEREO);
            }
        }

        uint32_t currentTime = SDL_GetTicks();
        if (cfg.autoShuffleVisuals && (currentTime - lastPresetChange >= PRESET_DURATION)) {
            load_random_preset(pm);
            lastPresetChange = currentTime;
        }

        projectm_opengl_render_frame(pm);
        SDL_GL_SwapWindow(visualWin);

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            // Window Close / Quit
            if (e.type == SDL_QUIT || (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE)) {
                visualsRunning = false; // The loop will exit and clean up naturally
            }
            
            // Resizing
            else if (e.type == SDL_WINDOWEVENT) {
                if (e.window.event == SDL_WINDOWEVENT_RESIZED || 
                    e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    
                    int newW = e.window.data1;
                    int newH = e.window.data2;
                    glViewport(0, 0, newW, newH);
                    projectm_set_window_size(pm, newW, newH);
                }
            }

            // Keyboard Events
            else if (e.type == SDL_KEYDOWN) {
                switch (e.key.keysym.sym) {
                    case SDLK_q:
                        visualsRunning = false;
                        break;
                    case SDLK_s:
                        play_random();
                        currentSong = "Buffering...";
                        break;
                    case SDLK_f:
                        play_favorite();
                        break;
                    case SDLK_m: {
                        const char* cmd_mute[] = {"cycle", "mute", NULL};
                        mpv_command(mpv, cmd_mute);
                        break;
                    }
                    case SDLK_p: {
                        const char* cmd_pause[] = {"cycle", "pause", NULL};
                        mpv_command(mpv, cmd_pause);
                        break;
                    }
                      case SDLK_ESCAPE: {
                        uint32_t flags = SDL_GetWindowFlags(visualWin);
                        bool isFullscreen = (flags & SDL_WINDOW_FULLSCREEN_DESKTOP);
                        if (e.key.keysym.sym == SDLK_ESCAPE && !isFullscreen) break;
                        SDL_SetWindowFullscreen(visualWin, isFullscreen ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                        SDL_ShowCursor(isFullscreen ? SDL_ENABLE : SDL_DISABLE);
                        
                        int w, h;
                        SDL_GetWindowSize(visualWin, &w, &h);
                        glViewport(0, 0, w, h);
                        projectm_set_window_size(pm, w, h);
                        break;
                    }
                }
            }

            else if (e.type == SDL_MOUSEWHEEL) {
                set_volume(e.wheel.y > 0 ? '+' : '-');
            }


            else if (e.type == SDL_MOUSEBUTTONDOWN) {
                if (e.button.button == SDL_BUTTON_MIDDLE) {
                    const char* cmd_mute[] = {"cycle", "mute", NULL};
                    mpv_command(mpv, cmd_mute);
                }
                else if (e.button.button == SDL_BUTTON_RIGHT) {
                    load_random_preset(pm);
                    lastPresetChange = SDL_GetTicks();
                }
                else if (e.button.button == SDL_BUTTON_LEFT && e.button.clicks == 2) {
                    uint32_t flags = SDL_GetWindowFlags(visualWin);
                    bool isFullscreen = (flags & SDL_WINDOW_FULLSCREEN_DESKTOP);
                    SDL_SetWindowFullscreen(visualWin, isFullscreen ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                    SDL_ShowCursor(isFullscreen ? SDL_ENABLE : SDL_DISABLE);
                    
                    int w, h;
                    SDL_GetWindowSize(visualWin, &w, &h);
                    glViewport(0, 0, w, h);
                    projectm_set_window_size(pm, w, h);
                }
            }
        }

        
        snooze(16000); 
    }
    
    // Cleanup
    cleanup_capture_device();
    
    if (glContext) { 
        SDL_GL_MakeCurrent(visualWin, NULL); 
        SDL_GL_DeleteContext(glContext); 
        glContext = nullptr; 
    }
    if (visualWin) { 
        SDL_DestroyWindow(visualWin); 
        visualWin = nullptr; 
    }
    if (pm) {
        projectm_destroy(pm);
        pm = nullptr;
    }

    if (glContext) { SDL_GL_DeleteContext(glContext); glContext = nullptr; }
    if (visualWin) { SDL_DestroyWindow(visualWin); visualWin = nullptr; } 
    if (pm) { projectm_destroy(pm); pm = nullptr; } 
	#endif
    return B_OK;
   
}


void SuperMusicWindow::StartVisuals() {
	#ifdef USE_PROJECTM
    init_visuals(); 
    if (visualsRunning) {
        thread_id vThread = spawn_thread(VisualsThread, "VisualsLoop", B_NORMAL_PRIORITY, NULL);
        resume_thread(vThread);
    }
    #endif
}

void SuperMusicWindow::StopVisuals() {
	#ifdef USE_PROJECTM
    visualsRunning = false;
    #endif
}


#ifdef USE_PROJECTM
void load_specific_preset(const char* filename) {
    const char* home = getenv("HOME");
    if (!home || !filename) return;
    
    std::string configPath = std::string(home) + "/config/settings/SuperMusicThingy/milk_presets/";

    for (const auto& entry : std::filesystem::recursive_directory_iterator(configPath)) {
        if (entry.is_regular_file() && entry.path().filename() == filename) {
            gPendingPresetPath = entry.path().string();
             break;
        }
    }
}

#endif

class WheelSlider : public BSlider {
public:
    // Added 'orientation' parameter
    WheelSlider(const char* name, const char* label, BMessage* msg, 
                int32 min, int32 max, orientation orient)
        : BSlider(name, label, msg, min, max, orient) {}

    virtual void MessageReceived(BMessage* msg) {
        if (msg->what == B_MOUSE_WHEEL_CHANGED) {
            float dy;
            if (msg->FindFloat("be:wheel_delta_y", &dy) == B_OK) {
                int32 min, max;
                GetLimits(&min, &max);
                int32 newValue = Value() - (int32)dy;                 
                if (newValue < min) newValue = min;
                if (newValue > max) newValue = max;
                
                SetValue(newValue);
                Invoke(); 
            }
        } else {
            BSlider::MessageReceived(msg);
        }
    }
};



class ClickableURL : public BStringView {
public:
    ClickableURL(const char* name, const char* text, const char* url)
        : BStringView(name, text), fUrl(url) {
        SetHighColor(0, 102, 204); 
    }

    void MouseDown(BPoint point) override {
        const char* url = fUrl.String();
        be_roster->Launch("text/html", 1, (char**)&url);
    }

private:
    BString fUrl;
};


class VolumeSlider : public BSlider {
public:
    VolumeSlider(const char* name, const char* label, BMessage* message, 
                 int32 min, int32 max)
        : BSlider(name, label, message, min, max, B_HORIZONTAL) {}

    virtual void MessageReceived(BMessage* message) {
        if (message->what == B_MOUSE_WHEEL_CHANGED) {
            float deltaY;
            if (message->FindFloat("be:wheel_delta_y", &deltaY) == B_OK) {
                               int32 newValue = Value() - (int32)(deltaY * 5);                
                if (newValue > 100) newValue = 100;
                if (newValue < 0) newValue = 0;
                
                SetValue(newValue);
                Invoke(); 
            }
        } else {
            BSlider::MessageReceived(message);
        }
    }
};



class SpectrumView : public BView {
public:
    SpectrumView(BRect frame, const char* name)
        : BView(frame, name, B_FOLLOW_ALL, B_WILL_DRAW | B_FRAME_EVENTS) {
        SetViewColor(B_TRANSPARENT_COLOR); 
        fCurrentLevel = -60.0;
        memset(frequencyData, 0, 64);
    }
    
    void UpdateLevel(double level) {
        if (level > fCurrentLevel) {
            fCurrentLevel = level;
        } else {
            fCurrentLevel = (fCurrentLevel * 0.85) + (level * 0.15);
        }
        Invalidate();
    }

    virtual void Draw(BRect updateRect) {
        BRect b = Bounds();        
        SetHighColor(0, 0, 0);
        FillRect(b);        
        float floor = -60.0f;
        float peak = (float)fCurrentLevel;
        if (peak < floor) peak = floor;
        float magnitude = (peak - floor) / (0.0f - floor);
        float width = b.Width();
        float height = b.Height();
        int numBars = 64;
        float barWidth = width / numBars;

        for (int i = 0; i < numBars; i++) {
            float jitter = 0.8f + ((rand() % 40) / 100.0f); 
            float barHeight = magnitude * height * jitter;            
            if (barHeight > height) barHeight = height;
            SetHighColor(0, 200 + (rand() % 55), 50 + (i * 2));             
            FillRect(BRect(i * barWidth, height - barHeight, 
                           (i + 1) * barWidth - 1, height));
        }
    }

    // Note: real FFT would go here
    void UpdateData(const uint8* data, size_t size) {
        memcpy(frequencyData, data, size > 64 ? 64 : size);
        Invalidate();
    }

private:
    double fCurrentLevel; 
    uint8 frequencyData[64];
};





void SuperMusicWindow::UpdateStatus(const char* station, const char* song) {
    if (Lock()) {
        fStationView->SetText("");
        fSongView->SetText(song);
        Unlock();
    }
}


SuperMusicWindow::SuperMusicWindow()
    : BWindow(BRect(100, 100, 550, 380), "SuperMusicThingy", B_TITLED_WINDOW, 
              B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS | B_QUIT_ON_WINDOW_CLOSE)
{
    fAlbumArt = nullptr;
    #ifdef USE_PROJECTM
    fProjectM = pm; 
	#endif
    
    setenv("LADSPA_PATH", "/boot/system/lib/ladspa", 1);     
 
    BFont largeFont(be_bold_font);
    BFont smallFont(be_bold_font);
    
    float scale = be_plain_font->Size() / 12.0f; 
	largeFont.SetSize(18.0f * scale);
	smallFont.SetSize(12.0f * scale);
	
    fTabView = new BTabView("tab_container");
    fTabView->SetExplicitMinSize(BSize(380 * scale, 700 * scale));

    fTabView->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

    // ==========================================
    // TAB 1: PLAYER VIEW (The "Radio" Interface)
    // ==========================================
    BGroupView* playerGroup = new BGroupView(B_VERTICAL, 10);
    playerGroup->SetName("Radio"); 

    // Text Labels
    fStationView = new BStringView("", "Press Play or Shuffle");
    fStationView->SetFont(&largeFont);
    fStationView->SetAlignment(B_ALIGN_CENTER);   
    
	fDescView = new SongLabel("description_view");
	fDescView->SetFontAndColor(&smallFont);
	fDescView->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	fDescView->SetExplicitMinSize(BSize(B_SIZE_UNSET, 60)); 

    fSongView = new SongLabel("song_view");
    fSongView->SetFontAndColor(&smallFont);
    fSongView->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
    
    fquality = new BStringView("quality", "");
    fquality->SetFont(&smallFont);
    
    fListenersView = new BStringView("listeners", "");
    fListenersView->SetFont(&smallFont);
    
    // Album Art
    fArtView = new AlbumArtView();
	fArtView->SetExplicitSize(BSize(325 * scale, 325 * scale)); 
    fArtView->SetExplicitMinSize(BSize(325 * scale, 325 * scale));
	fArtView->SetExplicitMaxSize(BSize(325 * scale, 325 * scale));
    
    BBitmap* heartIcon = GetVectorIcon(kIconFav, kIconFavSize, 40);
	fBtnAddFav = new IconButton("btn_add_fav", heartIcon, new BMessage(MSG_ADD_FAV));
	fBtnAddFav->SetExplicitSize(BSize(40, 40));
	
	BBitmap* pauseIcon = GetVectorIcon(kIconPause, kIconPauseSize, 40);
    IconButton* pauseBtn = new IconButton("btn_pause", pauseIcon, new BMessage(MSG_PAUSE));
	pauseBtn->SetExplicitSize(BSize(75, 75)); 
	
	BBitmap* playIcon = GetVectorIcon(kIconPlay, kIconPlaySize, 40);
    IconButton* playBtn = new IconButton("btn_play", playIcon, new BMessage(MSG_PLAY));
	playBtn->SetExplicitSize(BSize(75, 75)); 
    
    BBitmap* stopIcon = GetVectorIcon(kIconStop, kIconStopSize, 40);
    IconButton* stopBtn = new IconButton("btn_stop", stopIcon, new BMessage(MSG_STOP));
	stopBtn->SetExplicitSize(BSize(75, 75)); 
	
	BBitmap* shuffleIcon = GetVectorIcon(kIconShuffle, kIconShuffleSize, 40);
   	fShuffleBtn = new IconButton("btn_shuffle", shuffleIcon, new BMessage(MSG_SHUFFLE));
	fShuffleBtn->SetExplicitSize(BSize(75, 75)); 	
    
    fVolumeSlider = new VolumeSlider("volume", "Volume", new BMessage(MSG_VOL_CHANGE), 0, 100);
    fVolumeSlider->SetValue(100);
    fVolumeSlider->SetTarget(this); 
    fVolumeSlider->SetModificationMessage(new BMessage(MSG_VOL_CHANGE));


    // --- LAYOUT BUILDER FOR PLAYER TAB ---
    BLayoutBuilder::Group<>(playerGroup, B_VERTICAL, 10)
        .SetInsets(10)
        .Add(fArtView)      
        //.Add(fStationView) 
        .Add(fDescView) 
        .Add(fSongView)
		.AddGlue()
        .AddGroup(B_HORIZONTAL, 0) 
            .AddGroup(B_VERTICAL, 0) 
                .Add(fListenersView)
                .Add(fquality)
            .End()
            .AddGlue() 
            .AddGroup(B_VERTICAL, 5) 
                .Add(fBtnAddFav)
            .End()
        .End()
        // End Split Row
        .AddGlue()
        .Add(fVolumeSlider)
        .AddGroup(B_HORIZONTAL, 10)
            .Add(stopBtn)
            .Add(pauseBtn)
            .Add(playBtn)
            .Add(fShuffleBtn)
        .End();


    // ==========================================
    // TAB 2: STATIONS VIEW (The Directory)
    // ==========================================
    BGroupView* stationGroup = new BGroupView(B_VERTICAL, 0);
    stationGroup->SetName("Stations"); 

    fStationList = new BListView("station_list");
    fStationList->SetInvocationMessage(new BMessage(MSG_PLAY_STATION)); 
    
    BLayoutBuilder::Group<>(stationGroup, B_VERTICAL, 0)
        .SetInsets(10)
        .Add(new BScrollView("station_scroll", fStationList, 0, false, true))
    .End();

    // ==========================================
    // TAB 3. FAVORITES VIEW (The List)
    // ==========================================
    BGroupView* favGroup = new BGroupView(B_VERTICAL, 10);
    favGroup->SetName("Fav"); 

    fFavList = new FavListView("favorites_list");
	fFavList->SetInvocationMessage(new BMessage(MSG_PLAY_FAV)); 
    
    BLayoutBuilder::Group<>(favGroup, B_VERTICAL, 0)
        .SetInsets(10)
        .Add(new BScrollView("fav_scroll", fFavList, 0, false, true))
    .End();

    // ==========================================
    // TAB 4: CONFIG VIEW (Placeholder)
    // ==========================================
	BGroupView* configGroup = new BGroupView(B_VERTICAL, 10);
	configGroup->SetName("Config");

	// --- Quality Selection (Menu Field) ---
	BPopUpMenu* qualityMenu = new BPopUpMenu("Select");
	BMessage* msg320k = new BMessage(MSG_CFG_QUALITY); msg320k->AddString("val", "320k");
	BMessage* msg256k = new BMessage(MSG_CFG_QUALITY); msg256k->AddString("val", "256k");
	BMessage* msg128k = new BMessage(MSG_CFG_QUALITY); msg128k->AddString("val", "128k");
	BMessage* msg64k  = new BMessage(MSG_CFG_QUALITY); msg64k->AddString("val", "64k"); 
	BMessage* msg32k  = new BMessage(MSG_CFG_QUALITY); msg32k->AddString("val", "32k");

	qualityMenu->AddItem(new BMenuItem("320k", msg320k));
	qualityMenu->AddItem(new BMenuItem("256k", msg256k));
	qualityMenu->AddItem(new BMenuItem("128k", msg128k));
	qualityMenu->AddItem(new BMenuItem("64k", msg64k));  
	qualityMenu->AddItem(new BMenuItem("32k", msg32k));

	BMenuItem* selectedItem = qualityMenu->FindItem(cfg.quality.c_str());
	if (selectedItem) selectedItem->SetMarked(true);

    BStringView* qualityLabel = new BStringView("lbl_qual", "Audio Quality:");
    BMenuField* qualityField = new BMenuField("quality_field", NULL, qualityMenu);

    // 1. Create the Checkbox
    BCheckBox* chkNotify = new BCheckBox("chk_notify", "Show Notifications", new BMessage(MSG_CFG_NOTIFY));
    chkNotify->SetValue(cfg.showNotifications ? B_CONTROL_ON : B_CONTROL_OFF);
    
    // 2. Create the Menu (unchanged logic)
    BPopUpMenu* sizeMenu = new BPopUpMenu("Select");
    int sizes[] = {128, 96, 64, 40, 32};
    for (int s : sizes) {
        BString label;
        label << s << "x" << s;    
        BMessage* msg = new BMessage(MSG_CFG_ICON_SIZE); msg->AddInt32("val", s);    
        BMenuItem* item = new BMenuItem(label.String(), msg);    
        if (s == cfg.notifyIconSize) {
            item->SetMarked(true);
        }    
        sizeMenu->AddItem(item);
    }
    
    BStringView* sizeLabel = new BStringView("lbl_size", "Notify Icon Size:"); 
    BMenuField* sizeField = new BMenuField("size_field", NULL, sizeMenu);

    // 3. NEW: The "MilkDrop" Container
    // Group the label and field together
    fSizeContainer = new BGroupView(B_HORIZONTAL);
    fSizeContainer->AddChild(sizeLabel);
    fSizeContainer->AddChild(sizeField);
    
    // Hide it immediately if the checkbox is off
    if (!cfg.showNotifications) {
        fSizeContainer->Hide();
    }



    // --- Checkboxes ---

	fPresetToggle = new BCheckBox("preset_toggle", "MilkDrop Presets:", new BMessage(MSG_TOGGLE_PRESETS));
	
	fPresetToggle->SetValue(B_CONTROL_OFF); 

	fPresetList = new PresetListView("preset_list");
	fPresetList->SetSelectionMessage(new BMessage(MSG_PRESET_SELECTED));
	
	fPresetScroll = new BScrollView("preset_scroll", fPresetList, 0, true, true, B_FANCY_BORDER);
	fPresetScroll->Hide(); 
	fPresetScroll->SetExplicitMinSize(BSize(B_SIZE_UNSET, 150));
	fPresetScroll->SetExplicitMaxSize(BSize(B_SIZE_UNSET, 300));
   
    fVisualsCheckbox = new BCheckBox(BRect(10, 10, 200, 30), "visuals_toggle", 
    "Enable Visualizer", new BMessage(MSG_TOGGLE_VISUALS));
    fVisualsCheckbox->SetValue(cfg.showVisuals ? B_CONTROL_ON : B_CONTROL_OFF);    
    
	fShuffleFavsCheckbox = new BCheckBox("shuffle_favs", "Shuffle Only Favorites", 
    new BMessage(MSG_SHUFFLE_FAVS_CHANGED));
	fShuffleFavsCheckbox->SetValue(cfg.shuffleFavsOnly ? B_CONTROL_ON : B_CONTROL_OFF);

    
    BCheckBox* chkShuffle = new BCheckBox("chk_shuffle", "Auto Shuffle On Start", new BMessage(MSG_CFG_AUTO_SHUFFLE));
    chkShuffle->SetValue(cfg.autoShuffle ? B_CONTROL_ON : B_CONTROL_OFF);    
    
    BCheckBox* chkPresetTimer = new BCheckBox("chk_PresetTimer", "Auto Shuffle Visual Presets 30/s", new BMessage(MSG_CFG_AUTO_PresetTimer));
    chkPresetTimer->SetValue(cfg.autoShuffleVisuals ? B_CONTROL_ON : B_CONTROL_OFF);

    BCheckBox* chkTheme = new BCheckBox("chk_theme", "Dark Theme", new BMessage(MSG_CFG_THEME));
    chkTheme->SetValue(cfg.updateTheme == "Dark" ? B_CONTROL_ON : B_CONTROL_OFF);
 

	// --- EQ & Mastering Section ---
	
	
	fEQToggle = new BCheckBox("eq_toggle", "Enable 10-Band EQ", new BMessage(MSG_TOGGLE_EQ));
	fEQToggle->SetValue(cfg.eqEnabled ? B_CONTROL_ON : B_CONTROL_OFF);
	

	fEQContainer = new BGroupView(B_HORIZONTAL, 3);
	fEQContainer->SetName("EQPanel");
	if (!cfg.eqEnabled) {
    	fEQContainer->Hide();
	}

	fSpectrum = new SpectrumView(BRect(0, 0, 400, 50), "spectrum"); 
	fSpectrum->SetExplicitMinSize(BSize(400, 50));
	fSpectrum->SetExplicitMaxSize(BSize(B_SIZE_UNSET, 50)); 

	const char* freqLabels[] = { "50Hz", "100Hz", "156Hz", "220Hz", "311Hz", "440Hz", "622Hz", "880Hz", "1k2", "1k7" };


	for (int i = 0; i < 10; i++) {
    	BGroupView* bandGroup = new BGroupView(B_VERTICAL, 2);
        fEQSliders[i] = new WheelSlider(freqLabels[i], "", new BMessage(MSG_EQ_CHANGED), -15, 15, B_VERTICAL);
    	fEQSliders[i]->SetModificationMessage(new BMessage(MSG_EQ_CHANGED));
    	fEQSliders[i]->SetValue(0);
    
    	BStringView* lbl = new BStringView(NULL, freqLabels[i]);
    	lbl->SetFontSize(9);
    
    	bandGroup->AddChild(fEQSliders[i]);
    	bandGroup->AddChild(lbl);
    	fEQContainer->AddChild(bandGroup);
	}


	// Limiter Section
    BGroupView* limitGroup = new BGroupView(B_VERTICAL, 5);
    BStringView* lTitle = new BStringView(NULL, "Limiter");
    lTitle->SetFont(be_bold_font);
    limitGroup->AddChild(lTitle);

    fLimitInput = new WheelSlider("limit_in", "In", new BMessage(MSG_EQ_CHANGED), -20, 20, B_HORIZONTAL);
    fLimitInput->SetModificationMessage(new BMessage(MSG_EQ_CHANGED));

    fLimitLimit = new WheelSlider("limit_thr", "Lmt", new BMessage(MSG_EQ_CHANGED), -20, 0, B_HORIZONTAL);
    fLimitLimit->SetModificationMessage(new BMessage(MSG_EQ_CHANGED));

    fLimitRelease = new WheelSlider("limit_rel", "Rel", new BMessage(MSG_EQ_CHANGED), 10, 1000, B_HORIZONTAL);
    fLimitRelease->SetModificationMessage(new BMessage(MSG_EQ_CHANGED));

    limitGroup->AddChild(fLimitInput);
    limitGroup->AddChild(fLimitLimit);
    limitGroup->AddChild(fLimitRelease);
    fEQContainer->AddChild(limitGroup);
	

	for (int i = 0; i < 10; i++) {
    	fEQSliders[i]->SetValue((int32)cfg.eqBands[i]);
	}

	fLimitInput->SetValue((int32)cfg.limitIn);
	fLimitLimit->SetValue((int32)cfg.limitLmt);
	fLimitRelease->SetValue((int32)cfg.limitRel);
	UpdateMPVFilters();


         

    // --- Layout ---
BLayoutBuilder::Group<>(configGroup, B_VERTICAL, 15) 
    .SetInsets(23)
    .AddGroup(B_HORIZONTAL, 5) 
        .Add(qualityLabel)    
        .Add(qualityField)    
        .AddGlue()
    .End()
    .Add(chkNotify)
    .Add(fSizeContainer) 
    .Add(chkShuffle)
    .Add(fShuffleFavsCheckbox)
    .Add(chkTheme)
    .Add(fEQToggle)
    .Add(fEQContainer)
    //.Add(fSpectrum)

     #ifdef USE_PROJECTM
    .Add(fPresetToggle)
    .Add(fPresetScroll) 
    .Add(fVisualsCheckbox)
    .Add(chkPresetTimer)
     #endif
    .AddGlue()
.End();



    // ==========================================
    // TAB 4: ABOUT VIEW
    // ==========================================
    BGroupView* aboutGroup = new BGroupView(B_VERTICAL, 10);
    aboutGroup->SetName("About");
    aboutGroup->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

    // 1. Header Styles
    BFont titleFont(be_bold_font);
    titleFont.SetSize(26.0);

    BFont boldFont(be_bold_font);
    boldFont.SetSize(14.0);

    // 2. Text Components
    BStringView* titleApp = new BStringView("abt_title", "SuperMusicThingy");
    titleApp->SetFont(&titleFont);
    titleApp->SetAlignment(B_ALIGN_CENTER);

    BStringView* txtVer = new BStringView("abt_ver", "Version 1.0.0 (Haiku)");
    txtVer->SetAlignment(B_ALIGN_CENTER);
    
  	ClickableURL* txturl = new ClickableURL("abt_url", 
    	"Source Available Online (click me!)", 
    	"https://github.com/ablyssx74/HaikuSuperMusicThingy");
	txturl->SetAlignment(B_ALIGN_CENTER);  

    BStringView* txtCopy = new BStringView("abt_copy", "Copyright " B_UTF8_COPYRIGHT " 2026 Kris Beazley");
    txtCopy->SetAlignment(B_ALIGN_CENTER);
    
    BStringView* txtEmail = new BStringView("abt_mail", "jb@epluribusunix.net");
    txtEmail->SetAlignment(B_ALIGN_CENTER);
    
    BStringView* txtAI = new BStringView("abtAI", "AI Assisted");
    txtAI->SetAlignment(B_ALIGN_CENTER);

    // 3. Credits List
    BStringView* txtCredit = new BStringView("abt_cred", "Powered By:");
    txtCredit->SetFont(&boldFont);
    txtCredit->SetAlignment(B_ALIGN_CENTER);

    BStringView* c1 = new BStringView("c1", "SomaFM (Radio Service)");
    BStringView* c2 = new BStringView("c2", "MPV (Playback Core)");
    BStringView* c3 = new BStringView("c3", "nlohmann/json (The Data)");
    BStringView* c4 = new BStringView("c4", "Haiku Interface Kit (The GUI)");
    BStringView* c5 = new BStringView("c5", "libsdl / projectM / OpenGL (The Visuals)");
    BStringView* c6 = new BStringView("c6", "SVGear (Scalable Vector Graphics)");
    BStringView* c7 = new BStringView("c7", "libcurl (Network/Streaming)");
    BStringView* c8 = new BStringView("c8", "ladspa (EQ/Limiter Effects)");
    BStringView* c9 = new BStringView("c9", "Some AI Assistance");   
    
    // Center the credits 
    c1->SetAlignment(B_ALIGN_CENTER);
    c2->SetAlignment(B_ALIGN_CENTER);
    c3->SetAlignment(B_ALIGN_CENTER);
    c4->SetAlignment(B_ALIGN_CENTER);
    c5->SetAlignment(B_ALIGN_CENTER);
    c6->SetAlignment(B_ALIGN_CENTER);
    c7->SetAlignment(B_ALIGN_CENTER);
    c8->SetAlignment(B_ALIGN_CENTER);
	c9->SetAlignment(B_ALIGN_CENTER);
	
	
    // 4. Layout
    BLayoutBuilder::Group<>(aboutGroup, B_VERTICAL, 5)
        .SetInsets(20)
        .AddGlue() // Pushes content to the middle
        .Add(titleApp)
        .Add(txtVer)
        .Add(txturl)
        .AddStrut(10)
        .Add(txtCopy)        
        .Add(txtEmail)
        .Add(txtAI)
        .AddStrut(30) // Spacer
        .Add(txtCredit)
        .AddStrut(5)
        .Add(c1)
        .Add(c2)
        .Add(c3)
        .Add(c4)
        .Add(c5)
        .Add(c6)
        .Add(c7)
        .Add(c8)
        .AddGlue()
    .End();

    // 3. Attach Tabs
    fTabView->AddTab(playerGroup);
    fTabView->AddTab(stationGroup); 
    fTabView->AddTab(favGroup);
    fTabView->AddTab(configGroup);
    fTabView->AddTab(aboutGroup); 

    // 4. Final Window Layout 
    BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
        .SetInsets(0)
        .Add(fTabView)
    .End();
    
    std::string configPath = std::string(getenv("HOME")) + "/config/settings/SuperMusicThingy/milk_presets/";
	PopulatePresetList(fPresetList, configPath.c_str());

    UpdateFavButtons();
    ApplyTheme(); 
    PopulateStationList();
    DownloadStationIcons();
    RefreshFavorites();
    if (cfg.showVisuals) {
        StartVisuals();
    }
}

void SuperMusicWindow::SendNotification(const char* songTitle) {
    if (!songTitle || strlen(songTitle) == 0) return;    
    std::string song = songTitle;
    if (song.find(currentStation) == 0) {
        song.erase(0, currentStation.length());
        size_t start = song.find_first_not_of(": -");
        if (start != std::string::npos) song = song.substr(start);
    }

    static const std::vector<std::string> skip_patterns = {
        "Generic", ".pls", "-pls", ".aac", "-aac", ".mp3", "-mp3"
    };
    for (const auto& pattern : skip_patterns) {
        if (song.find(pattern) != std::string::npos) return;
    }

    BNotification notify(B_INFORMATION_NOTIFICATION);
    notify.SetGroup("SuperMusicThingy");
    notify.SetTitle(currentStation.c_str());
    notify.SetContent(song.c_str());

    if (fAlbumArt && fAlbumArt->IsValid()) {        
        int dstW = cfg.notifyIconSize;
        int dstH = cfg.notifyIconSize;        

        BBitmap* scaledIcon = new BBitmap(BRect(0, 0, dstW - 1, dstH - 1), fAlbumArt->ColorSpace());        
        if (scaledIcon->IsValid()) {
            uint8* srcBits = (uint8*)fAlbumArt->Bits();
            uint32 srcBPR = fAlbumArt->BytesPerRow();
            int srcW = fAlbumArt->Bounds().IntegerWidth() + 1;
            int srcH = fAlbumArt->Bounds().IntegerHeight() + 1;
            
            uint8* dstBits = (uint8*)scaledIcon->Bits();
            uint32 dstBPR = scaledIcon->BytesPerRow();

            for (int y = 0; y < dstH; y++) {
                for (int x = 0; x < dstW; x++) {
                    int srcX = (x * srcW) / dstW;
                    int srcY = (y * srcH) / dstH;

                    uint32* srcPixel = (uint32*)(srcBits + (srcY * srcBPR) + (srcX * 4));
                    uint32* dstPixel = (uint32*)(dstBits + (y * dstBPR) + (x * 4));                    
                    
                    *dstPixel = *srcPixel;
                }
            }
            
            notify.SetIcon(scaledIcon);
            delete scaledIcon; 
        }
    }

    notify.Send();
}

void SuperMusicWindow::UpdateUI() {
    //if (fStationView) fStationView->SetText(currentStation.c_str());
    
    float scale = be_plain_font->Size() / 12.0f;
    
    BFont smallFont(be_bold_font);
    smallFont.SetSize(12.0f * scale); 
    
    
    if (fSongView) {
        fSongView->SetText("Buffering...");
        fSongView->SetFontAndColor(&smallFont); 
    }

    if (fDescView) {
        fDescView->SetText(currentDesc.c_str());
        fDescView->SetFontAndColor(&smallFont);
    }

    BString lStr("Listeners: ");
    lStr << currentListeners.c_str();
    if (fListenersView) fListenersView->SetText(lStr.String());

    UpdateFavButtons();
}


void SuperMusicWindow::UpdateMPVFilters() {
    if (!fEQToggle || fEQToggle->Value() == B_CONTROL_OFF) {
        mpv_set_property_string(mpv, "af", "");
        return;
    }

    BString filterChain;
    filterChain = "@bouncy:lavfi=[";

    BString eqPart;
    eqPart << "ladspa=file='/boot/system/lib/ladspa/mbeq_1197.so':p=mbeq:c=";
    
    for (int i = 0; i < 10; i++) {
        BString val;
        val.SetToFormat("%.2f", (float)fEQSliders[i]->Value());
        eqPart << val << (i == 9 ? "" : "|");
    }
    eqPart << "|0|0|0|0|0,";
    filterChain << eqPart;

    BString limiterPart;
    limiterPart.SetToFormat("ladspa=file='/boot/system/lib/ladspa/fast_lookahead_limiter_1913.so':p=fastLookaheadLimiter:c=%.2f|%.2f|%.2f,",
        (float)fLimitInput->Value(), 
        (float)fLimitLimit->Value(), 
        (float)fLimitRelease->Value() / 1000.0f);
    filterChain << limiterPart;

	//filterChain << "astats=metadata=1:reset=1]"; 

	if (cfg.showSpectrumVisuals) {
    	filterChain << "astats=metadata=1:reset=1]"; 
		} else {
    	filterChain << "]";
	}


    int error = mpv_set_property_string(mpv, "af", filterChain.String());
    if (error < 0) {
        fprintf(stderr, ">> MPV Filter Failed: %s\n", mpv_error_string(error));
    }
}



void SuperMusicWindow::MessageReceived(BMessage* message)
{
    switch (message->what) {
        
        
        case MSG_UPDATE_BITRATE: {
    		int32 kbps;
    		if (message->FindInt32("kbps", &kbps) == B_OK) {
        		BString qStr("Quality: ");
        		qStr << kbps << "k";
        		if (fquality) fquality->SetText(qStr.String());
    		}
    		break;
		}	

        
		case MSG_ADD_FAV: {
    		if (is_favorite()) {        
         		delete_favorite(); 
   		 	} else {        
        		save_favorite(); 
   			 }
    		RefreshFavorites(); 
    		UpdateFavButtons(); 
    		break;
			}
			
			case MSG_DEL_FAV: { 
    			int32 index = fFavList->CurrentSelection();
    			if (index >= 0) {
        			StationItem* item = (StationItem*)fFavList->ItemAt(index);
        			if (item) {           
            			std::string stationToDelete = item->GetChannel().title;
            			std::string savedCurrent = currentStation;
            			currentStation = stationToDelete;
            			delete_favorite(); 
            			currentStation = savedCurrent;
            			RefreshFavorites(); 
        			}
    			}
    			break;
			}

		case MSG_PLAY_FAV: {
    		int32 index = message->GetInt32("index", -1);
    		if (index < 0 && fFavList) {
        		index = fFavList->CurrentSelection();
    		}

    		if (index >= 0) {
                StationItem* item = dynamic_cast<StationItem*>(fFavList->ItemAt(index));
        		if (item) {           
            		this->PlayStation(item->GetChannel());
        		}
    		}
    		
            BString lStr("Listeners: ");
            lStr << currentListeners.c_str();
            if (fListenersView) fListenersView->SetText(lStr.String());
            
    		this->UpdateUI();
    		break;
		}
		
		case MSG_SHUFFLE_FAVS_CHANGED: {
    		cfg.shuffleFavsOnly = (fShuffleFavsCheckbox->Value() == B_CONTROL_ON);
    		save_config(); 
    		break;
		}

		case MSG_SHUFFLE: {
    		if (cfg.shuffleFavsOnly) {
        		play_favorite();
    		} else {
        		play_random();
    		}
    		this->UpdateUI();
    		break;
		} 
            
        case MSG_UPDATE_SONG: {
            const char* song = message->GetString("song", "Unknown");
            if (fSongView) fSongView->SetText(song);
            if (cfg.showNotifications) {
        		SendNotification(song);
   			 }
            
            break;
        }
        
        case MSG_CFG_AUTO_SHUFFLE: {
        BCheckBox* chk = dynamic_cast<BCheckBox*>(FindView("chk_shuffle"));
        	if (chk) {
            	cfg.autoShuffle = (chk->Value() == B_CONTROL_ON);
            	save_config(); 
        	}
        	break;
    	}              
    	
    	case MSG_CFG_AUTO_PresetTimer: {
        BCheckBox* chk = dynamic_cast<BCheckBox*>(FindView("chk_PresetTimer"));
        	if (chk) {
            	cfg.autoShuffleVisuals = (chk->Value() == B_CONTROL_ON);
            	save_config(); 
        	}
        	break;
    	}

    	case MSG_CFG_NOTIFY: {
        BCheckBox* chk = dynamic_cast<BCheckBox*>(FindView("chk_notify"));
        if (chk) {
            cfg.showNotifications = (chk->Value() == B_CONTROL_ON);            
            if (cfg.showNotifications)
                fSizeContainer->Show();
            else
                fSizeContainer->Hide();
            InvalidateLayout();

            	save_config();
        	}
        	break;
    	}
    	
    	case MSG_TOGGLE_EQ: {
    		BCheckBox* chk = dynamic_cast<BCheckBox*>(FindView("eq_toggle"));
    		if (chk) {
        		cfg.eqEnabled = (chk->Value() == B_CONTROL_ON);            
        		if (cfg.eqEnabled)
            		fEQContainer->Show();
        		else
            		fEQContainer->Hide();
        
        		InvalidateLayout();
        		save_config();
        		UpdateMPVFilters(); 
    		}
    		break;
		}


    	
		case MSG_PLAY_STATION: {
    		int32 index = fStationList->CurrentSelection();
    		if (index >= 0) {
        		StationItem* item = (StationItem*)fStationList->ItemAt(index);
        		this->PlayStation(item->GetChannel()); 

            	BString lStr("Listeners: ");
            	lStr << currentListeners.c_str();
            	if (fListenersView) fListenersView->SetText(lStr.String());
        
        		this->UpdateUI();
    		}
    		break;
		}

        case MSG_CFG_ICON_SIZE: {
    		int32 newSize;
    		if (message->FindInt32("val", &newSize) == B_OK) {
        		cfg.notifyIconSize = newSize;
        		save_config(); 
    		}
    		break;
		}

    	case MSG_CFG_QUALITY: {
        const char* val;
        if (message->FindString("val", &val) == B_OK) {
            cfg.quality = val;
            save_config();
            
            BString qStr("Quality: ");
            qStr << get_bitrate_text().c_str();
            if (fquality) fquality->SetText(qStr.String());
            
        	}
        	break;
    	}
 
       case MSG_CFG_THEME: {
        BCheckBox* chk = dynamic_cast<BCheckBox*>(FindView("chk_theme"));
        if (chk) {
            cfg.updateTheme = (chk->Value() == B_CONTROL_ON) ? "Dark" : "Default";
            save_config();
  			ApplyTheme(); 
        	}
        	break;
    	}            

		case MSG_UPDATE_ART: {
    		BBitmap* newArt;
    		if (message->FindPointer("bitmap", (void**)&newArt) == B_OK) {
        		if (Lock()) {
            		fArtCache[currentStationID] = newArt;
            		fAlbumArt = newArt;
            		if (fArtView) {
                		((AlbumArtView*)fArtView)->SetBitmap(fAlbumArt);
            		}
            		Unlock();
        		}
    		}
    		break;
		}

        case MSG_STOP:
            mpv_command_string(mpv, "stop");
            if (fSongView) fSongView->SetText("Stopped");
            break;
            
		case MSG_PAUSE: {
    		mpv_command_string(mpv, "cycle pause");
			const char* song = message->GetString("song", "Unknown");
    		int is_paused = 0;
    		mpv_get_property(mpv, "pause", MPV_FORMAT_FLAG, &is_paused);

    		if (fSongView) {
        		if (is_paused) {
            		fSongView->SetText("Paused");
        		} else {
            		char* current_title = mpv_get_property_string(mpv, "media-title");
            		if (current_title) {
                		song = current_title; 
                		mpv_free(current_title);
            		}
            		fSongView->SetText(song);
        		}
    		}
    		break;
		}  
		
		case MSG_PLAY: { 
    		int is_paused = 0;
    		mpv_get_property(mpv, "pause", MPV_FORMAT_FLAG, &is_paused);
    		const char* song = message->GetString("song", "Unknown");
    		if (is_paused) {
       		 mpv_command_string(mpv, "set pause no");        
            		char* current_title = mpv_get_property_string(mpv, "media-title");
            		if (current_title) {
                		song = current_title; 
                		mpv_free(current_title);
            		}
            		fSongView->SetText(song);
    		} 
    		else if (fStationList->CurrentSelection() < 0) {
        		fTabView->Select(1); 
    		} 
    		else {
        		int32 index = fStationList->CurrentSelection();
        		StationItem* item = (StationItem*)fStationList->ItemAt(index);
        		if (item) {
            		this->PlayStation(item->GetChannel());   
            		this->UpdateUI();
        		}
    		}
    		break;
		}              
    		
        case MSG_VOL_CHANGE: {
            if (fVolumeSlider) {
                int32 value = fVolumeSlider->Value();
                double vol = (double)value;
                mpv_set_property(mpv, "volume", MPV_FORMAT_DOUBLE, &vol);
            }
            break;
        }
  
  
//--------------------------------- Proectm         
        #ifdef USE_PROJECTM
        case MSG_TOGGLE_VISUALS:
        {
            int32 value = 0;
            if (message->FindInt32("be:value", &value) == B_OK) {
                cfg.showVisuals = (value == B_CONTROL_ON);
                if (cfg.showVisuals) {
                    StartVisuals(); 
                } else {
                    StopVisuals();
                }
            }
            break;
        }       
        
//--------------------------------- Proectm           
		case MSG_REFRESH_PRESETS: {
    		const char* home = getenv("HOME");
    		if (home && fPresetList) {
        		std::string path = std::string(home) + "/config/settings/SuperMusicThingy/milk_presets/";
        		PopulatePresetList(fPresetList, path.c_str());
    		}
    		break;
		}
		
		
//--------------------------------- Proectm   		
		case MSG_PRESET_SELECTED: {
    		int32 index = fPresetList->CurrentSelection();
    		if (index >= 0) {
        		BStringItem* item = (BStringItem*)fPresetList->ItemAt(index);
        		load_specific_preset(item->Text()); 
        		if (chkShuffle) chkShuffle->SetValue(B_CONTROL_OFF);
        		cfg.autoShuffle = false;
        		cfg.autoShuffleVisuals = false;
    		}
    		break;
		}
		
//--------------------------------- Proectm   
		case MSG_TOGGLE_PRESETS: {
    		bool show = (fPresetToggle->Value() == B_CONTROL_ON);    
    		if (show) {
        		fPresetScroll->Show();
    		} else {
        		fPresetScroll->Hide();    	
        	}    
    		InvalidateLayout();
    		break;
		}
		#endif
//--------------------------------- Proectm     
		
		case MSG_EQ_CHANGED: {
    		cfg.eqEnabled = (fEQToggle->Value() == B_CONTROL_ON);
    		for(int i=0; i<10; i++) cfg.eqBands[i] = fEQSliders[i]->Value();
    		cfg.limitIn = fLimitInput->Value();
    		cfg.limitLmt = fLimitLimit->Value();
    		cfg.limitRel = fLimitRelease->Value();
    
    		UpdateMPVFilters();
    		save_config(); 
    		break;
		}

          
		case MSG_UPDATE_BOUNCE: {
    		double level;
    		if (message->FindDouble("level", &level) == B_OK) {
        		fSpectrum->UpdateLevel(level);
    		}
    		break;
		}		

        case MSG_AUDIO_READY:
            UpdateMPVFilters(); 
            break;


        case B_QUIT_REQUESTED:
            be_app->PostMessage(B_QUIT_REQUESTED);
            break;

        default:
            BWindow::MessageReceived(message);
            break;
    }
}

void RecursiveColorApply(BView* view, rgb_color bg, rgb_color txt) {
    if (!view) return;

    view->SetViewColor(bg);
    view->SetLowColor(bg);
    view->SetHighColor(txt);
    
    BSlider* slider = dynamic_cast<BSlider*>(view);
    if (slider) {
        slider->UseFillColor(true, &txt);
    }

    BStringView* stringView = dynamic_cast<BStringView*>(view);
    if (stringView) {
        stringView->SetHighColor(txt);
    }

    BTextView* textView = dynamic_cast<BTextView*>(view);
    if (textView) {
        textView->SetFontAndColor(NULL, B_FONT_ALL, &txt);
    }

    BListView* listView = dynamic_cast<BListView*>(view);
    if (listView) {
        listView->SetViewColor(bg);
        listView->SetLowColor(bg);
        listView->SetHighColor(txt);        
 
        for (int32 i = 0; i < listView->CountItems(); i++) {
            listView->InvalidateItem(i);
        }
         listView->Invalidate();
    }

    BCheckBox* checkBox = dynamic_cast<BCheckBox*>(view);
    if (checkBox) {
        checkBox->SetHighColor(txt);
    }

    view->Invalidate();

    for (int32 i = 0; i < view->CountChildren(); i++) {
        RecursiveColorApply(view->ChildAt(i), bg, txt);
    }
}


class FavItem : public BStringItem {
public:
    FavItem(const char* text) : BStringItem(text) {}

    void DrawItem(BView* owner, BRect frame, bool complete = false) override {
        rgb_color bg;        
        if (IsSelected()) {
            bg = ui_color(B_LIST_SELECTED_BACKGROUND_COLOR);
        } else {
            bg = owner->ViewColor(); 
        }

        if (IsSelected() || complete) {
            owner->SetHighColor(bg);
            owner->FillRect(frame);
        }

        rgb_color txt;
        if (IsSelected()) {
            txt = ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR);
        } else {
            int brightness = (bg.red + bg.green + bg.blue) / 3;
            if (brightness < 128) 
                txt = {255, 255, 255, 255};
            else 
                txt = {0, 0, 0, 255};
        }
        owner->SetHighColor(txt);
        owner->MovePenTo(frame.left + 5, frame.bottom - 3); 
        owner->DrawString(Text());
    }
};


void SuperMusicWindow::ApplyTheme() {
    rgb_color bgVal;
    rgb_color txtVal;

    if (cfg.updateTheme == "Dark") {
        bgVal = {40, 40, 40, 255};      // Dark Grey
        txtVal = {255, 255, 255, 255};  // Pure White
    } else {
        bgVal = ui_color(B_PANEL_BACKGROUND_COLOR);
        txtVal = ui_color(B_PANEL_TEXT_COLOR);
    }

    float scale = be_bold_font->Size() / 12.0f; 

    BFont boldFont(be_bold_font);
    boldFont.SetSize(12.0 * scale);

    if (Lock()) {
        if (fTabView) {
            fTabView->SetViewColor(bgVal);
            
            for (int32 i = 0; i < fTabView->CountTabs(); i++) {
                BView* tabView = fTabView->ViewForTab(i);
                RecursiveColorApply(tabView, bgVal, txtVal);
            }
        }
        
	    if (fDescView) {
    		fDescView->SetViewColor(bgVal);
    		fDescView->SetFontAndColor(&boldFont, B_FONT_ALL, &txtVal);
    		fDescView->Invalidate();
		}
		
		if (fSongView) {
			fSongView->SetViewColor(bgVal);
        	fSongView->SetFontAndColor(&boldFont, B_FONT_ALL, &txtVal);
        	fSongView->Invalidate();
    	}
  
        
        if (fStationList) {    
   			 fStationList->SetFlags(fStationList->Flags() | B_FRAME_EVENTS);
   			 
       		 if (fStationList->Parent()) {
            	fStationList->SetViewColor(bgVal); 
            	fStationList->SetLowColor(bgVal);
            	fStationList->SetHighColor(txtVal); 
            	fStationList->Invalidate();
        	}
        }
        
        if (fFavList) {
            fFavList->SetViewColor(bgVal);
            fFavList->SetLowColor(bgVal);
            fFavList->SetHighColor(txtVal); 
            fFavList->Invalidate(); 
        }    
        if (fBtnAddFav) {
    		fBtnAddFav->SetViewColor(bgVal);
    		fBtnAddFav->SetHighColor(txtVal); 
    		fBtnAddFav->Invalidate();
		}    

        if (fTabView) fTabView->Invalidate();
        Unlock();
    }
}


void SuperMusicWindow::RefreshFavorites() {
    if (!fFavList) return;
    fFavList->MakeEmpty();

    BPath path;
    if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
        path.Append("SuperMusicThingy/favorites.txt");
        
        std::ifstream infile(path.Path());
        if (infile.is_open()) {
            std::string line;
            while (std::getline(infile, line)) {
                if (line.empty()) continue;

                for (const auto& ch : channels) {
                    std::string chUrl = BASE_URL + ch.id + ".pls";
                    if (chUrl == line) {
                        StationItem* item = new StationItem(ch);
                        if (fIconCache.count(ch.id) > 0) {
                            item->SetIcon(new BBitmap(fIconCache[ch.id]));
                        }
                        
                        fFavList->AddItem(item); 
                        break; 
                    }
                }
            }
            infile.close();
        }
    }
}



void SuperMusicWindow::UpdateFavButtons() {
    bool isFav = is_favorite();        
    if (fBtnAddFav) {
        IconButton* favBtn = dynamic_cast<IconButton*>(fBtnAddFav);
        if (favBtn) {
            favBtn->SetFavorite(isFav);
        }        
        fBtnAddFav->SetEnabled(true); 
    }
}



SuperMusicWindow::~SuperMusicWindow()
{
    for (auto& [id, bitmap] : fIconCache) {
        if (bitmap) {
            delete bitmap;
            bitmap = nullptr;
        }
    }
    fIconCache.clear();

    // Clean up art cache safely
    for (auto& [id, bitmap] : fArtCache) {
        if (bitmap) {
            delete bitmap;
            bitmap = nullptr;
        }
    }
    fArtCache.clear();
    
    fAlbumArt = nullptr; 

    #if ENABLE_VISUALIZER
    cleanup_capture_device();
    
    if (glContext) { 
        SDL_GL_MakeCurrent(visualWin, NULL); 
        SDL_GL_DeleteContext(glContext); 
        glContext = nullptr; 
    }
    if (visualWin) { 
        SDL_DestroyWindow(visualWin); 
        visualWin = nullptr; 
    }
    if (pm) {
        projectm_destroy(pm);
        pm = nullptr;
    }
    #endif
}



class SuperMusicApp : public BApplication {
public:
    SuperMusicApp() : BApplication("application/x-vnd.HaikuSuperMusicThingy") {}

    virtual void ReadyToRun() {
        load_config();
        fetch_channels();
        init_mpv();
        #ifdef USE_PROJECTM
  		if (visualsRunning) {
        thread_id visualThread = spawn_thread(VisualsThread, "VisualsLoop", B_NORMAL_PRIORITY, NULL);
        resume_thread(visualThread);
   		}
		#endif

        gGuiWindow = new SuperMusicWindow();      
        gGuiWindow->Show();
        
        thread_id mpvThread = spawn_thread(mpv_loop_thread, "mpv_event_loop", 
    	B_NORMAL_PRIORITY, gGuiWindow);
    	resume_thread(mpvThread);
    	
    	if (cfg.autoShuffle) {
        gGuiWindow->PostMessage(MSG_SHUFFLE);
    }    	
}
     
    
virtual bool QuitRequested() {           	   	
    	mpvthread_running = false;
        if (mpv) {
            mpv_terminate_destroy(mpv);
        }
        save_config();
        return true;
    }
};


int32 mpv_loop_thread(void* data) {
    SuperMusicWindow* win = (SuperMusicWindow*)data;
    int32 lastBitrate = 0;

    while (mpvthread_running) {
        if (notifyTimer > 0 && std::time(nullptr) >= notifyTimer) {
            currentSong = pendingSong;
            pendingSong = "";
            notifyTimer = 0; 
            if (win) {
                BMessage msg(MSG_UPDATE_SONG);
                msg.AddString("song", currentSong.c_str());
                win->PostMessage(&msg);
            }
        }

        mpv_event *event = mpv_wait_event(mpv, 0.02); 
        if (event->event_id == MPV_EVENT_NONE) continue;
        if (event->event_id == MPV_EVENT_SHUTDOWN) break;
        
        if (event->event_id == MPV_EVENT_PROPERTY_CHANGE) {
            mpv_event_property *prop = (mpv_event_property *)event->data;
            if (!prop || !prop->name || !prop->data) continue;
            
            std::string propName = prop->name;
            if (propName == "media-title") {
                char* title_ptr = *(char **)prop->data;
                if (title_ptr) {
                    std::string newTitle = title_ptr;
                    if (newTitle.find("http") != 0 && newTitle != currentSong) {
                        pendingSong = newTitle;
                        notifyTimer = std::time(nullptr) + 2;
                    }
                }
            }
            else if (propName == "audio-bitrate") {
                double bps = *(double*)prop->data;
                int32 kbps = (int32)(bps / 1000);
                if (kbps > 0 && kbps != lastBitrate && win) {
                    lastBitrate = kbps;
                    BMessage msg(MSG_UPDATE_BITRATE);
                    msg.AddInt32("kbps", kbps);
                    win->PostMessage(&msg);
                }
            }
            else if (propName == "audio-params") {
                if (prop->format == MPV_FORMAT_NODE && win) {
                    win->PostMessage(MSG_AUDIO_READY);
                }
            }
            else if (propName == "af-metadata/bouncy") {
                if (prop->format == MPV_FORMAT_NODE) {
                    mpv_node* node = (mpv_node*)prop->data;
                    if (node->format == MPV_FORMAT_NODE_MAP) {
            			for (int i = 0; i < node->u.list->num; i++) {
                			if (strstr(node->u.list->keys[i], "Peak_level")) {
                    			double peak = atof(node->u.list->values[i].u.string);
                                if (win) {
                                    BMessage bounce(MSG_UPDATE_BOUNCE);
                                    bounce.AddDouble("level", peak);
                                    win->PostMessage(&bounce);
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
    return 0;
}




bool SuperMusicWindow::QuitRequested() {
    if (mpv) {
        mpv_command_string(mpv, "quit");
    }
    StopVisuals();
    snooze(100000); 
    be_app->PostMessage(B_QUIT_REQUESTED);
    
    return true; 
}


int main() {
	std::srand(std::time(nullptr)); 
	ensure_config_dir();
    SuperMusicApp app;   
    app.Run();    
    return 0;
}

