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
    bool showNotifications = false;
    bool showVisuals = false;
    bool autoShuffle = false;
    #ifdef USE_SYSTRAY
    bool sysTray = true;
    #else
    bool sysTray = false;
    #endif
    bool autoShuffleVisuals = false;
    bool showSpectrumVisuals = false;
    bool autoVsync = false;
    bool ladspaEnabled = false;
    bool shuffleFavsOnly = false;
    bool compactMode = false;
    int notifyIconSize = 64; 
    std::string updateTheme = "Default";
    std::string quality = "128k";
    bool eqEnabled = false;
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
    cfg.showSpectrumVisuals = false;   
    cfg.shuffleFavsOnly = false; 
    cfg.eqEnabled = false;
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
                cfg.showSpectrumVisuals = j.value("showSpectrumVisuals", false);   
                cfg.shuffleFavsOnly = j.value("shuffleFavsOnly", false); 
                cfg.eqEnabled = j.value("eqEnabled", false);                
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
        : BView(frame, name, B_FOLLOW_ALL, B_WILL_DRAW | B_FRAME_EVENTS) {
        SetViewColor(B_TRANSPARENT_COLOR); 
        fCurrentLevel = -60.0;
        
        // FIX: frequencyData is a real array, memset now gets the correct destination pointer
        memset(frequencyData, 0, 64);
        
        // Setup initial default palette
        for (int i = 0; i < 64; i++) {
            fBarHeights[i] = 0.0f;
            fBarVelocities[i] = 0.0f;
            fPeakHeights[i] = 0.0f;
            fPeakHold[i] = 0;
            fArtworkPalette[i] = { (uint8)(40 + i * 2), 210, (uint8)(255 - i * 3), 255 };
        }
        srand(time(nullptr));
    }
    
    void UpdateLevel(double level) {
    	if (!cfg.showSpectrumVisuals || !cfg.eqEnabled) return;
  
        if (level > fCurrentLevel) {
            fCurrentLevel = level;
        } else {
            fCurrentLevel = (fCurrentLevel * 0.88) + (level * 0.12);
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

        // MULTI-ROW MATRIX: Define three distinct vertical zones to sample
        int32 rows[3] = {
            (int32)(height * 0.30f),  // Upper Zone: Captures sunset yellow & palm trees
            (int32)(height * 0.55f),  // Middle Zone: Captures bridge neon purples & pinks
            (int32)(height * 0.75f)   // Lower Zone: Captures blue/cyan grid lines & text glow
        };

        for (int i = 0; i < 64; i++) {
            float horizontalPercent = (float)i / 64.0f;
            int32 targetPixelX = (int32)(horizontalPercent * width);
            int32 byteOffset = targetPixelX * 4; 

            uint32 sumRed = 0, sumGreen = 0, sumBlue = 0;

            // Sample across all three vertical coordinate zones
            for (int r = 0; r < 3; r++) {
                uint8* rowPtr = bitsBase + (rows[r] * bpr);
                sumBlue  += rowPtr[byteOffset + 0];
                sumGreen += rowPtr[byteOffset + 1];
                sumRed   += rowPtr[byteOffset + 2];
            }

            // Calculate the blended average for this spectrum column slice
            uint8 finalRed   = (uint8)(sumRed / 3);
            uint8 finalGreen = (uint8)(sumGreen / 3);
            uint8 finalBlue  = (uint8)(sumBlue / 3);

            // Safety Guard: Avoid muddy backgrounds; boost vibrancy if too dark
            if (finalRed < 35 && finalGreen < 35 && finalBlue < 35) {
                fArtworkPalette[i] = { 244, 90, 160, 255 }; // Vaporwave Hot Pink fallback
            } else {
                fArtworkPalette[i] = { finalRed, finalGreen, finalBlue, 255 };
            }
        }
        Invalidate();
    }






    virtual void Draw(BRect updateRect) {
    	
    if (!cfg.showSpectrumVisuals || !cfg.eqEnabled) {
        if (Parent() != nullptr) {
            SetHighColor(Parent()->ViewColor());
        } else {
            SetHighColor(ui_color(B_PANEL_BACKGROUND_COLOR)); // Standard fallback
        }
        FillRect(Bounds());
        return;
    }


    	
        BRect b = Bounds();        
        float floor = -45.0f; // Raised floor slightly to tighten baseline noise
        float peak = (float)fCurrentLevel;
        
        if (peak < floor) peak = floor;
        if (peak > 0.0f) peak = 0.0f;

        float masterMagnitude = (peak - floor) / (0.0f - floor);

        // Dynamically locate the slider pointer
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

        // --- FIXED 50% MASTER ACCELERATION SENSITIVITY DAMPENER ---
        // 1. Force a strict global 50% reduction right out of the gate
        float masterSensitivityMultiplier = 0.50f;

        // 2. Apply a quadratic divisor curve to counteract high limiter gain settings.
        // As currentInputDb climbs from 0 to +20dB, this divisor smoothly expands, 
        // dynamically pulling the top peaks down from the screen ceiling.
        float limiterDivisor = 1.0f + (currentInputDb * 0.065f);

        // Compute the highly attenuated final rendering magnitude
        masterMagnitude = (powf(masterMagnitude, 2.0f) * masterSensitivityMultiplier) / limiterDivisor;

        float width = b.Width();
        float height = b.Height();
        int numBars = 64;
        float barWidth = width / numBars;

        const float springStiffness = 0.28f; 
        const float springDamping = 0.74f;   

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

            float finalBarHeight = fBarHeights[i];
            if (finalBarHeight > height) finalBarHeight = height;
            if (finalBarHeight < 0.0f) {
                finalBarHeight = 0.0f;
                fBarVelocities[i] = 0.0f; 
            }

            if (finalBarHeight >= fPeakHeights[i]) {
                fPeakHeights[i] = finalBarHeight;
                fPeakHold[i] = 6; 
            } else {
                if (fPeakHold[i] > 0) {
                    fPeakHold[i]--;
                } else {
                    fPeakHeights[i] -= (height * 0.025f); 
                    if (fPeakHeights[i] < 0.0f) fPeakHeights[i] = 0.0f;
                }
            }

            SetHighColor(fArtworkPalette[i]);             
            FillRect(BRect(i * barWidth, height - finalBarHeight, 
                           (i + 1) * barWidth - 1, height));

            if (fPeakHeights[i] > finalBarHeight && fPeakHeights[i] > 2.0f) {
                rgb_color peakColor = fArtworkPalette[i];
                peakColor.red   = (uint8)min_c(255, peakColor.red + 50);
                peakColor.green = (uint8)min_c(255, peakColor.green + 50);
                peakColor.blue  = (uint8)min_c(255, peakColor.blue + 50);
                
                SetHighColor(peakColor); 
                StrokeLine(BPoint(i * barWidth, height - fPeakHeights[i]),
                           BPoint((i + 1) * barWidth - 1, height - fPeakHeights[i]));
            }
        }
    }


    void UpdateData(const uint8* data, size_t size) {
    	if (!cfg.showSpectrumVisuals || !cfg.eqEnabled) return;
        memcpy(frequencyData, data, size > 64 ? 64 : size);
        Invalidate();
    }

private:
    double    fCurrentLevel; 
    uint8     frequencyData[64]; // FIX: Restored missing array length bound constraints
    float     fBarHeights[64];   // FIX: Restored array bounds
    float     fBarVelocities[64];// FIX: Restored array bounds
    float     fPeakHeights[64];  // FIX: Restored array bounds
    int       fPeakHold[64];     // FIX: Restored array bounds
    rgb_color fArtworkPalette[64];// FIX: Restored array bounds
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
    
    if (view->ViewColor().red != bg.red || view->HighColor().red != txt.red) {
        view->SetViewColor(bg);
        view->SetLowColor(bg);
        view->SetHighColor(txt);
        
        // Handle specific types that need extra love
        if (BSlider* slider = dynamic_cast<BSlider*>(view)) {
            slider->UseFillColor(true, &txt);
        }

        if (BTextView* textView = dynamic_cast<BTextView*>(view)) {
            textView->SetFontAndColor(NULL, B_FONT_ALL, &txt);
        }

       if (BListView* listView = dynamic_cast<BListView*>(view)) {
            for (int32 i = 0; i < listView->CountItems(); i++) {
                listView->InvalidateItem(i);
            }
        }

        view->Invalidate();
    }
    
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
        bg2Val = {0, 0, 0, 255};      // Dark Grey
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
    if (Lock()) {
        fStationView->SetText("");
        fSongView->SetText(song);
        Unlock();
    }
}





void SuperMusicWindow::UpdateTrayState(bool enabled, bool hideWindow) {
    BDeskbar deskbar;
    const char* trayItemName = "SuperMusicTrayIcon"; 

    if (enabled) {
        if (!deskbar.HasItem(trayItemName)) {
            status_t err = B_ERROR;

#if defined(__x86_64__)
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
    : BWindow(BRect(100, 100, 550, 380), "SuperMusicThingy", B_TITLED_WINDOW, 
              B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS | B_QUIT_ON_WINDOW_CLOSE)
{
    fAlbumArt = nullptr;
    #ifdef USE_PROJECTM
    fProjectM = pm; 
	#endif
    
    setenv("LADSPA_PATH", "/boot/system/lib/ladspa_HaikuSuperMusicThingy", 1);     
 
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
    
    fquality = new BStringView("quality", "");
    fquality->SetFont(&smallFont);
    
    fListenersView = new BStringView("listeners", "");
    fListenersView->SetFont(&smallFont);
	
	fCompactModeRadio = new BCheckBox("chk_compact_radio", "Compact", new BMessage(MSG_COMPACTM_CHANGED));
	fCompactModeRadio->SetFont(&smallFont);
	
	fCompactModeConfig = new BCheckBox("chk_compact_config", "Compact Mode", new BMessage(MSG_COMPACTM_CHANGED));

	
   
    
    // Album Art

    fArtView = new AlbumArtView();
	fArtView->SetExplicitSize(BSize(325 * scale, 325 * scale)); 
    fArtView->SetExplicitMinSize(BSize(325 * scale, 325 * scale));
	fArtView->SetExplicitMaxSize(BSize(325 * scale, 325 * scale));

	fSpectrum = new SpectrumView(BRect(0, 0, 200, 50), "spectrum"); 
	fSpectrum->SetExplicitMinSize(BSize(200, 50));
	fSpectrum->SetExplicitMaxSize(BSize(B_SIZE_UNSET, 50)); 
    
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
    
    fVolumeSlider = new WheelSlider("volume", "Vol", new BMessage(MSG_VOL_CHANGE), 0, 100, B_HORIZONTAL, 5);
    fVolumeSlider->SetValue(100);
    fVolumeSlider->SetTarget(this); 
    fVolumeSlider->SetModificationMessage(new BMessage(MSG_VOL_CHANGE));


    // --- LAYOUT BUILDER FOR PLAYER TAB ---

fControlStack = new BGroupView(B_VERTICAL, 5); 

BLayoutBuilder::Group<>(fControlStack, B_VERTICAL, 5)

    .SetInsets(5)  
    .Add(fVolumeSlider)
        .AddGroup(B_HORIZONTAL, 10)
        .AddGlue() 
        .Add(fStopBtn)
        .Add(fPauseBtn)
        .Add(fPlayBtn)
        .Add(fShuffleBtn)
      .End();

BLayoutBuilder::Group<>(fPlayerGroup, B_VERTICAL, 0)
    .SetInsets(20)
    .Add(fArtView) 
    
    // Meta information section
    .AddGroup(B_HORIZONTAL, 10)     	
        .Add(fDescView) 
    .End()
    
    .AddGroup(B_HORIZONTAL, 10) 
        .Add(fSongView)
    .End()
    
    .AddGroup(B_HORIZONTAL, 10) 
        .Add(fSpectrum)  
    .End()
    
    .AddGlue()
    
    // Controls and settings footer section
    .AddGroup(B_HORIZONTAL, 16) 
        .AddGroup(B_VERTICAL, 6) 
            .AddStrut(5)     	
            .Add(fListenersView)
            .Add(fquality)
            .Add(fCompactModeRadio)               
        .End()        
        .Add(fBtnAddFav) // Now cleanly grouped horizontally alongside the settings vertical stack
    .End()
    
    // Direct stack attachment without double-glue sandwiching
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
    BMessage* msgSleep15  = new BMessage(MSG_SLEEP_CHANGED); msgSleep15->AddInt32("minutes", 15);
    BMessage* msgSleep30  = new BMessage(MSG_SLEEP_CHANGED); msgSleep30->AddInt32("minutes", 30);
    BMessage* msgSleep1h  = new BMessage(MSG_SLEEP_CHANGED); msgSleep1h->AddInt32("minutes", 60);
    BMessage* msgSleep3h  = new BMessage(MSG_SLEEP_CHANGED); msgSleep3h->AddInt32("minutes", 180); 
    BMessage* msgSleep6h  = new BMessage(MSG_SLEEP_CHANGED); msgSleep6h->AddInt32("minutes", 360);  
    BMessage* msgSleep8h  = new BMessage(MSG_SLEEP_CHANGED); msgSleep8h->AddInt32("minutes", 480);

    fSleepMenu->AddItem(new BMenuItem("Disabled", msgSleep0));
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
    
    
    fChkSysTray = new BCheckBox("chk_sysTray", "Use System tray", new BMessage(MSG_CFG_SYS_TRAY));
    fChkSysTray->SetValue(cfg.sysTray ? B_CONTROL_ON : B_CONTROL_OFF);
    fChkSysTray->SetEnabled(false);    
    
    // Theme and Presets
    fChkTheme = new BCheckBox("chk_theme", "Dark Theme (Experimental)", new BMessage(MSG_CFG_THEME));
    fChkTheme->SetValue(cfg.updateTheme == "Dark" ? B_CONTROL_ON : B_CONTROL_OFF);

    fPresetToggle = new BCheckBox("preset_toggle", "MilkDrop Presets:", new BMessage(MSG_TOGGLE_PRESETS));
    fPresetToggle->SetValue(B_CONTROL_OFF); 

    // Visuals and Favorites
    fVisualsCheckbox = new BCheckBox("visuals_toggle", "Enable Visualizer (Experimental)", new BMessage(MSG_TOGGLE_VISUALS));
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
	
    fEnableSpectrum = new BCheckBox("chk_spectrum", "Enable Spectrum Bars", new BMessage(MSG_TOGGLE_Spectrum));
	fEnableSpectrum->SetValue(cfg.showSpectrumVisuals ? B_CONTROL_ON : B_CONTROL_OFF); 
    
	bool ladspaSupported = IsFFmpegLadspaAvailable();
	fEnableladspa = new BCheckBox("enable_ladspa", "Use Ladspa", new BMessage(MSG_TOGGLE_LADSPA)); 

	if (!ladspaSupported) {
    	fEnableladspa->SetEnabled(false);
    	fEnableladspa->SetLabel("Use Ladspa (Not compiled in FFmpeg)");
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
    .Add(fChkTheme)
   	.Add(fEQToggle)
   	.Add(fEnableladspa)
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

        //if (cfg.showSpectrumVisuals) {
            filterChain << ",@bouncy:astats=metadata=1:reset=1"; 
       // }
    

    } else {
        // --- LADSPA FILTERS (mbeq_1197) ---
        filterChain = "@bouncy:lavfi=[";

        BString eqPart;
        eqPart << "ladspa=file='/boot/system/lib/ladspa_HaikuSuperMusicThingy/mbeq_1197.so':p=mbeq:c=";
        
        for (int i = 0; i < numBands; i++) {
            BString val;
            val.SetToFormat("%.2f", (float)fEQSliders[i]->Value());
            eqPart << val << (i == (numBands - 1) ? "" : "|");
        }
        eqPart << ",";
        filterChain << eqPart;

        BString limiterPart;
        limiterPart.SetToFormat("ladspa=file='/boot/system/lib/ladspa_HaikuSuperMusicThingy/fast_lookahead_limiter_1913.so':p=fastLookaheadLimiter:c=%.2f|%.2f|%.2f,",
            (float)fLimitInput->Value(), 
            (float)fLimitLimit->Value(), 
            (float)fLimitRelease->Value() / 1000.0f);
        filterChain << limiterPart;

        //if (cfg.showSpectrumVisuals) 
            filterChain << "astats=metadata=1:reset=1]"; 
        //else 
            //filterChain << "]";
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




		
		case MSG_COMPACTM_CHANGED: {
    		void* source = nullptr;
    		message->FindPointer("source", &source);
    
    		bool newState;
    
    		// Check if this message was sent programmatically during startup
    		if (source == nullptr) {
        		newState = cfg.compactMode;
        
        		// Sync both UI controls to match the loaded configuration
        		if (fCompactModeConfig) 
            		fCompactModeConfig->SetValue(newState ? B_CONTROL_ON : B_CONTROL_OFF);
        		if (fCompactModeRadio) 
            		fCompactModeRadio->SetValue(newState ? B_CONTROL_ON : B_CONTROL_OFF);
    		} else {
        		// Handle normal user click interactions
        		if (source == fCompactModeConfig) {
            		newState = (fCompactModeConfig->Value() == B_CONTROL_ON);
            		if (fCompactModeRadio) fCompactModeRadio->SetValue(newState ? B_CONTROL_ON : B_CONTROL_OFF);
        		} else {
            		newState = (fCompactModeRadio->Value() == B_CONTROL_ON);
            		if (fCompactModeConfig) fCompactModeConfig->SetValue(newState ? B_CONTROL_ON : B_CONTROL_OFF);
        		}

        		// Only update and save config if changed by explicit user interaction
        		cfg.compactMode = newState;
        		save_config();
    		}

    		// 3. Safety Check: Stop if critical views are missing
    		if (fPlayerGroup == NULL || fControlStack == NULL)
        		break;

    		// 4. Setup sizes based on current state
    		float scale = be_plain_font->Size() / 12.0f; 
    		float artSize = cfg.compactMode ? (96 * scale) : (325 * scale);
    		float btnSize = cfg.compactMode ? (40 * scale) : (75 * scale);
    		float favSize = cfg.compactMode ? (40 * scale) : (40 * scale);

    		// 5. Apply Layout orientations
    		fPlayerGroup->GroupLayout()->SetOrientation(cfg.compactMode ? B_HORIZONTAL : B_VERTICAL);
    		fPlayerGroup->GroupLayout()->SetSpacing(cfg.compactMode ? 5 : 10);
    		fControlStack->GroupLayout()->SetOrientation(B_VERTICAL); 
    
    		// 6. Update Widget Sizes
    		fDescView->SetAlignment(B_ALIGN_LEFT);
			fSongView->SetAlignment(B_ALIGN_LEFT);
	
			if (fBtnAddFav) {
        		BSize favTargetSize(favSize, favSize);
        		fBtnAddFav->SetExplicitSize(favTargetSize);
        		fBtnAddFav->SetExplicitMinSize(favTargetSize);
        		fBtnAddFav->SetExplicitMaxSize(favTargetSize); 
    		}

    		fArtView->SetExplicitSize(BSize(artSize, artSize));
    		fArtView->SetExplicitMinSize(BSize(artSize, artSize));
    		fArtView->SetExplicitMaxSize(BSize(artSize, artSize));
    		//fBtnAddFav->SetExplicitSize(BSize(favSize, favSize));
    		fPlayBtn->SetExplicitSize(BSize(btnSize, btnSize));
    		fStopBtn->SetExplicitSize(BSize(btnSize, btnSize));
    		fPauseBtn->SetExplicitSize(BSize(btnSize, btnSize));
    		fShuffleBtn->SetExplicitSize(BSize(btnSize, btnSize));

     // 7. Toggle Tabs and Extra Info
    if (cfg.compactMode) {
        fprintf(stderr, "[DEBUG-7] Entering Compact Mode section\n");
        fCompactModeRadio->Show();
        fDescView->Hide();      
        fSongView->Hide();      
        fquality->Show();
        fListenersView->Show();
        fSpectrum->Hide();

        BTab* tabsToRemove[] = { fRadioTab, fStationTab, fFavTab, fConfigTab, fAboutTab };
        BGroupView* groupsToRemove[] = { fRadioGroup, fStationGroup, fFavGroup, fConfigGroup, fAboutGroup };

        for (int32 i = fTabView->CountTabs() - 1; i >= 0; i--) {
            BTab* tab = fTabView->TabAt(i);
            for (int m = 0; m < 5; m++) {
                if (tab == tabsToRemove[m]) {
                    fprintf(stderr, "[DEBUG-7] Unparenting GroupView index %d from window layout\n", m);
                    if (groupsToRemove[m] != nullptr) {
                        fTabView->ContainerView()->RemoveChild(groupsToRemove[m]);
                    }
                    fprintf(stderr, "[DEBUG-7] Removing Tab index %d from tab view container\n", m);
                    fTabView->RemoveTab(i);
                }
            }
        }
        
        fRadioTab = nullptr;
        fStationTab = nullptr;
        fFavTab = nullptr;
        fConfigTab = nullptr;
        fAboutTab = nullptr;
        fprintf(stderr, "[DEBUG-7] All tabs purged. Compact entry complete.\n");

     } else {
        fprintf(stderr, "[DEBUG-7] Exiting Compact Mode section\n");
        fCompactModeRadio->Hide();
        fDescView->Show();
        fSongView->Show();
        fquality->Show();
        fListenersView->Show();
        fSpectrum->Show();
        
        fDescView->SetAlignment(B_ALIGN_CENTER);
        fSongView->SetAlignment(B_ALIGN_CENTER);
        
        if (fVolumeSlider) fVolumeSlider->SetTarget(this);
        
        for (int i = 0; i < 15; i++) {
            if (fEQSliders[i]) fEQSliders[i]->SetTarget(this);
        }

        fprintf(stderr, "[DEBUG-7] Re-instantiating missing structures\n");
        if (fRadioTab == nullptr)   { fRadioTab = new BTab();   fprintf(stderr, "[DEBUG-7] Alloc fRadioTab\n"); }
        if (fStationTab == nullptr) { fStationTab = new BTab(); fprintf(stderr, "[DEBUG-7] Alloc fStationTab\n"); }
        if (fFavTab == nullptr)     { fFavTab = new BTab();     fprintf(stderr, "[DEBUG-7] Alloc fFavTab\n"); }
        if (fConfigTab == nullptr)  { fConfigTab = new BTab();  fprintf(stderr, "[DEBUG-7] Alloc fConfigTab\n"); }
        if (fAboutTab == nullptr)   { fAboutTab = new BTab();   fprintf(stderr, "[DEBUG-7] Alloc fAboutTab\n"); }

        BTab* tabsToAdd[] = { fRadioTab, fStationTab, fFavTab, fConfigTab, fAboutTab };
        BGroupView* groups[] = { fRadioGroup, fStationGroup, fFavGroup, fConfigGroup, fAboutGroup };
        const char* labels[] = { "Radio", "Stations", "Fav", "Config", "About" };

        for (int i = 0; i < 5; i++) {
            fprintf(stderr, "[DEBUG-7] Iteration loop check start for index %d (%s)\n", i, labels[i]);
            if (tabsToAdd[i] == nullptr || groups[i] == nullptr) continue;

            bool found = false;
            int32 currentTabCount = fTabView->CountTabs();
            for (int32 j = 0; j < currentTabCount; j++) {
                if (fTabView->TabAt(j) == tabsToAdd[i]) {
                    found = true;
                    break;
                }
            }

            if (!found) {
                // FORCE DETACH: Strip any lingering parent references from the layout view 
                // before passing it back into the BTabView engine.
                if (groups[i]->Parent() != nullptr) {
                    fprintf(stderr, "[DEBUG-7] Purging lingering parent from group %d\n", i);
                    groups[i]->RemoveSelf(); 
                }
                
                tabsToAdd[i]->SetLabel(labels[i]);
                fprintf(stderr, "[DEBUG-7] Safely calling fTabView->AddTab() for index %d\n", i);
                fTabView->AddTab(groups[i], tabsToAdd[i]);
            }
        }
        fprintf(stderr, "[DEBUG-7] Tab rebuild generation sequence done\n");
    }






    		fArtView->InvalidateLayout();
    		fPlayerGroup->InvalidateLayout();
    		this->Layout(true); 
    		ApplyTheme(); 

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
                    printf("[Visualizer] Syncing shuffle artwork colors for ID: %s\n", currentStationID.c_str());
                    this->fSpectrum->AdaptToAlbumArt(fIconCache[currentStationID]);
                }
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
            break;
        }

    	
    	case MSG_TOGGLE_LADSPA: {
    		cfg.ladspaEnabled = (fEnableladspa->Value() == B_CONTROL_ON);
    		save_config();
    		UpdateMPVFilters(); 
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

        		InvalidateLayout();
        		ResizeToPreferred();
        		save_config();
        		UpdateMPVFilters(); 
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
                        printf("[Visualizer] Found icon in cache for %s. Extracting colors...\n", chan.title.c_str());
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
                        printf("[Visualizer] Found icon in cache for %s. Extracting colors...\n", chan.title.c_str());
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

                    // --- INTEGRATED VISUALIZER COLOR ADAPTATION ---
                    // 1. Fallback to fAlbumArt if provided, otherwise check cache
                    BBitmap* targetArt = fAlbumArt;
                    if (targetArt == nullptr && fIconCache.count(currentStationID) > 0) {
                        targetArt = fIconCache[currentStationID];
                        printf("[Visualizer] Falling back to icon cache for station %s. Extracting colors...\n", currentStationID.c_str());
                    }

                    // 2. Safely apply the color adaptation within the Window Lock context
                    if (targetArt != nullptr && this->fSpectrum != nullptr) {
                        this->fSpectrum->AdaptToAlbumArt(targetArt);
                    }
                    // ----------------------------------------------

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
    		ResizeToPreferred();
    		break;
		}
		#endif
//--------------------------------- Proectm     

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
                    } // Closes: if (node->format == MPV_FORMAT_NODE_MAP && cfg.ladspaEnabled)
        			
        			if (node->format == MPV_FORMAT_NODE_MAP && node->u.list != nullptr && !cfg.ladspaEnabled) {
            
            			for (int i = 0; i < node->u.list->num; i++) {
                			if (node->u.list->keys[i] != nullptr && strstr(node->u.list->keys[i], "Peak_level")) {
                    
                    			double peak = 0.0;
                    			mpv_node* valNode = &node->u.list->values[i];
                    
                    			// FIXED UNION MEMBER FIELDS: Added matching underscores
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

#if defined(__x86_64__)
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

