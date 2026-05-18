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
#include <ControlLook.h>
#include <NodeInfo.h>
#include <Deskbar.h>
#include <Dragger.h>
#include <MessageRunner.h>
#include <InterfaceDefs.h>
#include <Region.h>
#include <Slider.h>


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
#include <cmath>

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

#include "icons.h"
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


volatile bool fIsQuitting; 
std::string statusMsg = "";
std::time_t statusExpiry = 0;
bool mpvthread_running = true;
SuperMusicWindow* gGuiWindow = nullptr; 
int32 mpv_loop_thread(void* data);
using json = nlohmann::json;

class SuperMusicWindow; 




void ensure_config_dir() {
    BPath path;

    if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
        path.Append("SuperMusicThingy");
        if (create_directory(path.Path(), 0755) == B_OK) {
        } else {            
        }
    }
}


bool IsFFmpegLadspaAvailable() {
    FILE* fp = popen("ffmpeg -filters 2>/dev/null", "r");
    if (fp == NULL) return false;

    char buffer[256];
    bool found = false;
    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        // Look for the specific 'ladspa' filter line
        if (strstr(buffer, " ladspa ")) {
            found = true;
            break;
        }
    }
    pclose(fp);
    return found;
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





const float kPresetRock[] = {
    4.0, 3.5, 3.0, 2.5, 2.0, 1.0, -1.0, -1.0, 
    0.0, 1.0, 1.5, 2.0, 2.5, 3.5, 4.0
};

const float kPresetJazz[] = {
    3.0, 2.5, 2.0, 1.5, 1.0, 2.0, -1.0, -1.0, 
    -0.5, 0.0, 0.5, 1.0, 1.5, 2.5, 3.0
};

const float kPresetBass[] = {
    6.0, 5.5, 5.0, 4.0, 2.0, 1.0, 0.0, 0.0, 
    0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0
};

const float kPresetFlat[] = {
    0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 
    0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0
};


// Volatile State Tracker Variables
double user_base_volume = 100.0; // Captures slider adjustments
bool is_fading = false;
double fade_target_vol = 0.0;
double fade_start_vol = 0.0;
bigtime_t fade_start_time = 0;
bigtime_t fade_duration_us = 0;


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
	if (fIsQuitting) return; 
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
    bool showNotifications = false;
    bool showVisuals = false;
    bool autoShuffle = false;
    bool compactModeTitle = true;
    bool compactModeDesc = true;
    #ifdef USE_SYSTRAY
    bool sysTray = true;
    #else
    bool sysTray = false;
    #endif
    bool autoShuffleVisuals = false;
    bool showSpectrumVisuals = true;
    bool autoVsync = false;
    bool ladspaEnabled = false;
    bool shuffleFavsOnly = false;
    bool compactMode = false;
    int notifyIconSize = 64; 
    std::string updateTheme = "Default";
    std::string quality = "128k";
    bool eqEnabled = true;
    float eqBands[15] = {0.0f}; 
    float limitIn = 0.0f;
    float limitLmt = 0.0f;
    float limitRel = 100.0f; 
} cfg;

int selectedConfig = 0;


void save_config() {
    json j;
    j["quality"] = cfg.quality;
    j["compactMode"] = cfg.compactMode;
    j["notifyIconSize"] = cfg.notifyIconSize;
    j["updateTheme"] = cfg.updateTheme;
    j["showNotifications"] = cfg.showNotifications;
    j["autoShuffle"] = cfg.autoShuffle;
    j["sysTray"] = cfg.sysTray;
    j["compactModeTitle"] = cfg.compactModeTitle;
    j["compactModeDesc"] = cfg.compactModeDesc;
    j["ladspaEnabled"] = cfg.ladspaEnabled;
    j["autoShuffleVisuals"] = cfg.autoShuffleVisuals;
    j["showSpectrumVisuals"] = cfg.showSpectrumVisuals;
    j["shuffleFavsOnly"] = cfg.shuffleFavsOnly;
    j["autoVsync"] = cfg.autoVsync;
    j["showVisuals"] = cfg.showVisuals;
    j["eqEnabled"] = cfg.eqEnabled;
    json eqArray = json::array();
    for (int i = 0; i < 15; i++) {
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

    // 1. ALWAYS populate strict, hardcoded memory safe defaults first
    cfg.quality = "128k";
    cfg.notifyIconSize = 64;
    cfg.compactMode = false;
    cfg.compactModeTitle = true;
    cfg.compactModeDesc = true;
    cfg.updateTheme = "Default";
    cfg.showNotifications = false;
    cfg.autoShuffle = false;
#ifdef USE_SYSTRAY
    cfg.sysTray = true;
#else
    cfg.sysTray = false;
#endif
    cfg.autoShuffleVisuals = false;
    cfg.autoVsync = false;
    cfg.ladspaEnabled = false;
    cfg.showVisuals = false;   
    cfg.showSpectrumVisuals = true;   
    cfg.shuffleFavsOnly = false; 
    cfg.eqEnabled = true;
    for (int i = 0; i < 15; i++) {
        cfg.eqBands[i] = 0.0f; // flat EQ defaults
    }
    cfg.limitIn = 0.0f;
    cfg.limitLmt = 0.0f;
    cfg.limitRel = 100.0f;

    // 2. Attempt parsing over top of those defaults
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
                cfg.compactMode = j.value("compactMode", false);
                cfg.compactModeDesc = j.value("compactModeDesc", false);
                cfg.compactModeTitle = j.value("compactModeTitle", true);
                cfg.updateTheme = j.value("updateTheme", "Default");
                cfg.showNotifications = j.value("showNotifications", false);
                cfg.autoShuffle = j.value("autoShuffle", false);
                #ifdef USE_SYSTRAY
                cfg.sysTray = j.value("sysTray", true);
                #else
                cfg.sysTray = j.value("sysTray", false);
                #endif
                cfg.autoShuffleVisuals = j.value("autoShuffleVisuals", false);
                cfg.autoVsync = j.value("autoVsync", false);
                cfg.ladspaEnabled = j.value("ladspaEnabled", false);
                cfg.showVisuals = j.value("showVisuals", false);   
                cfg.showSpectrumVisuals = j.value("showSpectrumVisuals", true);   
                cfg.shuffleFavsOnly = j.value("shuffleFavsOnly", false); 
                cfg.eqEnabled = j.value("eqEnabled", true);                
                if (j.contains("eqBands") && j["eqBands"].is_array()) {
                     for (size_t i = 0; i < 15 && i < j["eqBands"].size(); i++) {
                        cfg.eqBands[i] = j["eqBands"][i].get<float>();
                    }
                }
                cfg.limitIn = j.value("limitIn", 0.0f);
                cfg.limitLmt = j.value("limitLmt", 0.0f);
                cfg.limitRel = j.value("limitRel", 100.0f);                                    
            } catch(...) {
                // Parsing failed, but defaults are safely assigned
            }
        } else {

            save_config(); 
        }
    }
}






class SpectrumView : public BView {
public:

    SpectrumView(BRect frame, const char* name)
        : BView(frame, name, B_FOLLOW_ALL, B_WILL_DRAW | B_FRAME_EVENTS | B_PULSE_NEEDED) {
        SetViewColor(B_TRANSPARENT_COLOR);       
       
        fCurrentLevel = -60.0;       
        fVisualizerMode = MODE_BARS; 
        fLastDataTime = 0;
        memset(frequencyData, 0, 64);
        fLeftScore = 0; 
        fRightScore = 0;
		// Initialize Motorcycle endless runner variables
		fMotoY = 0.0f;
		fMotoVelocityY = 0.0f;
		fMotoCrashTicks = 0;
		fMotoScore = 0;
		fStuntTextY = 0.0f;
		fStuntTextLife = 0;
		fStuntTextStr = "";


		
		// Force a staggering pipeline gap so obstacles do not stack on top of each other
		fObsX[0] = 340.0f;
		fObsIsPit[0] = false;
		fObsHeightScale[0] = 1.0f;

		fObsX[1] = 520.0f; // Positioned further right down the scrolling track line
		fObsIsPit[1] = true;
		fObsHeightScale[1] = 0.8f;
		
		// Initialize exhaust particle arrays to an inactive starting state
		for (int s = 0; s < 12; s++) {
    		fSparkLife[s] = 0;
    		fSparkX[s] = 0.0f;
    		fSparkY[s] = 0.0f;
    		fSparkDX[s] = 0.0f;
    		fSparkDY[s] = 0.0f;
		}
        
        // Mode 6 Parallax Scenery positions and sizes
		fMtnScrollX = 0.0f;
		for (int t = 0; t < 4; t++) {
    		fTreeX[t] = 50.0f + (t * 90.0f) + (rand() % 30);
    		fTreeHeight[t] = 12.0f + (rand() % 10);
		}
		// Mode 6 Cloud positions and scale profiles
		for (int c = 0; c < 3; c++) {
    		fCloudX[c] = (float)(rand() % 350);
    		fCloudY[c] = 4.0f + (rand() % 8); // Position safely in the upper ceiling sky margin
    		fCloudSize[c] = 16.0f + (rand() % 14); // Varying cloud widths
		}


        
        
        for (int i = 0; i < 64; i++) {
            fBarHeights[i] = 0.0f;
            fBarVelocities[i] = 0.0f;
            fPeakHeights[i] = 0.0f;
            fPeakHold[i] = 0;
            fArtworkPalette[i] = { (uint8)(40 + i * 2), 210, (uint8)(255 - i * 3), 255 };
        }
        srand(time(nullptr));
        
        for (int r = 0; r < 75; r++) {
    		fRainX[r] = (float)(rand() % 1000) / 1000.0f;
    		fRainY[r] = (float)(rand() % 1000) / 1000.0f;
    		fRainSpeed[r] = 0.01f + ((rand() % 100) / 10000.0f);
		}
		fRainInitNeedsPulse = false;

        // Initialize Ball 1 (Bass Sphere)
        fBallX[0] = frame.Width() * 0.25f;
        fBallY[0] = frame.Height() * 0.50f;
        fBallDX[0] = 3.5f;
        fBallDY[0] = -2.5f;
        fBallSize[0] = 12.0f;

        // Initialize Ball 2 (Treble Sphere)
        fBallX[1] = frame.Width() * 0.75f;
        fBallY[1] = frame.Height() * 0.50f;
        fBallDX[1] = -3.0f;
        fBallDY[1] = 3.5f;
        fBallSize[1] = 10.0f;
        
        // EasterEgg1
		fDogDrawActive = false;
		fDogDrawX = 0.0f;
		fDogDrawY = 0.0f;

    }
    
    virtual void MessageReceived(BMessage* message) override {
        switch (message->what) {
            // FIXED SYSTEM CONSTANT: Replaced B_MOUSE_WHEEL with proper Haiku naming convention
            case B_MOUSE_WHEEL_CHANGED: {
                if (fVisualizerMode == MODE_PONG_BALLS) {
                    float deltaY = 0.0f;
                    
                    // Extracts vertical scroll track directions seamlessly from internal systems
                    if (message->FindFloat("be:wheel_delta_y", &deltaY) == B_OK) {
                        // SENSITIVITY 
                        float scrollSensitivityMultiplier = 15.6f; 
                        fRightPaddlePos += deltaY * scrollSensitivityMultiplier;
                        
                        // Local execution limits keep the paddle inside boundaries smoothly
                        BRect b = Bounds();
                        float paddleH = 21.0f;
                        if (fRightPaddlePos < paddleH / 2.0f) fRightPaddlePos = paddleH / 2.0f;
                        if (fRightPaddlePos > b.Height() - (paddleH / 2.0f)) fRightPaddlePos = b.Height() - (paddleH / 2.0f);
                        
                        Invalidate(); // Updates rendering canvas targets natively
                    }
                }
                break;
            }
            default:
                BView::MessageReceived(message);
                break;
        }
    }

    
void UpdateLevel(double level) {
    if (!cfg.showSpectrumVisuals || !cfg.eqEnabled) return;

    bigtime_t now = system_time();

    // 1. NATIVE AUDIO BUFFER TRACKING ENGINE
    static bigtime_t sLastCallbackTime = 0;
    static bigtime_t sSmoothedNativeBufferUs = 0;

    if (sLastCallbackTime > 0 && now > sLastCallbackTime) {
        bigtime_t current_native_buffer = now - sLastCallbackTime;

        if (sSmoothedNativeBufferUs == 0) {
            sSmoothedNativeBufferUs = current_native_buffer;
        } else {
            sSmoothedNativeBufferUs = (bigtime_t)((sSmoothedNativeBufferUs * 0.90) + (current_native_buffer * 0.10));
        }

        // Fix: Use a 1140ms baseline offset (matching your logs) to align with mpv playback pipeline,
        // and dynamically add the native 190ms hardware buffer variation on top of it.
        bigtime_t basePlaybackDelayUs = 1140000; 
        fAudioHardwareDelayUs = basePlaybackDelayUs + sSmoothedNativeBufferUs + fManualSyncOffsetUs;
    } else {
        fAudioHardwareDelayUs = 1330000 + fManualSyncOffsetUs; 
    }
    sLastCallbackTime = now;

    if (fAudioHardwareDelayUs < 0) fAudioHardwareDelayUs = 0;


/*
    // 2. TIMING AND DATA DEBUG PRINT OUT
    static int debug_throttle_counter = 0;
    static bigtime_t sLastLoggedLatencyUs = 0; // Tracks previous latency to detect shifts
    bool latency_changed = false;
    long long latency_delta_us = 0;

    // Detect if latency changed by more than 1ms (1000 us) to filter out microscopic jitter
    if (sLastLoggedLatencyUs > 0) {
        latency_delta_us = (long long)fAudioHardwareDelayUs - (long long)sLastLoggedLatencyUs;
        if (llabs(latency_delta_us) >= 1000) { 
            latency_changed = true;
        }
    }

    // Convert the smoothed native buffer time directly into sample counts
    long long sample_frame_size = (long long)((double)sSmoothedNativeBufferUs / 1000000.0 * 44100.0);

    if (++debug_throttle_counter >= 60) {
        debug_throttle_counter = 0;
        int active_buffer_elements = (fHistoryHead - fHistoryTail + 512) % 512;

        // Build the change alert string if a shift was detected
        char change_alert[64] = "";
        if (latency_changed) {
            snprintf(change_alert, sizeof(change_alert), " -> [LATENCY CHANGED: %+lld us]", latency_delta_us);
            sLastLoggedLatencyUs = fAudioHardwareDelayUs; // Update baseline after logging change
        } else if (sLastLoggedLatencyUs == 0) {
            sLastLoggedLatencyUs = fAudioHardwareDelayUs; // Initialize baseline on first log
        }

        fprintf(stderr, "[SPECTRUM DEBUG] Input Level: %6.2f dB | NATIVE FRAME SIZE: ~%lld samples | Native Latency: %4.1f ms (%lld us) | Cache Slots Filled: %d/512%s\n", 
                level, 
                sample_frame_size,
                (double)fAudioHardwareDelayUs / 1000.0, 
                (long long)fAudioHardwareDelayUs, 
                active_buffer_elements,
                change_alert);
    }

*/

    // 3. EXPANDED BUFFER PIPELINE INDEXING
    fLevelHistory[fHistoryHead] = level;
    fTimeHistory[fHistoryHead] = now;
    fHistoryHead = (fHistoryHead + 1) % 512;
    
    if (fHistoryHead == fHistoryTail) {
        fHistoryTail = (fHistoryTail + 1) % 512; 
    }

    bigtime_t target_audio_time = now - fAudioHardwareDelayUs;
    double delayed_level = level; 
    bool found_match = false;

    int current_idx = fHistoryTail;
    while (current_idx != fHistoryHead) {
        int next_idx = (current_idx + 1) % 512;
        if (next_idx == fHistoryHead) break; 

        if (fTimeHistory[current_idx] <= target_audio_time && fTimeHistory[next_idx] > target_audio_time) {
            delayed_level = fLevelHistory[current_idx];
            found_match = true;
            fHistoryTail = current_idx; // Safely release history slots behind our match point
            break;
        }
        current_idx = next_idx;
    }

    // FIXED LEAK PROTECTION: If target window was not found, 
    // advance tail to discard stale historical buffers
    if (!found_match && fHistoryHead != fHistoryTail) {
        delayed_level = fLevelHistory[fHistoryTail];
        // Advance tail if our oldest timestamp has already aged out past our target window
        if (fTimeHistory[fHistoryTail] < target_audio_time) {
            fHistoryTail = (fHistoryTail + 1) % 512;
        }
    }

    // 4. Asymmetric Attack Rendering Calculations
    if (delayed_level > fCurrentLevel) {
        fCurrentLevel = delayed_level; 
    } else {
        fCurrentLevel = (fCurrentLevel * 0.72) + (delayed_level * 0.28); 
    }
    
    Invalidate();
}




    void AdaptToAlbumArt(BBitmap* artBitmap) {
        if (artBitmap == nullptr || artBitmap->InitCheck() != B_OK) return;        
        color_space space = artBitmap->ColorSpace();
        if (space != B_RGBA32 && space != B_RGB32) return;
        BRect bounds = artBitmap->Bounds();
        int32 width = (int32)bounds.Width() + 1;
        int32 height = (int32)bounds.Height() + 1;        
        uint8* bitsBase = (uint8*)artBitmap->Bits();
        int32 bpr = artBitmap->BytesPerRow();
        if (!bitsBase) return;

        int32 rows[] = {
            (int32)(height * 0.30f),  
            (int32)(height * 0.55f),  
            (int32)(height * 0.75f)   
        };

        for (int i = 0; i < 64; i++) {
            float horizontalPercent = (float)i / 64.0f;
            int32 targetPixelX = (int32)(horizontalPercent * width);
            int32 byteOffset = targetPixelX * 4; 
            uint32 sumRed = 0, sumGreen = 0, sumBlue = 0;

            for (int r = 0; r < 3; r++) {
                uint8* rowPtr = bitsBase + (rows[r] * bpr);
                sumBlue  += rowPtr[byteOffset + 0];
                sumGreen += rowPtr[byteOffset + 1];
                sumRed   += rowPtr[byteOffset + 2];
            }

            uint8 finalRed   = (uint8)(sumRed / 3);
            uint8 finalGreen = (uint8)(sumGreen / 3);
            uint8 finalBlue  = (uint8)(sumBlue / 3);

            if (finalRed < 35 && finalGreen < 35 && finalBlue < 35) {
                uint8 maxChannel = max_c(finalRed, max_c(finalGreen, finalBlue));
    
                if (maxChannel == 0) {
                    fArtworkPalette[i] = { 40, 50, 60, 255 }; 
                } else {
                    float boostFactor = 60.0f / (float)maxChannel;
                    fArtworkPalette[i] = {
                        (uint8)min_c(255, (int)(finalRed * boostFactor)),
                        (uint8)min_c(255, (int)(finalGreen * boostFactor)),
                        (uint8)min_c(255, (int)(finalBlue * boostFactor)),
                        255
                    };
                }
            } else {
                fArtworkPalette[i] = { finalRed, finalGreen, finalBlue, 255 };
            }
        }
        Invalidate();
    }

    virtual void AttachedToWindow() override {
        BView::AttachedToWindow();    
        if (Window() != nullptr) {
            Window()->SetPulseRate(50000); 
        }
    }

/*
virtual void KeyDown(const char* bytes, int32 numBytes) override {
    if (numBytes == 1) {
        if (bytes[0] == '+') {
            fManualSyncOffsetUs += 10000; // Shift visuals back by an extra 10ms
            
            fprintf(stderr, "[SYNC CONTROL] Offset Adjusted: +10ms | Total Manual Offset: %4.1f ms\n", 
                    (double)fManualSyncOffsetUs / 1000.0);
            return;
        } 
        else if (bytes[0] == '-') {
            fManualSyncOffsetUs -= 10000; // Shift visuals forward by 10ms
            
            fprintf(stderr, "[SYNC CONTROL] Offset Adjusted: -10ms | Total Manual Offset: %4.1f ms\n", 
                    (double)fManualSyncOffsetUs / 1000.0);
            return;
        }
    }
    BView::KeyDown(bytes, numBytes);
}
*/

    virtual void MouseDown(BPoint point) override {
        BMessage* message = Window()->CurrentMessage();
        int32 buttons = 0;
        int32 clicks = 0; // Added tracker for double clicks
        
        MakeFocus(true);

        if (message != nullptr && message->FindInt32("buttons", &buttons) == B_OK) {
            // Fetch system double click count 
            message->FindInt32("clicks", &clicks);

            if (buttons & B_SECONDARY_MOUSE_BUTTON) {
                fVisualizerMode = (fVisualizerMode + 1) % MODE_COUNT;
                Invalidate();
                return; 
            }
            
            if (buttons & B_PRIMARY_MOUSE_BUTTON) {
                if (fVisualizerMode == MODE_MOTO_RIDER && fMotoCrashTicks == 0) {
                    // --- DOUBLE CLICK IN THE AIR = TRIGGER/CHAIN FLIPS ---
                    if (clicks >= 2 && fMotoY > 0.05f) {
                        fIsFlipping = true;
                        // Give an extra lift bump on every flip chain to help clear the height gap
                        fMotoVelocityY += 2.0f; 
                        return;
                    }

                    
                    // --- SINGLE CLICK ON THE GROUND = REGULAR JUMP ---
                    if (fMotoY <= 0.05f) {
                        float audioBonus = (fCurrentLevel > -35.0f) ? (35.0f + (float)fCurrentLevel) * 0.12f : 0.0f;
                        fMotoVelocityY = 4.0f + audioBonus; 
                        return;
                    }
                }

                if (fVisualizerMode == MODE_PONG_BALLS) {
                    return; 
                }
            }
        }
        BView::MouseDown(point);
    }




virtual void Pulse() override {
    BRect b = Bounds();
    float w = b.Width();
    float viewHeight = b.Height();

    if (viewHeight <= 0.0f || w <= 0.0f) return;

    // ====================================================================
    // 1. MASTER AUDIO MAGNITUDE & LIMITER CALCULATIONS
    // ====================================================================
    const float floorDb = -45.0f;
    float peak = (float)fCurrentLevel;        
    if (peak < floorDb) peak = floorDb;
    if (peak > 0.0f) peak = 0.0f;
    
    // Normalize master volume strictly between 0.0 and 1.0
    float masterMagnitude = (peak - floorDb) / (0.0f - floorDb);
    float currentInputDb = 0.0f;
    
    if (Window() != nullptr) {
        BSlider* inputSlider = dynamic_cast<BSlider*>(Window()->FindView("LimitInput"));
        if (inputSlider == nullptr) {
            inputSlider = dynamic_cast<BSlider*>(Window()->FindView("input_gain"));
        }
        if (inputSlider != nullptr) {
            currentInputDb = (float)inputSlider->Value();
        }
    }

    // Protection mapping to ensure limiter divisor never hits 0 or flips inverted
    if (currentInputDb < 0.0f) currentInputDb = 0.0f; 

    const float masterSensitivityMultiplier = 0.87f;
    float limiterDivisor = 1.0f + (currentInputDb * 0.065f);
    masterMagnitude = (powf(masterMagnitude, 2.0f) * masterSensitivityMultiplier) / limiterDivisor;

    // ====================================================================
    // 2. SPRING PHYSICS & BIN CALCULATIONS
    // ====================================================================
    const float springStiffness = 0.28f; 
    const float springDamping = 0.74f;   
    bool isTimingOut = ((system_time() - fLastDataTime) > 100000);

    for (int i = 0; i < 64; i++) {
        // Frequency-dependent scaling curve
        float frequencyScale = 1.0f;
        if (i < 12) {
            frequencyScale = 1.15f + ((12 - i) * 0.03f); 
        } else if (i > 45) {
            frequencyScale = 0.85f - ((i - 45) * 0.02f); 
        }

        // Replaced rand() flickering with a controlled pseudo-organic pulse scale
        float organicScale = 0.95f + (0.10f * sinf(i * 0.25f)); 
        
        // Calculate target constraint ceiling to prevent spring breakdown
        float targetHeight = masterMagnitude * viewHeight * frequencyScale * organicScale;
        if (targetHeight > viewHeight) targetHeight = viewHeight;

        // Apply Hooke's Law Spring Force
        float displacement = targetHeight - fBarHeights[i];
        float springForce = displacement * springStiffness;
        fBarVelocities[i] = (fBarVelocities[i] + springForce) * springDamping;
        fBarHeights[i] += fBarVelocities[i];
        
        // Boundaries enforcement
        if (fBarHeights[i] > viewHeight) {
            fBarHeights[i] = viewHeight;
            fBarVelocities[i] = 0.0f;
        }
        if (fBarHeights[i] < 0.0f) {
            fBarHeights[i] = 0.0f;
            fBarVelocities[i] = 0.0f; 
        }

        // Peak Hold logic processing
        if (fBarHeights[i] >= fPeakHeights[i]) {
            fPeakHeights[i] = fBarHeights[i];
            fPeakHold[i] = 6; 
        } else {
            if (fPeakHold[i] > 0) {
                fPeakHold[i]--;
            } else {
                fPeakHeights[i] -= (viewHeight * 0.025f); 
                if (fPeakHeights[i] < 0.0f) fPeakHeights[i] = 0.0f;
            }
        }

        // Integrated Timeout Smoothing Decay
        if (isTimingOut) {
            if (fBarHeights[i] > 0.05f) {
                fBarHeights[i] *= 0.75f; 
                fBarVelocities[i] *= 0.50f;
            } else {
                fBarHeights[i] = 0.0f;
                fBarVelocities[i] = 0.0f;
            }
            if (fPeakHeights[i] > 0.0f) {
                fPeakHeights[i] -= 1.5f;
                if (fPeakHeights[i] < 0.0f) fPeakHeights[i] = 0.0f;
            }
        }
    }

    // Handle overall input volume attenuation tracking on silence
    if (isTimingOut && fCurrentLevel > floorDb) {
        fCurrentLevel = (fCurrentLevel * 0.80f) + (floorDb * 0.20f);
    }



        // ====================================================================
        // 3. SINGLE-BALL PONG PHYSICS ENGINE (Scoring Inverted + Ball Chasing Dog)
        // ====================================================================
        if (fVisualizerMode == MODE_PONG_BALLS) {
            float bassImpact = (fBarHeights[2] + fBarHeights[6] + fBarHeights[12]) / 3.0f;
            float paddleH = 21.0f; 

            // Always tick down your procedural explosion frames inside the physics engine
            if (fPongExplosionTick > 0) {
                fPongExplosionTick--;
            }

            // --- LEFT PADDLE AUTOMATED AUTOPILOT (IMPERFECT AI) ---
            float leftTargetY = viewHeight / 2.0f;
            
            if (fBallDX[0] < 0) {
                static float currentAIError = 0.0f;
                static bool errorCalculated = false;
                
                float midPointX = startX_cached + (artworkWidth_cached / 2.0f);
                if (fBallX[0] > midPointX) {
                    errorCalculated = false; 
                }
                
                if (fBallX[0] <= midPointX && !errorCalculated) {
                    if ((rand() % 100) < 35) {
                        currentAIError = (float)((rand() % 32) - 16); 
                    } else {
                        currentAIError = 0.0f; 
                    }
                    errorCalculated = true;
                }
                leftTargetY = fBallY[0] + currentAIError;
            }

            leftTargetY += fLeftPaddleTargetOffset;
            if (leftTargetY < paddleH / 2.0f) leftTargetY = paddleH / 2.0f;
            if (leftTargetY > viewHeight - (paddleH / 2.0f)) leftTargetY = viewHeight - (paddleH / 2.0f);

            fLeftPaddlePos += (leftTargetY - fLeftPaddlePos) * (0.13f + bassImpact * 0.02f);

            // --- RIGHT PADDLE MOUSE WHEEL DRIVEN BOUNDS ---
            if (fRightPaddlePos < paddleH / 2.0f) fRightPaddlePos = paddleH / 2.0f;
            if (fRightPaddlePos > viewHeight - (paddleH / 2.0f)) fRightPaddlePos = viewHeight - (paddleH / 2.0f);

            // --- SCORE WATCH & BALL PHYSICS WITH AUTO-RESET TIMER ---
            int k = 0; 
            static bigtime_t winStartTime = 0;
            static bool timerStarted = false;

            // Declare persistent, static dog metrics inside the physics engine layout scope
            static bool dogActive = false;
            static bool dogRunningAway = false;
            static float dogX = 0.0f;
            static float dogY = 0.0f;
            static bigtime_t dogSpawnTime = 0;

            if (fLeftScore >= 10 || fRightScore >= 10) {
                fBallX[k] = startX_cached + (artworkWidth_cached / 2.0f);
                fBallY[k] = viewHeight / 2.0f;
                dogActive = false; // Disable dog on victory screen

                if (!timerStarted) {
                    winStartTime = system_time(); 
                    timerStarted = true;
                }

                if (system_time() - winStartTime >= 3000000) {
                    fLeftScore = 0;
                    fRightScore = 0;
                    timerStarted = false;
                    winStartTime = 0;

                    fBallDX[k] = ((rand() % 100) > 50) ? 3.5f : -3.5f;
                    fBallDY[k] = ((rand() % 100) > 50) ? 2.5f : -2.5f;
                }
            } else {
                timerStarted = false;
                winStartTime = 0;

                // Freeze ball movement briefly if a miss occurred so user can see the shockwave
                if (fMotoCrashTicks > 0) {
                    fMotoCrashTicks--;
                } else {
                    float audioSpeedBoost = 1.0f + (bassImpact * 0.05f * 0.65f);
                    float moveX = (fBallDX[k] * 0.90f) * audioSpeedBoost;
                    float moveY = (fBallDY[k] * 0.90f) * audioSpeedBoost;
                    
                    const float MAX_SPEED_X = 12.0f;
                    if (moveX > MAX_SPEED_X) moveX = MAX_SPEED_X;
                    if (moveX < -MAX_SPEED_X) moveX = -MAX_SPEED_X;

                    fBallX[k] += moveX;
                    fBallY[k] += moveY;
                }
           
                fBallSize[k] = 11.0f; 
                float radius = fBallSize[k] / 2.0f;

                // Ceiling / Floor bounces
                if (fBallY[k] - radius < 0) { fBallY[k] = radius; fBallDY[k] = -fBallDY[k]; }
                else if (fBallY[k] + radius > viewHeight) { fBallY[k] = viewHeight - radius; fBallDY[k] = -fBallDY[k]; }

                // ------------------------------------------------------------
                // --- BALL-CHASING DOG PHYSICS ENGINE SUBROUTINE ---
                // ------------------------------------------------------------
                if (!dogActive) {
                    // 0.5% chance to spawn dog from the floor every frame
                    if ((rand() % 1000) < 5) {
                        dogX = startX_cached + (artworkWidth_cached / 2.0f);
                        dogY = viewHeight + 15.0f; // Start hidden below screen edge
                        dogSpawnTime = system_time();
                        dogActive = true;
                        dogRunningAway = false;
                    }
                } else {
                    if (!dogRunningAway) {
                        // Phase A: Active ball pursuit over 4.0 seconds (4000000 microseconds)
                        if (system_time() - dogSpawnTime >= 4000000) {
                            dogRunningAway = true;
                        } else {
                            // Smooth interpolation vector pursuit (chases ball's current coordinates)
                            dogX += (fBallX[k] - dogX) * 0.07f;
                            dogY += (fBallY[k] - dogY) * 0.07f;
                        }
                    } else {
                        // Phase B: Run away off the screen boundary
                        dogX += 4.5f; // Fast break to the right
                        dogY += (viewHeight - 10.0f - dogY) * 0.1f; // Diagonally head toward bottom corner
                        
                        // Clean up state when completely past boundaries
                        if (dogX > startX_cached + artworkWidth_cached + 25.0f) {
                            dogActive = false;
                        }
                    }
                }
                // Save coordinates globally/locally using temporary class pointer fields or shared global markers
                // For direct access in the drawing code, we will bind these to quick global aliases
                fDogDrawActive = dogActive;
                fDogDrawX = dogX;
                fDogDrawY = dogY;
                // ------------------------------------------------------------

                // Left Paddle Collision check
                float leftPaddleRightEdge = startX_cached + 5.0f;
                if (fBallX[k] - radius <= leftPaddleRightEdge && fBallX[k] + radius >= startX_cached && fBallDX[k] < 0) {
                    if (fBallY[k] >= fLeftPaddlePos - (paddleH / 2.0f) - 3.0f && fBallY[k] <= fLeftPaddlePos + (paddleH / 2.0f) + 3.0f) {
                        fBallX[k] = leftPaddleRightEdge + radius;
                        fBallDX[k] = -fBallDX[k];
                        fBallDX[k] *= 1.10f;
                        
                        float relativeIntersectY = fLeftPaddlePos - fBallY[k];
                        float normalizedIntersectY = relativeIntersectY / (paddleH / 2.0f);
                        float randomFactor = ((rand() % 20) - 10) / 50.0f;
                        fBallDY[k] = (-normalizedIntersectY * 3.5f) + randomFactor;

                        fLeftPaddleTargetOffset = (float)((rand() % 16) - 8); 
                    }
                }

                // Right Paddle Collision check
                float rightPaddleLeftEdge = startX_cached + artworkWidth_cached - 5.0f;
                if (fBallX[k] + radius >= rightPaddleLeftEdge && fBallX[k] - radius <= startX_cached + artworkWidth_cached && fBallDX[k] > 0) {
                    if (fBallY[k] >= fRightPaddlePos - (paddleH / 2.0f) - 3.0f && fBallY[k] <= fRightPaddlePos + (paddleH / 2.0f) + 3.0f) {
                        fBallX[k] = rightPaddleLeftEdge - radius;
                        fBallDX[k] = -fBallDX[k];
                        fBallDX[k] *= 1.10f;
                        
                        float relativeIntersectY = fRightPaddlePos - fBallY[k];
                        float normalizedIntersectY = relativeIntersectY / (paddleH / 2.0f);
                        float randomFactor = ((rand() % 20) - 10) / 50.0f;
                        fBallDY[k] = (-normalizedIntersectY * 3.5f) + randomFactor;
                    }
                }

                // Out of bounds reset path handler (DEDICATED FIXED SHOCKWAVE TRIGGER)
                if (fBallX[k] < startX_cached || fBallX[k] > startX_cached + artworkWidth_cached) {
                    
                    // --- INVERTED SCORE ROUTING ALLOCATION ---
                    if (fBallX[k] < startX_cached) {
                        fRightScore++; 
                    } else {
                        fLeftScore++; 
                    }
                    // ------------------------------------------

                    // 1. Snapshot the exact screen location of the ball's demise
                    fPongExplosionX = fBallX[k];
                    fPongExplosionY = fBallY[k];
                    
                    // 2. Start our custom animation tick counter (lasts 20 loop frames)
                    fPongExplosionTick = 20;

                    // 3. Keep the ball frozen for 25 frames so the player sees the blast expand
                    fMotoCrashTicks = 25; 

                    // 4. Reposition the ball to the center for the next round
                    fBallX[k] = startX_cached + (artworkWidth_cached / 2.0f);
                    fBallY[k] = viewHeight / 2.0f;
                    fBallDX[k] = ((rand() % 100) > 50) ? 3.5f : -3.5f;
                    fBallDY[k] = ((rand() % 100) > 50) ? 2.5f : -2.5f;
                }
            }
        }




        // ====================================================================
        // 4. MODE 5 RAINDROPS PHYSICS UPDATES
        // ====================================================================
        if (fVisualizerMode == MODE_RAINDROPS) {
            for (int r = 0; r < 75; r++) {
                int freqIdx = (int)(fRainX[r] * 63.0f);
                fRainY[r] += fRainSpeed[r] * (1.0f + (fBarHeights[freqIdx] / viewHeight) * 2.5f);
                if (fRainY[r] > 1.0f) {
                    fRainY[r] = 0.0f;
                    fRainX[r] = (float)(rand() % 1000) / 1000.0f;
                    fRainSpeed[r] = 0.01f + ((rand() % 100) / 10000.0f);
                }
            }
        }

  		// ====================================================================
        // 5. MODE 6: MULTI-OBSTACLE PHYSICS ENGINE, SCORE TRACKER & BACKFIRE
        // ====================================================================
        if (fVisualizerMode == MODE_MOTO_RIDER) {
            // Sample columns 2, 3, and 4 early to drive dynamic growing obstacles
            float lowBassPulse = (fBarHeights[2] + fBarHeights[3] + fBarHeights[4]) / 3.0f;
            float bassNormalized = (viewHeight > 0.0f) ? (lowBassPulse / viewHeight) : 0.0f;
            
            if (fMotoCrashTicks > 0) {
                fMotoCrashTicks--;
                if (fMotoCrashTicks == 0) { // Reset game pipeline on crash recovery loop
                    fObsX[0] = artworkWidth_cached + 40.0f;
                    fObsIsPit[0] = rand() % 5; // Expanded: 0=Rock, 1=Pit, 2=Water, 3=4-Spikes, 4=5-Spikes
                    fObsHeightScale[0] = 0.6f + ((rand() % 8) / 10.0f);                    
                    fObsX[1] = fObsX[0] + 180.0f; // Keep the separation buffer intact
                    fObsIsPit[1] = rand() % 5; // Expanded
                    fObsHeightScale[1] = 0.6f + ((rand() % 8) / 10.0f);                    
                    fMotoY = 0.0f;
                    fMotoVelocityY = 0.0f;
                    fMotoScore = 0; // Wipe score on structural crashes
                    fIsFlipping = false;
                    fFlipRotation = 0.0f;
                }
            } else { // Loop tracking and moving both active obstacle queue instances
                for (int o = 0; o < 2; o++) {
                    float baseObstacleSpeed = 5.8f; 
                    fObsX[o] -= baseObstacleSpeed;                     
                    
                    // --- HAZARD 1: DYNAMIC HORIZONTAL MOVEMENT ---
                    // Solid rocks weave back and forth dynamically over time
                    if (o == 0 && fObsIsPit[o] == 0) {
                        float weaveTime = (float)system_time() / 1000000.0f;
                        fObsX[o] += sinf(weaveTime * 6.5f) * 1.8f; // Aggressive lateral shifting
                    } 
                    
                    // Recycle obstacle back to the right margins once it rolls left off-screen
                    if (fObsX[o] < -50.0f) { // Adjusted lower edge slightly for wider spike obstacles
                        int otherIndex = (o == 0) ? 1 : 0;
                        fObsX[o] = max_c(artworkWidth_cached + 40.0f, fObsX[otherIndex] + 160.0f + (rand() % 60));                        
                        fObsIsPit[o] = rand() % 5; // Cycle smoothly into rock, pit, water, or multi-spikes
                        fObsHeightScale[o] = 0.6f + ((rand() % 8) / 10.0f);                        
                        fMotoScore++; // Successfully cleared a hazard! Increment score tally
                    }
                } // Parallax scrolling speed tracking
                fMtnScrollX -= 0.95f; 
                if (fMtnScrollX < -240.0f) {
                    fMtnScrollX += 240.0f;
                } 
                for (int m = 0; m < 4; m++) {
                    if (fMtnHeightScale[m] <= 0.01f) {
                        fMtnHeightScale[m] = 0.7f + ((rand() % 7) / 5.0f); 
                    }
                }
                for (int t = 0; t < 4; t++) {
                    fTreeX[t] -= 1.85f; 
                    if (fTreeX[t] < -20.0f) {
                        fTreeX[t] = artworkWidth_cached + 10.0f + (rand() % 40);
                        fTreeHeight[t] = 12.0f + (rand() % 10);
                    }
                }
                for (int c = 0; c < 3; c++) {
                    fCloudX[c] -= 0.20f;
                    if (fCloudX[c] < -40.0f) {
                        fCloudX[c] = artworkWidth_cached + 10.0f + (rand() % 50);
                        fCloudY[c] = 4.0f + (rand() % 8);
                        fCloudSize[c] = 16.0f + (rand() % 14);
                    }
                } 
                
                // Trigger a backfire burst when a heavy bass beat slams past an intense threshold
                if (fMotoCrashTicks == 0 && lowBassPulse > (viewHeight * 0.52f)) {
                    for (int s = 0; s < 12; s++) {
                        if (fSparkLife[s] <= 0) {
                            fSparkLife[s] = 8 + (rand() % 8);  fSparkX[s] = 0.0f; fSparkY[s] = 0.0f; fSparkDX[s] = -1.8f - ((rand() % 15) / 10.0f);  fSparkDY[s] = -0.5f + ((rand() % 20) / 10.0f);
                        }
                    }
                } 
                for (int s = 0; s < 12; s++) {
                    if (fSparkLife[s] > 0) {
                        fSparkLife[s]--; fSparkX[s] += fSparkDX[s]; fSparkY[s] += fSparkDY[s]; fSparkDX[s] *= 0.88f; fSparkDY[s] += 0.15f; 
                    }
                } 
                fMotoY += fMotoVelocityY;
                fMotoVelocityY -= 0.45f; 
                if (fMotoY <= 0.0f) {
                    fMotoY = 0.0f; fMotoVelocityY = 0.0f;                    
                    fIsFlipping = false; fFlipRotation = 0.0f;
                } 
                if (fIsFlipping) {
                    fFlipRotation += 18.0f; 
                    if (fFlipRotation >= 360.0f) {
                        fIsFlipping = false; fFlipRotation = 0.0f;
                        fMotoScore += 5;                         
                        fStuntTextStr.SetTo("+5 STUNT!");
                        fStuntTextY = fMotoY + 22.0f; 
                        fStuntTextLife = 25;          
                    }
                } 
                if (fStuntTextLife > 0) {
                    fStuntTextLife--;      
                    fStuntTextY += 0.8f;   
                } 

                // ====================================================================
                // 5B. BACKFLIP ANIMATION STEP TRACKER & MULTI-FLIP BONUS CALCULATIONS
                // ====================================================================
                static int consecutiveFlipCount = 0; // Tracks flips within a single jump

                fMotoY += fMotoVelocityY;
                fMotoVelocityY -= 0.45f; 
                
                if (fMotoY <= 0.0f) {
                    fMotoY = 0.0f; 
                    fMotoVelocityY = 0.0f;                    
                    
                    // Safely terminate flip if the rider touches down
                    fIsFlipping = false; 
                    fFlipRotation = 0.0f;
                    consecutiveFlipCount = 0; // Reset landing tracker combo anchor
                } 

                if (fIsFlipping) {
                    fFlipRotation += 18.0f; // Spin velocity frame step
                    
                    if (fFlipRotation >= 360.0f) {
                        fFlipRotation -= 360.0f; // Reset wheel loop rotation step cleanly
                        consecutiveFlipCount++;  // Increments: 1 for single, 2 for double!
                        
                        // Progressive scoring scaling rewards system (+5, +15, +30...)
                        int scoreBonus = 5 * consecutiveFlipCount;
                        fMotoScore += scoreBonus; 
                        
                        // --- RANDOM STUNT PHRASE GENERATOR ENGINE ---
                        const char* randomPhrases[10] = {
                            "Way to go!",
                            "You Rock!",
                            "Front Flip Mania!",
                            "Excellent!",
                            "Wow!",
                            "Watch it!",
                            "Go Go Go!",
                            "Non Stop!",
                            "No way!",
                            "Look Out!"
                        };
                        int phraseIndex = rand() % 10;
                        const char* chosenPhrase = randomPhrases[phraseIndex];

                        // Build the message layout (e.g., "+15 DOUBLE FLIP!! You Rock!")
                        if (consecutiveFlipCount == 1) {
                            fStuntTextStr.SetToFormat("+5 %s", chosenPhrase);
                        } else if (consecutiveFlipCount == 2) {
                            fStuntTextStr.SetToFormat("+15 DOUBLE FLIP!! %s", chosenPhrase);
                        } else {
                            fStuntTextStr.SetToFormat("+%" B_PRId32 " MEGA FLIP!!! %s", (int32)(5 * consecutiveFlipCount), chosenPhrase);
                        }
                        
                        // FIXED COORDINATES: Center explicitly on screen to prevent canvas clipping
                        fStuntTextY = viewHeight / 2.0f; 
                        fStuntTextLife = 35; // Extended frame visibility slightly for larger announcements
                        
                        fIsFlipping = false; 
                    }
                } 

                if (fStuntTextLife > 0) {
                    fStuntTextLife--;      
                    fStuntTextY -= 0.4f; // Float upward gently from screen center baseline
                }



                // ====================================================================
                // 5C. ADVANCED MULTI-OBJECT COLLISION BOX INTERSECT TESTS
                // ====================================================================
                float bikeLeft = 35.0f;  
                float bikeRight = 55.0f;                
                if (fIsFlipping) {
                    bikeLeft = 41.0f; bikeRight = 49.0f;
                }                
                for (int o = 0; o < 2; o++) {
                    if (fObsIsPit[o] == 1 || fObsIsPit[o] == 2) {  // PIT (1) or WATER (2)
                        if (fMotoY <= 0.1f && bikeRight > fObsX[o] && bikeLeft < fObsX[o] + 24.0f) {
                            fMotoCrashTicks = 30; 
                        }
                    } else if (fObsIsPit[o] == 0) {  // SOLID ROCK (0)
                        float audioGrowthFactor = 1.0f + (bassNormalized * 2.0f); 
                        float currentObsHeight = 10.0f * fObsHeightScale[o] * audioGrowthFactor;                        
                        if (fObsX[o] >= bikeLeft && fObsX[o] <= bikeRight && fMotoY < currentObsHeight) {
                            fMotoCrashTicks = 30; 
                        }
                    } else { 
                        // NEW TYPE 3 (4 Spikes) & TYPE 4 (5 Spikes) COLLISION DETECTOR PASS
                        int spikeCount = (fObsIsPit[o] == 3) ? 4 : 5;
                        float spikeWidth = 8.0f; 
                        
                        // Audio growth multiplier calculation (Cleaned up and toned down to 1.2f)
                        float spikeAudioScale = 1.0f + (bassNormalized * 1.2f); 

                        // Cycle across each individual triangle blade to parse sub-hitbox elevations
                        for (int sIdx = 0; sIdx < spikeCount; sIdx++) {
                            float sLeft = fObsX[o] + (sIdx * spikeWidth);
                            float sRight = sLeft + spikeWidth;
                            
                            // If the motorcycle bounding box intersects this specific triangle's horizontal span
                            if (bikeRight >= sLeft && bikeLeft <= sRight) {
                                // Deterministic variation for spike sizing sequence heights
                                float sizeVariation = 0.7f + (((sIdx * 3) % 5) / 6.0f); // Yields varied scaling metrics
                                float finalSpikeHeight = 7.0f * fObsHeightScale[o] * sizeVariation * spikeAudioScale; 

                                if (fMotoY < finalSpikeHeight) {
                                    fMotoCrashTicks = 30; // Structural crash triggered
                                    break;
                                }
                            }
                        }
                    }
                }
            } 
        }




        Invalidate(); 
    }




    virtual void Draw(BRect updateRect) override {       
        if (!cfg.showSpectrumVisuals || !cfg.eqEnabled) {
            if (Parent() != nullptr) {
                SetHighColor(Parent()->ViewColor());
            } else {
                SetHighColor(ui_color(B_PANEL_BACKGROUND_COLOR));
            }
            FillRect(Bounds());
            return;
        }       
        
        BRect b = Bounds();        
        float floor = -45.0f;
        float peak = (float)fCurrentLevel;        
        if (peak < floor) peak = floor;
        if (peak > 0.0f) peak = 0.0f;
        float masterMagnitude = (peak - floor) / (0.0f - floor);
        float currentInputDb = 0.0f;
        
        if (Window() != nullptr) {
            BSlider* inputSlider = dynamic_cast<BSlider*>(Window()->FindView("LimitInput"));
            if (inputSlider == nullptr) {
                inputSlider = dynamic_cast<BSlider*>(Window()->FindView("input_gain"));
            }
            if (inputSlider != nullptr) {
                currentInputDb = (float)inputSlider->Value();
            }
        }

        float masterSensitivityMultiplier = 0.87f;
        float limiterDivisor = 1.0f + (currentInputDb * 0.065f);
        masterMagnitude = (powf(masterMagnitude, 2.0f) * masterSensitivityMultiplier) / limiterDivisor;
        
        float height = b.Height();
        int numBars = 64;
        float artworkWidth = 325.0f; 
        float totalViewWidth = b.Width();
        float startX = (totalViewWidth - artworkWidth) / 2.0f;
        float barWidth = artworkWidth / numBars;
        const float springStiffness = 0.28f; 
        const float springDamping = 0.74f;   

        // Cache view limits into loop dimensions for Pulse() engine threads
        artworkWidth_cached = artworkWidth;
        startX_cached = startX;

        for (int i = 0; i < numBars; i++) {
            float frequencyScale = 1.0f;
            if (i < 12) {
                frequencyScale = 1.15f + ((12 - i) * 0.03f); 
            } else if (i > 45) {
                frequencyScale = 0.85f - ((i - 45) * 0.02f); 
            }

            float punchFactor = 0.80f + ((rand() % 40) / 100.0f); 
            float targetHeight = masterMagnitude * height * frequencyScale * punchFactor;

            float displacement = targetHeight - fBarHeights[i];
            float springForce = displacement * springStiffness;
            fBarVelocities[i] = (fBarVelocities[i] + springForce) * springDamping;
            fBarHeights[i] += fBarVelocities[i];
            
            if (fBarHeights[i] > height) fBarHeights[i] = height;
            if (fBarHeights[i] < 0.0f) {
                fBarHeights[i] = 0.0f;
                fBarVelocities[i] = 0.0f; 
            }

            if (fBarHeights[i] >= fPeakHeights[i]) {
                fPeakHeights[i] = fBarHeights[i];
                fPeakHold[i] = 6; 
            } else {
                if (fPeakHold[i] > 0) {
                    fPeakHold[i]--;
                } else {
                    fPeakHeights[i] -= (height * 0.025f); 
                    if (fPeakHeights[i] < 0.0f) fPeakHeights[i] = 0.0f;
                }
            }
        }

        // Fetch parent panel color
        rgb_color bgCol = (Parent() != nullptr) ? Parent()->ViewColor() : ui_color(B_PANEL_BACKGROUND_COLOR);
        SetHighColor(bgCol);
        FillRect(b);

        // --- RENDER MODES ---
        if (fVisualizerMode == MODE_BARS) {
        	SetDrawingMode(B_OP_ALPHA);
            for (int i = 0; i < numBars; i++) {
                float finalBarHeight = fBarHeights[i];
                
                SetHighColor(fArtworkPalette[i]);             
                FillRect(BRect(startX + (i * barWidth), height - finalBarHeight, 
                   startX + ((i + 1) * barWidth) - 1, height));

                if (fPeakHeights[i] > finalBarHeight && fPeakHeights[i] > 2.0f) {
                    rgb_color peakColor = fArtworkPalette[i];
                    peakColor.red   = (uint8)min_c(255, peakColor.red + 50);
                    peakColor.green = (uint8)min_c(255, peakColor.green + 50);
                    peakColor.blue  = (uint8)min_c(255, peakColor.blue + 50);
                    
                    SetHighColor(peakColor); 
                    StrokeLine(BPoint(startX + (i * barWidth), height - fPeakHeights[i]),
                               BPoint(startX + ((i + 1) * barWidth) - 1, height - fPeakHeights[i]));
                                SetDrawingMode(B_OP_COPY);
                }
            }
        } 
        else if (fVisualizerMode == MODE_LINE_WAVE) {
        	SetDrawingMode(B_OP_ALPHA);
            SetPenSize(2.5f);
            float midY = height / 2.0f;

            BPoint points[64];
            for (int i = 0; i < numBars; i++) {
                float currentX = startX + (i * barWidth) + (barWidth / 2.0f);
                
                float fadeWindow = 1.0f;
                if (i < 8)  fadeWindow = (float)i / 8.0f;
                if (i > 55) fadeWindow = (float)(63 - i) / 8.0f;
                
                float offset = fBarHeights[i] * 0.5f * fadeWindow; 
                float currentY = (i % 2 == 0) ? (midY - offset) : (midY + offset);
                points[i] = BPoint(currentX, currentY);
            }

            for (int i = 0; i < numBars - 1; i++) {
                SetHighColor(fArtworkPalette[i]);

                int i0 = (i == 0) ? 0 : i - 1; int i1 = i; int i2 = i + 1; int i3 = (i + 2 >= numBars) ? numBars - 1 : i + 2;
                BPoint p0 = points[i0]; BPoint p1 = points[i1]; BPoint p2 = points[i2]; BPoint p3 = points[i3];

                const int steps = 4; BPoint prevSegmentPoint = p1;

                for (int s = 1; s <= steps; s++) {
                    float t = (float)s / (float)steps; float t2 = t * t; float t3 = t2 * t;
                    float f1 = -0.5f * t3 + t2 - 0.5f * t; float f2 = 1.5f * t3 - 2.5f * t2 + 1.0f; float f3 = -1.5f * t3 + 2.0f * t2 + 0.5f * t; float f4 = 0.5f * t3 - 0.5f * t2;
                    BPoint currSegmentPoint(p0.x * f1 + p1.x * f2 + p2.x * f3 + p3.x * f4, p0.y * f1 + p1.y * f2 + p2.y * f3 + p3.y * f4);
                    StrokeLine(prevSegmentPoint, currSegmentPoint);
                    prevSegmentPoint = currSegmentPoint;
                }
            }
            SetDrawingMode(B_OP_COPY);
            SetPenSize(1.0f); 
        }
        else if (fVisualizerMode == MODE_LONG_WAVE) {
        	SetDrawingMode(B_OP_ALPHA);
            float midY = height / 2.0f;
            const int numNodes = 10; BPoint nodes[10];
            
            // INSET BOUNDS: Margins pull endpoints safely inside the clipping region
            float innerWidth = artworkWidth - 4.0f;
            float adjustedStartX = startX + 2.0f;

            for (int i = 0; i < numNodes; i++) {
                nodes[i].x = adjustedStartX + (innerWidth * ((float)i / (float)(numNodes - 1)));
            }


            float peakAmplitudes[8] = { 0.0f };
            for (int chunk = 0; chunk < 8; chunk++) {
                float sum = 0.0f; int startBar = chunk * 8;
                for (int sub = 0; sub < 8; sub++) { sum += fBarHeights[startBar + sub]; }
                peakAmplitudes[chunk] = (sum / 8.0f) * 0.45f; 
            }

            nodes[0].y = midY; nodes[9].y = midY;
            nodes[1].y = midY - peakAmplitudes[0]; nodes[2].y = midY + peakAmplitudes[1]; 
            nodes[3].y = midY - peakAmplitudes[2]; nodes[4].y = midY + peakAmplitudes[3]; 
            nodes[5].y = midY - peakAmplitudes[4]; nodes[6].y = midY + peakAmplitudes[5]; 
            nodes[7].y = midY - peakAmplitudes[6]; nodes[8].y = midY + peakAmplitudes[7]; 

            // --- PASS 1: GLOW SHADOW PASS ---
            SetPenSize(6.0f); 
            for (int glowMirror = 0; glowMirror < 2; glowMirror++) { 
                for (int i = 0; i < numNodes - 1; i++) {
                    int i0 = (i == 0) ? 0 : i - 1; int i1 = i; int i2 = i + 1; int i3 = (i + 2 >= numNodes) ? numNodes - 1 : i + 2;
                    BPoint p0 = nodes[i0]; BPoint p1 = nodes[i1]; BPoint p2 = nodes[i2]; BPoint p3 = nodes[i3];
                    
                    if (glowMirror == 1) { 
                        p0.y = midY + (midY - p0.y); p1.y = midY + (midY - p1.y); 
                        p2.y = midY + (midY - p2.y); p3.y = midY + (midY - p3.y); 
                    }
                    
                    rgb_color colStart = fArtworkPalette[(int)(((float)i / (float)(numNodes - 1)) * 63.0f)];
                    rgb_color colEnd = fArtworkPalette[(int)(((float)(i + 1) / (float)(numNodes - 1)) * 63.0f)];
                    
                    BPoint prevSegmentPoint(p0.x * 0.0f + p1.x * 1.0f + p2.x * 0.0f + p3.x * 0.0f, p0.y * 0.0f + p1.y * 1.0f + p2.y * 0.0f + p3.y * 0.0f);
                    
                    const int steps = 24;
                    for (int s = 1; s <= steps; s++) {
                        float t = (float)s / (float)steps; float t2 = t * t; float t3 = t2 * t;
                        float f1 = -0.5f * t3 + t2 - 0.5f * t; float f2 = 1.5f * t3 - 2.5f * t2 + 1.0f; float f3 = -1.5f * t3 + 2.0f * t2 + 0.5f * t; float f4 = 0.5f * t3 - 0.5f * t2;
                        
                        rgb_color glowColor; 
                        float rR = (colStart.red + (colEnd.red - colStart.red) * t) * 0.4f; 
                        float rG = (colStart.green + (colEnd.green - colStart.green) * t) * 0.4f; 
                        float rB = (colStart.blue + (colEnd.blue - colStart.blue) * t) * 0.4f;
                        
                        if (glowMirror == 1) { glowColor = { (uint8)(rR * 0.5f + bgCol.red * 0.5f), (uint8)(rG * 0.5f + bgCol.green * 0.5f), (uint8)(rB * 0.5f + bgCol.blue * 0.5f), 255 }; }
                        else { glowColor = { (uint8)rR, (uint8)rG, (uint8)rB, 255 }; }
                        
                        SetHighColor(glowColor);
                        BPoint curr(p0.x * f1 + p1.x * f2 + p2.x * f3 + p3.x * f4, p0.y * f1 + p1.y * f2 + p2.y * f3 + p3.y * f4);
                        StrokeLine(prevSegmentPoint, curr); prevSegmentPoint = curr;
                        SetDrawingMode(B_OP_COPY);
                    }
                }
            }

            // --- PASS 2: CRISP FOREGROUND PASS ---
            SetPenSize(3.0f); 
            for (int fgMirror = 0; fgMirror < 2; fgMirror++) { 
                for (int i = 0; i < numNodes - 1; i++) {
                    int i0 = (i == 0) ? 0 : i - 1; int i1 = i; int i2 = i + 1; int i3 = (i + 2 >= numNodes) ? numNodes - 1 : i + 2;
                    BPoint p0 = nodes[i0]; BPoint p1 = nodes[i1]; BPoint p2 = nodes[i2]; BPoint p3 = nodes[i3];
                    
                    if (fgMirror == 1) { 
                        p0.y = midY + (midY - p0.y); p1.y = midY + (midY - p1.y); 
                        p2.y = midY + (midY - p2.y); p3.y = midY + (midY - p3.y); 
                    }
                    
                    rgb_color colStart = fArtworkPalette[(int)(((float)i / (float)(numNodes - 1)) * 63.0f)];
                    rgb_color colEnd = fArtworkPalette[(int)(((float)(i + 1) / (float)(numNodes - 1)) * 63.0f)];
                    
                    BPoint prevSegmentPoint(p0.x * 0.0f + p1.x * 1.0f + p2.x * 0.0f + p3.x * 0.0f, p0.y * 0.0f + p1.y * 1.0f + p2.y * 0.0f + p3.y * 0.0f);
                    
                    const int steps = 24;
                    for (int s = 1; s <= steps; s++) {
                        float t = (float)s / (float)steps; float t2 = t * t; float t3 = t2 * t;
                        float f1 = -0.5f * t3 + t2 - 0.5f * t; float f2 = 1.5f * t3 - 2.5f * t2 + 1.0f; float f3 = -1.5f * t3 + 2.0f * t2 + 0.5f * t; float f4 = 0.5f * t3 - 0.5f * t2;
                        
                        rgb_color blendedColor; 
                        float rR = colStart.red + (colEnd.red - colStart.red) * t; 
                        float rG = colStart.green + (colEnd.green - colStart.green) * t; 
                        float rB = colStart.blue + (colEnd.blue - colStart.blue) * t;
                        
                        if (fgMirror == 1) { blendedColor = { (uint8)(rR * 0.5f + bgCol.red * 0.5f), (uint8)(rG * 0.5f + bgCol.green * 0.5f), (uint8)(rB * 0.5f + bgCol.blue * 0.5f), 255 }; }
                        else { blendedColor = { (uint8)rR, (uint8)rG, (uint8)rB, 255 }; }
                        
                        SetHighColor(blendedColor);
                        BPoint curr(p0.x * f1 + p1.x * f2 + p2.x * f3 + p3.x * f4, p0.y * f1 + p1.y * f2 + p2.y * f3 + p3.y * f4);
                        StrokeLine(prevSegmentPoint, curr); prevSegmentPoint = curr;
                    }
                }
            }
            SetDrawingMode(B_OP_COPY);
            SetPenSize(1.0f); 
        }

        else if (fVisualizerMode == MODE_PONG_BALLS) {
            SetDrawingMode(B_OP_ALPHA);
            float paddleH = 21.0f; // Matches shorter design specification constraints

            float bgBrightness = (bgCol.red * 0.299f) + (bgCol.green * 0.587f) + (bgCol.blue * 0.114f);
            bool isLightPanel = (bgBrightness > 150.0f); // Detect bright/default system themes

            // --- DRAW CENTER DASH PARTITION LINE ---
            if (bgBrightness < 100.0f) { 
                SetHighColor(50, 230, 100, 140); // Soft retro glowing green for dark themes
            } else if (isLightPanel) {
                SetHighColor(100, 105, 110, 255); // Bolder charcoal gray for high-visibility light themes
            } else { 
                SetHighColor(160, 165, 170, 120); // Default middle theme gray fallback
            }

            SetPenSize(1.5f);
            float verticalPadding = 6.0f; 
            for (float dY = verticalPadding; dY < (height - verticalPadding); dY += 12.0f) {
                StrokeLine(BPoint(startX + (artworkWidth / 2.0f), dY), 
                           BPoint(startX + (artworkWidth / 2.0f), dY + 6.0f));
            }
            
            // --- RETRO ARCADE SCORE TRACKING DISPLAY ---
            BFont scoreFont;
            GetFont(&scoreFont);
            scoreFont.SetSize(14.0f); // Large and readable layout
            SetFont(&scoreFont);

            // High Contrast Text Color Swap Optimization
            if (isLightPanel) {
                SetHighColor(40, 45, 50, 255); // Deep high-contrast charcoal black text
            } else {
                SetHighColor(255, 255, 255, 220); // Default clean light/white text overlay
            }

            BString leftScoreStr, rightScoreStr;
            leftScoreStr.SetToFormat("%" B_PRId32, fLeftScore);
            rightScoreStr.SetToFormat("%" B_PRId32, fRightScore);

            // Calculate centered offsets for left and right scoreboard text blocks
            float midPointX = startX + (artworkWidth / 2.0f);
            float scoreY = 18.0f; // Position safely near the top ceiling margin

            DrawString(leftScoreStr.String(), BPoint(midPointX - 35.0f, scoreY));
            DrawString(rightScoreStr.String(), BPoint(midPointX + 22.0f, scoreY));


            // --- PROCEDURAL WHITE VECTOR DOG RENDERING OVERLAY ---
            if (fDogDrawActive) {
                SetDrawingMode(B_OP_ALPHA);
                SetHighColor(255, 255, 255, 230); // Clean white dog body
                
                // Draw main body torso
                FillRect(BRect(fDogDrawX - 7.0f, fDogDrawY - 4.0f, fDogDrawX + 7.0f, fDogDrawY + 3.0f));
                
                // Draw head block
                FillRect(BRect(fDogDrawX + 4.0f, fDogDrawY - 9.0f, fDogDrawX + 11.0f, fDogDrawY - 3.0f));
                
                // Draw legs
                FillRect(BRect(fDogDrawX - 6.0f, fDogDrawY + 3.0f, fDogDrawX - 4.0f, fDogDrawY + 8.0f)); // Back Leg
                FillRect(BRect(fDogDrawX + 4.0f, fDogDrawY + 3.0f, fDogDrawX + 6.0f, fDogDrawY + 8.0f)); // Front Leg
                
                // Little wagging tail
                SetPenSize(1.5f);
                static int tailWag = 0;
                tailWag++;
                if (tailWag % 2 == 0) {
                    StrokeLine(BPoint(fDogDrawX - 7.0f, fDogDrawY - 2.0f), BPoint(fDogDrawX - 11.0f, fDogDrawY - 6.0f));
                } else {
                    StrokeLine(BPoint(fDogDrawX - 7.0f, fDogDrawY - 2.0f), BPoint(fDogDrawX - 12.0f, fDogDrawY - 2.0f));
                }
            }


            // --- VICTORY WIN MESSAGE SCREEN OVERLAY WITH COUNTDOWN ---
            // Keep variables persistent across frames at function-level scope
            static bigtime_t drawWinStartTime = 0;
            static bool drawTimerStarted = false;

            if (fLeftScore >= 10 || fRightScore >= 10) {
                if (!drawTimerStarted) {
                    drawWinStartTime = system_time();
                    drawTimerStarted = true;
                }
                
                // Calculate seconds remaining (from 3 down to 1)
                bigtime_t elapsed = system_time() - drawWinStartTime;
                int secondsLeft = 3 - (int)(elapsed / 1000000);
                if (secondsLeft < 1) secondsLeft = 1; 

                BFont winFont;
                GetFont(&winFont);
                winFont.SetSize(16.0f); 
                winFont.SetFace(B_BOLD_FACE);
                SetFont(&winFont);
                
                SetHighColor(255, 215, 0, 255); // Golden color text
                
                BString winStr;
                if (fLeftScore >= 10) {
                    winStr.SetTo("COMPUTER WINS!");
                    DrawString(winStr.String(), BPoint(midPointX - 65.0f, (height / 2.0f) - 6.0f));
                } else {
                    winStr.SetTo("YOU WIN!");
                    DrawString(winStr.String(), BPoint(midPointX - 35.0f, (height / 2.0f) - 6.0f));
                }
                
                // Draw the countdown subtext
                BFont subFont;
                GetFont(&subFont);
                subFont.SetSize(10.0f); 
                SetFont(&subFont);
                SetHighColor(200, 200, 200, 200); // Muted silver overlay
                
                BString countStr;
                countStr.SetToFormat("Restarting in %d...", secondsLeft);
                DrawString(countStr.String(), BPoint(midPointX - 42.0f, (height / 2.0f) + 12.0f));
                
                // Revert font adjustments back to defaults for remaining passes
                SetFont(&scoreFont);
            } else {
                // Safely reset drawing timer flags when game is active
                drawTimerStarted = false;
                drawWinStartTime = 0;
            }

            // --- DRAW LEFT PADDLE WITH SAFETY CONTRAST CHECK ---
            rgb_color leftPaddleCol = fArtworkPalette[4];
            float leftPaddleBrightness = (leftPaddleCol.red * 0.299f) + 
                                         (leftPaddleCol.green * 0.587f) + 
                                         (leftPaddleCol.blue * 0.114f);
            if (leftPaddleBrightness < 80.0f) {
                SetHighColor(50, 230, 100, 255); // Vibrant neon green fallback
            } else {
                SetHighColor(leftPaddleCol);
            }
            FillRect(BRect(startX, fLeftPaddlePos - (paddleH / 2.0f), startX + 5.0f, fLeftPaddlePos + (paddleH / 2.0f)));

            // --- DRAW RIGHT PADDLE WITH SAFETY CONTRAST CHECK ---
            rgb_color rightPaddleCol = fArtworkPalette[58];
            float rightPaddleBrightness = (rightPaddleCol.red * 0.299f) + 
                                          (rightPaddleCol.green * 0.587f) + 
                                          (rightPaddleCol.blue * 0.114f);
            if (rightPaddleBrightness < 80.0f) {
                SetHighColor(50, 230, 100, 255); // Vibrant neon green fallback
            } else {
                SetHighColor(rightPaddleCol);
            }
            FillRect(BRect(startX + artworkWidth - 5.0f, fRightPaddlePos - (paddleH / 2.0f), startX + artworkWidth, fRightPaddlePos + (paddleH / 2.0f)));

            // --- LAYER: PROCEDURAL RADIAL ARC SHOCKWAVE EXPLOSION ---
            if (fPongExplosionTick > 0) {
                int progress = 21 - fPongExplosionTick; 
                float currentRadius = progress * 1.8f; // Expanded step factor for speed scaling
                
                SetPenSize(2.0f); // Bold vector edges
                
                for (int ring = 0; ring < 3; ring++) {
                    float ringRadius = currentRadius - (ring * 4.0f);
                    if (ringRadius < 1.0f) continue;

                    // Flash between flame-orange and neon-electric cyan
                    if (ring % 2 == 0) {
                        SetHighColor(255, 90, 0, (uint8)(fPongExplosionTick * 12)); 
                    } else {
                        SetHighColor(0, 240, 255, (uint8)(fPongExplosionTick * 12));
                    }

                    // Render crosshair starburst fragments around the center point
                    StrokeLine(BPoint(fPongExplosionX - ringRadius, fPongExplosionY), BPoint(fPongExplosionX - ringRadius - 4.0f, fPongExplosionY));
                    StrokeLine(BPoint(fPongExplosionX + ringRadius, fPongExplosionY), BPoint(fPongExplosionX + ringRadius + 4.0f, fPongExplosionY));
                    StrokeLine(BPoint(fPongExplosionX, fPongExplosionY - ringRadius), BPoint(fPongExplosionX, fPongExplosionY - ringRadius - 4.0f));
                    StrokeLine(BPoint(fPongExplosionX, fPongExplosionY + ringRadius), BPoint(fPongExplosionX, fPongExplosionY + ringRadius + 4.0f));
                    
                    // Draw outer expanding shockwave boundary circle
                    StrokeEllipse(BPoint(fPongExplosionX, fPongExplosionY), ringRadius, ringRadius);
                }
            }

            // Render the two spheres
            // FIXED RENDERING PASS: Forces the loop boundary down from 2 to 1 (Only draws active k = 0 ball)
            for (int k = 0; k < 1; k++) {
                // Keep the ball completely hidden if it is dead/exploding so it doesn't overlap the shockwave
                if (fMotoCrashTicks > 0) continue;

                rgb_color glowColor = fArtworkPalette[10]; // Clean fixed color pairing accent
                SetHighColor(glowColor.red, glowColor.green, glowColor.blue, 120);
                FillEllipse(BPoint(fBallX[k], fBallY[k]), (fBallSize[k] / 2.0f) + 3.0f, (fBallSize[k] / 2.0f) + 3.0f);

                SetHighColor(255, 255, 255, 255);
                FillEllipse(BPoint(fBallX[k], fBallY[k]), fBallSize[k] / 2.0f, fBallSize[k] / 2.0f);
            }
            
            SetDrawingMode(B_OP_COPY);
            SetPenSize(1.0f);
        }





        else if (fVisualizerMode == MODE_RAINDROPS) {
            // Mode 5: Audio-Reactive Falling Particle Rain Drops
            SetDrawingMode(B_OP_ALPHA);
            SetPenSize(1.8f);

            float systemTimeSec = (float)system_time() / 1000000.0f;
            float pulseWave = (sinf(systemTimeSec * 4.5f) + 1.0f) / 2.0f; 
            float dynamicOpacityPct = 0.35f + (pulseWave * 0.50f); 

            for (int i = 0; i < 75; i++) {
                int frequencyIndex = (int)(fRainX[i] * 63.0f);
                float audioDrive = fBarHeights[frequencyIndex] / height; 

                fRainY[i] += fRainSpeed[i] * (1.0f + audioDrive * 2.5f);

                if (fRainY[i] > 1.0f) {
                    fRainY[i] = 0.0f;
                    fRainX[i] = (float)(rand() % 1000) / 1000.0f;
                    fRainSpeed[i] = 0.01f + ((rand() % 100) / 10000.0f);
                }

                float currentX = startX + (fRainX[i] * artworkWidth);
                float currentY = fRainY[i] * height;

                rgb_color dropColor = fArtworkPalette[frequencyIndex];

                rgb_color transparentBlendedColor;
                transparentBlendedColor.red   = (uint8)(dropColor.red   * dynamicOpacityPct + bgCol.red   * (1.0f - dynamicOpacityPct));
                transparentBlendedColor.green = (uint8)(dropColor.green * dynamicOpacityPct + bgCol.green * (1.0f - dynamicOpacityPct));
                transparentBlendedColor.blue  = (uint8)(dropColor.blue  * dynamicOpacityPct + bgCol.blue  * (1.0f - dynamicOpacityPct));
                transparentBlendedColor.alpha = 255;

                float tailLength = 4.0f + (audioDrive * 12.0f); 
                SetHighColor(transparentBlendedColor);
                StrokeLine(BPoint(currentX, currentY - tailLength), BPoint(currentX, currentY));
            }
            
            // --- LAYER: RENDER ACTIVE DETONATION SPARK PARTICLES ---
            SetPenSize(2.2f); // Gives particles a visible, punchy retro pixel weight
            for (int s = 0; s < 12; s++) {
                if (fSparkLife[s] > 0) {
                    // Flash high-contrast orange vs neon bright yellow spark clusters
                    if (rand() % 100 > 45) {
                        SetHighColor(255, 65, 0, 255);   // High-heat Vermilion
                    } else {
                        SetHighColor(255, 225, 10, 255);  // Retro Arcade Yellow
                    }

                    // FIX: Render directly onto coordinates now that math is uncoupled
                    float sx = fSparkX[s];
                    float sy = fSparkY[s];
                    
                    FillRect(BRect(sx, sy, sx + 2.0f, sy + 2.0f));
                }
            }


            
            SetDrawingMode(B_OP_COPY);
            SetPenSize(1.0f);
        }
        
        
		else if (fVisualizerMode == MODE_MOTO_RIDER) {
            // Mode 6: Endless Motorcycle Runner with Parallax & Scoreboard Display
            SetDrawingMode(B_OP_ALPHA);
            float baselineY = height - 2.0f; float bgBrightness = (bgCol.red * 0.299f) + (bgCol.green * 0.587f) + (bgCol.blue * 0.114f); bool isDarkBg = (bgBrightness < 100.0f);
            // Fetch live audio vars to match the physics thread scaling calculations exactly
            float lowBassPulse = (fBarHeights[2] + fBarHeights[3] + fBarHeights[4]) / 3.0f; float bassNormalized = (height > 0.0f) ? (lowBassPulse / height) : 0.0f;             
            // --- LAYER 0: SKY RESIDENT LAYER (Drifting Clouds) ---
            SetPenSize(1.0f); SetHighColor(isDarkBg ? rgb_color{110, 125, 140, 120} : rgb_color{200, 205, 210, 150});
            for (int c = 0; c < 3; c++) {
                float cx = startX + fCloudX[c]; float cy = fCloudY[c]; float cw = fCloudSize[c];
                FillEllipse(BPoint(cx, cy), cw / 2.0f, 3.5f); FillEllipse(BPoint(cx + (cw * 0.2f), cy - 2.0f), cw / 3.0f, 3.0f); FillEllipse(BPoint(cx - (cw * 0.2f), cy - 1.0f), cw / 3.5f, 2.5f);
            }             
            // --- LAYER 1: DISTANT PARALLAX MOUNTAINS (FIXED STUTTER & RANDOM SIZES) ---
            // Loops 4 cleanly separated sequential indices across the pre-calculated width buffers
            for (int m = 0; m < 4; m++) {
                // Read custom sizing profile scalar set inside your math loop
                float currentMtnScale = (fMtnHeightScale[m] > 0.01f) ? fMtnHeightScale[m] : (0.8f + (m * 0.15f));
                float peakHeight = 26.0f * currentMtnScale;                
                // Matches the strict 240px wide modular boundary step to keep the scrolling continuous
                float mx = fMtnScrollX + (m * 240.0f);                
                BPoint triangle[3] = {
                    BPoint(startX + mx, baselineY),
                    BPoint(startX + mx + 120.0f, baselineY - peakHeight), 
                    BPoint(startX + mx + 240.0f, baselineY)
                };                
                // Base Solid Mountain Color
                SetHighColor(isDarkBg ? rgb_color{55, 68, 82, 255} : rgb_color{190, 198, 205, 255}); FillPolygon(triangle, 3);                 
                // Outer Structural Depth Accent Edging Line
                SetHighColor(isDarkBg ? rgb_color{85, 100, 115, 255} : rgb_color{165, 175, 185, 255}); SetPenSize(1.2f); StrokePolygon(triangle, 3);                
            }             
            // --- LAYER 2: MIDGROUND LAYER (Random Green Stick Trees) ---
            SetHighColor(35, 155, 75, 255); SetPenSize(1.5f);   
            for (int t = 0; t < 4; t++) {
                float tx = startX + fTreeX[t]; float th = fTreeHeight[t];
                StrokeLine(BPoint(tx, baselineY), BPoint(tx, baselineY - th));
                StrokeLine(BPoint(tx, baselineY - th), BPoint(tx - 4.0f, baselineY - th + 5.0f)); StrokeLine(BPoint(tx, baselineY - th), BPoint(tx + 4.0f, baselineY - th + 5.0f));
                StrokeLine(BPoint(tx, baselineY - th + 4.0f), BPoint(tx - 6.0f, baselineY - th + 10.0f)); StrokeLine(BPoint(tx, baselineY - th + 4.0f), BPoint(tx + 6.0f, baselineY - th + 10.0f));
            }                 
            // --- LAYER 3: LIVE GAME GROUND RUNNER horizon tracks ---
            SetHighColor(isDarkBg ? rgb_color{80, 90, 100, 255} : rgb_color{180, 185, 190, 255}); 
            SetPenSize(2.0f);  StrokeLine(BPoint(startX, baselineY), BPoint(startX + artworkWidth, baselineY)); SetHighColor(bgCol); 
            for (int o = 0; o < 2; o++) {
                // If the obstacle is either a pit (1) or water pocket (2), carve out the black drop gap
                if (fObsIsPit[o] == 1 || fObsIsPit[o] == 2) {
                    StrokeLine(BPoint(startX + fObsX[o] + 1.0f, baselineY), BPoint(startX + fObsX[o] + 23.0f, baselineY));
                }
            }            
            // --- LAYER 4: MULTI-HAZARD DRAW ENGINES (High Contrast Rocks, Pits, Water Blue Pools, & Spikes) ---
            int32 themeColorIndex = 20; 
            for (int o = 0; o < 2; o++) {
                if (fObsIsPit[o] == 0) {
                    // --- HAZARD TYPE 0: SOLID ROCK BLOCK ---
                    float audioGrowthFactor = 1.0f;
                    if (o == 1) { 
                        audioGrowthFactor += (bassNormalized * 1.6f); 
                    }                    
                    float finalObsHeight = 10.0f * fObsHeightScale[o] * audioGrowthFactor; float obsWidth = (fObsHeightScale[o] < 0.9f) ? 7.0f : ((fObsHeightScale[o] > 1.2f) ? 5.0f : 10.0f);                    
                    BRect rockBounds(startX + fObsX[o], baselineY - finalObsHeight, startX + fObsX[o] + obsWidth, baselineY);  SetHighColor(fArtworkPalette[themeColorIndex]);
                    FillRect(rockBounds);                       
                    if (fObsHeightScale[o] > 1.2f) {
                        SetHighColor(240, 70, 70, 255); 
                        FillRect(BRect(startX + fObsX[o], baselineY - finalObsHeight, startX + fObsX[o] + obsWidth, baselineY - finalObsHeight + 2.0f));
                    }                    
                    // CRITICAL VISIBILITY CORRECTION: Wrap stone geometry in bright safety strokes if background is dark
                    SetPenSize(1.0f);
                    SetHighColor(isDarkBg ? rgb_color{255, 255, 255, 220} : rgb_color{0, 0, 0, 220});
                    StrokeRect(rockBounds);                    
                } else if (fObsIsPit[o] == 1) {
                    // --- HAZARD TYPE 1: EMPTY GROUND PIT GAP ---
                    SetHighColor(fArtworkPalette[themeColorIndex]);
                    FillRect(BRect(startX + fObsX[o] - 2.0f, baselineY - 3.0f, startX + fObsX[o], baselineY));
                    FillRect(BRect(startX + fObsX[o] + 24.0f, baselineY - 3.0f, startX + fObsX[o] + 26.0f, baselineY));                    
                    // Add high contrast neon warning trim to edges
                    SetHighColor(255, 60, 60, 255);
                    StrokeLine(BPoint(startX + fObsX[o], baselineY), BPoint(startX + fObsX[o] + 24.0f, baselineY));                    
                } else if (fObsIsPit[o] == 2) {
                    // --- HAZARD TYPE 2: NEON WATER POOL (BLUE OBSTACLE) ---
                    BRect waterBounds(startX + fObsX[o], baselineY + 1.0f, startX + fObsX[o] + 24.0f, baselineY + 6.0f);                    
                    SetHighColor(0, 130, 255, 255); // Rich deep hazard blue pool fill
                    FillRect(waterBounds);                    
                    SetHighColor(0, 240, 255, 255); // Radiant glowing surface layer line
                    StrokeLine(BPoint(startX + fObsX[o], baselineY + 1.0f), BPoint(startX + fObsX[o] + 24.0f, baselineY + 1.0f));                    
                    // Safety shoreline markers
                    SetHighColor(isDarkBg ? rgb_color{255, 255, 255, 180} : rgb_color{0, 0, 0, 180});
                    StrokeLine(BPoint(startX + fObsX[o], baselineY), BPoint(startX + fObsX[o], baselineY + 4.0f));
                    StrokeLine(BPoint(startX + fObsX[o] + 24.0f, baselineY), BPoint(startX + fObsX[o] + 24.0f, baselineY + 4.0f));
                } else if (fObsIsPit[o] == 3 || fObsIsPit[o] == 4) {
                    // --- HAZARD TYPE 3 & 4: PULSING SHARP SHOCK-SPIKE BLADES ---
                    int spikeCount = (fObsIsPit[o] == 3) ? 4 : 5;
                    float spikeWidth = 8.0f;
                    float spikeAudioScale = 1.0f + (bassNormalized * 1.2f);

                    for (int sIdx = 0; sIdx < spikeCount; sIdx++) {
                        float leftX = startX + fObsX[o] + (sIdx * spikeWidth);
                        float rightX = leftX + spikeWidth;
                        float centerX = leftX + (spikeWidth / 2.0f);

                        // Match physical sizing structure profile equations precisely
                        float sizeVariation = 0.7f + (((sIdx * 3) % 5) / 6.0f);
                        float currentSpikeHeight = 7.0f * fObsHeightScale[o] * sizeVariation * spikeAudioScale;
                        float tipY = baselineY - currentSpikeHeight;

                        // Solid Semi-Translucent Triangle Body Fill
                        SetHighColor(255, 90, 0, 95); 
                        BPoint spikeTri[3] = { BPoint(leftX, baselineY), BPoint(centerX, tipY), BPoint(rightX, baselineY) };
                        FillPolygon(spikeTri, 3);

                        // High Contrast Solid Accent Border Outlines
                        SetPenSize(1.2f);
                        SetHighColor(isDarkBg ? rgb_color{255, 110, 50, 240} : rgb_color{210, 40, 10, 240});
                        StrokeLine(BPoint(leftX, baselineY), BPoint(centerX, tipY));
                        StrokeLine(BPoint(centerX, tipY), BPoint(rightX, baselineY));
                    }
                }
            }                      
           
            // --- LAYER 5: SCOREBOARD TRACKING DISPLAY TEXT ---
            BFont scoreFont;   
            GetFont(&scoreFont);
            scoreFont.SetSize(11.0f); 
            SetFont(&scoreFont);            
            SetHighColor(isDarkBg ? rgb_color{0, 240, 255, 200} : rgb_color{50, 60, 70, 220});
            BString scoreStr;
            scoreStr.SetToFormat("SCORE: %" B_PRId32, fMotoScore);
            DrawString(scoreStr.String(), BPoint(startX + artworkWidth - 68.0f, 15.0f));            
            
            // --- LAYER 6: MOTORCYCLE RIDER VEHICLE BODY & FLIP MECHANIC ---
            float riderX = startX + 45.0f; 
            float riderY = baselineY - fMotoY - 6.0f;

            if (fMotoCrashTicks > 0) {
                SetHighColor(240, 70, 70, 255);
                StrokeLine(BPoint(riderX - 8, baselineY - 4), BPoint(riderX + 8, baselineY - 8));
                StrokeLine(BPoint(riderX - 4, baselineY - 10), BPoint(riderX + 6, baselineY - 4));
            } else {
                // Compute rotation transformation matrices if mid-air stunt is active
                float rad = fFlipRotation * (M_PI / 180.0f);
                float cosR = cosf(rad);
                float sinR = sinf(rad);

                SetHighColor(isDarkBg ? rgb_color{255, 255, 255, 255} : rgb_color{0, 0, 0, 255});
                SetPenSize(1.5f);

                // Calculate relative rotated offsets for bike frame points relative to core body center
                BPoint frameLeft(riderX + (-10.0f * cosR - 0.0f * sinR), riderY + (-10.0f * sinR + 0.0f * cosR));
                BPoint frameRight(riderX + (10.0f * cosR - (-2.0f) * sinR), riderY + (10.0f * sinR + (-2.0f) * cosR));
                StrokeLine(frameLeft, frameRight);
                
                BPoint neckBase(riderX + (6.0f * cosR - (-2.0f) * sinR), riderY + (6.0f * sinR + (-2.0f) * cosR));
                BPoint handlebars(riderX + (8.0f * cosR - (-9.0f) * sinR), riderY + (8.0f * sinR + (-9.0f) * cosR));
                StrokeLine(neckBase, handlebars);

                // Sparks stream dynamically out from the rotated exhaust tailpipe placement
                BPoint tailpipe(riderX + (-10.0f * cosR - 1.0f * sinR), riderY + (-10.0f * sinR + 1.0f * cosR));
                for (int s = 0; s < 12; s++) {
                    if (fSparkLife[s] > 0) {
                        SetHighColor(255, rand() % 80 + 150, (rand() % 100 > 50) ? 0 : 255, 255); 
                        float sx = tailpipe.x + fSparkX[s];
                        float sy = tailpipe.y + fSparkY[s];
                        FillRect(BRect(sx, sy, sx + 1.5f, sy + 1.5f));
                    }
                }
                
                // Rotated Wheel Positions (Offsets originally were X:-8, Y:+4 and X:+8, Y:+4)
                BPoint frontWheel(riderX + (8.0f * cosR - 4.0f * sinR), riderY + (8.0f * sinR + 4.0f * cosR));
                BPoint backWheel(riderX + (-8.0f * cosR - 4.0f * sinR), riderY + (-8.0f * sinR + 4.0f * cosR));

                SetHighColor(fArtworkPalette[4]);
                StrokeEllipse(backWheel, 4, 4);                
                SetHighColor(fArtworkPalette[58]);
                StrokeEllipse(frontWheel, 4, 4); 
                
                // Rotated Driver Head and Extremities
                BPoint driverHead(riderX + (0.0f * cosR - (-14.0f) * sinR), riderY + (0.0f * sinR + (-14.0f) * cosR));
                BPoint driverSpine(riderX + (0.0f * cosR - (-11.0f) * sinR), riderY + (0.0f * sinR + (-11.0f) * cosR));
                BPoint driverHip(riderX + (-2.0f * cosR - (-4.0f) * sinR), riderY + (-2.0f * sinR + (-4.0f) * cosR));
                
                SetHighColor(isDarkBg ? rgb_color{0, 240, 255, 255} : rgb_color{20, 30, 40, 255});
                FillEllipse(driverHead, 2.5f, 2.5f); 
                StrokeLine(driverSpine, driverHip); 
                
                BPoint driverFoot(riderX + (-6.0f * cosR - 0.0f * sinR), riderY + (-6.0f * sinR + 0.0f * cosR));
                StrokeLine(driverHip, driverFoot); 
                
                BPoint handlebarsGrip(riderX + (6.0f * cosR - (-7.0f) * sinR), riderY + (6.0f * sinR + (-7.0f) * cosR));
                StrokeLine(driverHip, handlebarsGrip); 
            }
            
               // --- LAYER 6.5: FLOATING STUNT POPUP OVERLAY ---
                if (fStuntTextLife > 0) {
                    BFont stuntFont;
                    GetFont(&stuntFont);
                    stuntFont.SetSize(10.0f);
                    stuntFont.SetFace(B_BOLD_FACE); // Make it pop!
                    SetFont(&stuntFont);

                    // Compute dynamic text color brightness fading out as the life timer expires
                    uint8 alphaFade = (uint8)((fStuntTextLife / 25.0f) * 255.0f);
                    
                    // High-visibility electric gold/yellow stunt text
                    SetHighColor(255, 215, 0, alphaFade); 
                    
                    // Render string offset relative to riderX position
                    float popupTextX = riderX - 22.0f;
                    float popupTextY = baselineY - fStuntTextY;
                    
                    DrawString(fStuntTextStr.String(), BPoint(popupTextX, popupTextY));
                }


            SetDrawingMode(B_OP_COPY);
            SetPenSize(1.0f);
        }

    }

    void UpdateData(const uint8* data, size_t size) {
        if (!cfg.showSpectrumVisuals || !cfg.eqEnabled) return;
        memcpy(frequencyData, data, size > 64 ? 64 : size);
        Invalidate();
    }

private:

    double    fCurrentLevel; 
    uint8     frequencyData[64]; 
    float     fBarHeights[64];   
    float     fBarVelocities[64];
    float     fPeakHeights[64];  
    int       fPeakHold[64];     
    rgb_color fArtworkPalette[64];
    bigtime_t fLastDataTime;    
    int32     fVisualizerMode; 
    

    // Cached Layout Geometry Variables
    float     fCachedWidth;
    float     fCachedStartX;

    // Pong Engine State Storage Values
    float     fBallX[2];
    float     fBallY[2];
    float     fBallDX[2];
    float     fBallDY[2];
    float     fBallSize[2];
    float     startX_cached;
    float     artworkWidth_cached;

    // Rain Drop State Arrays 
    float     fRainX[75];
    float     fRainY[75];
    float     fRainSpeed[75];
    bool      fRainInitNeedsPulse;
    
    float     fLeftPaddlePos;
    float     fRightPaddlePos;
    float     fLeftPaddleTargetOffset;
    float     fRightPaddleTargetOffset;
    
    int32     fLeftScore;
    int32     fRightScore;

    // Mode 6: Endless Moto Rider Game Variables
    float     fMotoY;
    float     fMotoVelocityY;
    int32     fMotoCrashTicks;

    // Obstacle Trackers (Array Size 2)
    float     fObsX[2];
    int fObsIsPit[2]; 
    float     fObsHeightScale[2];
    float fMtnHeightScale[4]; 
    
    // Scoreboard Tracker
    int32     fMotoScore;

    // Parallax Background Storage Objects
    float     fMtnScrollX;
    float     fTreeX[4];
    float     fTreeHeight[4];
    float     fCloudX[3];
    float     fCloudY[3];
    float     fCloudSize[3];

    // --- FIX COMPLETE: Explicit dimensions declared so compiler sees arrays ---
    float     fSparkX[12];       
    float     fSparkY[12];
    float     fSparkDX[12];      
    float     fSparkDY[12];      
    int32     fSparkLife[12];    
    
    // Latency cache fix
	double    fLevelHistory[512];
	bigtime_t fTimeHistory[512];
	int       fHistoryHead = 0;
	int       fHistoryTail = 0;
	bigtime_t fAudioHardwareDelayUs = 130000; 
	bigtime_t fManualSyncOffsetUs = 0; 
	
	int fPongExplosionTick = 0;
	float fPongExplosionX = 0.0f;
	float fPongExplosionY = 0.0f;
	
	bool        fIsFlipping;
    float       fFlipRotation;
    bigtime_t   fLastClickTime;
    
    float     fStuntTextY;       // Relative vertical height offset for the floating text
	int32     fStuntTextLife;    // Ticks remaining before the popup disappears (opacity timer)
	BString   fStuntTextStr;   

    bool      fDogDrawActive;
    float     fDogDrawX;
    float     fDogDrawY;
};





class SongLabel : public BTextView {
public:
    SongLabel(const char* name) : BTextView(name) {
        MakeEditable(false);
        MakeSelectable(false);
        SetWordWrap(true);
        SetAlignment(B_ALIGN_CENTER);
        SetInsets(2, 2, 2, 2); 
        //SetExplicitMinSize(BSize(B_SIZE_UNSET, 50));        
        fScrollOffset = 0.0f;
        fWaitTicks = 0;
        fIsWrapped = true;
        fRawText = "";
    }
    
    BSize MinSize() override {
        if (!cfg.compactMode) {
            // Give it a flexible size based on plain font text metrics or standard text constraints
            float scale = be_plain_font->Size() / 12.0f;
            return BSize(150.0f, 32.0f * scale); // 2 lines or text wrapper safe space
        }
        return BSize(150.0f, 16.0f); 
    }


    BSize PreferredSize() override {
        if (!cfg.compactMode) {
            return BTextView::PreferredSize(); 
        }
        return BSize(150.0f, 20.0f);
    }

    BSize MaxSize() override {
        if (!cfg.compactMode) {
            return BTextView::MaxSize(); 
        }
        return BSize(B_SIZE_UNLIMITED, 24.0f); 
    }

    void SetText(const char* text, const text_run_array* runs = nullptr) {
        if (text == nullptr) {
            fRawText = "";
            BTextView::SetText("");
        } else {
            fRawText = text;
            BTextView::SetText(text, runs);
        }
        ResetMarquee();
    }

    void ResetMarquee() {
        fScrollOffset = 0.0f;
        fWaitTicks = 0;
        Invalidate();
    }
    
    void SetCompactMode(bool enabled) {
        BString currentText(fRawText.String());
    	
        if (!enabled) {
            fScrollOffset = 0.0f;
            fWaitTicks = 0;
            fIsWrapped = true;
            SetWordWrap(true);
            SetAlignment(B_ALIGN_CENTER);            
            BRect r = Bounds();
            r.InsetBy(2, 2);
            SetTextRect(r);
        } else {
            fScrollOffset = 0.0f;
            fWaitTicks = 0;
            fIsWrapped = false;
            SetWordWrap(false);
            
            // CHANGE THIS: Keep alignment centered by default in compact layout setup
            SetAlignment(B_ALIGN_CENTER);            
            BRect r = Bounds();
            r.left = 2; 
            r.right = 99999.0f; 
            SetTextRect(r);
        }
        BTextView::SetText(""); 
        BTextView::SetText(currentText.String()); 
        Invalidate();
    }

    void Pulse() override {
        BTextView::Pulse();

        if (!cfg.compactMode) {
            if (!fIsWrapped || fScrollOffset != 0.0f) {
                fScrollOffset = 0.0f;
                fWaitTicks = 0;
                fIsWrapped = true;
                SetWordWrap(true);
                SetAlignment(B_ALIGN_CENTER);
                BRect r = Bounds();
                r.InsetBy(2, 2);
                SetTextRect(r);
                Invalidate();
            }
            return;
        }
        BFont currentFont;
        GetFontAndColor(0, &currentFont);
        float textWidth = currentFont.StringWidth(fRawText.String());
        float viewWidth = Bounds().Width();

        if (textWidth <= viewWidth) {
            fScrollOffset = 0.0f;
            return;
        }

        if (fScrollOffset == 0.0f && fWaitTicks < 30) {
            fWaitTicks++;
            return;
        }
        fScrollOffset += 1.2f; 

        if (fScrollOffset > (textWidth + 30.0f)) {
            fScrollOffset = -viewWidth;
            fWaitTicks = 0;
        }

        Invalidate();
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
        if (!cfg.compactMode) {
            SetTextRect(r);
        } else {
            r.right = 99999;
            SetTextRect(r);
        }
    }    

    void SetCustomFont(const BFont* font) {
        SetFontAndColor(font); 
        Invalidate();
    }

    void Draw(BRect updateRect) override {
        if (!cfg.compactMode) {
            BTextView::Draw(updateRect);
            return;
        }
        PushState();
        BRegion clipRegion(Bounds());
        ConstrainClippingRegion(&clipRegion);

        rgb_color bgColor = ViewColor();
        SetLowColor(bgColor);
        FillRect(Bounds(), B_SOLID_LOW);

        BFont currentFont;
        rgb_color fontColor;
        GetFontAndColor(0, &currentFont, &fontColor);
        
        font_height fh;
        currentFont.GetHeight(&fh);
        float textY = (Bounds().Height() - (fh.ascent + fh.descent)) / 2.0f + fh.ascent;

        SetHighColor(fontColor);
        SetFont(&currentFont);

        float viewWidth = Bounds().Width();
        float textWidth = currentFont.StringWidth(fRawText.String());

        // CHANGE THIS: Calculate centered position vs marquee scroll path dynamically
        float startX = 0.0f;
        if (textWidth <= viewWidth) {
            // Text fits cleanly! Center it horizontally in the white space gap
            startX = (viewWidth - textWidth) / 2.0f;
        } else {
            // Text overflows, fall back to moving marquee tracking offset coordinates
            startX = -fScrollOffset;
        }

        // Render the calculated position
        DrawString(fRawText.String(), BPoint(startX, textY));

        // --- OVERLAY GRADIENT EDGE FADERS ---
        if (textWidth > viewWidth) {
            float viewHeight = Bounds().Height();
            float fadeWidth = 9.0f; 

            SetDrawingMode(B_OP_ALPHA);
            SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_OVERLAY);

            for (int x = 0; x < (int)fadeWidth; x++) {
                float factor = (float)x / fadeWidth; 
                uint8 alphaVal = (uint8)((1.0f - factor) * 255);
                
                rgb_color fadeColor = bgColor;
                fadeColor.alpha = alphaVal;
                SetHighColor(fadeColor);

                StrokeLine(BPoint((float)x, 0.0f), BPoint((float)x, viewHeight));
                StrokeLine(BPoint(viewWidth - 1.0f - x, 0.0f), BPoint(viewWidth - 1.0f - x, viewHeight));
            }
        }

        PopState();
    }
    
private:
    float   fScrollOffset;
    int32   fWaitTicks;
    bool    fIsWrapped;
    BString fRawText; 
};






void SuperMusicWindow::DownloadStationIcons() {
	
	if (fIsQuitting) return; 
	
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


class ColorItem : public BStringItem {
public:
    ColorItem(const char* text) : BStringItem(text) {}

    virtual void DrawItem(BView* owner, BRect frame, bool complete = false) {
        if (cfg.updateTheme == "Dark") {
            if (IsSelected()) {
                owner->SetHighColor(ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR));
            } else {
                owner->SetHighColor(255, 255, 255); // White for Dark Mode
            }
        } else {
            if (IsSelected()) {
                owner->SetHighColor(ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR));
            } else {
                owner->SetHighColor(ui_color(B_LIST_ITEM_TEXT_COLOR)); 
            }
        }
        
        BStringItem::DrawItem(owner, frame, complete);
    }
};




void RecursiveColorApply(BView* view, rgb_color bg, rgb_color txt) {
    if (!view) return;
    
    // Explicitly apply theme properties to the current view component
    view->SetViewColor(bg);
    view->SetLowColor(bg);
    view->SetHighColor(txt);
    
    // --- Special Type Invalidation overrides ---
    if (BSlider* slider = dynamic_cast<BSlider*>(view)) {
        slider->UseFillColor(true, &txt);
    }

    if (BTextView* textView = dynamic_cast<BTextView*>(view)) {
        textView->SetFontAndColor(NULL, B_FONT_ALL, &txt);
    }

    if (BStringView* stringView = dynamic_cast<BStringView*>(view)) {
        // Explicitly forces text label strings to draw cleanly in the correct theme color
        stringView->SetHighColor(txt);
    }

    if (BListView* listView = dynamic_cast<BListView*>(view)) {
        for (int32 i = 0; i < listView->CountItems(); i++) {
            listView->InvalidateItem(i);
        }
    }

    view->Invalidate();
    
    // Safely iterate through every single child object recursively without skipping branches
    for (int32 i = 0; i < view->CountChildren(); i++) {
        RecursiveColorApply(view->ChildAt(i), bg, txt);
    }
}





void SuperMusicWindow::ApplyTheme() {
    rgb_color bgVal;
    rgb_color bg2Val;
    rgb_color txtVal;

    if (cfg.updateTheme == "Dark") {
        bgVal = {40, 40, 40, 255};      // Dark Grey
        bg2Val = {0, 0, 0, 255};        // Pure Black for lists
        txtVal = {255, 255, 255, 255};  // Pure White
    } else {
        bgVal = ui_color(B_PANEL_BACKGROUND_COLOR);
        bg2Val = ui_color(B_PANEL_BACKGROUND_COLOR);
        txtVal = ui_color(B_PANEL_TEXT_COLOR);
    }

    float scale = be_bold_font->Size() / 12.0f; 

    BFont boldFont(be_bold_font);
    boldFont.SetSize(12.0 * scale);

    if (Lock()) {
		if (fTabView) {
    		fTabView->SetViewColor(bgVal);
    
    		for (int32 i = 0; i < fTabView->CountTabs(); i++) {
        		BTab* tab = fTabView->TabAt(i);
        		if (tab == nullptr) continue; // Safety check for the tab object

        		BView* tabView = tab->View(); // Get the view associated with the tab
        		if (tabView != nullptr) {
                    RecursiveColorApply(tabView, bgVal, txtVal);
        		}
    		}
		}

        if (fPresetList) {
            fPresetList->SetViewColor(bg2Val);
            fPresetList->SetLowColor(bgVal);
            fPresetList->SetHighColor(txtVal); 
            fPresetList->Invalidate();
        }

        if (fPresetScroll) {
            fPresetScroll->SetViewColor(bgVal);
            if (BScrollBar* sb = fPresetScroll->ScrollBar(B_VERTICAL)) {
                sb->SetViewColor(bgVal);
                sb->Invalidate();
            }
            fPresetScroll->Invalidate();
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

        // --- NEW CONFIG DECK THEME PROFILES SYNC ---
        // 1. Cleanly update all 15 EQ sliders
        for (int i = 0; i < 15; i++) {
            if (fEQSliders[i]) {
                fEQSliders[i]->SetViewColor(bgVal);
                fEQSliders[i]->SetLowColor(bgVal);
                fEQSliders[i]->SetHighColor(txtVal); // Keeps textual frequency tags visible
                fEQSliders[i]->Invalidate();
            }
        }

        // 2. Clear out container backgrounds and apply high contrast text labels to the limiter sliders
        BSlider* limiterSliders[] = { fLimitInput, fLimitLimit, fLimitRelease };
        for (int s = 0; s < 3; s++) {
            if (limiterSliders[s]) {
                limiterSliders[s]->SetViewColor(bgVal);
                limiterSliders[s]->SetLowColor(bgVal);
                limiterSliders[s]->SetHighColor(txtVal); // Forces "In", "Lmt", and "Rel" strings to draw correctly
                
                // Traverse internal child structures for custom WheelSlider variations
                for (int32 c = 0; c < limiterSliders[s]->CountChildren(); c++) {
                    BView* child = limiterSliders[s]->ChildAt(c);
                    if (child) {
                        child->SetViewColor(bgVal);
                        child->SetLowColor(bgVal);
                        child->SetHighColor(txtVal);
                        child->Invalidate();
                    }
                }
                limiterSliders[s]->Invalidate();
            }
        }

        if (fTabView) fTabView->Invalidate();
        Unlock();
    }
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
		mpv_observe_property(mpv, 0, "volume", MPV_FORMAT_DOUBLE);


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
    // If we are targeting 0 (fading out for a station change), set it instantly.
    // This prevents flooding the event loop with intermediate volume properties.
    if (target_vol == 0.0) {
        mpv_set_property(mpv, "volume", MPV_FORMAT_DOUBLE, &target_vol);
        return;
    }

    // Fallback for any other synchronous volume changes
    double current_vol = 0;
    mpv_get_property(mpv, "volume", MPV_FORMAT_DOUBLE, &current_vol);

    const int steps = 10; // Keep steps low to prevent event lag
    double step_size = (target_vol - current_vol) / steps;
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

            if (!currentAlbumArtUrl.empty() && gGuiWindow) {
                if (gGuiWindow->Lock()) {
                    if (gGuiWindow->fArtCache.count(currentStationID) > 0) {
                        gGuiWindow->fAlbumArt = gGuiWindow->fArtCache[currentStationID];
                        if (gGuiWindow->fArtView) {
                            ((AlbumArtView*)gGuiWindow->fArtView)->SetBitmap(gGuiWindow->fAlbumArt);
                        }
                    } else {
                        gGuiWindow->fAlbumArt = nullptr;
                        if (gGuiWindow->fArtView) {
                            ((AlbumArtView*)gGuiWindow->fArtView)->SetBitmap(nullptr);
                        }

                        std::thread([artUrl = currentAlbumArtUrl]() {
                            download_art(artUrl);
                        }).detach();
                    }
                    gGuiWindow->Unlock();
                }
            }
            break;
        }
    }

    //double original_vol;
    //mpv_get_property(mpv, "volume", MPV_FORMAT_DOUBLE, &original_vol);
    fade_volume(mpv, 0, 400);
    currentSong = "Loading Favorite...";
    if (gGuiWindow && gGuiWindow->Lock()) {
        gGuiWindow->UpdateStatus(currentDesc.c_str(), currentSong.c_str());
        gGuiWindow->Unlock();
    }

    const char *cmd[] = {"loadfile", finalUrl.c_str(), NULL};
    mpv_command(mpv, cmd);
    
    //fade_volume(mpv, original_vol, 600);
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

    //double original_vol;
    //mpv_get_property(mpv, "volume", MPV_FORMAT_DOUBLE, &original_vol);
    fade_volume(mpv, 0, 400); 

    currentSong = "Buffering...";
    UpdateStatus(currentStation.c_str(), currentSong.c_str());

    std::string url = get_quality_url(chan); 
    const char *cmd[] = {"loadfile", url.c_str(), NULL};
    mpv_command(mpv, cmd);    
   // fade_volume(mpv, original_vol, 600);
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

    //double original_vol;        
    //mpv_get_property(mpv, "volume", MPV_FORMAT_DOUBLE, &original_vol);
    fade_volume(mpv, 0, 400);

    int idx = rand() % channels.size();
    Channel& chan = channels[idx];
    currentStation = chan.title;
    currentStationID = chan.id; 
    currentDesc = chan.desc;
    currentListeners = chan.listeners;
    currentSong = "Buffering...";
    currentAlbumArtUrl = chan.largeimage;

    if (!currentAlbumArtUrl.empty() && gGuiWindow) {
        if (gGuiWindow->Lock()) {          
            if (gGuiWindow->fArtCache.count(currentStationID) > 0) {
                gGuiWindow->fAlbumArt = gGuiWindow->fArtCache[currentStationID];
                if (gGuiWindow->fArtView) {
                    ((AlbumArtView*)gGuiWindow->fArtView)->SetBitmap(gGuiWindow->fAlbumArt);
                }
            } else {
                gGuiWindow->fAlbumArt = nullptr;
                if (gGuiWindow->fArtView) {
                    ((AlbumArtView*)gGuiWindow->fArtView)->SetBitmap(nullptr);
                }
                
                std::thread([url = currentAlbumArtUrl]() {
                    download_art(url);
                }).detach();
            }
            gGuiWindow->UpdateStatus(currentDesc.c_str(), currentSong.c_str());
            gGuiWindow->Unlock();
        }
    }
    
    std::string url = get_quality_url(chan); 
    const char *cmd[] = {"loadfile", url.c_str(), NULL};
    mpv_command(mpv, cmd);
    
    //fade_volume(mpv, original_vol, 600);
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
	if (list == nullptr)
    return;
    list->MakeEmpty();
    if (!std::filesystem::exists(folderPath)) return;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(folderPath)) {
        if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().string();
            if (ext == ".milk" || ext == ".milk2") {
                list->AddItem(new ColorItem(entry.path().filename().string().c_str()));
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
    while (visualsRunning && pm) { 
    
    
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
    WheelSlider(const char* name, const char* label, BMessage* msg, 
                int32 min, int32 max, orientation orient, int32 multiplier = 1)
        : BSlider(name, label, msg, min, max, orient),
          fMultiplier(multiplier) {}

    virtual void MessageReceived(BMessage* msg) {
        if (msg->what == B_MOUSE_WHEEL_CHANGED) {
            float dy;
            if (msg->FindFloat("be:wheel_delta_y", &dy) == B_OK) {
                int32 min, max;
                GetLimits(&min, &max);
                
                // Scale the delta by our multiplier
                int32 newValue = Value() - (int32)(dy * fMultiplier);
                
                // Clamping values within limits
                if (newValue < min) newValue = min;
                if (newValue > max) newValue = max;
                
                SetValue(newValue);
                Invoke(); 
            }
        } else {
            BSlider::MessageReceived(msg);
        }
    }

private:
    int32 fMultiplier;
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



class ClickableURLIcon : public BView {
public:
    ClickableURLIcon(const char* name, BBitmap* icon, const char* url)
        : BView(name, B_WILL_DRAW), fIcon(icon), fUrl(url) {}

    void Draw(BRect updateRect) override {
        if (fIcon) {
            SetDrawingMode(B_OP_ALPHA);
            DrawBitmap(fIcon, BPoint(0, 0));
        }
    }

    void MouseDown(BPoint point) override {
        const char* url = fUrl.String();
        be_roster->Launch("text/html", 1, (char**)&url);
    }

    virtual void GetPreferredSize(float* _width, float* _height) override {
        if (fIcon) {
            *_width = fIcon->Bounds().Width();
            *_height = fIcon->Bounds().Height();
        }
    }

private:
    BBitmap* fIcon;
    BString  fUrl;
};



class IconView : public BView {
public:
    IconView(BBitmap* bitmap)
        : BView("icon_view", B_WILL_DRAW),
          fBitmap(bitmap) {
        // Essential: Tell the layout kit we want a fixed size
        SetExplicitMinSize(BSize(64, 64));
        SetExplicitMaxSize(BSize(64, 64));
        SetExplicitPreferredSize(BSize(64, 64));
    }

virtual ~IconView() {
    delete fBitmap;
}

    virtual void Draw(BRect updateRect) {
        if (!fBitmap) return;
        
        SetDrawingMode(B_OP_ALPHA);
        // Center the icon within the view bounds
        float x = (Bounds().Width() - fBitmap->Bounds().Width()) / 2.0f;
        float y = (Bounds().Height() - fBitmap->Bounds().Height()) / 2.0f;
        DrawBitmap(fBitmap, BPoint(x, y));
    }

private:
    BBitmap* fBitmap;
};





void SuperMusicWindow::UpdateStatus(const char* station, const char* song) {
    if (!Lock()) return;

    const char* safeStation = (station != nullptr) ? station : "";
    const char* safeSong = (song != nullptr) ? song : "Streaming...";

    if (fDescView != nullptr) {
        ((SongLabel*)fDescView)->SetText(safeStation);
    }

    if (fSongView != nullptr) {
        ((SongLabel*)fSongView)->SetText(safeSong);
    }
    
    Unlock();
}






void SuperMusicWindow::UpdateTrayState(bool enabled, bool hideWindow) {
    BDeskbar deskbar;
    const char* trayItemName = "SuperMusicTrayIcon"; 

    if (enabled) {
        if (!deskbar.HasItem(trayItemName)) {
            status_t err = B_ERROR;

			#ifndef IS_HAIKU_32BIT
            // 64-bit Native: Keep using the fast internal executable allocation reference
            app_info info;
            be_app->GetAppInfo(&info);             
            err = deskbar.AddItem(&info.ref);
			#else
            // 32-bit Hybrid: Dynamically look up the signature of the GCC 2 shared add-on library
            entry_ref addonRef;
            if (be_roster->FindApp("application/x-vnd.SuperMusicTrayIconLibrary", &addonRef) == B_OK) {
                err = deskbar.AddItem(&addonRef);
            }
			#endif
            
            if (err == B_OK && hideWindow) {
                Hide();
            }
        } else if (hideWindow) {
            Hide();
        }
    } else {
        if (deskbar.HasItem(trayItemName)) {
            deskbar.RemoveItem(trayItemName);
        }
        if (IsHidden()) Show();
    }
}




SuperMusicWindow::SuperMusicWindow()
    : BWindow(BRect(100, 100, 575, 250), "SuperMusicThingy", B_TITLED_WINDOW, 
    B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS | B_QUIT_ON_WINDOW_CLOSE)
    
{

	SetPulseRate(50000); 
    fAlbumArt = nullptr;
    #ifdef USE_PROJECTM
    fProjectM = pm; 
	#endif
    
    setenv("LADSPA_PATH", "/boot/home/config/non-packaged/lib/ladspa", 1);     
 
    BFont largeFont(be_bold_font);
    BFont smallFont(be_bold_font);
    
    float scale = be_plain_font->Size() / 12.0f; 
	largeFont.SetSize(18.0f * scale);
	smallFont.SetSize(12.0f * scale);
	
    fTabView = new BTabView("tab_container");
    //fTabView->SetExplicitMinSize(BSize(380 * scale, 700 * scale));

    fTabView->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

    // ==========================================
    // TAB 1: PLAYER VIEW (The "Radio" Interface)
    // ==========================================
    fPlayerGroup = new BGroupView(B_VERTICAL, 10);
    fPlayerGroup->SetName("Radio"); 

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
    fSongView->SetAlignment(B_ALIGN_CENTER);
    
    fquality = new BStringView("quality", "");
    fquality->SetFont(&smallFont);    
    fListenersView = new BStringView("listeners", "");
    fListenersView->SetFont(&smallFont);	
	fCompactModeRadio = new BCheckBox("chk_compact_radio", "Compact", new BMessage(MSG_COMPACTM_CHANGED));
	fCompactModeRadio->SetFont(&smallFont);	
	fCompactModeConfig = new BCheckBox("chk_compact_config", "Compact Mode", new BMessage(MSG_COMPACTM_CHANGED));

	
   
    
    // Album Art

    fArtView = new AlbumArtView();
	fArtView->SetExplicitSize(BSize(350 * scale, 350 * scale)); 
    fArtView->SetExplicitMinSize(BSize(350 * scale, 350 * scale));
	fArtView->SetExplicitMaxSize(BSize(350 * scale, 350 * scale));
	if (cfg.showSpectrumVisuals) fSpectrum = new SpectrumView(BRect(0, 0, 350, 100), "spectrum"); 
    if (!cfg.showSpectrumVisuals) fSpectrum = new SpectrumView(BRect(0, 0, 350, 1), "spectrum"); 
	//fSpectrum->SetExplicitMinSize(BSize(350, 75));
	//fSpectrum->SetExplicitMaxSize(BSize(350, B_SIZE_UNSET)); 
    
    BBitmap* heartIcon = GetVectorIcon(kIconFav, kIconFavSize, 40);
	fBtnAddFav = new IconButton("btn_add_fav", heartIcon, new BMessage(MSG_ADD_FAV));
	fBtnAddFav->SetExplicitSize(BSize(40, 40));

	
	
	BBitmap* pauseIcon = GetVectorIcon(kIconPause, kIconPauseSize, 40);
    fPauseBtn = new IconButton("btn_pause", pauseIcon, new BMessage(MSG_PAUSE));
	fPauseBtn->SetExplicitSize(BSize(75, 75)); 
	
	BBitmap* playIcon = GetVectorIcon(kIconPlay, kIconPlaySize, 40);
    fPlayBtn = new IconButton("btn_play", playIcon, new BMessage(MSG_PLAY));
	fPlayBtn->SetExplicitSize(BSize(75, 75)); 
    
    BBitmap* stopIcon = GetVectorIcon(kIconStop, kIconStopSize, 40);
    fStopBtn = new IconButton("btn_stop", stopIcon, new BMessage(MSG_STOP));
	fStopBtn->SetExplicitSize(BSize(75, 75)); 
	
	BBitmap* shuffleIcon = GetVectorIcon(kIconShuffle, kIconShuffleSize, 40);
   	fShuffleBtn = new IconButton("btn_shuffle", shuffleIcon, new BMessage(MSG_SHUFFLE));
	fShuffleBtn->SetExplicitSize(BSize(75, 75)); 	
    
    fVolumeSlider = new WheelSlider("volume", "", new BMessage(MSG_VOL_CHANGE), 0, 100, B_HORIZONTAL, 5);
    fVolumeSlider->SetValue(100);
    fVolumeSlider->SetTarget(this); 
    fVolumeSlider->SetModificationMessage(new BMessage(MSG_VOL_CHANGE));


    // --- LAYOUT BUILDER FOR PLAYER TAB ---

fControlStack = new BGroupView(B_VERTICAL, 5); 

BLayoutBuilder::Group<>(fControlStack, B_VERTICAL, 5)

    .SetInsets(5)  
    .Add(fVolumeSlider)
        .AddGroup(B_HORIZONTAL, 5)
        //.AddGlue() 
        .Add(fStopBtn)
        .Add(fPauseBtn)
        .Add(fPlayBtn)
        .Add(fShuffleBtn)
        //.AddGlue() 
      .End();

BGroupView* fMetaAndSpectrumStack = new BGroupView(B_VERTICAL, 5);
BLayoutBuilder::Group<>(fMetaAndSpectrumStack, B_VERTICAL, 5)
    .SetInsets(5)
    .AddStrut(1)
    .Add(fDescView)
    .Add(fSongView)  
    .Add(fSpectrum)
    .AddStrut(2) 
.End();


BLayoutBuilder::Group<>(fPlayerGroup, B_VERTICAL, 5)
    .SetInsets(10)
    .Add(fArtView) 
    .Add(fMetaAndSpectrumStack)     
    .AddGlue()    
    .AddGroup(B_HORIZONTAL, 10) 
        .AddGroup(B_VERTICAL, 6) 
            .AddStrut(1)     	
            .Add(fListenersView)
            .Add(fquality)
            .Add(fCompactModeRadio)               
        .End()        
        .Add(fBtnAddFav) 
    .End()    
.Add(fControlStack);




    // ==========================================
    // TAB 2: STATIONS VIEW (The Directory)
    // ==========================================
    fStationGroup = new BGroupView(B_VERTICAL, 0);
    fStationGroup->SetName("Stations"); 
    // Set container color to prevent bleed-through
    fStationGroup->SetViewColor(ui_color(B_LIST_BACKGROUND_COLOR));
    
    fPresetList = new PresetListView("preset_list");
	fPresetList->SetSelectionMessage(new BMessage(MSG_PRESET_SELECTED));
    
    fPresetScroll = new BScrollView("preset_scroll", fPresetList, 0, true, true, B_NO_BORDER);
    fPresetScroll->Hide();
    fPresetScroll->SetExplicitMinSize(BSize(B_SIZE_UNSET, 150));
	fPresetScroll->SetExplicitMaxSize(BSize(B_SIZE_UNSET, 300));

    fStationList = new BListView("station_list");
    fStationList->SetInvocationMessage(new BMessage(MSG_PLAY_STATION)); 
    
    // 1. Change to B_NO_BORDER to kill the light ghost lines
    BScrollView* stationScroll = new BScrollView("station_scroll", fStationList, 
        0, false, true, B_NO_BORDER); 
    
    stationScroll->SetViewColor(ui_color(B_LIST_BACKGROUND_COLOR));

    if (BScrollBar* sb = stationScroll->ScrollBar(B_VERTICAL)) {
        // Force the tray to be dark
        sb->SetViewColor(ui_color(B_LIST_BACKGROUND_COLOR));
    }

    BLayoutBuilder::Group<>(fStationGroup, B_VERTICAL, 0)
        .SetInsets(10)
        .Add(stationScroll) 
    .End();


    // ==========================================
    // TAB 3. FAVORITES VIEW (The List)
    // ==========================================
    fFavGroup = new BGroupView(B_VERTICAL, 10);
    fFavGroup->SetName("Fav"); 
    fFavGroup->SetViewColor(ui_color(B_LIST_BACKGROUND_COLOR));

    fFavList = new FavListView("favorites_list");
    fFavList->SetInvocationMessage(new BMessage(MSG_PLAY_FAV)); 
    
    // 1. Change to B_NO_BORDER here as well
    BScrollView* favScroll = new BScrollView("fav_scroll", fFavList, 
        0, false, true, B_NO_BORDER);
    
    favScroll->SetViewColor(ui_color(B_LIST_BACKGROUND_COLOR));

    if (BScrollBar* sb = favScroll->ScrollBar(B_VERTICAL)) {
        sb->SetViewColor(ui_color(B_LIST_BACKGROUND_COLOR));
    }

    BLayoutBuilder::Group<>(fFavGroup, B_VERTICAL, 0)
        .SetInsets(10)
        .Add(favScroll)
    .End();



    // ==========================================
    // TAB 4: CONFIG VIEW (Placeholder)
    // ==========================================
	fConfigGroup = new BGroupView(B_VERTICAL, 10);
	fConfigGroup->SetName("Config");
	
    BBitmap* configIcon = GetVectorIcon(kIconConfig, kIconConfigSize, 64);
    fConfigLogo = new IconView(configIcon);
  


    // --- Quality Selection (Menu Field) ---
    fQualityMenu = new BPopUpMenu("Select");
    
    // Create the messages
    BMessage* msg320k = new BMessage(MSG_CFG_QUALITY); msg320k->AddString("val", "320k");
    BMessage* msg256k = new BMessage(MSG_CFG_QUALITY); msg256k->AddString("val", "256k");
    BMessage* msg128k = new BMessage(MSG_CFG_QUALITY); msg128k->AddString("val", "128k");
    BMessage* msg64k  = new BMessage(MSG_CFG_QUALITY); msg64k->AddString("val", "64k"); 
    BMessage* msg32k  = new BMessage(MSG_CFG_QUALITY); msg32k->AddString("val", "32k");

    // Add items to the class member fQualityMenu
    fQualityMenu->AddItem(new BMenuItem("320k", msg320k));
    fQualityMenu->AddItem(new BMenuItem("256k", msg256k));
    fQualityMenu->AddItem(new BMenuItem("128k", msg128k));
    fQualityMenu->AddItem(new BMenuItem("64k", msg64k));  
    fQualityMenu->AddItem(new BMenuItem("32k", msg32k));

    // Mark the saved preference
    BMenuItem* selectedItem = fQualityMenu->FindItem(cfg.quality.c_str());
    if (selectedItem) 
        selectedItem->SetMarked(true);

    fQualityLabel = new BStringView("lbl_qual", "Audio Quality:");
    
    // Assign to the member field
    fQualityField = new BMenuField("quality_field", NULL, fQualityMenu);


//-----------------------------
    // --- Sleep Timer Setup (BMenuField) ---
    fSleepMenu = new BPopUpMenu("Disabled");
    fSleepMenu->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT, B_ALIGN_TOP));
    BMessage* msgSleep0   = new BMessage(MSG_SLEEP_CHANGED); msgSleep0->AddInt32("minutes", 0);
    //BMessage* msgSleep1   = new BMessage(MSG_SLEEP_CHANGED); msgSleep1->AddInt32("minutes", 1);
    BMessage* msgSleep15  = new BMessage(MSG_SLEEP_CHANGED); msgSleep15->AddInt32("minutes", 15);
    BMessage* msgSleep30  = new BMessage(MSG_SLEEP_CHANGED); msgSleep30->AddInt32("minutes", 30);
    BMessage* msgSleep1h  = new BMessage(MSG_SLEEP_CHANGED); msgSleep1h->AddInt32("minutes", 60);
    BMessage* msgSleep3h  = new BMessage(MSG_SLEEP_CHANGED); msgSleep3h->AddInt32("minutes", 180); 
    BMessage* msgSleep6h  = new BMessage(MSG_SLEEP_CHANGED); msgSleep6h->AddInt32("minutes", 360);  
    BMessage* msgSleep8h  = new BMessage(MSG_SLEEP_CHANGED); msgSleep8h->AddInt32("minutes", 480);

    fSleepMenu->AddItem(new BMenuItem("Disabled", msgSleep0));
    //fSleepMenu->AddItem(new BMenuItem("1 Minute", msgSleep1));
    fSleepMenu->AddItem(new BMenuItem("15 Minutes", msgSleep15));
    fSleepMenu->AddItem(new BMenuItem("30 Minutes", msgSleep30));
    fSleepMenu->AddItem(new BMenuItem("1 Hour", msgSleep1h));
    fSleepMenu->AddItem(new BMenuItem("3 Hours", msgSleep3h));                                     
    fSleepMenu->AddItem(new BMenuItem("6 Hours", msgSleep6h));                                      
    fSleepMenu->AddItem(new BMenuItem("8 Hours", msgSleep8h));

    // Default target initialization
    fSleepMenu->ItemAt(0)->SetMarked(true);
    fSleepRunner = NULL;

    fSleepLabel = new BStringView("lbl_sleep", "Sleep Timer:");
    fSleepField = new BMenuField("sleep_field", NULL, fSleepMenu);
//-----------------------------

    

  
    fChkNotify = new BCheckBox("chk_notify", "Notifications", new BMessage(MSG_CFG_NOTIFY));
    fChkNotify->SetValue(cfg.showNotifications ? B_CONTROL_ON : B_CONTROL_OFF);

    // Icon Size Menu
    fSizeMenu = new BPopUpMenu("Icon Size");
    int sizes[] = {128, 96, 64, 40, 32};
    for (int s : sizes) {
        BString label;
        label << s << "x" << s;    
        BMessage* msg = new BMessage(MSG_CFG_ICON_SIZE); 
        msg->AddInt32("val", s);    
        BMenuItem* item = new BMenuItem(label.String(), msg);    
        
        if (s == cfg.notifyIconSize) {
            item->SetMarked(true);
        }    
        fSizeMenu->AddItem(item);
    }
    
    // Size Field
    fSizeField = new BMenuField("size_field", NULL, fSizeMenu);
    fSizeField->SetExplicitMaxSize(BSize(95, B_SIZE_UNLIMITED));
    fSizeLabel = new BStringView("lbl_size", "Icon Size:");
    

	fSizeContainer = new BGroupView(B_HORIZONTAL, 5); // Use Horizontal for label next to field
	fSizeContainer->AddChild(fSizeLabel);
	fSizeContainer->AddChild(fSizeField);


    // ==========================================
    // --- Checkboxes ---
    // ==========================================    
    fChkShuffle = new BCheckBox("chk_shuffle", "Auto Shuffle On Start", new BMessage(MSG_CFG_AUTO_SHUFFLE));
    fChkShuffle->SetValue(cfg.autoShuffle ? B_CONTROL_ON : B_CONTROL_OFF);
    
    
    fChkSysTray = new BCheckBox("chk_sysTray", "System Tray", new BMessage(MSG_CFG_SYS_TRAY));
    fChkSysTray->SetValue(cfg.sysTray ? B_CONTROL_ON : B_CONTROL_OFF);
    fChkSysTray->SetEnabled(false);    
    
    // Theme and Presets
    fChkTheme = new BCheckBox("chk_theme", "Dark Theme (Experimental)", new BMessage(MSG_CFG_THEME));
    fChkTheme->SetValue(cfg.updateTheme == "Dark" ? B_CONTROL_ON : B_CONTROL_OFF);
    
    fCmpTitle = new BCheckBox("fCmpTitle_toggle", "Compact Mode: Show Title", new BMessage(MSG_SHOW_TITLE));
    fCmpTitle->SetValue(cfg.compactModeTitle ? B_CONTROL_ON : B_CONTROL_OFF);
    
    fCmpSong = new BCheckBox("fCmpSong_toggle", "Compact Mode: Show Description", new BMessage(MSG_SHOW_DESC));
    fCmpSong->SetValue(cfg.compactModeDesc ? B_CONTROL_ON : B_CONTROL_OFF);    


    fPresetToggle = new BCheckBox("preset_toggle", "MilkDrop Presets:", new BMessage(MSG_TOGGLE_PRESETS));
    fPresetToggle->SetValue(B_CONTROL_OFF); 

    // Visuals and Favorites
    fVisualsCheckbox = new BCheckBox("visuals_toggle", "Visualizer (Experimental)", new BMessage(MSG_TOGGLE_VISUALS));
    fVisualsCheckbox->SetValue(cfg.showVisuals ? B_CONTROL_ON : B_CONTROL_OFF);  
    
    fShuffleFavsCheckbox = new BCheckBox("shuffle_favs", "Shuffle Only Favorites", new BMessage(MSG_SHUFFLE_FAVS_CHANGED));
    fShuffleFavsCheckbox->SetValue(cfg.shuffleFavsOnly ? B_CONTROL_ON : B_CONTROL_OFF);  
    
    fChkPresetTimer = new BCheckBox("chk_PresetTimer", "Auto Shuffle Visual Presets 30/s", new BMessage(MSG_CFG_AUTO_PresetTimer));
    fChkPresetTimer->SetValue(cfg.autoShuffleVisuals ? B_CONTROL_ON : B_CONTROL_OFF);


 
    // ==========================================
	// --- EQ & Mastering Section ---
    // ==========================================
	BPopUpMenu* presetMenu = new BPopUpMenu("Select Preset");
	presetMenu->AddItem(new BMenuItem("Flat", new BMessage(MSG_SET_PRESET_FLAT)));
	presetMenu->AddItem(new BMenuItem("Rock", new BMessage(MSG_SET_PRESET_ROCK)));
	presetMenu->AddItem(new BMenuItem("Jazz", new BMessage(MSG_SET_PRESET_JAZZ)));
	presetMenu->AddItem(new BMenuItem("Bass Boost", new BMessage(MSG_SET_PRESET_BASS)));

	fPresetField = new BMenuField("presets", "", presetMenu);

	fApplyEQBtn = new BButton("apply_eq", "Apply EQ", new BMessage(MSG_EQ_CHANGED));

	BGroupView* buttonRow = new BGroupView(B_HORIZONTAL, 10);
	buttonRow->SetExplicitAlignment(BAlignment(B_ALIGN_CENTER, B_ALIGN_TOP));
	buttonRow->AddChild(fPresetField); 
	buttonRow->AddChild(fApplyEQBtn);

	fEQToggle = new BCheckBox("eq_toggle", "15-Band EQ", new BMessage(MSG_TOGGLE_EQ));
	fEQToggle->SetValue(cfg.eqEnabled ? B_CONTROL_ON : B_CONTROL_OFF);
	
    fEnableSpectrum = new BCheckBox("chk_spectrum", "Spectrum Bars", new BMessage(MSG_TOGGLE_Spectrum));
	fEnableSpectrum->SetValue(cfg.showSpectrumVisuals ? B_CONTROL_ON : B_CONTROL_OFF); 
    
	bool ladspaSupported = IsFFmpegLadspaAvailable();
	fEnableladspa = new BCheckBox("enable_ladspa", "Ladspa", new BMessage(MSG_TOGGLE_LADSPA)); 

	if (!ladspaSupported) {
    	fEnableladspa->SetEnabled(false);
    	fEnableladspa->SetLabel("Ladspa (Not compiled in FFmpeg)");
		} else {
    	fEnableladspa->SetValue(cfg.ladspaEnabled ? B_CONTROL_ON : B_CONTROL_OFF);
	}


    fEQContainer = new BGroupView(B_VERTICAL, 5); 
    fEQContainer->SetName("EQPanel");

    BGroupView* limitGroup = new BGroupView(B_VERTICAL, 5);
    BStringView* lTitle = new BStringView(NULL, "Limiter");
    lTitle->SetFont(be_bold_font);
    limitGroup->AddChild(lTitle);

 	fLimitInput = new WheelSlider("limit_in", "In", NULL, -20, 20, B_HORIZONTAL);
	fLimitInput->SetValue((int32)cfg.limitIn);
	fLimitLimit = new WheelSlider("limit_thr", "Lmt", NULL, -20, 0, B_HORIZONTAL);
	fLimitLimit->SetValue((int32)cfg.limitLmt);
	fLimitRelease = new WheelSlider("limit_rel", "Rel", NULL, 10, 1000, B_HORIZONTAL);
	fLimitRelease->SetValue((int32)cfg.limitRel);

    limitGroup->AddChild(fLimitInput);
    limitGroup->AddChild(fLimitLimit);
    limitGroup->AddChild(fLimitRelease);


	
    const char* freqLabels[] = { 
        "50", "100", "156", "220", "311", "440", "622", "880", 
        "1k2", "1k7", "2k5", "3k5", "5k", "10k", "20k" 
    };

    BGroupView* eqSliderRow = new BGroupView(B_HORIZONTAL, 2);

    for (int i = 0; i < 15; i++) {
        BGroupView* bandGroup = new BGroupView(B_VERTICAL, 2);
    
        BMessage* eqMsg = new BMessage(MSG_EQ_CHANGED);
        eqMsg->AddInt32("band", i);
        
        fEQSliders[i] = new WheelSlider(freqLabels[i], "", NULL, -15, 15, B_VERTICAL, 1);
        fEQSliders[i]->SetValue((int32)cfg.eqBands[i]);
        
   
        fEQSliders[i]->SetTarget(this);
        fEQSliders[i]->SetModificationMessage(NULL); 
    
        BStringView* lbl = new BStringView(NULL, freqLabels[i]);
        lbl->SetFontSize(8); 
        lbl->SetExplicitAlignment(BAlignment(B_ALIGN_CENTER, B_ALIGN_VERTICAL_UNSET));
    
        bandGroup->AddChild(fEQSliders[i]);
        bandGroup->AddChild(lbl);
        eqSliderRow->AddChild(bandGroup); 
    }


	eqSliderRow->AddChild(limitGroup); 
	fEQContainer->AddChild(eqSliderRow); 
	fEQContainer->AddChild(buttonRow);
	
	if (!cfg.eqEnabled) {
    	fEQContainer->Hide();
    	fEnableladspa->Hide();
    	fEnableSpectrum->Hide();
	}
	
	
    if (!cfg.showNotifications) fSizeContainer->Hide();	

         
// ==========================================
// --- Layout ---
// ==========================================
BLayoutBuilder::Group<>(fConfigGroup, B_VERTICAL, 0) 
    .SetInsets(15)
   
    .AddGroup(B_HORIZONTAL, 0)
        .Add(fConfigLogo)
        .AddGlue() 
    .End()    
    .AddStrut(10) 
    .AddGroup(B_HORIZONTAL, 0) 
        .AddGroup(B_VERTICAL, 5) 
            .Add(fQualityLabel)    
            .Add(fQualityField)    
            .AddStrut(5)
            .Add(fSleepLabel)
            .Add(fSleepField)
            .AddStrut(5)
        .End()
 
        .AddGlue() 
    .End()    
    .AddGroup(B_HORIZONTAL, 5)    
        .Add(fChkNotify)
        .Add(fSizeContainer)    	
        .AddGlue()
    .End()  	
    .Add(fChkSysTray)
    .Add(fChkShuffle)
    .Add(fShuffleFavsCheckbox)
    .Add(fCompactModeConfig)
    //.Add(fCmpTitle)  // Still testing these two
    //.Add(fCmpSong)   
    .Add(fChkTheme)
   	.Add(fEQToggle)
   	//.Add(fEnableladspa)  // Doesn't work as good as native mpv plugins and needs to be built into ffmpeg. But leaving in code for future debugging.
   	.Add(fEnableSpectrum)
   	.Add(fEQContainer)
     #ifdef USE_PROJECTM
    .Add(fPresetToggle)
    .Add(fPresetScroll) 
    .Add(fVisualsCheckbox)
    .Add(fChkPresetTimer)
     #endif
    .AddGlue()
.End();



    // ==========================================
    // TAB 4: ABOUT VIEW
    // ==========================================
	fAboutGroup = new BGroupView(B_VERTICAL, 4);
	fAboutGroup->SetName("About");
    fAboutGroup->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

    // 1. Header Styles
    BFont titleFont(be_bold_font);
    titleFont.SetSize(26.0);

    BFont boldFont(be_bold_font);
    boldFont.SetSize(14.0);

    // 2. Icon and Text Components
    // Get the vector icon - I bumped the size to 64 for a nice clear logo
    BBitmap* aboutIcon = GetVectorIcon(kIconAbout, kIconAboutSize, 64);
    IconView* appLogo = new IconView(aboutIcon);
    appLogo->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT, B_ALIGN_TOP));
    
    BBitmap* URLIcon = GetVectorIcon(kIconURL, kIconURLSize, 48);
    IconView* appURL = new IconView(URLIcon);
    appURL->SetExplicitAlignment(BAlignment(B_ALIGN_CENTER, B_ALIGN_TOP));

    BStringView* titleApp = new BStringView("abt_title", "SuperMusicThingy");
    titleApp->SetFont(&titleFont);
    titleApp->SetAlignment(B_ALIGN_CENTER);

    BStringView* txtVer = new BStringView("abt_ver", "Version 1.0.0 (Haiku)");
    txtVer->SetAlignment(B_ALIGN_CENTER);
    
    ClickableURL* txturl = new ClickableURL("abt_url", "Source Available Online", 
    "https://github.com/ablyssx74/HaikuSuperMusicThingy"); txturl->SetAlignment(B_ALIGN_CENTER);

    ClickableURLIcon* iconLink = new ClickableURLIcon("abt_url", URLIcon, "https://github.com/ablyssx74/HaikuSuperMusicThingy");
 

    BStringView* txtCopy = new BStringView("abt_copy", "Copyright " B_UTF8_COPYRIGHT " 2026 Kris Beazley");
    txtCopy->SetAlignment(B_ALIGN_CENTER);
    
    BStringView* txtEmail = new BStringView("abt_mail", "supermusicthingy@epluribusunix.net");
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

 
    
    c1->SetAlignment(B_ALIGN_CENTER);
    c2->SetAlignment(B_ALIGN_CENTER);
    c3->SetAlignment(B_ALIGN_CENTER);
    c4->SetAlignment(B_ALIGN_CENTER);
    c5->SetAlignment(B_ALIGN_CENTER);
    c6->SetAlignment(B_ALIGN_CENTER);
    c7->SetAlignment(B_ALIGN_CENTER);


    
    // 4. Layout
    BLayoutBuilder::Group<>(fAboutGroup, B_VERTICAL, 5)
    	.Add(appLogo) 
        .SetInsets(20)
        .AddGlue()        
        .Add(titleApp)
        .Add(txtVer)
        .Add(txturl)
        .AddGroup(B_HORIZONTAL, 100) 
        .AddGlue()        
    	.Add(iconLink)	
        .AddGlue()
    	.End()      
        .AddStrut(1)
        .Add(txtCopy)        
        .Add(txtEmail)
        .Add(txtAI)
        .AddStrut(30) 
        .Add(txtCredit)
        .AddStrut(5)
        .Add(c1)
        .Add(c2)
        .Add(c3)
        .Add(c4)
        .Add(c5)
        .Add(c6)
        .Add(c7)
        .AddGlue()
    .End();


    // 3. Attach Tabs
    fTabView->AddTab(fPlayerGroup);
	fStationTab = new BTab(fStationGroup);
	fTabView->AddTab(fStationGroup, fStationTab);

	fFavTab = new BTab(fFavGroup);
	fTabView->AddTab(fFavGroup, fFavTab);

	fConfigTab = new BTab(fConfigGroup);
	fTabView->AddTab(fConfigGroup, fConfigTab);    
    
	fAboutTab = new BTab(fAboutGroup); 
	fTabView->AddTab(fAboutGroup, fAboutTab);

    // 4. Final Window Layout 
    BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
        .SetInsets(0)
        .Add(fTabView)
    .End();
    
    PopulateStationList();
    
    std::string configPath = std::string(getenv("HOME")) + "/config/settings/SuperMusicThingy/milk_presets/";
	PopulatePresetList(fPresetList, configPath.c_str());

    UpdateFavButtons();
     
   
    DownloadStationIcons();    
    RefreshFavorites();
    if (cfg.showVisuals) {
        StartVisuals();
    }
    ApplyTheme();
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
    const int numBands = 15; // Matches mbeq_1197 standard
    
    // Standard mbeq_1197 frequency centers
    float frequencies[] = {
        50, 100, 156, 220, 311, 440, 622, 880, 
        1250, 1750, 2500, 3500, 5000, 10000, 20000
    };

    if (!cfg.ladspaEnabled) {
        // --- NATIVE FILTERS (Direct Comma Chaining) ---
        filterChain = ""; 
        
        for (int i = 0; i < numBands; i++) {
            BString band;
            band.SetToFormat("equalizer=f=%.0f:width_type=o:w=1:g=%.2f,", 
                             frequencies[i], (float)fEQSliders[i]->Value());
            filterChain << band;
        }

        // CORRECT AMPLITUDE SCALING MATH FORMULA:
        // Convert dB values (-20 to +20) from the sliders directly into the linear gain metrics mpv expects
        float inputGain = pow(10.0f, (float)fLimitInput->Value() / 20.0f); 
        
        // Convert the negative dB limit threshold value (-20 to 0) to a linear scaling amplitude limit
        float limitVal = pow(10.0f, (float)fLimitLimit->Value() / 20.0f);

        // Standard fallback safety clamps to protect memory allocations
        if (limitVal <= 0.001f) limitVal = 0.001f;
        if (inputGain <= 0.001f) inputGain = 0.001f;

        BString limiterPart;
        limiterPart.SetToFormat("alimiter=level_in=%.2f:limit=%.2f:release=%.2f",
                                inputGain, limitVal, (float)fLimitRelease->Value());
        filterChain << limiterPart;

	   filterChain << ",asetnsamples=n=1024,@bouncy:astats=metadata=1:reset=1";
       //filterChain << ",@bouncy:astats=metadata=1:reset=1"; 


    

    } else {
        // --- LADSPA FILTERS (mbeq_1197) ---
        filterChain = "@bouncy:lavfi=[";

        BString eqPart;
        eqPart << "ladspa=file='/boot/home/config/non-packaged/lib/ladspa/mbeq_1197.so':p=mbeq:c=";
        
        for (int i = 0; i < numBands; i++) {
            BString val;
            val.SetToFormat("%.2f", (float)fEQSliders[i]->Value());
            eqPart << val << (i == (numBands - 1) ? "" : "|");
        }
        eqPart << ",";
        filterChain << eqPart;

        BString limiterPart;
        limiterPart.SetToFormat("ladspa=file='/boot/home/config/non-packaged/lib/ladspa/fast_lookahead_limiter_1913.so':p=fastLookaheadLimiter:c=%.2f|%.2f|%.2f,",
            (float)fLimitInput->Value(), 
            (float)fLimitLimit->Value(), 
            (float)fLimitRelease->Value() / 1000.0f);
        filterChain << limiterPart;

            filterChain << "astats=metadata=1:reset=1]"; 


    }
    

    mpv_set_property_string(mpv, "af", filterChain.String());
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
            			UpdateFavButtons();
        			}
    			}
    			break;
			}
			
			
	
    	
    	case MSG_TOGGLE_LADSPA: {
    		cfg.ladspaEnabled = (fEnableladspa->Value() == B_CONTROL_ON);
    		save_config();
    		UpdateMPVFilters(); 
    		break;
		}	
		
		case MSG_SHOW_TITLE: {
       	 	BCheckBox* chk = dynamic_cast<BCheckBox*>(FindView("fCmpTitle_toggle"));
        	if (chk) {
            	cfg.compactModeTitle = (chk->Value() == B_CONTROL_ON);
            	save_config(); 
        	}
        		break;
    	}
        
        case MSG_SHOW_DESC: {
        	BCheckBox* chk = dynamic_cast<BCheckBox*>(FindView("fCmpSong_toggle"));
        	if (chk) {
            	cfg.compactModeDesc = (chk->Value() == B_CONTROL_ON);
            	save_config(); 
        	}
        		break;
    	}

    	
	case MSG_TOGGLE_EQ: {
    		BCheckBox* chk = dynamic_cast<BCheckBox*>(FindView("eq_toggle"));
    		if (chk) {
        		cfg.eqEnabled = (chk->Value() == B_CONTROL_ON);            
        
        		if (cfg.eqEnabled) {
            		fEnableladspa->Show();
            		fEnableSpectrum->Show();
            		fChkShuffle->Show();  
            		fEQContainer->Show();
        		} else {
            		fEnableladspa->Hide();  
            		fEnableSpectrum->Hide();
            		fEQContainer->Hide();
        		}
        
                // Force parent container and layout elements to instantly clear 
                // and wipe away stale pixels using your custom dark theme colors
                if (fSpectrum != nullptr) {
                    fSpectrum->Invalidate();
                    if (fSpectrum->Parent()) fSpectrum->Parent()->Invalidate();
                }
                if (fEQContainer) {
                    fEQContainer->Invalidate();
                    if (fEQContainer->Parent()) fEQContainer->Parent()->Invalidate();
                }


        		
        		if (cfg.showSpectrumVisuals) {
                        fSpectrum->SetExplicitMinSize(BSize(350, 100));
                        fSpectrum->SetExplicitMaxSize(BSize(350, 100));
                        fSpectrum->SetExplicitPreferredSize(BSize(350, 100));
             
            	}
 
  				if (!cfg.eqEnabled) {
                    	fSpectrum->SetExplicitMinSize(BSize(350, 1));
                        fSpectrum->SetExplicitMaxSize(BSize(350, 1));
                        fSpectrum->SetExplicitPreferredSize(BSize(350, 1));                    
                    
  				}
             

    			fPlayerGroup->InvalidateLayout();
    			ApplyTheme(); 
        		InvalidateLayout();
        		ResizeToPreferred();
        		save_config();
        		UpdateMPVFilters(); 
    			}
    		break;
		}
		


        case MSG_TOGGLE_Spectrum: {
            cfg.showSpectrumVisuals = (fEnableSpectrum->Value() == B_CONTROL_ON);
            save_config();

            if (fSpectrum != nullptr) {
                // Force Haiku app_server to clear the parent container boundaries
                fSpectrum->Invalidate();
                if (fSpectrum->Parent()) {
                    fSpectrum->Parent()->Invalidate();
                }
            }

            this->UpdateMPVFilters();
            
            if (cfg.showSpectrumVisuals) {
                        fSpectrum->SetExplicitMinSize(BSize(350, 100));
                        fSpectrum->SetExplicitMaxSize(BSize(350, 100));
                        fSpectrum->SetExplicitPreferredSize(BSize(350, 100));
             
            }
 
  			if (!cfg.showSpectrumVisuals || !cfg.eqEnabled) {
                    	fSpectrum->SetExplicitMinSize(BSize(350, 1));
                        fSpectrum->SetExplicitMaxSize(BSize(350, 1));
                        fSpectrum->SetExplicitPreferredSize(BSize(350, 1));                    
                    
  			}
             
			
    		fPlayerGroup->InvalidateLayout();
    		ApplyTheme(); 
        	ResizeToPreferred();
            break;
        }
        
       case MSG_CFG_THEME: {
       		#ifdef IS_HAIKU_32BIT
       		        BCheckBox* chk = dynamic_cast<BCheckBox*>(FindView("chk_theme"));
        			if (chk) {
            			cfg.updateTheme = (chk->Value() == B_CONTROL_ON) ? "Dark" : "Default";
            			save_config();
  						ApplyTheme(); 
        					}
        				break;       		
       		#else
       		
            BCheckBox* chk = dynamic_cast<BCheckBox*>(FindView("chk_theme"));
            if (chk) {
                // Fix 1: Declare isDark explicitly
                bool isDark = (chk->Value() == B_CONTROL_ON);
                cfg.updateTheme = isDark ? "Dark" : "Default";
                save_config();
                ApplyTheme(); 
                
                // Rebuild the tabs to force the top navigation buttons to snap into place
                if (!cfg.compactMode && fTabView) {
                    // 1. Completely strip out existing tab items
                    for (int32 i = fTabView->CountTabs() - 1; i >= 0; i--) {
                        BTab* tab = fTabView->TabAt(i);
                        if (tab == fStationTab || tab == fFavTab || tab == fConfigTab || tab == fAboutTab) {
                            fTabView->RemoveTab(i);
                        }
                    }

                    // 2. Re-allocate them cleanly so they hook into the new look
                    BGroupView* groups[] = { fStationGroup, fFavGroup, fConfigGroup, fAboutGroup };
                    const char* labels[] = { "Stations", "Fav", "Config", "About" };
                    BTab** dynamicTabs[] = { &fStationTab, &fFavTab, &fConfigTab, &fAboutTab };

                    for (int i = 0; i < 4; i++) { 
                        if (groups[i] == nullptr || dynamicTabs[i] == nullptr) continue;
                        
                        *dynamicTabs[i] = new BTab();
                        (*dynamicTabs[i])->SetLabel(labels[i]);
                        fTabView->AddTab(groups[i], *dynamicTabs[i]);
                    }

                    // 3. Keep the user pinned to the "Config" tab index so the menu doesn't jump
                    for (int32 j = 0; j < fTabView->CountTabs(); j++) {
                        BTab* currentTab = fTabView->TabAt(j);
                        if (currentTab && currentTab->Label() && strcmp(currentTab->Label(), "Config") == 0) {
                            fTabView->Select(j);
                            break;
                        }
                    }

                    // 4. Force the inner controls to blend seamlessly with the dark panel background
                    rgb_color bgDarkColor = rgb_color{40, 40, 40, 255}; 

                    // Style the 15-band EQ sliders array
                    for (int i = 0; i < 15; i++) {
                        if (fEQSliders[i]) {
                            if (isDark) {
                                fEQSliders[i]->SetViewColor(bgDarkColor);
                                fEQSliders[i]->SetLowColor(bgDarkColor);
                            } else {
                                fEQSliders[i]->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
                                fEQSliders[i]->SetLowColor(ui_color(B_PANEL_BACKGROUND_COLOR));
                            }
                            fEQSliders[i]->Invalidate();
                        }
                    }

                      // Fix 2: Use BSlider* base class matching the class pointer initialization types
                    BSlider* limiterSliders[] = { fLimitInput, fLimitLimit, fLimitRelease };
                    
                    for (int s = 0; s < 3; s++) {
                        if (limiterSliders[s]) {
                            if (isDark) {
                                limiterSliders[s]->SetViewColor(bgDarkColor);
                                limiterSliders[s]->SetLowColor(bgDarkColor);
                                
                                // FIX: Force the text label foreground color to white in dark mode
                                limiterSliders[s]->SetHighColor(255, 255, 255, 255);
                            } else {
                                limiterSliders[s]->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
                                limiterSliders[s]->SetLowColor(ui_color(B_PANEL_BACKGROUND_COLOR));
                                
                                // Reset the label color back to default charcoal/black for light mode
                                limiterSliders[s]->SetHighColor(0, 0, 0, 255);
                            }
                            
                            // Recolor any internal view wrappers inside the components
                            for (int32 c = 0; c < limiterSliders[s]->CountChildren(); c++) {
                                BView* child = limiterSliders[s]->ChildAt(c);
                                if (child) {
                                    if (isDark) {
                                        child->SetViewColor(bgDarkColor);
                                        child->SetLowColor(bgDarkColor);
                                        child->SetHighColor(255, 255, 255, 255); // Force inner nested text labels to white
                                    } else {
                                        child->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
                                        child->SetLowColor(ui_color(B_PANEL_BACKGROUND_COLOR));
                                        child->SetHighColor(0, 0, 0, 255);
                                    }
                                    child->Invalidate();
                                }
                            }
                            limiterSliders[s]->Invalidate();
                        }
                    }

                }

                this->InvalidateLayout(true);
                this->Layout(true);
            }
            break;
            #endif
        } 



         



		case MSG_COMPACTM_CHANGED: {
    		void* source = nullptr;
    		message->FindPointer("source", &source);
    
    		bool newState;
    
    		if (source == nullptr) {
        		newState = cfg.compactMode;
        		if (fCompactModeConfig) fCompactModeConfig->SetValue(newState ? B_CONTROL_ON : B_CONTROL_OFF);
        		if (fCompactModeRadio) fCompactModeRadio->SetValue(newState ? B_CONTROL_ON : B_CONTROL_OFF);
    		} else {
        		if (source == fCompactModeConfig) {
            		newState = (fCompactModeConfig->Value() == B_CONTROL_ON);
        		} else {
            		newState = (fCompactModeRadio->Value() == B_CONTROL_ON);
        		}

        		cfg.compactMode = newState;
        		save_config();

                // Sync the UI controls
                if (fCompactModeRadio) fCompactModeRadio->SetValue(newState ? B_CONTROL_ON : B_CONTROL_OFF);
                if (fCompactModeConfig) fCompactModeConfig->SetValue(newState ? B_CONTROL_ON : B_CONTROL_OFF);
    		}

    		// 3. Safety Check: Stop if critical views are missing
    		if (fPlayerGroup == NULL || fControlStack == NULL)
        		break;

    		// 4. Setup sizes based on current state
    		float scale = be_plain_font->Size() / 12.0f; 
    		float artSize = cfg.compactMode ? (116 * scale) : (350 * scale);
    		float btnSize = cfg.compactMode ? (40 * scale) : (75 * scale);
    		float favSize = cfg.compactMode ? (40 * scale) : (75 * scale);

    		// 5. Apply Layout orientations
    		fPlayerGroup->GroupLayout()->SetOrientation(cfg.compactMode ? B_HORIZONTAL : B_VERTICAL);
    		fPlayerGroup->GroupLayout()->SetSpacing(cfg.compactMode ? 5 : 10);
    		fControlStack->GroupLayout()->SetOrientation(B_VERTICAL); 
    
    		// 6. Update Widget Sizes
            if (fDescView) {
                ((SongLabel*)fDescView)->SetCompactMode(cfg.compactMode);
            }
            if (fSongView) {
                ((SongLabel*)fSongView)->SetCompactMode(cfg.compactMode);
                
                if (cfg.compactMode) {
                    float expandedWidth = 220.0f * scale; 
                    fSongView->SetExplicitMinSize(BSize(expandedWidth, B_SIZE_UNSET));
                    fSongView->SetExplicitPreferredSize(BSize(expandedWidth, B_SIZE_UNSET));
                    fSongView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 24.0f * scale));
                    
                    float sliderWidth = (btnSize * 4) + (5 * 3); // Fits exactly over the 4 buttons + spacing
   					fVolumeSlider->SetExplicitMaxSize(BSize(sliderWidth, B_SIZE_UNSET));
    				fVolumeSlider->SetExplicitPreferredSize(BSize(sliderWidth, B_SIZE_UNSET));
                    
                    if (!cfg.showSpectrumVisuals || !cfg.eqEnabled) {
                    	  float expandedWidth = 220.0f * scale; 
                          fSpectrum->SetExplicitMinSize(BSize(expandedWidth, 10.0f * scale));
                          fSpectrum->SetExplicitMaxSize(BSize(expandedWidth, 10.0f * scale));
                    	  fSpectrum->SetExplicitPreferredSize(BSize(expandedWidth, 10.0f * scale));
                    
                		}
                    if (cfg.showSpectrumVisuals && cfg.eqEnabled) {
                    	
                    		if (!cfg.compactModeDesc && !cfg.compactModeTitle) {
                    			//fDescView->Hide(); 
        						//fSongView->Hide();   					
                        		fSpectrum->SetExplicitMinSize(BSize(350, 100));
                        		fSpectrum->SetExplicitMaxSize(BSize(350, 100));
                        		fSpectrum->SetExplicitPreferredSize(BSize(350, 100));
                        		//fSpectrum->SetExplicitAlignment(BAlignment(B_ALIGN_CENTER, B_ALIGN_MIDDLE));
    							//this->InvalidateLayout(true);
                        		
                    		}
                        		
                    		if (!cfg.compactModeDesc && cfg.compactModeTitle) {                    	
                    			fSpectrum->SetExplicitMinSize(BSize(350, 100));
                        		fSpectrum->SetExplicitMaxSize(BSize(350, 100));
                        		fSpectrum->SetExplicitPreferredSize(BSize(350, 100));
                    		}
                        		
                    	    if (cfg.compactModeDesc && !cfg.compactModeTitle) {                    		
                    		    fSpectrum->SetExplicitMinSize(BSize(350, 100));
                        		fSpectrum->SetExplicitMaxSize(BSize(350, 100));
                        		fSpectrum->SetExplicitPreferredSize(BSize(350, 100));
                        		
                    	    }
                    	    if (cfg.compactModeDesc && cfg.compactModeTitle) {

                        		fSpectrum->SetExplicitMinSize(BSize(350, 100));
                        		fSpectrum->SetExplicitMaxSize(BSize(350, 100));
                        		fSpectrum->SetExplicitPreferredSize(BSize(350, 100));                         		
                        	} 
                        	                    	
                    }
                    
                    
                    // Calculate parent stack size dynamically so it never crushes the spectrum
                    if (fMetaAndSpectrumStack != nullptr && (uintptr_t)fMetaAndSpectrumStack > 0x1000) {  
                        float stackHeight = 100.0f * scale; // Default baseline fallback
                        
                        if (cfg.showSpectrumVisuals && cfg.eqEnabled) {
                            if (!cfg.compactModeDesc && !cfg.compactModeTitle) {
                                stackHeight = 100.0f; // Matches your 350x100 spectrum perfectly
                            } else if (!cfg.compactModeDesc && cfg.compactModeTitle) {                    	
                                stackHeight = 100.0f * scale; // Spectrum (90) + Title text space
                            } else if (cfg.compactModeDesc && !cfg.compactModeTitle) {                    		
                                stackHeight = 100.0f * scale; // Spectrum (90) + Desc text space
                            } else if (cfg.compactModeDesc && cfg.compactModeTitle) {
                                stackHeight = 100.0f * scale;  // Spectrum (25) + Both text lines space
                            }
                        }
                        
                        fMetaAndSpectrumStack->SetExplicitMinSize(BSize(350 * scale, stackHeight));
                        fMetaAndSpectrumStack->SetExplicitMaxSize(BSize(350 * scale, stackHeight));
                        fMetaAndSpectrumStack->SetExplicitPreferredSize(BSize(350 * scale, stackHeight));
                    }   
                    
                } else {

                    fSongView->SetExplicitMinSize(BSize(B_SIZE_UNSET, B_SIZE_UNSET));
                    fSongView->SetExplicitPreferredSize(BSize(B_SIZE_UNSET, B_SIZE_UNSET));
                    fSongView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));
                    
                    fVolumeSlider->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
    				fVolumeSlider->SetExplicitPreferredSize(BSize(B_SIZE_UNSET, B_SIZE_UNSET));
                    
                    if (cfg.showSpectrumVisuals) {
                    	fSpectrum->SetExplicitMinSize(BSize(350, 100));
                        fSpectrum->SetExplicitMaxSize(BSize(350, 100));
                        fSpectrum->SetExplicitPreferredSize(BSize(350, 100));
                        
                    }
                    
                   if (!cfg.showSpectrumVisuals || !cfg.eqEnabled) {
                    	fSpectrum->SetExplicitMinSize(BSize(350, 1));
                        fSpectrum->SetExplicitMaxSize(BSize(350, 1));
                        fSpectrum->SetExplicitPreferredSize(BSize(350, 1));     
                        
                    if (fMetaAndSpectrumStack != nullptr && (uintptr_t)fMetaAndSpectrumStack > 0x1000) {
                        fMetaAndSpectrumStack->SetExplicitMinSize(BSize(350, 1));
                        fMetaAndSpectrumStack->SetExplicitMaxSize(BSize(350, 1));
                        fMetaAndSpectrumStack->SetExplicitPreferredSize(BSize(350, 1));
                    }
                   }
                }
            }

			if (fBtnAddFav) {
        		BSize favTargetSize(favSize, favSize);
        		fBtnAddFav->SetExplicitSize(favTargetSize);
        		fBtnAddFav->SetExplicitMinSize(favTargetSize);
        		fBtnAddFav->SetExplicitMaxSize(favTargetSize); 
    		}

			if (cfg.compactModeDesc && cfg.compactModeTitle) { artSize = cfg.compactMode ? (150 * scale) : (350 * scale); }
    		fArtView->SetExplicitSize(BSize(artSize, artSize));
    		fArtView->SetExplicitMinSize(BSize(artSize, artSize));
    		fArtView->SetExplicitMaxSize(BSize(artSize, artSize));
    		fBtnAddFav->SetExplicitSize(BSize(favSize, favSize));
    		
    		fStopBtn->SetExplicitSize(BSize(btnSize, btnSize));
    		fPauseBtn->SetExplicitSize(BSize(btnSize, btnSize));
    		fPlayBtn->SetExplicitSize(BSize(btnSize, btnSize)); 
    		fShuffleBtn->SetExplicitSize(BSize(btnSize, btnSize));

    		// 7. Toggle Tabs and Extra Info
    		if (cfg.compactMode) {
    			fPlayerGroup->GroupLayout()->SetInsets(5);
        		fControlStack->GroupLayout()->SetInsets(2);
        		
        		fCompactModeRadio->Show();
        		if (cfg.compactModeDesc)  { fDescView->Show(); } else { fDescView->Hide(); }
        		if (cfg.compactModeTitle) { fSongView->Show(); } else { fSongView->Hide(); }    
        		fquality->Show();
        		fListenersView->Show();
        		fSpectrum->Show();

        		for (int32 i = fTabView->CountTabs() - 1; i >= 0; i--) {
            		BTab* tab = fTabView->TabAt(i);
            		if (tab == fStationTab || tab == fFavTab || tab == fConfigTab || tab == fAboutTab)
                		fTabView->RemoveTab(i);
        		}
     		} else {
     			fPlayerGroup->GroupLayout()->SetInsets(20);
        		fControlStack->GroupLayout()->SetInsets(5);        
       			fSongView->SetExplicitMaxSize(BSize(B_SIZE_UNSET, B_SIZE_UNSET));
        			
       			
        		fCompactModeRadio->Show();
        		fDescView->Show();
        		fSongView->Show();
        		fquality->Show();
        		fListenersView->Show();
        		fSpectrum->Show();
        
        		if (fVolumeSlider) fVolumeSlider->SetTarget(this);
    	
        		for (int i = 0; i < 15; i++) {
            		if (fEQSliders[i]) fEQSliders[i]->SetTarget(this);
        		}
 
                BGroupView* groups[] = { fStationGroup, fFavGroup, fConfigGroup, fAboutGroup };
                const char* labels[] = { "Stations", "Fav", "Config", "About" };
                BTab** dynamicTabs[] = { &fStationTab, &fFavTab, &fConfigTab, &fAboutTab };

                for (int i = 0; i < 4; i++) { 
                    if (groups[i] == nullptr || dynamicTabs[i] == nullptr) continue;

                    bool found = false;
                    for (int32 j = 0; j < fTabView->CountTabs(); j++) {
                        if (fTabView->TabAt(j) == *dynamicTabs[i]) {
                            found = true;
                            break;
                        }
                    }

                    if (!found) {
                        *dynamicTabs[i] = new BTab();
                        (*dynamicTabs[i])->SetLabel(labels[i]);
                        fTabView->AddTab(groups[i], *dynamicTabs[i]);
                    }
                }
    		}

    		fArtView->InvalidateLayout();
    		fPlayerGroup->InvalidateLayout();
    		this->Layout(true); 
    		ApplyTheme(); 
    		
    		if (fDescView ) {
                ((SongLabel*)fDescView)->SetCompactMode(cfg.compactMode);
            }
            if (fSongView ) {
                ((SongLabel*)fSongView)->SetCompactMode(cfg.compactMode);
            }


            if (fMetaAndSpectrumStack != nullptr && (uintptr_t)fMetaAndSpectrumStack > 0x1000) {
                fMetaAndSpectrumStack->InvalidateLayout();
                if (cfg.showSpectrumVisuals) fMetaAndSpectrumStack->Layout(true);
                if (!cfg.showSpectrumVisuals) fMetaAndSpectrumStack->Layout(false);
            }
            if (fSpectrum) {
                fSpectrum->InvalidateLayout();
            }



    		BString deferredSelect;
    		if (message->FindString("deferred_select", &deferredSelect) == B_OK && fTabView) {
        		fTabView->InvalidateLayout();
        		fTabView->Layout(true);
        
        		const char* matchLabel = "Radio";
        		int32 fallbackIndex = 0;

        		if (deferredSelect == "stations") {
            		matchLabel = "Stations";
            		fallbackIndex = 1;
        		} else if (deferredSelect == "favorites") {
            		matchLabel = "Fav";
            		fallbackIndex = 2;
        		} else if (deferredSelect == "eq") {
            		matchLabel = "Config";
            		fallbackIndex = 3;
        		}

        		bool found = false;
        		for (int32 i = 0; i < fTabView->CountTabs(); i++) {
            		BTab* tab = fTabView->TabAt(i);
            		if (tab && tab->Label() != nullptr && strcmp(tab->Label(), matchLabel) == 0) {
                		fTabView->Select(i);
                		found = true;
                		break;
            		}
        		}
        
        		if (!found && fTabView->CountTabs() > fallbackIndex) {
            		fTabView->Select(fallbackIndex); 
        		}
    		}
    		break;
		}


		case MSG_SLEEP_CHANGED: {
    		delete fSleepRunner;
    		fSleepRunner = NULL;

    		int32 minutes = 0;
    		if (message->FindInt32("minutes", &minutes) == B_OK) {
        		if (minutes > 0) {
            		// Convert target configuration back into microsecond delay units
            		bigtime_t delay = (bigtime_t)minutes * 60 * 1000000;
            
            		BMessage tickMessage(MSG_SLEEP_TIMER_TICK);
            		fSleepRunner = new BMessageRunner(BMessenger(this), &tickMessage, delay, 1);

        		} 
   			}
    		break;
		}

		case MSG_SLEEP_TIMER_TICK: {
    
    		delete fSleepRunner;
    		fSleepRunner = NULL;

    		if (fSleepMenu && fSleepMenu->ItemAt(0)) {
        		fSleepMenu->ItemAt(0)->SetMarked(true);
    		}

    		if (mpv) {
        		mpv_command_string(mpv, "quit");
    		}
    
    		StopVisuals();

    		BDeskbar deskbar;
    		if (deskbar.HasItem("SuperMusicTrayIcon")) {
        		deskbar.RemoveItem("SuperMusicTrayIcon");
    		}
    		save_config();

    		be_app->PostMessage(B_QUIT_REQUESTED);
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

            // Centralized Visualizer Hook: At this point, play_random/play_favorite 
            // have already set 'currentStationID' to the new random track.
            if (fIconCache.count(currentStationID) > 0 && fIconCache[currentStationID] != nullptr) {
                if (this->fSpectrum != nullptr) {
                    this->fSpectrum->AdaptToAlbumArt(fIconCache[currentStationID]);
                }
            }

    		this->UpdateUI();
    		break;
		}

            
        case MSG_UPDATE_SONG: {
            const char* song = message->GetString("song", "Unknown");
            
            // Sync your application's global track tracker variable
            currentSong = (song != nullptr) ? song : "Unknown";

            UpdateStatus(currentDesc.c_str(), currentSong.c_str());

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
            if (cfg.showNotifications)
                fSizeContainer->Show();
            else
                fSizeContainer->Hide();
            InvalidateLayout();

            	save_config();
        	}
        	break;
    	}
    	


    	
		case MSG_PLAY_STATION: {
    		int32 index = fStationList->CurrentSelection();
    		if (index >= 0) {
        		StationItem* item = (StationItem*)fStationList->ItemAt(index);
        		if (item) {
            		this->PlayStation(item->GetChannel()); 

                	BString lStr("Listeners: ");
                	lStr << currentListeners.c_str();
                	if (fListenersView) fListenersView->SetText(lStr.String());
                	
                	Channel chan = item->GetChannel();
                    if (fIconCache.count(chan.id) > 0 && fIconCache[chan.id] != nullptr) {
                       // printf("[Visualizer] Found icon in cache for %s. Extracting colors...\n", chan.title.c_str());
                        if (this->fSpectrum != nullptr) {
                            this->fSpectrum->AdaptToAlbumArt(fIconCache[chan.id]);
                        }
                    } 
            
                    if (cfg.compactMode) {
                        //cfg.compactMode = false;
                        BMessage compactMsg(MSG_COMPACTM_CHANGED);
                        compactMsg.AddString("deferred_select", "radio");
                        this->PostMessage(&compactMsg);
                    }

            		this->UpdateUI();
        		}
    		}
    		break;
		}

		case MSG_PLAY_FAV: {
    		int32 favoriteIndex = -1;
            if (message->FindInt32("index", &favoriteIndex) != B_OK) {
                favoriteIndex = -1;
            }

    		if (favoriteIndex < 0 && fFavList) {
        		favoriteIndex = fFavList->CurrentSelection();
    		}

    		if (favoriteIndex >= 0 && fFavList) {
                // FIX: Swap out runtime dynamic_cast for an explicit static pointer conversion
                StationItem* item = (StationItem*)fFavList->ItemAt(favoriteIndex);
        		if (item != nullptr) {           
            		this->PlayStation(item->GetChannel());

                    BString lStr("Listeners: ");
                    lStr << currentListeners.c_str();
                    if (fListenersView) fListenersView->SetText(lStr.String());
                    
                    Channel chan = item->GetChannel();
                    if (fIconCache.count(chan.id) > 0 && fIconCache[chan.id] != nullptr) {
                        if (this->fSpectrum != nullptr) {
                            this->fSpectrum->AdaptToAlbumArt(fIconCache[chan.id]);
                        }
                    } 

                    if (cfg.compactMode) {
                        cfg.compactMode = false;
                        BMessage compactMsg(MSG_COMPACTM_CHANGED);
                        compactMsg.AddString("deferred_select", "radio");
                        this->PostMessage(&compactMsg);
                    }
                    
                    this->UpdateUI();
        		}
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
 

        case MSG_UPDATE_ART: {
            BBitmap* newArt;
            if (message->FindPointer("bitmap", (void**)&newArt) == B_OK) {
                if (Lock()) {
                    fArtCache[currentStationID] = newArt;
                    fAlbumArt = newArt;
                    
                    if (fArtView) {
                        ((AlbumArtView*)fArtView)->SetBitmap(fAlbumArt);
                    }
  
                    BBitmap* targetArt = fAlbumArt;
                    if (targetArt == nullptr && fIconCache.count(currentStationID) > 0) {
                        targetArt = fIconCache[currentStationID];
                    }
           
                    if (targetArt != nullptr && this->fSpectrum != nullptr) {
                        this->fSpectrum->AdaptToAlbumArt(targetArt);
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
    		
    		// FIX: Use proper Haiku FindString API pattern to avoid null assignment
    		const char* song = nullptr;
    		if (message->FindString("song", &song) != B_OK || song == nullptr) {
        		song = "Unknown";
    		}

    		if (is_paused) {
       		 	mpv_command_string(mpv, "set pause no");        
            	char* current_title = mpv_get_property_string(mpv, "media-title");
            	if (current_title) {
                	song = current_title; 
                	// DO NOT clear song pointer yet; use a temporary swap if freeing immediately
                	fSongView->SetText(song);
                	mpv_free(current_title);
            	} else {
                	fSongView->SetText(song);
            	}
    		} 
    		else if (fStationList->CurrentSelection() < 0) {
                if (cfg.compactMode) {
                    cfg.compactMode = false; 
                    BMessage compactMsg(MSG_COMPACTM_CHANGED);
                    compactMsg.AddString("deferred_select", "stations"); 
                    this->PostMessage(&compactMsg);
                } else {
                    if (fTabView) fTabView->Select(1); 
                }
    		} 
    		else {
        		int32 index = fStationList->CurrentSelection();
        		StationItem* item = (StationItem*)fStationList->ItemAt(index);
        		if (item) {
            		this->PlayStation(item->GetChannel());   
                    
                    if (cfg.compactMode) {
                        //cfg.compactMode = false; 
                        BMessage compactMsg(MSG_COMPACTM_CHANGED);
                        compactMsg.AddString("deferred_select", "radio"); 
                        this->PostMessage(&compactMsg);
                    }

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
  
  
//--------------------------------- Projectm         
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
        
//--------------------------------- Projectm           
		case MSG_REFRESH_PRESETS: {
    		const char* home = getenv("HOME");
    		if (home && fPresetList) {
        		std::string path = std::string(home) + "/config/settings/SuperMusicThingy/milk_presets/";
        		PopulatePresetList(fPresetList, path.c_str());
    		}
    		break;
		}
		
		
//--------------------------------- Projectm   		
		case MSG_PRESET_SELECTED: {
    		int32 index = fPresetList->CurrentSelection();
    		if (index >= 0) {
        		BStringItem* item = (BStringItem*)fPresetList->ItemAt(index);
        		load_specific_preset(item->Text()); 
        		 if (fChkShuffle != nullptr) {
                		fChkShuffle->SetValue(B_CONTROL_OFF);
            		}	
        		cfg.autoShuffle = false;
        		cfg.autoShuffleVisuals = false;
    		}
    		break;
		}
		
//--------------------------------- Projectm   
		case MSG_TOGGLE_PRESETS: {
    		bool show = (fPresetToggle->Value() == B_CONTROL_ON);    
    		if (show) {
        		fPresetScroll->Show();
    		} else {
        		fPresetScroll->Hide();    	
        	}    
    		InvalidateLayout();
    		ResizeToPreferred();
    		break;
		}
		#endif
//--------------------------------- Projectm     





		case MSG_EQ_RESET:
		{
    		for (int i = 0; i < 10; i++) {
        		fEQSliders[i]->SetValue(0);
    		}    
    		fLimitInput->SetValue(0);
    		fLimitLimit->SetValue(0);
    		fLimitRelease->SetValue(100); 
    		// UpdateMPVFilters();
    		break;
		}
		
		case MSG_EQ_CHANGED: {
    		cfg.eqEnabled = (fEQToggle->Value() == B_CONTROL_ON);
    		for(int i=0; i<15; i++) cfg.eqBands[i] = fEQSliders[i]->Value();
    		cfg.limitIn = fLimitInput->Value();
    		cfg.limitLmt = fLimitLimit->Value();
    		cfg.limitRel = fLimitRelease->Value();     		   
    		UpdateMPVFilters();
    		save_config(); 
    		if (fSpectrum) fSpectrum->Invalidate(); 
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
            
        case MSG_SET_PRESET_ROCK:
    		ApplyPreset(kPresetRock);
    		break;    		
		case MSG_SET_PRESET_BASS:
    		ApplyPreset(kPresetBass);
    		break;
		case MSG_SET_PRESET_JAZZ:
    		ApplyPreset(kPresetJazz);
    		break;
    	case MSG_SET_PRESET_FLAT:
    		ApplyPreset(kPresetFlat);
    		break;    		


		case MSG_ACTIVATE_APP: {
    		if (IsHidden()) {
        		Show();
    		} else {
        		Activate(true);
    		}

    		BString targetTab;
    		if (message->FindString("target_tab", &targetTab) == B_OK) {
        		if (cfg.compactMode) {
            		cfg.compactMode = false;
            
            		BMessage compactMsg(MSG_COMPACTM_CHANGED);
            		compactMsg.AddString("deferred_select", targetTab);
            		this->PostMessage(&compactMsg);
            		break; 
        		}
        
        		if (fTabView) {
            		const char* matchLabel = "Radio";
            		if (targetTab == "stations")  matchLabel = "Stations"; 
            		if (targetTab == "favorites") matchLabel = "Fav"; 
            		if (targetTab == "eq")        matchLabel = "Config";

            		for (int32 i = 0; i < fTabView->CountTabs(); i++) {
                		BTab* tab = fTabView->TabAt(i);
                		if (tab && tab->Label() != nullptr && strcmp(tab->Label(), matchLabel) == 0) {
                    		fTabView->Select(i);
                    		break;
                		}
            		}
        		}
    		}
    		break;
		}



        
		case MSG_CFG_SYS_TRAY: {
    		BCheckBox* chk = dynamic_cast<BCheckBox*>(FindView("chk_sysTray"));
    
    		if (chk) {
        		cfg.sysTray = (chk->Value() == B_CONTROL_ON);
        
        		save_config(); 
        		UpdateTrayState(cfg.sysTray);
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
	
	if (cfg.compactMode) {
        // Delete only the tabs that were unlinked and hidden from fTabView
        delete fStationTab;
        delete fFavTab;
        delete fConfigTab;
        delete fAboutTab;
    }
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
    delete fSleepRunner;
    fAlbumArt = nullptr; 

    #ifdef USE_PROJECTM
    if (pm) {
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
    
        	projectm_destroy(pm);
        	pm = nullptr;
    }
    #endif
}



class SuperMusicApp : public BApplication {
public:
    SuperMusicApp() : BApplication("application/x-vnd.HaikuSuperMusicThingy") {}
	virtual void MessageReceived(BMessage* message);
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

    	// --- COLD BOOT TRAY CLEANUP ENGINE ---
    	BDeskbar deskbar;
    	if (deskbar.HasItem("SuperMusicTrayIcon")) {
        	if (!cfg.sysTray) {
            	// Remove the zombie icon immediately if the user turned this option off
            	deskbar.RemoveItem("SuperMusicTrayIcon");
        	}
    	}
    	// -------------------------------------

    	gGuiWindow = new SuperMusicWindow();      
    	gGuiWindow->Show();
    
    	// Clean re-binding loop if sysTray option is checked
    	if (cfg.sysTray && gGuiWindow->Lock()) {
        	gGuiWindow->UpdateTrayState(true, false); 
        	gGuiWindow->Unlock();
    	}
    
    	if (cfg.compactMode) {			
        	gGuiWindow->PostMessage(MSG_COMPACTM_CHANGED);
    	}

    	thread_id mpvThread = spawn_thread(mpv_loop_thread, "mpv_event_loop", 
        	B_NORMAL_PRIORITY, gGuiWindow);
    	resume_thread(mpvThread);
    
    	if (cfg.autoShuffle) {
        	gGuiWindow->PostMessage(MSG_SHUFFLE);
    	}
}

     
    
virtual bool QuitRequested() { 

    	BDeskbar deskbar;
   		 if (deskbar.HasItem("SuperMusicTrayIcon")) {
        deskbar.RemoveItem("SuperMusicTrayIcon");
   		 }          	   	
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
        // --- NATIVE TICK-BASED VOLUME FADER OVER TIME ---
        if (is_fading) {
            bigtime_t elapsed = system_time() - fade_start_time;
            if (elapsed >= fade_duration_us) {
                // Fade duration complete! Snap strictly to target and clear flag
                mpv_set_property(mpv, "volume", MPV_FORMAT_DOUBLE, &fade_target_vol);
                is_fading = false;
            } else {
                // Calculate time progress as a 0.0 to 1.0 fraction
                double t = (double)elapsed / fade_duration_us;
                // Sweeping exponential scale curve
                double next_vol = fade_start_vol + (fade_target_vol - fade_start_vol) * pow(t, 1.09);
                mpv_set_property(mpv, "volume", MPV_FORMAT_DOUBLE, &next_vol);
            }
        }

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

        mpv_event *event = mpv_wait_event(mpv, 0.01); 
        
        if (event->event_id == MPV_EVENT_NONE) {
           // snooze(15000); // Sleep for 15ms to pace the thread if MPV is idle
            continue;
        }
        
        if (event->event_id == MPV_EVENT_SHUTDOWN) break;

        // --- INTERCEPT FILE_LOADED (THE BUFFER COMPLETED METER) ---
        if (event->event_id == MPV_EVENT_FILE_LOADED) {
            // Stream audio bytes are entering hardware! Turn on the non-blocking fade engine.
            mpv_get_property(mpv, "volume", MPV_FORMAT_DOUBLE, &fade_start_vol);
            fade_target_vol = user_base_volume; // Target whatever slider setting user has
            fade_start_time = system_time();    // Capture high precision Haiku microsecond timestamp
            //fade_duration_us = 546000;         // 780ms 
            fade_duration_us = 160000;         // 160ms 
            is_fading = true;
        }
        
        if (event->event_id == MPV_EVENT_PROPERTY_CHANGE) {
            mpv_event_property *prop = (mpv_event_property *)event->data;
            if (!prop || !prop->name || !prop->data) continue;
            
            std::string propName = prop->name;

            // TRACK USER BASE VOLUME SEPARATELY
			if (propName == "volume") {
    			double v = *(double*)prop->data;
    			// Only save the user's base volume if it is greater than zero
    			if (!is_fading && v > 0.0) {
        			user_base_volume = v;
    			}
			}

            else if (propName == "media-title") {
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
                    
                    if (node->format == MPV_FORMAT_NODE_MAP && cfg.ladspaEnabled) {
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
                    
                    if (node->format == MPV_FORMAT_NODE_MAP && node->u.list != nullptr && !cfg.ladspaEnabled) {
                        for (int i = 0; i < node->u.list->num; i++) {
                            if (node->u.list->keys[i] != nullptr && strstr(node->u.list->keys[i], "Peak_level")) {
                
                                double peak = 0.0;
                                mpv_node* valNode = &node->u.list->values[i];
                
                                if (valNode->format == MPV_FORMAT_STRING && valNode->u.string != nullptr) {
                                    peak = atof(valNode->u.string);
                                } else if (valNode->format == MPV_FORMAT_DOUBLE) {
                                    peak = valNode->u.double_; 
                                } else if (valNode->format == MPV_FORMAT_INT64) {
                                    peak = (double)valNode->u.int64; 
                                } else {
                                    continue; 
                                }

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
	
	BDeskbar deskbar;
   	if (deskbar.HasItem("SuperMusicTrayIcon")) {
        deskbar.RemoveItem("SuperMusicTrayIcon");
   	}   
   	
    if (cfg.sysTray) {
        UpdateTrayState(true);
        return false; 
    }

    fIsQuitting = true; 

    if (mpv) {
        mpv_command_string(mpv, "quit");
    }
    StopVisuals();

    snooze(500000); 
    
    be_app->PostMessage(B_QUIT_REQUESTED);
    return true; 
}


void SuperMusicWindow::ApplyPreset(const float* values) {
    for (int i = 0; i < 15; i++) {
        fEQSliders[i]->SetValue((int32)values[i]);
    }
  
    UpdateMPVFilters();
}

class MyIcon : public BView {
public:
    MyIcon(BRect frame) 
        : BView(frame, "SuperMusicTrayIcon", B_FOLLOW_NONE, B_WILL_DRAW | B_FRAME_EVENTS | B_FULL_UPDATE_ON_RESIZE) {
        fIcon = NULL;
        _LoadIcon();
    }

    MyIcon(BMessage* archive) : BView(archive) {
        fIcon = NULL;
        _LoadIcon();
    }

    virtual ~MyIcon() { delete fIcon; }
    static _EXPORT BArchivable* Instantiate(BMessage* archive);

    virtual void AttachedToWindow() {
        BView::AttachedToWindow();
        _UpdateBackgroundColor();
    }


    virtual void FrameResized(float newWidth, float newHeight) {
        BView::FrameResized(newWidth, newHeight);
        _LoadIcon();
        Invalidate();
    }

		virtual status_t Archive(BMessage* archive, bool deep = true) const {
    		status_t err = BView::Archive(archive, deep);
    		if (err != B_OK) return err;
    
    		// Explicitly target the layout class identification
    		archive->AddString("class", "MyIcon");

  			#ifndef IS_HAIKU_32BIT
    		// 64-bit Native: Everything is unified under a modern compiler toolchain
    		archive->AddString("add_on", "application/x-vnd.HaikuSuperMusicThingy"); 
			#else
    		// 32-bit Hybrid: Tell GCC 2 Deskbar to load the separate GCC 2 shared library
    		archive->AddString("add_on", "application/x-vnd.SuperMusicTrayIconLibrary"); 
			#endif
    
    return B_OK;
}

		virtual void MessageReceived(BMessage* message) {
    		switch (message->what) {
        		case B_COLORS_UPDATED:
            		_UpdateBackgroundColor();
            		_LoadIcon(); 
            		Invalidate();
            		break;
        		case B_QUIT_REQUESTED: {
            		// Let the Replicant drop itself directly out of the Deskbar container shelf
            		BDeskbar deskbar;
            		if (deskbar.HasItem("SuperMusicTrayIcon")) {
                		deskbar.RemoveItem("SuperMusicTrayIcon");
            		}
            		break;
        		}
        		default:
            		BView::MessageReceived(message);
            		break;
    		}
		}


    virtual void Draw(BRect updateRect) {
        // Render system background cleanly
        if (Parent()) {
            SetLowColor(Parent()->ViewColor());
            FillRect(updateRect, B_SOLID_LOW);
        }

        if (fIcon) {
            SetDrawingMode(B_OP_ALPHA);
            SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
            
            // Center the icon inside the available tray bounds dynamically
            BRect bounds = Bounds();
            float iconSize = fIcon->Bounds().Width();
            float x = (bounds.Width() - iconSize) / 2.0f;
            float y = (bounds.Height() - iconSize) / 2.0f;
            
            DrawBitmap(fIcon, BPoint(x, y));
        } else {
            // Highlighting fallback vector matching Haiku UI defaults
            SetHighColor(ui_color(B_NAVIGATION_BASE_COLOR));
            FillRect(Bounds());
        }
    }

virtual void MouseDown(BPoint point) {
    int32 buttons;
    if (Window()->CurrentMessage()->FindInt32("buttons", &buttons) != B_OK)
        return;

    // Build the messenger targeting the main application running loop
    BMessenger appMessenger("application/x-vnd.HaikuSuperMusicThingy");

    // Fix Zombie State: If the main app isn't running, launch it!
    if (!appMessenger.IsValid()) {
        status_t launchErr = be_roster->Launch("application/x-vnd.HaikuSuperMusicThingy");
        if (launchErr == B_OK || launchErr == B_ALREADY_RUNNING) {
            // Re-initialize the messenger to catch the freshly spawned instance
            appMessenger = BMessenger("application/x-vnd.HaikuSuperMusicThingy");
        }
    }

    if (buttons & B_PRIMARY_MOUSE_BUTTON) {
        if (appMessenger.IsValid()) {
            appMessenger.SendMessage(MSG_ACTIVATE_APP);
        }
    } else if (buttons & B_SECONDARY_MOUSE_BUTTON) {
        BPopUpMenu *popup = new BPopUpMenu("tray_popup", false, false);        
        
        // 1. Show Player (Targets the Radio Tab)
        BMessage* showMsg = new BMessage(MSG_ACTIVATE_APP);
        showMsg->AddString("target_tab", "radio");
        popup->AddItem(new BMenuItem("Show Player", showMsg));
        
        popup->AddSeparatorItem();

        // 2. Stations Option (Now targets the distinct Stations Tab)
        BMessage* stationsMsg = new BMessage(MSG_ACTIVATE_APP);
        stationsMsg->AddString("target_tab", "stations");
        popup->AddItem(new BMenuItem("Stations", stationsMsg));

        // 3. Favorites Option
        BMessage* favsMsg = new BMessage(MSG_ACTIVATE_APP);
        favsMsg->AddString("target_tab", "favorites");
        popup->AddItem(new BMenuItem("Favorites", favsMsg));
        
        // 4. Equalizer Option
        BMessage* eqMessage = new BMessage(MSG_ACTIVATE_APP);
        eqMessage->AddString("target_tab", "eq"); 
        popup->AddItem(new BMenuItem("Equalizer", eqMessage));
        
        popup->AddSeparatorItem();
        popup->AddItem(new BMenuItem("Shuffle", new BMessage(MSG_SHUFFLE)));
        popup->AddSeparatorItem();
        popup->AddItem(new BMenuItem("Quit", new BMessage(B_QUIT_REQUESTED)));          
        
        if (appMessenger.IsValid()) {
            popup->SetTargetForItems(appMessenger);        
        } else {
            popup->SetTargetForItems(this); 
        }
        
        BPoint screenPoint = ConvertToScreen(point);        
        popup->Go(screenPoint, true, true, true);
    }

}


private:
    void _UpdateBackgroundColor() {
        if (Parent()) {
            SetViewColor(Parent()->ViewColor());
            SetLowColor(Parent()->ViewColor());
        } else {
            SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
            SetLowColor(ui_color(B_PANEL_BACKGROUND_COLOR));
        }
    }

    void _LoadIcon() {
        delete fIcon;
        fIcon = NULL;

        // Base sizing using modern look metrics matching the deskbar configuration
        float size = be_control_look->ComposeIconSize(B_MINI_ICON).Width();
        
        // Ensure bounds scale safely if target tray frame is smaller
        if (Bounds().Width() > 0 && Bounds().Width() < size) {
            size = Bounds().Width();
        }

        fIcon = new BBitmap(BRect(0, 0, size - 1, size - 1), B_RGBA32);

        entry_ref ref;
        if (be_roster->FindApp("application/x-vnd.HaikuSuperMusicThingy", &ref) == B_OK) {
            // Attempt HVIF vector extraction first for perfect auto-scaling
            if (BNodeInfo::GetTrackerIcon(&ref, fIcon, (icon_size)size) != B_OK) {
                BMimeType type("application/x-vnd.HaikuSuperMusicThingy");
                type.GetIcon(fIcon, (icon_size)size);
            }
        }
    }
    BBitmap* fIcon;
};






_EXPORT BArchivable* MyIcon::Instantiate(BMessage* data) {
    if (!validate_instantiation(data, "MyIcon"))
        return NULL;
    return new MyIcon(data);
}


extern "C" _EXPORT BView* instantiate_deskbar_item() {
    // Dynamically retrieve optimal system mini icon bounds (handles scale/HiDPI)
    float size = be_control_look->ComposeIconSize(B_MINI_ICON).Width();
    return new MyIcon(BRect(0, 0, size - 1, size - 1));
}


extern "C" _EXPORT BArchivable* instantiate_tray_icon(BMessage* data) {
    return MyIcon::Instantiate(data);
}

void SuperMusicApp::MessageReceived(BMessage* message) { 
    switch (message->what) {
        case MSG_ACTIVATE_APP:
        case MSG_SHUFFLE: { 
            BWindow* win = WindowAt(0);
            if (win) {
                win->PostMessage(message);
            }
            break;
            

            
        }
        default:
            BApplication::MessageReceived(message);
            break;
    }
}



int main() {
	std::srand(std::time(nullptr)); 
	ensure_config_dir();
    SuperMusicApp app;   
    app.Run();    
    return 0;
}

