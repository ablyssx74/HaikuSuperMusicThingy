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


// --- Haiku Storage Kit ---
#include <Path.h>
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
    MSG_VOL_CHANGE = 'vchg',
    MSG_UPDATE_SONG = 'updt', 
    MSG_UPDATE_ART = 'dart',    
    MSG_ADD_FAV     = 'adfv', 
    MSG_DEL_FAV     = 'dlfv',
    MSG_PLAY_FAV    = 'plfv',
    MSG_CFG_AUTO_SHUFFLE = 'c_as',
    MSG_CFG_AUTO_PresetTimer = 'c_pt',
    MSG_CFG_NOTIFY       = 'c_nt',
    MSG_CFG_QUALITY      = 'c_qu',
    MSG_CFG_THEME        = 'c_th',
    MSG_PLAY_STATION     = 'plst', 
    MSG_TOGGLE_VISUALS   = 'tvis',
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
                // Heart is a favorite: Solid/Bright
                SetDrawingMode(B_OP_ALPHA);
                SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
            } else {
                // Heart is NOT a favorite: Grayed/Blended
                SetDrawingMode(B_OP_BLEND);
            }
        } else {
            // Standard buttons (Stop/Shuffle): Always Solid/Bright
            SetDrawingMode(B_OP_ALPHA);
            SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
            
            // Optional: If the button is actually disabled, then blend it
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
            // Convert the raw memory buffer into a Haiku BBitmap
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
                    channels.push_back({
                        ch.value("title", ""),
                        ch.value("id", ""),
                        ch.value("description", ""),
                        ch.value("listeners", "0"),
                        ch.value("largeimage", ""),
                        ch.value("image", "")
                    });
                }
            } catch(...) {}
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
    
    	BFont font;
    	owner->GetFont(&font);
    	float oldSize = font.Size();
    	font.SetSize(10.0);
    	owner->SetFont(&font);
    
    	rgb_color descColor = textColor;
    	if (!IsSelected()) {
        	descColor.alpha = 180; // Slight transparency for visual hierarchy
        	owner->SetDrawingMode(B_OP_ALPHA);
    	} else {
        	owner->SetDrawingMode(B_OP_OVER);
    	}
    
    	owner->SetHighColor(descColor);
    	owner->MovePenTo(frame.left + textOffset, frame.top + 32);
    	owner->DrawString(fChannel.desc.c_str());
    
    	owner->SetDrawingMode(B_OP_COPY);
    	font.SetSize(oldSize);
    	owner->SetFont(&font);
	}

    virtual void Update(BView* owner, const BFont* font) override {
        SetHeight(42.0); // Slightly taller for breathing room
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
    	this->SetExplicitMinSize(BSize(300, 300));
    	this->SetExplicitMaxSize(BSize(300, 300));
    	this->SetExplicitPreferredSize(BSize(300, 300)); 
        
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


struct Config {
    bool showNotifications = true;
    bool showVisuals = false;
    bool autoShuffle = false;
    bool autoShuffleVisuals = false;
    bool autoVsync = false;
    bool shuffleFavsOnly = false;
    //int defaultVolume = 75;
    std::string updateTheme = "Dark";
    std::string quality = "Highest";
} cfg;

int selectedConfig = 0;


void save_config() {
    json j;
    j["quality"] = cfg.quality;
    j["updateTheme"] = cfg.updateTheme;
    j["showNotifications"] = cfg.showNotifications;
    j["autoShuffle"] = cfg.autoShuffle;
    j["autoShuffleVisuals"] = cfg.autoShuffleVisuals;
    j["shuffleFavsOnly"] = cfg.shuffleFavsOnly;
    j["autoVsync"] = cfg.autoVsync;
    j["showVisuals"] = cfg.showVisuals;
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
                cfg.quality = j.value("quality", "highest");
                cfg.updateTheme = j.value("updateTheme", "Dark");
                cfg.showNotifications = j.value("showNotifications", true);
                cfg.autoShuffle = j.value("autoShuffle", false);
                cfg.autoShuffleVisuals = j.value("autoShuffleVisuals", false);
                cfg.autoVsync = j.value("autoVsync", false);
                cfg.showVisuals = j.value("showVisuals", false);   
                cfg.shuffleFavsOnly = j.value("shuffleFavsOnly", false);              
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
}
    
    
    
void fade_volume(mpv_handle *mpv, double target_vol, double duration_ms) {
        double current_vol;
        mpv_get_property(mpv, "volume", MPV_FORMAT_DOUBLE, &current_vol);

        int steps = 20; // Number of small volume jumps
        double step_size = (target_vol - current_vol) / steps;
        int step_duration = (int)(duration_ms * 1000 / steps); // in microseconds

        for (int i = 0; i < steps; ++i) {
            current_vol += step_size;
            mpv_set_property(mpv, "volume", MPV_FORMAT_DOUBLE, &current_vol);
            usleep(step_duration);
        	}

        mpv_set_property(mpv, "volume", MPV_FORMAT_DOUBLE, &target_vol);
   		}
    
       std::string get_quality_url(const std::string& id) {
        if (cfg.quality == "Highest") {
            return BASE_URL + id + ".pls";
        }
        if (cfg.quality == "High") {
            return BASE_URL + id + "64.pls";
        }
        if (cfg.quality == "Low") {
            return BASE_URL + id + "32.pls";
        }
        // Default: 128k AAC (id + "130.pls")
        return BASE_URL + id + ".pls";
    }

    std::string get_bitrate_text() {
        if (cfg.quality == "Highest") return "128k";
        if (cfg.quality == "High")    return "64k";
        if (cfg.quality == "Low")     return "32k";
        return "";
}

// Save Station to favorites list while listening
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

    if (favs.empty()) {
        return;
    }

    std::string url = favs[rand() % favs.size()];
    
    size_t lastSlash = url.find_last_of('/');
    size_t lastDot = url.find_last_of('.');
    if (lastSlash != std::string::npos && lastDot != std::string::npos) {
        std::string id = url.substr(lastSlash + 1, lastDot - lastSlash - 1);
        
        for (const auto& ch : channels) {
            if (ch.id == id) {
                currentStation = ch.title;
                currentStationID = ch.id; 
                currentDesc = ch.desc;
                currentListeners = ch.listeners;
                currentAlbumArtUrl = ch.largeimage;

                if (!currentAlbumArtUrl.empty()) {
                    if (gGuiWindow && gGuiWindow->fArtCache.count(currentStationID) > 0) {
                        if (gGuiWindow->Lock()) {
                            gGuiWindow->fAlbumArt = gGuiWindow->fArtCache[currentStationID];
                            if (gGuiWindow->fArtView)
                                ((AlbumArtView*)gGuiWindow->fArtView)->SetBitmap(gGuiWindow->fAlbumArt);
                            gGuiWindow->Unlock();
                        }
                    } else {
                        // CACHE MISS
                        std::thread([url = currentAlbumArtUrl]() {
                            download_art(url);
                        }).detach();
                    }
                }
                break;
            }
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

    const char *cmd[] = {"loadfile", url.c_str(), NULL};
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

    std::string url = get_quality_url(chan.id); 
    const char *cmd[] = {"loadfile", url.c_str(), NULL};
    mpv_command(mpv, cmd);    
    fade_volume(mpv, original_vol, 500);
    if (Lock()) {
        fTabView->Select(0);
        Unlock();
    }
}


// Delete Station from favorites list while listening
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
    
    std::string url = get_quality_url(chan.id);
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
                    case SDLK_v:
                        load_random_preset(pm);
                        lastPresetChange = SDL_GetTicks();
                        break;
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
                    case SDLK_x: {
                        const char* cmd_stop[] = {"stop", NULL};
                        mpv_command(mpv, cmd_stop);
                        break;
                    }
                    case SDLK_p: {
                        const char* cmd_pause[] = {"cycle", "pause", NULL};
                        mpv_command(mpv, cmd_pause);
                        break;
                    }
                    case SDLK_EQUALS:
                    case SDLK_KP_PLUS:
                        set_volume('+');
                        break;
                    case SDLK_MINUS:
                    case SDLK_KP_MINUS:
                        set_volume('-');
                        break;
                    case SDLK_k:
                    case SDLK_ESCAPE: {
                        uint32_t flags = SDL_GetWindowFlags(visualWin);
                        bool isFullscreen = (flags & SDL_WINDOW_FULLSCREEN_DESKTOP);
                        
                        // If ESC, only toggle if currently fullscreen
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


void SuperMusicWindow::UpdateStatus(const char* station, const char* song) {
    if (Lock()) {
        fStationView->SetText("");
        fSongView->SetText(song);
        Unlock();
    }
}




SuperMusicWindow::SuperMusicWindow()
    : BWindow(BRect(100, 100, 550, 300), "SuperMusicThingy", B_TITLED_WINDOW, 
              B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS | B_QUIT_ON_WINDOW_CLOSE)
{
    fAlbumArt = nullptr;
    
    BFont largeFont(be_bold_font);
    BFont smallFont(be_bold_font);
    BFont smallFont2(be_bold_font);
    
    largeFont.SetSize(18.0); 
    smallFont.SetSize(12.0); 
	smallFont2.SetSize(10.0);
	
    fTabView = new BTabView("tab_container");
    fTabView->SetExplicitMinSize(BSize(345, 685)); 

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
	fDescView->SetFont(&smallFont); 
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
	fArtView->SetExplicitSize(BSize(300, 300)); 
    fArtView->SetExplicitMinSize(BSize(300, 300));
	fArtView->SetExplicitMaxSize(BSize(300, 300));
    
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
	BMessage* msgHighest = new BMessage(MSG_CFG_QUALITY); msgHighest->AddString("val", "Highest");
	BMessage* msgHigh = new BMessage(MSG_CFG_QUALITY); msgHigh->AddString("val", "High"); 
	BMessage* msgLow  = new BMessage(MSG_CFG_QUALITY); msgLow->AddString("val", "Low");

	qualityMenu->AddItem(new BMenuItem("Highest", msgHighest));
	qualityMenu->AddItem(new BMenuItem("High", msgHigh));  
	qualityMenu->AddItem(new BMenuItem("Low", msgLow));

	BMenuItem* selectedItem = qualityMenu->FindItem(cfg.quality.c_str());
	if (selectedItem) selectedItem->SetMarked(true);

    BStringView* qualityLabel = new BStringView("lbl_qual", "Audio Quality:"); 

    BMenuField* qualityField = new BMenuField("quality_field", NULL, qualityMenu);

    // --- Checkboxes ---
    BCheckBox* fVisualsCheckbox = new BCheckBox(BRect(10, 10, 200, 30), "visuals_toggle", 
    "Enable Visualizer", new BMessage(MSG_TOGGLE_VISUALS));
    fVisualsCheckbox->SetValue(cfg.showVisuals ? B_CONTROL_ON : B_CONTROL_OFF);
    
    
	fShuffleFavsCheckbox = new BCheckBox("shuffle_favs", "Shuffle only favorites", 
    new BMessage(MSG_SHUFFLE_FAVS_CHANGED));
	fShuffleFavsCheckbox->SetValue(cfg.shuffleFavsOnly ? B_CONTROL_ON : B_CONTROL_OFF);

    
    BCheckBox* chkShuffle = new BCheckBox("chk_shuffle", "Auto Shuffle On Start", new BMessage(MSG_CFG_AUTO_SHUFFLE));
    chkShuffle->SetValue(cfg.autoShuffle ? B_CONTROL_ON : B_CONTROL_OFF);    
    
    BCheckBox* chkPresetTimer = new BCheckBox("chk_PresetTimer", "Auto Shuffle Visual Presets 30/s", new BMessage(MSG_CFG_AUTO_PresetTimer));
    chkPresetTimer->SetValue(cfg.autoShuffleVisuals ? B_CONTROL_ON : B_CONTROL_OFF);

    BCheckBox* chkNotify = new BCheckBox("chk_notify", "Show Notifications", new BMessage(MSG_CFG_NOTIFY));
    chkNotify->SetValue(cfg.showNotifications ? B_CONTROL_ON : B_CONTROL_OFF);
    
    BCheckBox* chkTheme = new BCheckBox("chk_theme", "Dark Theme", new BMessage(MSG_CFG_THEME));
    chkTheme->SetValue(cfg.updateTheme == "Dark" ? B_CONTROL_ON : B_CONTROL_OFF);
    
       

    // --- Layout ---
    BLayoutBuilder::Group<>(configGroup, B_VERTICAL, 10)
        .SetInsets(20)
        .AddGroup(B_HORIZONTAL, 5) 
            .Add(qualityLabel)    
            .Add(qualityField)    
            .AddGlue()
        .End()
        .Add(chkShuffle)
        .Add(fShuffleFavsCheckbox)
        .Add(chkPresetTimer)
        .Add(chkNotify)
        .Add(chkTheme)
        .Add(fVisualsCheckbox)
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

    BStringView* txtCopy = new BStringView("abt_copy", "Copyright " B_UTF8_COPYRIGHT " 2026 Kris Beazley");
    txtCopy->SetAlignment(B_ALIGN_CENTER);
    
    BStringView* txtEmail = new BStringView("abt_mail", "jb@epluribusunix.net");
    txtEmail->SetAlignment(B_ALIGN_CENTER);

    // 3. Credits List
    BStringView* txtCredit = new BStringView("abt_cred", "Powered By:");
    txtCredit->SetFont(&boldFont);
    txtCredit->SetAlignment(B_ALIGN_CENTER);

    BStringView* c1 = new BStringView("c1", "SomaFM (Radio Service)");
    BStringView* c2 = new BStringView("c2", "MPV (Playback Core)");
    BStringView* c3 = new BStringView("c3", "nlohmann/json (The Data)");
    BStringView* c4 = new BStringView("c4", "Haiku Interface Kit (The GUI)");
    BStringView* c5 = new BStringView("c5", "libsdl / projectM / OpenGL (The Visuals)");
    BStringView* c6 = new BStringView("c6", "libcurl (Network/Streaming)");
    BStringView* c7 = new BStringView("c7", "Some AI Assistance");   
    
    // Center the credits
    c1->SetAlignment(B_ALIGN_CENTER);
    c2->SetAlignment(B_ALIGN_CENTER);
    c3->SetAlignment(B_ALIGN_CENTER);
    c4->SetAlignment(B_ALIGN_CENTER);
    c5->SetAlignment(B_ALIGN_CENTER);
    c6->SetAlignment(B_ALIGN_CENTER);
    c7->SetAlignment(B_ALIGN_CENTER);

    // 4. Layout
    BLayoutBuilder::Group<>(aboutGroup, B_VERTICAL, 5)
        .SetInsets(20)
        .AddGlue() // Pushes content to the middle
        .Add(titleApp)
        .Add(txtVer)
        .AddStrut(10)
        .Add(txtCopy)
        .Add(txtEmail)
        .AddStrut(30) // Spacer
        .Add(txtCredit)
        .AddStrut(5)
        .Add(c1)
        .Add(c2)
        .Add(c3)
        .Add(c4)
        .Add(c5)
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

    // Clean up song title based on current station
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
        int dstW = 64;
        int dstH = 64;        

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
    
    BFont smallFont(be_bold_font);
    smallFont.SetSize(12.0);
    
    
    if (fSongView) {
        fSongView->SetText("Buffering...");
        fSongView->SetFontAndColor(&smallFont); 
    }

    if (fDescView) {
        fDescView->SetText(currentDesc.c_str());
        fDescView->SetFontAndColor(&smallFont);
    }

    BString qStr("Quality: ");
    qStr << cfg.quality.c_str() << " (" << get_bitrate_text().c_str() << ")";
    if (fquality) fquality->SetText(qStr.String());

    BString lStr("Listeners: ");
    lStr << currentListeners.c_str();
    if (fListenersView) fListenersView->SetText(lStr.String());

    UpdateFavButtons();
}


void SuperMusicWindow::MessageReceived(BMessage* message)
{
    switch (message->what) {
        
        // --- FAVORITES LOGIC ---
        
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
            			UpdateFavButtons(); 
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
    		
    		BString qStr("Quality: ");
            qStr << cfg.quality.c_str() << " (" << get_bitrate_text().c_str() << ")";
            if (fquality) fquality->SetText(qStr.String());

            BString lStr("Listeners: ");
            lStr << currentListeners.c_str();
            if (fListenersView) fListenersView->SetText(lStr.String());
            
    		UpdateFavButtons(); 
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
            	save_config();
        	}
        	break;
    	}
    	
		case MSG_PLAY_STATION: {
    		int32 index = fStationList->CurrentSelection();
    		if (index >= 0) {
        		StationItem* item = (StationItem*)fStationList->ItemAt(index);
        		this->PlayStation(item->GetChannel()); 
        		
        	BString qStr("Quality: ");
            qStr << cfg.quality.c_str() << " (" << get_bitrate_text().c_str() << ")";
            if (fquality) fquality->SetText(qStr.String());

            BString lStr("Listeners: ");
            lStr << currentListeners.c_str();
            if (fListenersView) fListenersView->SetText(lStr.String());
        
        		UpdateFavButtons(); 
    		}
    		break;
		}

		case MSG_PLAY: { 
    		if (fStationList->CountItems() > 0) {
        		StationItem* item = (StationItem*)fStationList->ItemAt(0);        
        		if (item) {
            		this->PlayStation(item->GetChannel());             
            		BString qStr("Quality: ");
            		qStr << cfg.quality.c_str() << " (" << get_bitrate_text().c_str() << ")";
            		if (fquality) fquality->SetText(qStr.String());

            		BString lStr("Listeners: ");
            		lStr << currentListeners.c_str();
            		if (fListenersView) fListenersView->SetText(lStr.String());
        
            		UpdateFavButtons();            
            		fStationList->Select(0);
        		}
    		}
    		break;
		}

    	
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

    	case MSG_CFG_QUALITY: {
        const char* val;
        if (message->FindString("val", &val) == B_OK) {
            cfg.quality = val;
            save_config();
            BString qStr("Quality: ");
            qStr << cfg.quality.c_str() << " (" << get_bitrate_text().c_str() << ")";
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
                
    		
        case MSG_VOL_CHANGE: {
            if (fVolumeSlider) {
                int32 value = fVolumeSlider->Value();
                double vol = (double)value;
                mpv_set_property(mpv, "volume", MPV_FORMAT_DOUBLE, &vol);
            }
            break;
        }

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

    BFont boldFont(be_bold_font);
    boldFont.SetSize(12.0);

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
    for (auto const& [id, bitmap] : fIconCache) {
        delete bitmap;
    }
    fIconCache.clear();

    for (auto const& [id, bitmap] : fArtCache) {
        delete bitmap;
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
    while (mpvthread_running) {
        if (notifyTimer > 0 && std::time(nullptr) >= notifyTimer) {
        	std::string songToSend = pendingSong; 
            currentSong = pendingSong;
            pendingSong = "";
            notifyTimer = 0; 

            if (win) {
                BMessage msg(MSG_UPDATE_SONG);
                msg.AddString("song", currentSong.c_str());
                win->PostMessage(&msg);
            }
        }

        mpv_event *event = mpv_wait_event(mpv, 0.05);        
        if (event->event_id == MPV_EVENT_NONE) continue;
        if (event->event_id == MPV_EVENT_SHUTDOWN) break;
        if (event->event_id == MPV_EVENT_PROPERTY_CHANGE) {
            mpv_event_property *prop = (mpv_event_property *)event->data;
            if (prop && prop->data && prop->name) {
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

