/*
 * Copyright 2026, Kris Beazley supermusicthingy@epluribusunix.net
 * All rights reserved. Distributed under the terms of the MIT license.
 */

// ====================================================================
// Haiku API Framework Kits
// ====================================================================
// App Kit
#include <Application.h>
#include <Message.h>
#include <MessageRunner.h>
#include <Notification.h>
#include <Roster.h>

// Interface Kit
#include <AffineTransform.h>
#include <Bitmap.h>
#include <CheckBox.h>
#include <Control.h>
#include <ControlLook.h>
#include <Deskbar.h>
#include <Dragger.h>
#include <IconUtils.h>
#include <InterfaceDefs.h>
#include <ListView.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <Region.h>
#include <Screen.h>
#include <Shape.h>
#include <Slider.h>
#include <StringList.h>
#include <StringView.h>
#include <TextView.h>
#include <View.h>
#include <Window.h>
#include <Picture.h>


// Storage & Support Kits
#include <Directory.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <NodeInfo.h>
#include <Path.h>
#include <TranslationUtils.h>
#include <StorageKit.h>
#include <SupportKit.h>

// ====================================================================
// Third Party Engines & Libraries
// ====================================================================
#include <curl/curl.h>
#include <mpv/client.h>
#include "nlohmann/json.hpp"

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

// ====================================================================
// C++ Standard Library & POSIX Layers
// ====================================================================
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>



// ====================================================================
// Local Application Project Headers
// ====================================================================
#include "haiku-supermusicthingy.h"
#include "icons.h"



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
bool mpvthread_running = true;
SuperMusicWindow* gGuiWindow = nullptr; 
int32 mpv_loop_thread(void* data);
using json = nlohmann::json;
class SuperMusicWindow; 



// Volatile State Tracker Variables
double user_base_volume = 100.0; // Captures slider adjustments
float gVolumeScaleFactor = 1.0f; 
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



const float kPresetRock[] = {
    4.0, 3.5, 3.0, 2.5, 2.0, 1.0, -1.0, -1.0, 
    0.0, 1.0, 1.5, 2.0, 2.5, 3.5, 4.0
};

const float kPresetJazz[] = {
    3.0, 2.5, 2.0, 1.5, 1.0, 2.0, -1.0, -1.0, 
    -0.5, 0.0, 0.5, 1.0, 1.5, 2.5, 3.0
};

const float kPresetBass[] = {
    11.0, 9.0, 4.0, 2.0, 1.0, 1.0, 0.0, 0.0, 
    0.0, 0.0, 1.0, 3.0, 4.0, 7.0, 9.0
};

const float kPresetFlat[] = {
    0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 
    0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0
};

//@Config
struct Config {
	float currentVolume = 75.0f;   
    bool showNotifications = false;
    bool debugEnable = false;
    bool showVisuals = false;
    bool autoShuffle = false;
    bool enableTitles = true;
    bool enableDescriptions = true;
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
    std::string uTheme = "Dark";
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
    j["currentVolume"] = cfg.currentVolume;
    j["quality"] = cfg.quality;
    j["debugEnable"] = cfg.debugEnable;
    j["compactMode"] = cfg.compactMode;
    j["notifyIconSize"] = cfg.notifyIconSize;
    j["uTheme"] = cfg.uTheme;
    j["showNotifications"] = cfg.showNotifications;
    j["autoShuffle"] = cfg.autoShuffle;
    j["sysTray"] = cfg.sysTray;
    j["enableTitles"] = cfg.enableTitles;
    j["enableDescriptions"] = cfg.enableDescriptions;
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
    cfg.currentVolume = 1.0f;
    cfg.quality = "128k";
    cfg.notifyIconSize = 64;
    cfg.debugEnable = false;
    cfg.compactMode = false;
    cfg.enableTitles = true;
    cfg.enableDescriptions = true;
    cfg.uTheme = "Dark";
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
       cfg.eqBands[i] = kPresetRock[i]; 
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
                cfg.currentVolume = j.value("currentVolume", 1.0f);                
                cfg.quality = j.value("quality", "128k");
                int val = j.value("notifyIconSize", 64);
                if (val == 32 || val == 40 || val == 64 || val == 96 || val == 128) {
                    cfg.notifyIconSize = val;
                } else {
                    cfg.notifyIconSize = 64; 
                }
                cfg.compactMode = j.value("compactMode", false);
                cfg.debugEnable = j.value("debugEnable", false);
                cfg.enableDescriptions = j.value("enableDescriptions", true);
                cfg.enableTitles = j.value("enableTitles", true);
                cfg.uTheme = j.value("uTheme", "Dark");
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


static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}





// Helper function to safely hard-copy a single file using BFile streams
status_t CopyFile(BEntry& sourceEntry, BDirectory& destDir, const char* fileName) {
    BFile sourceFile(&sourceEntry, B_READ_ONLY);
    if (sourceFile.InitCheck() != B_OK) return sourceFile.InitCheck();

    BFile destFile;
    // Open destination file: create if missing, erase if it already exists, open for writing
    status_t status = destDir.CreateFile(fileName, &destFile, false);
    if (status != B_OK) return status;

    // Stream the data from source to destination using a small memory buffer
    char buffer[4096];
    ssize_t bytesRead;
    while ((bytesRead = sourceFile.Read(buffer, sizeof(buffer))) > 0) {
        destFile.Write(buffer, bytesRead);
    }
    return B_OK;
}

// Helper function to recursively copy a folder's contents
status_t CopyDirectory(BDirectory& sourceDir, BDirectory& destDir) {
    BEntry entry;
    sourceDir.Rewind();

    while (sourceDir.GetNextEntry(&entry) == B_OK) {
        char name[B_FILE_NAME_LENGTH];
        entry.GetName(name);

        if (entry.IsDirectory()) {
            // Create the sub-folder in the destination
            BDirectory subDestDir;
            if (destDir.CreateDirectory(name, &subDestDir) == B_OK) {
                BDirectory subSourceDir(&entry);
                CopyDirectory(subSourceDir, subDestDir); // Recurse
            }
        } else if (entry.IsFile()) {
            // Replaced the broken entry.CopyInto line with our safe file copy helper
            CopyFile(entry, destDir, name);
        }
    }
    return B_OK;
}

void ensure_config_dir() {
    BPath path;
    if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
        path.Append("SuperMusicThingy/milk_presets");
        
        if (create_directory(path.Path(), 0755) == B_OK) {
            BPath destPath(path);
            destPath.Append("presets_stock");
            
            BDirectory destDir;
            // Create the local presets_stock folder if it doesn't exist yet
            if (destDir.CreateDirectory(destPath.Path(), &destDir) == B_OK) {
                const char* targetPath = "/boot/system/data/HaikuSuperMusicThingy/milkdrops/presets_stock";
                BDirectory sourceDir(targetPath);
                
                if (sourceDir.InitCheck() == B_OK) {
                    // Perform the hard copy of the files
                    CopyDirectory(sourceDir, destDir);
                }
            }
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
        : BButton(name, "", msg), fIcon(icon), fIsFavorite(false),
          fIsHovered(false), fHoverAlpha(0.0f), fLastTime(system_time())
    {
        SetViewColor(B_TRANSPARENT_COLOR);
        SetFlags(Flags() | B_POINTER_EVENTS);
    }

    // ---ALLOCATE EXTRA DRAWING ROOM ---
    void GetPreferredSize(float* width, float* height) override {
        if (fIcon) {
            // Provide an extra 16 pixels of margin beyond the icon size
            // This ensures the 8px glow ring never hits the bounding box edge
            *width = fIcon->Bounds().Width() + 16.0f;
            *height = fIcon->Bounds().Height() + 16.0f;
        } else {
            BButton::GetPreferredSize(width, height);
        }
    }

    void SetFavorite(bool fav) {
        if (fIsFavorite != fav) {
            fIsFavorite = fav;
            Invalidate(); 
        }
    }

    // Call this whenever you swap icons between 64 and 40 to force a layout refresh
    void UpdateIcon(BBitmap* newIcon) {
        fIcon = newIcon;
        InvalidateLayout(); // Tells parent group to re-evaluate GetPreferredSize()
        Invalidate();
    }

    void MouseMoved(BPoint point, uint32 transit, const BMessage* message) override {
        BButton::MouseMoved(point, transit, message);
        
        bool wasHovered = fIsHovered;
        if (transit == B_ENTERED_VIEW) {
            fIsHovered = true;
        } else if (transit == B_EXITED_VIEW) {
            fIsHovered = false;
        }
        
        if (wasHovered != fIsHovered) {
            Invalidate();
        }
    }

    void Draw(BRect updateRect) override {
        BRect b = Bounds();
        float x = fIcon ? (b.Width() - fIcon->Bounds().Width()) / 2.0f : 0.0f;
        float y = fIcon ? (b.Height() - fIcon->Bounds().Height()) / 2.0f : 0.0f;

        if (Value() == B_CONTROL_ON) {
            x += 1.0f;
            y += 1.0f;
        }

        bigtime_t currentTime = system_time();
        float deltaTime = (float)(currentTime - fLastTime) / 1000000.0f;
        fLastTime = currentTime;

        if (deltaTime > 0.1f) deltaTime = 0.1f;

        const float fadeSpeed = 1.8f; 
        if (fIsHovered) {
            fHoverAlpha += deltaTime * fadeSpeed;
            if (fHoverAlpha > 1.0f) fHoverAlpha = 1.0f;
        } else {
            fHoverAlpha -= deltaTime * fadeSpeed;
            if (fHoverAlpha < 0.0f) fHoverAlpha = 0.0f;
        }

        if (fHoverAlpha > 0.0f && fHoverAlpha < 1.0f) {
            Invalidate();
        }

        rgb_color bgCol = (Parent() != nullptr) ? Parent()->ViewColor() : ui_color(B_PANEL_BACKGROUND_COLOR);

        // --- 2. RENDER HOVER GLOW LAYER (DYNAMICALLY SCALED) ---
        if (fHoverAlpha > 0.0f && IsEnabled() && fIcon) {
            SetDrawingMode(B_OP_ALPHA);
            
            rgb_color glowColor = {235, 235, 240, 255}; 
            if (bgCol.red > 200 && bgCol.green > 200 && bgCol.blue > 200) {
                glowColor = {40, 40, 45, 255}; 
            }

            float midX = b.Width() / 2.0f;
            float midY = b.Height() / 2.0f;
            
            // Base radius attaches cleanly to the rim of either the 64px or 40px icon assets
            float baseRadius = (fIcon->Bounds().Width() / 2.0f) + 1.0f;
            const int glowSteps = 6;

            // DYNAMIC SPREAD: Scale maximum glow ring width proportional to icon width
            // This prevents a huge 8px halo from bloating over the smaller 40px circle
            float maxGlowSpread = fIcon->Bounds().Width() * 0.125f; 

            for (int step = 0; step < glowSteps; step++) {
                float progress = (float)step / (float)glowSteps;
                float radius = baseRadius + (progress * maxGlowSpread);
                
                float alphaFactor = 0.30f * (1.0f - cosf((1.0f - progress) * (float)M_PI)) * fHoverAlpha;
                glowColor.alpha = (uint8)(255.0f * alphaFactor);
                
                SetHighColor(glowColor);
                StrokeEllipse(BPoint(midX, midY), radius, radius);
            }
        }

        // --- 3. RENDER THE ICON ---
        if (fIcon) {
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
    bool fIsHovered;
    float fHoverAlpha;       
    bigtime_t fLastTime;    
};





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
    // 1. Dynamically fetch the current theme background color
    rgb_color bgCol = (Parent() != nullptr) ? Parent()->ViewColor() : ui_color(B_PANEL_BACKGROUND_COLOR);

    if (fCurrentBitmap) {
        // Clear background with theme color
        SetHighColor(bgCol);
        FillRect(Bounds());
        
        // Draw the artwork
        SetDrawingMode(B_OP_ALPHA);
        DrawBitmap(fCurrentBitmap, fCurrentBitmap->Bounds(), Bounds(), B_FILTER_BITMAP_BILINEAR);        

        // 2. Draw the masking overlay using the dynamic theme color
        BRect b = Bounds();
        float w = b.Width();
        float h = b.Height();

        const float fadeSize = 8.0f; 
        
        // Duplicate the system background color to use as our mask
        rgb_color maskColor = bgCol; 

        for (float step = 0; step < fadeSize; step += 1.0f) {
            float linearProgress = step / fadeSize;
            float alphaFactor = 1.0f - (0.5f * (1.0f - cosf(linearProgress * (float)M_PI)));
            
            maskColor.alpha = (uint8)(255.0f * alphaFactor);
            SetHighColor(maskColor);

            // Draw concentric frames to feather the artwork into the theme background
            StrokeRect(BRect(step, step, w - step, h - step));
        }

        SetDrawingMode(B_OP_COPY); 
    } else {
        // Apply theme color here as well for consistency when empty
        SetHighColor(bgCol);
        FillRect(Bounds());
        
        // Calculate contrasting text color based on the theme (or default to a mid-gray)
        SetHighColor(120, 120, 120);
        
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




// Ladspa Smart Detection

// Define the structural layout matching FFmpeg's internal dynamic registration signatures
struct AVFilter {
    const char* name;
    // Remaining inner alignment paddings are ignored since we only parse the name string pointer
};

bool IsFFmpegLadspaAvailable() {
    bool hasLadspa = false;

    // 1. RUNTIME ENGINE QUERY: Intercept the active library iteration symbol in memory.
    // av_filter_iterate() is the official POSIX function exposed by modern libavfilter (FFmpeg 5 to 8+)
    // to iterate through all compiled audio/video filters in the active runtime context.
    const AVFilter* (*get_next_filter)(void**) = (const AVFilter* (*)(void**))dlsym(RTLD_DEFAULT, "av_filter_iterate");

    if (get_next_filter != nullptr) {
        void* opaque = nullptr;
        const AVFilter* filter = nullptr;
        
        // Loop through every single filter compiled into the running libavfilter engine
        while ((filter = get_next_filter(&opaque)) != nullptr) {
            if (filter->name != nullptr && strcmp(filter->name, "ladspa") == 0) {
                hasLadspa = true;
                break;
            }
        }
        
        if (cfg.debugEnable) {
            printf("[DEBUG FFmpeg Prober] Native Memory Graph Query -> 'av_filter_iterate' checked active engine.\n");
        }
    } else {
        // Fallback for older versions if av_filter_iterate is missing
        const AVFilter* (*get_next_filter_legacy)(const AVFilter*) = (const AVFilter* (*)(const AVFilter*))dlsym(RTLD_DEFAULT, "avfilter_next");
        if (get_next_filter_legacy != nullptr) {
            const AVFilter* filter = nullptr;
            while ((filter = get_next_filter_legacy(filter)) != nullptr) {
                if (filter->name != nullptr && strcmp(filter->name, "ladspa") == 0) {
                    hasLadspa = true;
                    break;
                }
            }
        }
    }

    if (cfg.debugEnable) {
        printf("[DEBUG FFmpeg Prober] Active Runtime Engine LADSPA Support Evaluation: %s\n", 
               hasLadspa ? "AVAILABLE" : "UNAVAILABLE");
    }

    return hasLadspa;
}



// ====================================================================
// MODULAR ADAPTIVE ACID MELTING FLOOR MODULE
// ====================================================================


// --- THE RESPONSIVE CYBER NEON EQUALIZER CONTAINER ---
class AcidMeltingView : public BView {
public:
    AcidMeltingView(BRect frame, BBitmap* snapshot, BWindow* parentWindow) 
        : BView(frame, "CyberNeonEqualizerView", B_FOLLOW_ALL_SIDES, B_WILL_DRAW) {
        fCachedImage = snapshot; // Preserved in case you require album art data channels
        fMainAppWindow = parentWindow;

        // Initialize historical peak decay buffers for smooth animations
        for (int b = 0; b < 64; b++) {
            fSmoothedHeight[b] = 0.0f;
            fPeakHeight[b] = 0.0f;
            fPeakAge[b] = 0;
            fLocalPalette[b] = { 40, 220, 70, 255 }; // Neo-Green default baseline
        }
        SetViewColor(B_TRANSPARENT_COLOR);
    }

    virtual ~AcidMeltingView() { }

    void UpdatePhysics(float* barHeights, float dtScale, float volumeScale, rgb_color* masterPalette) {
        float viewHeight = Bounds().Height();
        if (viewHeight <= 0.0f) viewHeight = 1.0f;

        for (int b = 0; b < 64; b++) {
            if (masterPalette != nullptr) {
                fLocalPalette[b] = masterPalette[b];
            }

            // ====================================================================
            // --- DYNAMIC COMPACT STATE INTENSITY TUNING ---
            // ====================================================================
            // If compact mode is true, reduce the multiplier so the capsules don't 
            // shoot up too high and block the playback controls or text layers.
            float intensityMultiplier = cfg.compactMode ? 1.5f : 4.5f;
            float targetDrive = barHeights[b] * volumeScale * intensityMultiplier;
            // ====================================================================
            
            if (targetDrive > viewHeight * 0.85f) {
                targetDrive = viewHeight * 0.85f; // Hard ceiling limit protection
            }

            // Balanced attack/decay smoothing logic
            if (targetDrive > fSmoothedHeight[b]) {
                fSmoothedHeight[b] += (targetDrive - fSmoothedHeight[b]) * 0.55f * dtScale;
            } else {
                // Linear steady decay drop down to the floor
                fSmoothedHeight[b] -= (5.0f * dtScale);
            }
            if (fSmoothedHeight[b] < 0.0f) fSmoothedHeight[b] = 0.0f;

            // Handle peak dots drop-down delays
            if (fSmoothedHeight[b] >= fPeakHeight[b]) {
                fPeakHeight[b] = fSmoothedHeight[b];
                fPeakAge[b] = 0;
            } else {
                fPeakAge[b]++;
                if (fPeakAge[b] > 10) { 
                    fPeakHeight[b] -= (3.0f * dtScale);
                }
            }
            if (fPeakHeight[b] < 0.0f) fPeakHeight[b] = 0.0f;
        }

        Invalidate();
    }



    virtual void Draw(BRect updateRect) override {
        // --- 1. SMART BACKGROUND DETECTION ---
        rgb_color sysBgColor = ui_color(B_PANEL_BACKGROUND_COLOR);
        bool isDarkModeActive = (cfg.uTheme == "Dark");

        // Clear view canvas first to prevent ghosting resize artifacts
        SetHighColor(isDarkModeActive ? rgb_color{20, 22, 26, 255} : sysBgColor);
        FillRect(Bounds());

        // --- 2. RENDER BACKGROUND SNAPSHOT ---
        if (fCachedImage && fCachedImage->IsValid()) {
            SetDrawingMode(B_OP_COPY);
            DrawBitmap(fCachedImage, fCachedImage->Bounds(), Bounds());

            // ====================================================================
            // --- LOCAL LIGHT MODE VISUALIZER SPACE SCRUB ---
            // ====================================================================
            // Erase the hardcoded black horizontal stripe artifact beneath the 
            // cover artwork by painting the lower blank area with system grey.
            if (!isDarkModeActive) {
                float imagePixelHeight = fCachedImage->Bounds().Height();
                float viewHeight = Bounds().Height();
                float viewWidth = Bounds().Width();

                if (viewHeight > imagePixelHeight) {
                    SetHighColor(sysBgColor);
                    FillRect(BRect(0.0f, imagePixelHeight, viewWidth, viewHeight));
                }
            }
            // ====================================================================
        }

        float viewWidth = Bounds().Width();
        float viewHeight = Bounds().Height();
        
        int totalBars = 32; 
        float horizontalPadding = 6.0f;
        float usableWidth = viewWidth - (horizontalPadding * 2.0f);
        float barWidth = usableWidth / (float)totalBars;
        float capsuleSpacing = 3.0f; // Added slight padding between columns for a matrix style look
        float actualPillWidth = barWidth - capsuleSpacing;
        if (actualPillWidth < 1.5f) actualPillWidth = 1.5f;

        // --- 3. MATRIX RENDER LOOP ---
        for (int i = 0; i < totalBars; i++) {
            int srcIdx = i * 2; 
            
            float currentH = fSmoothedHeight[srcIdx]; 
            float peakH = fPeakHeight[srcIdx];       

            float barLeftX = horizontalPadding + ((float)i * barWidth);
            float barRightX = barLeftX + actualPillWidth;

            rgb_color neonColor = fLocalPalette[srcIdx];

            // Render Segmented Cyber Pill Capsules
            if (currentH > 2.0f) {
                SetDrawingMode(B_OP_ALPHA);
                
                int totalSegments = 16; 
                float segmentHeight = (viewHeight * 0.4f) / (float)totalSegments; // Isolate height allocations

                for (int s = 0; s < totalSegments; s++) {
                    // Pull the base anchor slightly higher off the window frame edge
                    float segBottomY = viewHeight - 6.0f - ((float)s * (segmentHeight + 1.0f));
                    float segTopY = segBottomY - segmentHeight;
                    
                    if ((viewHeight - segTopY) > currentH) break;

                    // Neon Glow Saturation Gradient Pass
                    uint8 alphaFade = 130 + ((s * 125) / totalSegments);
                    SetHighColor(neonColor.red, neonColor.green, neonColor.blue, alphaFade);

                    BRect capsuleRect(barLeftX, segTopY, barRightX, segBottomY);
                    FillRoundRect(capsuleRect, actualPillWidth * 0.5f, 2.0f);
                }
            }

            // --- FLOATING PEAK DOTS ---
            if (peakH > 2.0f) {
                float peakTopY = viewHeight - 6.0f - peakH;
                if (peakTopY < 4.0f) peakTopY = 4.0f;
                float peakBottomY = peakTopY + 2.0f;

                BRect peakPillRect(barLeftX, peakTopY, barRightX, peakBottomY);
                
                if (isDarkModeActive) {
                    SetHighColor(255, 255, 255, 230); 
                    SetDrawingMode(B_OP_ALPHA);
                    FillRoundRect(peakPillRect, actualPillWidth * 0.5f, 1.0f);
                    
                    SetHighColor(neonColor.red, neonColor.green, neonColor.blue, 140); 
                    BRect glowRect = peakPillRect;
                    glowRect.InsetBy(-1.0f, -1.0f);
                    StrokeRoundRect(glowRect, actualPillWidth * 0.5f, 1.0f);
                } else {
                    SetHighColor(neonColor.red, neonColor.green, neonColor.blue, 255);
                    SetDrawingMode(B_OP_COPY);
                    FillRoundRect(peakPillRect, actualPillWidth * 0.5f, 1.0f);
                }
            }
        }

        SetDrawingMode(B_OP_COPY);
    }



    virtual void MouseDown(BPoint point) override {
        BMessage* message = Window()->CurrentMessage();
        int32 buttons = 0;

        if (message != nullptr && message->FindInt32("buttons", &buttons) == B_OK) {
            if (buttons & B_SECONDARY_MOUSE_BUTTON) {
                if (fMainAppWindow != nullptr) {
                    BMessenger messenger(fMainAppWindow);
                    if (messenger.IsValid()) {
                        BMessage cycleMsg('mcyc');
                        messenger.SendMessage(&cycleMsg);
                    }
                }
                return; 
            }
        }
        BView::MouseDown(point);
    }

private:
    BBitmap*  fCachedImage;
    BWindow*  fMainAppWindow;
    float     fSmoothedHeight[64];   
    float     fPeakHeight[64];   
    int32     fPeakAge[64];      
    rgb_color fLocalPalette[64]; 
};


// --- THE TRANSPARENT FLOATING WINDOW LAYER ---
class AcidMeltingWindow : public BWindow {
public:
    AcidMeltingWindow(BWindow* parent, BBitmap* snapshot)
        : BWindow(parent->Frame(), "CyberNeonEqualizerWindow", B_NO_BORDER_WINDOW_LOOK, 
                  B_FLOATING_SUBSET_WINDOW_FEEL, 
                  B_NOT_MOVABLE | B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_AVOID_FRONT) {
        fParent = parent; 
        AddToSubset(parent);
        fOverlayView = new AcidMeltingView(Bounds(), snapshot, parent);
        AddChild(fOverlayView);
    }

    void UpdateAudioPhysics(float* barHeights, float dtScale, float volumeScale, rgb_color* masterPalette) {
        // Dynamic Window Boundary Tracking Synchronizer
        // If parent resizes or fullscreens, our tracking layer scales instantly to match.
        if (fParent && fParent->Lock()) {
            BRect parentFrame = fParent->Frame();
            fParent->Unlock();
            
            if (Frame() != parentFrame) {
                MoveTo(parentFrame.left, parentFrame.top);
                ResizeTo(parentFrame.Width(), parentFrame.Height());
            }
        }

        if (Lock()) {
            fOverlayView->UpdatePhysics(barHeights, dtScale, volumeScale, masterPalette);
            Unlock();
        }
    }

private:
    AcidMeltingView* fOverlayView;
    BWindow*         fParent;
};





// --- THE INDEPENDENT RENDER VIEW CONTAINER ---
class ReplicaOverlayView : public BView {
public:
    // --- UPDATED CONSTRUCTOR: Fixed uninitialized wild pointer trackers ---
    ReplicaOverlayView(BRect frame, BBitmap* snapshot, BWindow* parentWindow) 
        : BView(frame, "ReplicaOverlayView", B_FOLLOW_ALL_SIDES, B_WILL_DRAW) {
        if (snapshot != nullptr && snapshot->IsValid()) {
            fCachedImage = new BBitmap(snapshot);
        } else {
            fCachedImage = nullptr;
        }
        fMainAppWindow = parentWindow; 
        
        
        // Seed 75 organic bubbles across the window canvas
        // Seed 75 organic bubbles across the window canvas with high size variance
        for (int b = 0; b < 75; b++) {
            fBubbleX[b] = (float)(rand() % 1000) / 1000.0f * frame.Width();
            fBubbleY[b] = (float)(rand() % 1000) / 1000.0f * frame.Height();
            fWobblePhase[b] = ((float)(rand() % 100) / 100.0f) * 6.28f;

            // --- NEW: DYNAMIC VARIANCE DISTRIBUTION ENGINE ---
            // We use a random distribution roll to create a highly varied environment
            int sizeRoll = rand() % 100;

            if (sizeRoll < 55) {
                // 1. TINY MICRO-BUBBLES (55% of total cluster)
                // Small, subtle background details ranging from 6px to 14px
                fBubbleRadius[b] = 6.0f + ((float)(rand() % 100) / 100.0f * 8.0f);
                fBubbleSpeedY[b] = 12.0f + ((float)(rand() % 100) / 100.0f * 10.0f); // Floats slightly quicker
                fBubbleSpeedX[b] = ((float)(rand() % 100) / 100.0f * 4.0f) - 2.0f;
            } 
            else if (sizeRoll < 90) {
                // 2. MEDIUM STANDARD BUBBLES (35% of total cluster)
                // Standard focal bubbles ranging from 15px to 32px
                fBubbleRadius[b] = 15.0f + ((float)(rand() % 100) / 100.0f * 17.0f);
                fBubbleSpeedY[b] = 6.0f + ((float)(rand() % 100) / 100.0f * 8.0f);
                fBubbleSpeedX[b] = ((float)(rand() % 100) / 100.0f * 3.0f) - 1.5f;
            } 
            else {
                // 3. MASSIVE LIQUID SPHERES (10% of total cluster)
                // Giant, heavy lenses ranging from 45px up to a massive 75px!
                fBubbleRadius[b] = 45.0f + ((float)(rand() % 100) / 100.0f * 30.0f);
                
                // Real physics rule: Giant bubbles carry more drag weight, so they float much slower!
                fBubbleSpeedY[b] = 2.0f + ((float)(rand() % 100) / 100.0f * 3.0f);
                fBubbleSpeedX[b] = ((float)(rand() % 100) / 100.0f * 1.5f) - 0.75f; // Hard to drift sideways
            }
        }

        SetViewColor(B_TRANSPARENT_COLOR);
    }


    virtual ~ReplicaOverlayView() { 
        delete fCachedImage;
    }

    void UpdatePhysics(float* barHeights, float dtScale, float volumeScale) {
        float viewWidth = Bounds().Width();
        float viewHeight = Bounds().Height();
        
        if (viewWidth <= 0.0f) viewWidth = 1.0f;
        if (viewHeight <= 0.0f) viewHeight = 1.0f;

        // Process physics adjustments frame-by-frame
        for (int b = 0; b < 75; b++) {
            // Map the bubble's X position to an audio frequency bar
            int freqIdx = (int)((fBubbleX[b] / viewWidth) * 63.0f);
            if (freqIdx < 0) freqIdx = 0;
            if (freqIdx > 63) freqIdx = 63;

            // Extract real-time audio drive data
            float audioDrive = (barHeights[freqIdx] / viewHeight) * volumeScale;

            // --- 1. MODIFIED: LAZY-RIVER FLOATING MOVEMENT ---
            // Reduced the audio thrust multiplier from 3.5f to 0.8f.
            // This prevents bubbles from aggressively rocketing upward during heavy music beats.
            fBubbleY[b] -= (fBubbleSpeedY[b] * (1.0f + audioDrive * 0.8f)) * dtScale;
            
            // --- MODIFIED: CALMED HORIZONTAL DRIFT SWSWAY ---
            // Slowed down the wiggle phase multiplier from 4.5f to 1.8f,
            // and reduced the horizontal weave distance swing from 15.0f down to 4.5f.
            fWobblePhase[b] += 1.8f * dtScale;
            fBubbleX[b] += (fBubbleSpeedX[b] + sinf(fWobblePhase[b]) * 4.5f) * dtScale;

        	// Random bubble radius dimensions ranging from 12px to 42px
        	fBubbleRadius[b] = 12.0f + ((float)(rand() % 100) / 100.0f * 30.0f);
            
        	// --- SLOWED DOWN INITIAL SEED SPEEDS ---
        	fBubbleSpeedX[b] = ((float)(rand() % 100) / 100.0f * 6.0f) - 3.0f;  // Old was * 20.0f
        	fBubbleSpeedY[b] = 5.0f + ((float)(rand() % 100) / 100.0f * 15.0f);  // Old was 15.0f + * 45.0f


             // 2. AUDIO PULSE REACTION: Inflate bubbles relative to their native baseline sizes!
            // Heavy beats inflate them outward up to an extra 45%, scaling perfectly across all variations.
            fBubbleRadius[b] = (fBubbleRadius[b]) * (1.0f + audioDrive * 0.45f);

            // Strict ceiling clamp protection to prevent giant bubbles from filling the whole screen space
            if (fBubbleRadius[b] > 110.0f) fBubbleRadius[b] = 110.0f;


            // 3. BOUNDARY LOOPING: Re-spool bubbles back to the bottom when they float past the ceiling
            if (fBubbleY[b] + fBubbleRadius[b] < 0.0f) {
                fBubbleY[b] = viewHeight + fBubbleRadius[b] + (float)(rand() % 50);
                fBubbleX[b] = ((float)(rand() % 1000) / 1000.0f) * viewWidth;
            }
            
            // Keep horizontal movement contained inside window borders
            if (fBubbleX[b] < 0.0f) fBubbleX[b] = viewWidth;
            if (fBubbleX[b] > viewWidth) fBubbleX[b] = 0.0f;
        }

        Invalidate();
    }



      virtual void Draw(BRect updateRect) override {
        if (!fCachedImage || !fCachedImage->IsValid()) return;

        float viewWidth = Bounds().Width();
        float viewHeight = Bounds().Height();

        // --- 1. SMART BACKGROUND DETECTION ---
        rgb_color sysBgColor;
        bool isDarkModeActive = (cfg.uTheme == "Dark");

        if (isDarkModeActive) {
            sysBgColor = rgb_color{40, 40, 40, 255}; // Clear view canvas with a deep charcoal background
        } else {
            sysBgColor = ui_color(B_PANEL_BACKGROUND_COLOR); // Match native Haiku theme color scheme
        }

        SetHighColor(sysBgColor);
        FillRect(updateRect);

        // --- LAYER 1: DRAW EACH MASKED APPSNAPSHOT BUBBLE ---
        for (int b = 0; b < 75; b++) {
            float bx = fBubbleX[b];
            float by = fBubbleY[b];
            float br = fBubbleRadius[b];

            if (bx + br < 0.0f || bx - br > viewWidth || by + br < 0.0f || by - br > viewHeight) {
                continue;
            }

            BRect bubbleRect(bx - br, by - br, bx + br, by + br);

            // Calculate organic lens refraction offset shift
            float refractionWarpX = sinf(fWobblePhase[b]) * 6.0f;
            BRect sourceRect = bubbleRect;
            sourceRect.OffsetBy(refractionWarpX, 10.0f); 

            // Use Haiku's native stack state to record a perfect vector shape mask
            PushState();
            
            // We create a temporary recording picture containing our perfect round circle shape
            BPicture roundMask;
            BeginPicture(&roundMask);
            SetHighColor(255, 255, 255, 255);
            FillEllipse(BPoint(bx, by), br, br);
            EndPicture();
            
            // Pass the address-of pointer (&roundMask) to match standard Interface Kit specifications
            ClipToPicture(&roundMask, BPoint(0,0), false);

            // Draw the captured app graphics masked perfectly round inside the sphere box
            SetDrawingMode(B_OP_COPY);
            DrawBitmap(fCachedImage, sourceRect, bubbleRect);
            
            PopState(); // Restores full canvas limits safely for the 3D sheen layer


            // --- LAYER 2: OVERLAY 3D GLASSY SURFACE SHEEN ---
            SetDrawingMode(B_OP_ALPHA);
            
            // Translucent dark border trim ring to make overlapping bubbles pop apart
            SetHighColor(0, 0, 0, 85);
            SetPenSize(2.5f);
            StrokeEllipse(BPoint(bx, by), br, br);

            // --- SMART 3D REFLECTION CONTRAST ADAPTATION ---
            if (isDarkModeActive) {
                // Translucent ice-white highlight shell rim outline
                SetHighColor(255, 255, 255, 140);
                SetPenSize(1.5f);
                StrokeEllipse(BPoint(bx, by), br - 1.0f, br - 1.0f);

                // Specular reflection bead highlight arc in the upper-left corner
                SetHighColor(255, 255, 255, 220);
            } else {
                // In Light Mode, use dark highlights so glassy reflections stay highly visible
                SetHighColor(0, 0, 0, 40);
                SetPenSize(1.5f);
                StrokeEllipse(BPoint(bx, by), br - 1.0f, br - 1.0f);

                SetHighColor(0, 0, 0, 90);
            }

            // Specular reflection bead highlight arc in the upper-left corner of the bubble
            SetPenSize(1.5f);
            BRect specularArcRect(bx - (br * 0.6f), by - (br * 0.6f), bx - (br * 0.1f), by - (br * 0.1f));
            StrokeArc(specularArcRect, 90.0f, 90.0f); // Upper left curve shine
        }

        SetDrawingMode(B_OP_COPY);
        SetPenSize(1.0f);
    }



    virtual void MouseDown(BPoint point) override {
        BMessage* message = Window()->CurrentMessage();
        int32 buttons = 0;

        if (message != nullptr && message->FindInt32("buttons", &buttons) == B_OK) {
            if (buttons & B_SECONDARY_MOUSE_BUTTON) {
                if (fMainAppWindow != nullptr) {
                    BMessenger messenger(fMainAppWindow);
                    if (messenger.IsValid()) {
                        BMessage cycleMsg('mcyc');
                        messenger.SendMessage(&cycleMsg);
                    }
                }
                return; 
            }
        }
        BView::MouseDown(point);
    }

private:
    BBitmap* fCachedImage;
    BWindow* fMainAppWindow;

    // --- CLEAN REPLICA FLOATING BUBBLE MATRIX DATA ---
    float    fBubbleX[75];      // Horizontal position
    float    fBubbleY[75];      // Vertical position
    float    fBubbleRadius[75]; // Dynamic size of each bubble
    float    fBubbleSpeedX[75]; // Organic drifting velocity
    float    fBubbleSpeedY[75]; // Vertical floating speed
    float    fWobblePhase[75];  // Sin phase tracking for liquid wiggle shapes

    
};


// --- THE TRANSPARENT FLOATING WINDOW LAYER ---
class ReplicaOverlayWindow : public BWindow {
public:
    ReplicaOverlayWindow(BWindow* parent, BBitmap* snapshot)
        : BWindow(parent->Frame(), "ReplicaOverlayWindow", B_NO_BORDER_WINDOW_LOOK, 
                  B_FLOATING_SUBSET_WINDOW_FEEL, 
                  B_NOT_MOVABLE | B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_AVOID_FRONT) {
        
        fParent = parent; 
        AddToSubset(parent);
        
        fOverlayView = new ReplicaOverlayView(Bounds(), snapshot, parent);
        AddChild(fOverlayView);
    }

    void UpdateAudioPhysics(float* barHeights, float dtScale, float volumeScale) {
        if (Lock()) {
            fOverlayView->UpdatePhysics(barHeights, dtScale, volumeScale);
            Unlock();
        }
    }

private:
    ReplicaOverlayView* fOverlayView;
    BWindow*            fParent;
};






//@spectrum
class SpectrumView : public BView {
public:

    SpectrumView(BRect frame, const char* name)
        : BView(frame, name, B_FOLLOW_ALL, B_WILL_DRAW | B_FRAME_EVENTS | B_PULSE_NEEDED) {
        SetViewColor(B_TRANSPARENT_COLOR);       
        
        fReplicaWin = nullptr;
        fAcidWin = nullptr;
        bRimage = NULL;
       
        fCurrentLevel = -60.0;       
        fVisualizerMode = MODE_BARS; 
        fLastDataTime = 0;
        memset(frequencyData, 0, 64);
        fLeftScore = 0; 
        fRightScore = 0;
        
        // Initialize Motorcycle endless runner variables
        fMotoY = 0.0f;
        fMotoVelocityY = 0.0f;
        fMotoCrashTicks = 0.0f; // Clean Float Literal
        fMotoScore = 0;
        fStuntTextY = 0.0f;
        fStuntTextLife = 0.0f;  // Clean Float Literal
        fStuntTextStr = "";

        // Neon Sign Timers
        fNeonBassSmooth = 0.0f;
        fNeonTrebleSmooth = 0.0f;
        fNeonFlickerTimer1 = 0.0f;
        fNeonFlickerTimer2 = 0.0f;
        
        // Dynamic Delta Time Tracking Anchor
        fPrevFrameTime = 0; // Absolute microsecond clock start index
        
        // Force a staggering pipeline gap so obstacles do not stack on top of each other
        fObsX[0] = 340.0f;
        fObsIsPit[0] = 0;       // Set as integer literal 0 (Matches 'int fObsIsPit[2]')
        fObsHeightScale[0] = 1.0f;

        fObsX[1] = 520.0f;      // Positioned further right down the scrolling track line
        fObsIsPit[1] = 1;       // Set as integer literal 1 (Corresponds to PIT type in Draw loop)
        fObsHeightScale[1] = 0.8f;
        
        // Initialize exhaust particle arrays to an inactive starting state
        for (int s = 0; s < 12; s++) {
            fSparkLife[s] = 0.0f; // Clean Float Literal
            fSparkX[s] = 0.0f;
            fSparkY[s] = 0.0f;
            fSparkDX[s] = 0.0f;
            fSparkDY[s] = 0.0f;
        }
        
        // Mode 6 Parallax Scenery positions and sizes
        fMtnScrollX = 0.0f;
        for (int m = 0; m < 4; m++) {
            fMtnHeightScale[m] = 0.0f; // Ensure mountain scalar memory cache defaults safely to 0
        }
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
            fPeakHold[i] = 0.0f; // Clean Float Literal
            fArtworkPalette[i] = { (uint8)(40 + i * 2), 210, (uint8)(255 - i * 3), 255 };
        }
        
        // Seed randomness explicitly before generator arrays process calculations
        srand(time(nullptr));
        
        for (int r = 0; r < 75; r++) {
            fRainX[r] = (float)(rand() % 1000) / 1000.0f;
            fRainY[r] = (float)(rand() % 1000) / 1000.0f;
            fRainSpeed[r] = 0.01f + ((rand() % 100) / 10000.0f);
        }
        fRainInitNeedsPulse = false;

        // ====================================================================
        // INITIALIZE ACTIVE ARCADE PONG BALL METRICS (FIXED SPECIFICATION)
        // ====================================================================
        // Centered vertically inside our 150px normalized height baseline track
        fBallX = frame.Width() * 0.25f;
        fBallY = 75.0f;          // <-- UPDATED: Normalized vertical center alignment
        fBallDX = 5.5f;          // Fast arcade starting speed vector
        fBallDY = -4.0f;
        fBallSize = 12.0f;
        
        // Initialize Ball 2 with explicit naming and force inactive state (Size 0.0)
        fBallX2 = frame.Width() * 0.75f;
        fBallY2 = 75.0f;         // <-- UPDATED: Normalized vertical center alignment
        fBallDX2 = -5.0f;
        fBallDY2 = 4.5f;
        fBallSize2 = 0.0f;       // Force inactive; probability loop handles activation
    

        
        // EasterEgg1 (Chasing Dog Engine)
        fDogDrawActive = false;
        fDogDrawX = 0.0f;
        fDogDrawY = 0.0f;
        
        fPongExplosionTick = 0.0f; // Clean Float Literal initialization mapping
        
        
    }

~SpectrumView() {
    
    delete bRimage;
    bRimage = nullptr;
}

    virtual void MouseDown(BPoint point) override {
        BMessage* message = Window()->CurrentMessage();
        int32 buttons = 0;
        int32 clicks = 0; 
        
        MakeFocus(true);

        if (message != nullptr && message->FindInt32("buttons", &buttons) == B_OK) {
            message->FindInt32("clicks", &clicks);

            // --- 1. RIGHT CLICK: CYCLE VISUALIZER GRAPHICS MODES ---
            if (buttons & B_SECONDARY_MOUSE_BUTTON) {
                int oldMode = fVisualizerMode;
                int targetNextMode = (fVisualizerMode + 1) % MODE_COUNT;

                // --- NEW: DETECT FULLSCREEN BYPASS HOOK ---
                bool isWindowInFullscreen = false;
                if (Window() != nullptr) {
                    SuperMusicWindow* mainWin = dynamic_cast<SuperMusicWindow*>(Window());
                    if (mainWin != nullptr) {
                        isWindowInFullscreen = mainWin->IsFullscreenActive();
                    }
                }

                // If in fullscreen, automatically skip past the replica and acid visualizers
                if (isWindowInFullscreen) {
                    while (targetNextMode == MODE_REPLICA || targetNextMode == MODE_ACID_MELT) {
                        targetNextMode = (targetNextMode + 1) % MODE_COUNT;
                    }
                }
                
                
                
				//@bypass
                // Assign the final verified mode
                fVisualizerMode = targetNextMode;

                // --- TEARDOWN OLD MODES CLEANLY ---
                if (oldMode == MODE_REPLICA && fReplicaWin != nullptr) {
                    fReplicaWin->Lock(); fReplicaWin->Quit(); fReplicaWin = nullptr;
                }
                else if (oldMode == MODE_ACID_MELT && fAcidWin != nullptr) {
                    fAcidWin->Lock(); fAcidWin->Quit(); fAcidWin = nullptr;
                }

                // --- INITIALIZE THE NEW SELECTION CHANNELS ---
                // (Guarded: These won't execute in fullscreen since the while loop skips past them)
                if (fVisualizerMode == MODE_REPLICA) {
                    CaptureAppSnapshot();
                    fReplicaWin = new ReplicaOverlayWindow(Window(), bRimage);
                    fReplicaWin->Show();
                } 
                else if (fVisualizerMode == MODE_ACID_MELT) {
                    CaptureAppSnapshot(); // Snaps clean layout coordinates
                    
                    fAcidWin = new AcidMeltingWindow(Window(), bRimage);
                    fAcidWin->Show();
                }

                if (bRimage != nullptr && fVisualizerMode != MODE_REPLICA && fVisualizerMode != MODE_ACID_MELT) {
                    delete bRimage; bRimage = nullptr;
                }

                Invalidate();
                return; 
            }

            // --- 2. MIDDLE MOUSE BUTTON (WHEEL CLICK): MOTORCYCLE GAME FULLSCREEN HOTKEY ---
            if (buttons & B_TERTIARY_MOUSE_BUTTON) {
                if (clicks == 2 && fVisualizerMode == MODE_MOTO_RIDER) {
                    if (Window() != nullptr) {
                        Window()->PostMessage(new BMessage(MSG_TOGGLE_FULLSCREEN));
                    }
                    return;
                }
            }

            // --- 3. LEFT CLICK: NORMAL MODE INTERACTIONS ---
            if (buttons & B_PRIMARY_MOUSE_BUTTON) {
                
                if (clicks == 2 && fVisualizerMode != MODE_MOTO_RIDER) {
                    if (Window() != nullptr) {
                        Window()->PostMessage(new BMessage(MSG_TOGGLE_FULLSCREEN));
                    }
                    return; 
                }
                
                // --- SNAPSHOT OVERLAY INTERACTION PROTECTION GUARDS ---
                // Blocks clicks from passing through and accidentally firing gameplay inputs
                if (fVisualizerMode == MODE_REPLICA || fVisualizerMode == MODE_ACID_MELT) {
                    return; 
                }
				//@motorjump
                // --- MODE: MOTORCYCLE RIDER INPUTS ---
                if (fVisualizerMode == MODE_MOTO_RIDER && fMotoCrashTicks == 0.0f) {
                    bigtime_t now = system_time();

                    // --- DETECT IF WINDOW IS IN FULLSCREEN MODE ---
                    bool isWindowInFullscreen = false;
                    if (Window() != nullptr) {
                        SuperMusicWindow* mainWin = dynamic_cast<SuperMusicWindow*>(Window());
                        if (mainWin != nullptr) {
                            isWindowInFullscreen = mainWin->IsFullscreenActive();
                        }
                    }

                    // --- AIR FLIP ENGINE (WITH COOLDOWN SAFETY) ---
                    if (fMotoY > 0.05f) {
                        // Check if 250ms have passed since the last click, and ensure 
                        // we aren't already locked into an active flip rotation
                        if (!fIsFlipping && (now - fLastClickTime < 250000)) {
                            fIsFlipping = true;
                            
                            // Moderated flip-boost: reduce from 2.0f to a stable 0.75f 
                            // to prevent rapid propeller-spinning in high-altitude full screen
                            //fMotoVelocityY += 0.75f; 
                            fMotoVelocityY += 1.25f; 
                            fLastClickTime = now; 
                            return;
                        }
                    }
                    
                    // --- GROUND JUMP ENGINE ---
                    if (fMotoY <= 0.05f) {
                        float audioBonus = (fCurrentLevel > -35.0f) ? (35.0f + (float)fCurrentLevel) * 0.12f : 0.0f;
                        
                        // Dynamic velocity tuning: Full screen needs slightly more push 
                        // to clear the vertical canvas pixel height, but 3.0f was over-inflating it.
                        // Use 1.4f for fullscreen scaling to preserve natural gravity tracking.
                        float jumpHeightMultiplier = isWindowInFullscreen ? 1.9f : 1.2f;
                        
                        fMotoVelocityY = (4.0f + audioBonus) * jumpHeightMultiplier; 
                        fLastClickTime = now; 
                        return;
                    }
                }


                // --- MODE: PONG BALLS INTERACTION ---
                if (fVisualizerMode == MODE_PONG_BALLS) {
                    return; 
                }

                // --- MODE: NEON SIGN INTERACTION: MANUAL POWER SURGE FLICKER ---
                if (fVisualizerMode == MODE_WERE_OPEN_NEON_SIGN) {
                    fNeonFlickerTimer1 = 0.25f; 
                    fNeonFlickerTimer2 = 0.35f; 
                    Invalidate(); 
                    return;
                }
            }
        }
        BView::MouseDown(point);
    }




    // @specmouse
    virtual void MessageReceived(BMessage* message) override {
        switch (message->what) {
        	
        // ================================================================
        // --- SAFE ASYNC OVERLAY TEARDOWN INTERFACE ---
        // ================================================================
        case 'tdwn': {
            // 1. Terminate the Replica Overlay window cleanly if active
            if (fReplicaWin != nullptr) {
                if (fReplicaWin->Lock()) {
                    fReplicaWin->Quit();
                    fReplicaWin = nullptr;
                }
            }

            // 2. Terminate the Acid Melt Overlay window cleanly if active
            if (fAcidWin != nullptr) {
                if (fAcidWin->Lock()) {
                    fAcidWin->Quit();
                    fAcidWin = nullptr;
                }
            }

            // 3. Fallback the private visualizer mode variable back to standard bars
            fVisualizerMode = MODE_BARS; 
            break;
        }
           // --- UNIFIED MODE DISMISSER FOR BOTH VISUALIZERS ---  
            case 'drep': { 
                if (fVisualizerMode == MODE_REPLICA || fVisualizerMode == MODE_ACID_MELT) {
                    
                    // 1. DETECT FULLSCREEN MODE STATUS
                    bool isWindowInFullscreen = false;
                    if (Window() != nullptr) {
                        SuperMusicWindow* mainWin = dynamic_cast<SuperMusicWindow*>(Window());
                        if (mainWin != nullptr) {
                            isWindowInFullscreen = mainWin->IsFullscreenActive();
                        }
                    }

                    // 2. Calculate the next mode context step upfront
                    int targetNextMode = (fVisualizerMode + 1) % MODE_COUNT;
                    
                    // 3. Tear down the replica overlay window if it exists
                    if (fReplicaWin != nullptr) {
                        if (fReplicaWin->Lock()) {
                            fReplicaWin->Quit();
                            fReplicaWin = nullptr;
                        }
                    }
                    
                    // 4. Tear down the acid melting overlay window if it exists
                    if (fAcidWin != nullptr) {
                        if (fAcidWin->Lock()) {
                            fAcidWin->Quit();
                            fAcidWin = nullptr;
                        }
                    }
                    
                    // 5. Deallocate the clean layout snapshot memory bitmap
                    if (bRimage != nullptr) {
                        delete bRimage;
                        bRimage = nullptr;
                    }
                    
                    // Assign the final verified mode and force native repaint
                    fVisualizerMode = targetNextMode;
                    Invalidate(); 
                    if (Window() != nullptr) {
                        Window()->UpdateIfNeeded(); 
                    }
                    
                    // 6. Only initialize ACID_MELT if we are NOT in fullscreen
                    if (fVisualizerMode == MODE_ACID_MELT && !isWindowInFullscreen) {
                        CaptureAppSnapshot(); 
                        fAcidWin = new AcidMeltingWindow(Window(), bRimage);
                        fAcidWin->Show();
                    }
                }
                break;
            }



            case B_MOUSE_WHEEL_CHANGED: {
                if (fVisualizerMode == MODE_PONG_BALLS) {
                    float deltaY = 0.0f;
                    //@Paddle Speed
                    if (message->FindFloat("be:wheel_delta_y", &deltaY) == B_OK) {
                        // 1. DETERMINE BASE SCROLL SENSITIVITY MULTIPLIER
                        float scrollSensitivityMultiplier = 18.6f; 
                        
                        // 2. DETECT FULLSCREEN MODE HOOKS
                        bool isWindowInFullscreen = false;
                        if (Window() != nullptr) {
                            SuperMusicWindow* mainWin = dynamic_cast<SuperMusicWindow*>(Window());
                            if (mainWin != nullptr) {
                                isWindowInFullscreen = mainWin->IsFullscreenActive();
                            }
                        }
                        
                      
                        // Multiply sensitivity explicitly to cross the expanded widescreen vertical coordinate track instantly
                        if (isWindowInFullscreen) {
                            scrollSensitivityMultiplier *= 3.0f;
                        }
                        
                        // Calculate final positional delta adjustment
                        fRightPaddlePos += deltaY * scrollSensitivityMultiplier;
                        
                        // 3. SECURE PADDLE CANVAS CLAMP BOUNDARIES
                        float fixedVisualizerHeight = Bounds().Height();
                        float paddleH = 21.0f;
                        
                        if (fRightPaddlePos < paddleH / 2.0f) {
                            fRightPaddlePos = paddleH / 2.0f;
                        }
                        if (fRightPaddlePos > fixedVisualizerHeight - (paddleH / 2.0f)) {
                            fRightPaddlePos = fixedVisualizerHeight - (paddleH / 2.0f);
                        }
                        
                        Invalidate(); 
                    }
                }
                break;
            }

            default:
                BView::MessageReceived(message);
                break;
        }
    }


//@screen
void CaptureAppSnapshot() {
    // 1. Safety Guard: Make sure the view is attached to a real window context
    if (Window() == nullptr) {
        if (cfg.debugEnable) printf("[DEBUG ERROR] No parent window context found!\n");
        return;
    }

    if (bRimage != NULL) {
        delete bRimage;
        bRimage = NULL;
    }

    // 2. CRITICAL CHANGE: Grab the frame bounds of the entire application window
    // Window()->Frame() provides absolute screen coordinates including the system borders
    BRect windowFrame = Window()->Frame();

    // 3. Allocate a 32-bit bitmap matching the complete window canvas size
    BRect targetBounds(0.0f, 0.0f, windowFrame.Width(), windowFrame.Height());
    bRimage = new BBitmap(targetBounds, B_RGBA32);

    // 4. Call Haiku's Screen API to pull the absolute desktop pixel layer
    BScreen screen(Window());
    if (screen.IsValid()) {
        screen.ReadBitmap(bRimage, false, &windowFrame);
        if (cfg.debugEnable) printf("[DEBUG] Full App snapshot taken! Sized: %dx%d\n", 
               (int)targetBounds.Width() + 1, (int)targetBounds.Height() + 1);
    } else {
        if (cfg.debugEnable) printf("[DEBUG ERROR] Failed to access BScreen layer!\n");
    }
}


    
BShape GenerateNeonLetterShape(int letterIndex, BPoint origin, float scale) {
    BShape shape;
    shape.Clear();

    // Box metrics for structural letter layouts (40x60 base box size)
    float w = 40.0f * scale;
    float h = 60.0f * scale;

    switch (letterIndex) {
        case 0: // --- LETTER 'O' (Rounded continuous pill capsule) ---
            shape.MoveTo(BPoint(origin.x + w / 2.0f, origin.y));
            // Top-right arch, bottom-right arch, bottom-left arch, top-left arch
            shape.BezierTo(BPoint(origin.x + w, origin.y), BPoint(origin.x + w, origin.y + h / 3.0f), BPoint(origin.x + w, origin.y + h / 2.0f));
            shape.BezierTo(BPoint(origin.x + w, origin.y + h * 2.0f / 3.0f), BPoint(origin.x + w, origin.y + h), BPoint(origin.x + w / 2.0f, origin.y + h));
            shape.BezierTo(BPoint(origin.x, origin.y + h), BPoint(origin.x, origin.y + h * 2.0f / 3.0f), BPoint(origin.x, origin.y + h / 2.0f));
            shape.BezierTo(BPoint(origin.x, origin.y + h / 3.0f), BPoint(origin.x, origin.y), BPoint(origin.x + w / 2.0f, origin.y));
            break;

        case 1: // --- LETTER 'P' (Vertical stem + loop filament) ---
            shape.MoveTo(BPoint(origin.x, origin.y + h));
            shape.LineTo(BPoint(origin.x, origin.y));
            shape.LineTo(BPoint(origin.x + w * 0.6f, origin.y));
            shape.BezierTo(BPoint(origin.x + w, origin.y), BPoint(origin.x + w, origin.y + h * 0.5f), BPoint(origin.x + w * 0.6f, origin.y + h * 0.5f));
            shape.LineTo(BPoint(origin.x, origin.y + h * 0.5f));
            break;

        case 2: // --- LETTER 'E' (Backbone with three horizontal tube tracks) ---
            shape.MoveTo(BPoint(origin.x + w, origin.y));
            shape.LineTo(BPoint(origin.x, origin.y));
            shape.LineTo(BPoint(origin.x, origin.y + h));
            shape.LineTo(BPoint(origin.x + w, origin.y + h));
            // Jump internal pen position to trace out the center filament ring
            shape.MoveTo(BPoint(origin.x, origin.y + h * 0.5f));
            shape.LineTo(BPoint(origin.x + w * 0.75f, origin.y + h * 0.5f));
            break;

        case 3: // --- LETTER 'N' (Double upright pillars + diagonal cross connection) ---
            shape.MoveTo(BPoint(origin.x, origin.y + h));
            shape.LineTo(BPoint(origin.x, origin.y));
            shape.LineTo(BPoint(origin.x + w, origin.y + h));
            shape.LineTo(BPoint(origin.x + w, origin.y));
            break;
    }

    return shape;
}


    
void UpdateLevel(double level) {
    if (!cfg.showSpectrumVisuals || !cfg.eqEnabled) return;

    bigtime_t now = system_time();

    // 1. NATIVE AUDIO BUFFER TRACKING ENGINE
    static bigtime_t sLastCallbackTime = 0;
    static bigtime_t sSmoothedNativeBufferUs = 0;

    // --- TRACK ELAPSED CALLBACK DELTA TIME ---
    float callbackDeltaTime = 0.05f; // Safe 20 FPS baseline fallback (0.05s)
    if (sLastCallbackTime > 0 && now > sLastCallbackTime) {
        bigtime_t current_native_buffer = now - sLastCallbackTime;
        callbackDeltaTime = (float)current_native_buffer / 1000000.0f;

        if (sSmoothedNativeBufferUs == 0) {
            sSmoothedNativeBufferUs = current_native_buffer;
        } else {
            sSmoothedNativeBufferUs = (bigtime_t)((sSmoothedNativeBufferUs * 0.90) + (current_native_buffer * 0.10));
        }

        // Use a 1140ms baseline offset (matching your logs) to align with mpv playback pipeline,
        // and dynamically add the native 190ms hardware buffer variation on top of it.
        bigtime_t basePlaybackDelayUs = 1140000; 
        fAudioHardwareDelayUs = basePlaybackDelayUs + sSmoothedNativeBufferUs + fManualSyncOffsetUs;
    } else {
        fAudioHardwareDelayUs = 1330000 + fManualSyncOffsetUs; 
    }
    sLastCallbackTime = now;

    if (fAudioHardwareDelayUs < 0) fAudioHardwareDelayUs = 0;



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
            if (cfg.debugEnable) snprintf(change_alert, sizeof(change_alert), " -> [LATENCY CHANGED: %+lld us]", latency_delta_us);
            sLastLoggedLatencyUs = fAudioHardwareDelayUs; // Update baseline after logging change
        } else if (sLastLoggedLatencyUs == 0) {
            sLastLoggedLatencyUs = fAudioHardwareDelayUs; // Initialize baseline on first log
        }

        if (cfg.debugEnable) fprintf(stderr, "[SPECTRUM DEBUG] Input Level: %6.2f dB | NATIVE FRAME SIZE: ~%lld samples | Native Latency: %4.1f ms (%lld us) | Cache Slots Filled: %d/512%s\n", 
                level, 
                sample_frame_size,
                (double)fAudioHardwareDelayUs / 1000.0, 
                (long long)fAudioHardwareDelayUs, 
                active_buffer_elements,
                change_alert);
    }



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

    // ====================================================================
    // 4. Asymmetric Attack Rendering Calculations (Uncoupled from Frame Rate)
    // ====================================================================
    if (delayed_level > fCurrentLevel) {
        fCurrentLevel = delayed_level; // Attack remains instantaneous (no scaling needed)
    } else {
        // --- DECOUPLING EXTRACTOR ---
        // Normalizes the decay slice against your 20 FPS original baseline (0.05 seconds).
        float callbackScale = callbackDeltaTime / 0.05f;
        
        // Prevent floating-point overflow bounds if there is a severe system lag spike
        if (callbackScale > 3.0f) callbackScale = 3.0f; 

        // Converted exponential fallback filter: new_decay = powf(old_decay, scale)
        double adjustedDecay = pow(0.72, (double)callbackScale);
        fCurrentLevel = (fCurrentLevel * adjustedDecay) + (delayed_level * (1.0 - adjustedDecay)); 
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
            
            // Prevent out-of-bounds index selection on the far right column edge
            if (targetPixelX >= width) targetPixelX = width - 1;
            if (targetPixelX < 0) targetPixelX = 0;

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
           // Window()->SetPulseRate(50000); 
            Window()->SetPulseRate(33333); 
      }
}


virtual void KeyDown(const char* bytes, int32 numBytes) override {
 if (cfg.debugEnable) {
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
}




// @specpulse
virtual void Pulse() override {
    // 1. Grab actual real-time drawing boundaries
    BRect b = Bounds();
    float w = b.Width() > 0.0f ? b.Width() + 1.0f : 350.0f;
    float viewHeight = b.Height() + 1.0f; 

    // 2. FALLBACK CRITICAL CHECK: If layout engines are recalculating and bounds are uninitialized,
    // fetch the explicit size constraints assigned by your geometry branch code
    if (viewHeight <= 1.0f) {
        viewHeight = ExplicitMinSize().Height();
    }
    if (w <= 1.0f) {
        w = ExplicitMinSize().Width();
    }

    // Safety Gate: Abandon processing if both fallbacks are still zero
    if (viewHeight <= 0.0f || w <= 0.0f) return;
    
    
    // ====================================================================
    // 0. DYNAMIC DELTA TIME CALCULATION
    // ====================================================================
    bigtime_t now = system_time();
    if (fPrevFrameTime == 0) {
        fPrevFrameTime = now;
    }
    
    // Calculate seconds elapsed since last frame
    float deltaTime = (float)(now - fPrevFrameTime) / 1000000.0f;
    fPrevFrameTime = now;

    // Prevent giant time leaps if the window is frozen or dragged
    if (deltaTime > 0.1f) deltaTime = 0.1f; 
    if (deltaTime <= 0.0f) deltaTime = 0.001f;

    // Baseline normalization factor: 20 FPS means 0.05 seconds per frame.
    // Added absolute division-by-zero guard protection around the delta timeline divisor
    float dtScale = 1.0f;
    if (deltaTime > 0.0f) {
        dtScale = deltaTime / 0.05f;
    }
    fDtScaleCached = dtScale; 

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
    // 2. SPRING PHYSICS & BIN CALCULATIONS (High-Movement Retuning)
    // ====================================================================
    // Tuned for a snappy spring punch and micro-oscillations per frame slice
    const float springStiffness = 0.95f * dtScale; 
    const float springDamping = powf(0.55f, dtScale);   
    bool isTimingOut = ((now - fLastDataTime) > 100000);

    // Dynamic timing tracking variable for organic flickering behavior
    float dynamicTimePhase = (float)now / 35000.0f; 

    for (int i = 0; i < 64; i++) {
        // Frequency-dependent scaling curve
        float frequencyScale = 1.0f;
        if (i < 12) {
            frequencyScale = 1.15f + ((12 - i) * 0.03f); 
        } else if (i > 45) {
            frequencyScale = 0.85f - ((i - 45) * 0.02f); 
        }

        // --- ENHANCED ALIVE ENGINE: Pseudo-Organic High-Energy Movement Noise ---
        // Combines a fast primary sine wave with a secondary offset harmonic wave
        float fastHarmonicWave = sinf(dynamicTimePhase + (i * 0.45f)) * 0.08f;
        float chaoticNoise = cosf((dynamicTimePhase * 1.6f) - (i * 0.75f)) * 0.06f;
        
        // Only inject the high-frequency jitter when audio magnitude is active
        float audioJitterMultiplier = 1.0f + (fastHarmonicWave + chaoticNoise) * (masterMagnitude * 1.5f);
        
        // Base low-frequency organic pulse scale
        float organicScale = (0.95f + (0.10f * sinf(i * 0.25f))) * audioJitterMultiplier;
        
        // --- ADDED SPECIFIC HEIGHT BOOST HERE ---
        // 1.5f makes this specific spectrum's bars 50% taller without changing masterSensitivity
        const float visualizerHeightBoost = 1.5f; 
        
        // Calculate target constraint ceiling to prevent spring breakdown
        float targetHeight = masterMagnitude * viewHeight * frequencyScale * organicScale * visualizerHeightBoost;
        if (targetHeight > viewHeight) targetHeight = viewHeight;

        // Apply Hooke's Law Spring Force scaled linearly with elapsed time
        float displacement = targetHeight - fBarHeights[i];
        float springForce = displacement * springStiffness;
        fBarVelocities[i] = (fBarVelocities[i] + springForce) * springDamping;
        fBarHeights[i] += fBarVelocities[i] * dtScale;
        
        // Boundaries enforcement
        if (fBarHeights[i] > viewHeight) {
            fBarHeights[i] = viewHeight;
            fBarVelocities[i] = 0.0f;
        }
        if (fBarHeights[i] < 0.0f) {
            fBarHeights[i] = 0.0f;
            fBarVelocities[i] = 0.0f; 
        }

        // Peak Hold logic processing (Decoupled ticks using persistent float tracking)
        if (fBarHeights[i] >= fPeakHeights[i]) {
            fPeakHeights[i] = fBarHeights[i];
            fPeakHold[i] = 6.0f * dtScale; 
        } else {
            if (fPeakHold[i] > 0.0f) {
                fPeakHold[i] -= dtScale; 
            } else {
                // Acceleration drop looks significantly more alive than uniform speed drops
                fPeakHeights[i] -= (viewHeight * 0.035f) * dtScale; 
                if (fPeakHeights[i] < 0.0f) fPeakHeights[i] = 0.0f;
            }
        }

        // Integrated Timeout Smoothing Decay (Converted to logarithmic time curves)
        if (isTimingOut) {
            if (fBarHeights[i] > 0.05f) {
                fBarHeights[i] *= powf(0.70f, dtScale); // Slightly faster tail clearout
                fBarVelocities[i] *= powf(0.45f, dtScale);
            } else {
                fBarHeights[i] = 0.0f;
                fBarVelocities[i] = 0.0f;
            }
            if (fPeakHeights[i] > 0.0f) {
                fPeakHeights[i] -= 2.0f * dtScale;
                if (fPeakHeights[i] < 0.0f) fPeakHeights[i] = 0.0f;
            }
        }
    }


    // Handle overall input volume attenuation tracking on silence
    if (isTimingOut && fCurrentLevel > floorDb) {
        // Converted exponential filter decay to delta-time format
        fCurrentLevel = (fCurrentLevel * powf(0.80f, dtScale)) + (floorDb * (1.0f - powf(0.80f, dtScale)));
    }


        // ====================================================================
        // 3. SINGLE-BALL PONG PHYSICS ENGINE (Scoring Inverted + Ball Chasing Dog)
        // ====================================================================
        if (fVisualizerMode == MODE_PONG_BALLS) {
            float bassImpact = (fBarHeights[2] + fBarHeights[6] + fBarHeights[12]) / 3.0f;
            
            // --- 1. DETECT IF WINDOW IS IN FULLSCREEN MODE ---
            bool isWindowInFullscreen = false;
            if (Window() != nullptr) {
                SuperMusicWindow* mainWin = dynamic_cast<SuperMusicWindow*>(Window());
                if (mainWin != nullptr) {
                    isWindowInFullscreen = mainWin->IsFullscreenActive();
                }
            }

            // --- 2. DYNAMIC PADDLE HEIGHT SCALE FACTOR ---
            // If fullscreen, double the paddle size from 21px to 42px to make blocking easier!
            float paddleH = isWindowInFullscreen ? 72.0f : 21.0f; 

            // Continuous float-based step conversion for tick timers
            if (fPongExplosionTick > 0) {
                fPongExplosionTick -= dtScale;
                if (fPongExplosionTick < 0) fPongExplosionTick = 0;
            }


            // --- LEFT PADDLE AUTOMATED AUTOPILOT (ROBUST IMPERFECT AI) ---
            // RESTORED: Dynamic pixel centering based on viewHeight
            static float leftTargetY = viewHeight / 2.0f;
            
            // EMERGENCY COLD-START RECOVERY ANCHOR (Re-mapped to dynamic pixel boundaries)
            if (fLeftPaddlePos < (paddleH / 2.0f) || fLeftPaddlePos > viewHeight - (paddleH / 2.0f) || leftTargetY < 0.0f || leftTargetY > viewHeight) {
                fLeftPaddlePos = viewHeight / 2.0f;
                leftTargetY = viewHeight / 2.0f;
            }
            
            // ====================================================================
            // 3A. DYNAMIC AI CAPABILITY MODULATION WAVE ENGINE (Continuous Pixel Offset)
            // ====================================================================
            float timeSecs = (float)now / 1000000.0f;
            const float cyclePeriodSeconds = 3.0f; 
            float aiPhase = (timeSecs / cyclePeriodSeconds) * 2.0f * (float)M_PI;
            
            // Generate basic 0.0f to 1.0f wave phase
            float waveValue = (cosf(aiPhase) + 1.0f) / 2.0f; 
            
            // Restricts the AI's maximum clumsiness to exactly 20%
            float mistakeFactor = waveValue * 1.0f; 
            
            // Scaled to actual pixel offsets so it impacts the viewHeight coordinate system noticeably
            // This causes the AI to organically wander up to 40 pixels off target
            float dynamicMaxErrorOffset = 40.0f * mistakeFactor; 
            float dynamicErrorChance = 35.0f * mistakeFactor; 
            // ====================================================================
            
            // AI Recalculates tracking coordinates ONLY when ball flies left toward it
            if (fBallDX < 0.0f) {
                static float currentAIError = 0.0f;
                static bool errorCalculated = false;
                
                float midPointX = startX_cached + (artworkWidth_cached / 2.0f);
                if (fBallX > midPointX) {
                    errorCalculated = false; 
                }
                
                if (fBallX <= midPointX && !errorCalculated) {
                    // Removed dtScale from probability check to keep it frame-rate independent
                    if ((rand() % 100) < dynamicErrorChance) {
                        float halfErrorRange = dynamicMaxErrorOffset / 2.0f;
                        
                        // Explicit integer truncation check ensures modulo never crashes on 0 or 1
                        int errorWindow = (int)dynamicMaxErrorOffset;
                        if (errorWindow > 1) {
                            currentAIError = (float)((rand() % errorWindow) - halfErrorRange); 
                        } else {
                            currentAIError = 0.0f; 
                        }
                    } else {
                        currentAIError = 0.0f; 
                    }
                    errorCalculated = true;
                }

                // Track ball position with error and offset adjustments in dynamic pixel space
                leftTargetY = fBallY + currentAIError + fLeftPaddleTargetOffset;
            } else {
                // RESTORED: Park at perfect pixel center when ball moves away to avoid vertical jitter
                leftTargetY = viewHeight / 2.0f;
            }

            // --- LEFT PADDLE AUTOMATED AUTOPILOT BOUNDS CHECK ---
            if (leftTargetY < paddleH / 2.0f) leftTargetY = paddleH / 2.0f;
            // INTACT: Dynamic pixel mapping
            if (leftTargetY > viewHeight - (paddleH / 2.0f)) leftTargetY = viewHeight - (paddleH / 2.0f);

            // SAFETY SHIELD: Clamp modifier value so the internal base of powf never drops below 0.05f
            float safeBassModifier = 0.10f + (bassImpact * 0.01f);
            if (safeBassModifier > 0.95f) safeBassModifier = 0.95f;

            float leftPaddleLerp = 1.0f - powf(1.0f - safeBassModifier, dtScale);
            fLeftPaddlePos += (leftTargetY - fLeftPaddlePos) * leftPaddleLerp;

            // HARD ACTION CLAMP: Instantly drops left paddle back into visible bounds if it ever slips out
            if (fLeftPaddlePos < paddleH / 2.0f) fLeftPaddlePos = paddleH / 2.0f;
            // INTACT: Dynamic pixel mapping
            if (fLeftPaddlePos > viewHeight - (paddleH / 2.0f)) fLeftPaddlePos = viewHeight - (paddleH / 2.0f);

            // --- RIGHT PADDLE MOUSE WHEEL DRIVEN BOUNDS ---
            // INTACT: Dynamic pixel mapping
            if (fRightPaddlePos < paddleH / 2.0f) fRightPaddlePos = paddleH / 2.0f;
            if (fRightPaddlePos > viewHeight - (paddleH / 2.0f)) fRightPaddlePos = viewHeight - (paddleH / 2.0f);

            // --- SCORE WATCH & BALL PHYSICS WITH AUTO-RESET TIMER ---
            static bool timerStarted = false;

            // Declare persistent, static dog metrics inside the physics engine layout scope
            static bool dogActive = false;
            static bool dogRunningAway = false;
            static float dogX = 0.0f;
            static float dogY = 0.0f;
            static bigtime_t dogSpawnTime = 0;

            if (fLeftScore >= 10 || fRightScore >= 10) {
                fBallX = startX_cached + (artworkWidth_cached / 2.0f);
                fBallY = 50.0f; 
                dogActive = false; 

                if (!timerStarted) {
                    fWinStartTime = now; 
                    timerStarted = true;
                }

                if (now - fWinStartTime >= 3000000) {
                    fLeftScore = 0;
                    fRightScore = 0;
                    timerStarted = false;
                    fWinStartTime = 0;

                    fBallDX = ((rand() % 100) > 50) ? 5.5f : -5.5f;
                    fBallDY = ((rand() % 100) > 50) ? 4.0f : -4.0f;
                }
            } else {
                timerStarted = false;
                fWinStartTime = 0;

                // Freeze ball movement briefly if a miss occurred so user can see the shockwave
                if (fMotoCrashTicks > 0.0f) {
                    fMotoCrashTicks -= dtScale;
                    if (fMotoCrashTicks < 0.0f) fMotoCrashTicks = 0.0f;
                } else {
                    float audioSpeedBoost = 1.0f + (bassImpact * 0.05f * 0.65f);
                    if (audioSpeedBoost > 2.5f) audioSpeedBoost = 2.5f;
					
                    // --- 1. DETECT IF WINDOW IS IN FULLSCREEN MODE ---
                    bool isWindowInFullscreen = false;
                    if (Window() != nullptr) {
                        SuperMusicWindow* mainWin = dynamic_cast<SuperMusicWindow*>(Window());
                        if (mainWin != nullptr) {
                            isWindowInFullscreen = mainWin->IsFullscreenActive();
                        }
                    }
					// @Ballspeed
                    // --- 2. CONFIGURE DYNAMIC BALL SPEED MULTIPLIER ---
                    // Boost the speed 50x if fullscreen is active, otherwise keep it standard (1.0f)
                    float fullscreenSpeedMultiplier = isWindowInFullscreen ? 3.0f : 1.0f;

                    // Apply the speed multiplier and audio boost together
                    float moveX = (fBallDX * 0.90f) * audioSpeedBoost * fullscreenSpeedMultiplier * dtScale;
                    float moveY = (fBallDY * 0.90f) * audioSpeedBoost * fullscreenSpeedMultiplier * dtScale;
                    
                    // --- 3. ADJUST SAFETY MAX SPEED LIMIT FOR FULLSCREEN ---
                    // Scale the maximum speed cap up by 50x as well so it doesn't limit the boost
                    float maxSpeedLimitX = 15.0f * fullscreenSpeedMultiplier * dtScale;
                    if (moveX > maxSpeedLimitX) moveX = maxSpeedLimitX;
                    if (moveX < -maxSpeedLimitX) moveX = -maxSpeedLimitX;

                    fBallX += moveX;
                    fBallY += moveY;
                }

                // Ceiling / Floor bounces
                fBallSize = 11.0f; 
                float radius = fBallSize / 2.0f;


                // ====================================================================
                // DYNAMIC CEILING & FLOOR BOUNCES FOR THE BALL
                // ====================================================================

                // Changed from 100.0f to viewHeight so the ball automatically targets 
                // the absolute bottom edge of your 150px layout canvas box!
                if (fBallY - radius < 0.0f) { 
                    fBallY = radius; 
                    fBallDY = -fBallDY; 
                }
                else if (fBallY + radius > viewHeight) { 
                    fBallY = viewHeight - radius; 
                    fBallDY = -fBallDY; // Correctly reflects upward at the real bottom edge
                }
                // ====================================================================


                // ------------------------------------------------------------
                // --- BALL-CHASING DOG PHYSICS ENGINE SUBROUTINE ---
                // ------------------------------------------------------------

                if (!dogActive) {
                    // Spawn probability rate scaled dynamically based on step frequency
                    if ((rand() % 1000) < (5.0f * dtScale)) {
                        dogX = startX_cached + (artworkWidth_cached / 2.0f);
                        dogY = 100.0f + 15.0f; // Start hidden below screen edge
                        dogSpawnTime = now;
                        dogActive = true;
                        dogRunningAway = false;
                    }
                } else {
                    if (!dogRunningAway) {
                        // Phase A: Active ball pursuit over 4.0 seconds (4000000 microseconds)
                        if (now - dogSpawnTime >= 4000000) {
                            dogRunningAway = true;
                        } else {
                            // Framerate independent pursuit lerp values
                            float dogLerp = 1.0f - powf(1.0f - 0.07f, dtScale);
                            dogX += (fBallX - dogX) * dogLerp;
                            dogY += (fBallY - dogY) * dogLerp;
                        }
                    } else {
                        // Phase B: Run away off the screen boundary
                        dogX += 4.5f * dtScale; // Fast break translation vector scaled
                        
                        float dogEscapeLerp = 1.0f - powf(1.0f - 0.10f, dtScale);
                        dogY += (100.0f - 10.0f - dogY) * dogEscapeLerp; 
                        
                        // Clean up state when completely past boundaries
                        if (dogX > startX_cached + artworkWidth_cached + 25.0f) {
                            dogActive = false;
                        }
                    }
                }
                // Save coordinates globally/locally using temporary class pointer fields or shared global markers
                fDogDrawActive = dogActive;
                fDogDrawX = dogX;
                fDogDrawY = dogY;
                
                           // ------------------------------------------------------------
                // --- CHAOTIC SECOND BALL HAZARD SUBROUTINE (NORMALIZED) ---
                // ------------------------------------------------------------
                if (fBallSize2 <= 0.0f) {
                    if ((rand() % 1000) < (1.0f * dtScale)) {
                        // --- 1. STABLE CENTER SPAWN ANCHOR ---
                        fBallX2 = startX_cached + (artworkWidth_cached / 2.0f);
                        fBallY2 = viewHeight / 2.0f; // Force start exactly in the dynamic middle of the layout canvas
                        
                        fBallDX2 = ((rand() % 100) > 50) ? 4.5f : -4.5f;
                        fBallDY2 = ((rand() % 100) > 50) ? 3.0f : -3.0f;
                        fBallSize2 = 10.0f; // Draws it into the frame
                        
                        fBonusFlashTick = 60.0f; 
                        fBonusFlashAlpha = 1.0f;
                    }
                } else {
                    float audioSpeedBoost = 1.0f + (bassImpact * 0.05f * 0.65f);
                    if (audioSpeedBoost > 2.5f) audioSpeedBoost = 2.5f; 

                    // --- DETECT IF WINDOW IS IN FULLSCREEN MODE FOR BALL 2 ---
                    bool isWindowInFullscreen = false;
                    if (Window() != nullptr) {
                        SuperMusicWindow* mainWin = dynamic_cast<SuperMusicWindow*>(Window());
                        if (mainWin != nullptr) {
                            isWindowInFullscreen = mainWin->IsFullscreenActive();
                        }
                    }

                    // --- CONFIGURE DYNAMIC BALL 2 SPEED MULTIPLIER ---
                    // Boost the speed 3x if fullscreen is active, matching the primary ball logic
                    float fullscreenSpeedMultiplier = isWindowInFullscreen ? 3.0f : 1.0f;

                    // Apply the speed multiplier and audio boost together
                    float moveX2 = (fBallDX2 * 0.90f) * audioSpeedBoost * fullscreenSpeedMultiplier * dtScale;
                    float moveY2 = (fBallDY2 * 0.90f) * audioSpeedBoost * fullscreenSpeedMultiplier * dtScale;

                    // --- ADJUST SAFETY MAX SPEED LIMIT FOR BALL 2 IN FULLSCREEN ---
                    float maxSpeedLimitX2 = 15.0f * fullscreenSpeedMultiplier * dtScale;
                    if (moveX2 > maxSpeedLimitX2) moveX2 = maxSpeedLimitX2;
                    if (moveX2 < -maxSpeedLimitX2) moveX2 = -maxSpeedLimitX2;

                    // Increment positions cleanly using the updated motion steps
                    fBallX2 += moveX2;
                    fBallY2 += moveY2;

                    float b2Radius = fBallSize2 / 2.0f;
                    
                    // --- 2. LOCKED DYNAMIC CEILING / FLOOR BOUNCES ---
                    // Updated from 150.0f to viewHeight to match primary ball fullscreen scaling
                    if (fBallY2 - b2Radius < 0.0f) { 
                        fBallY2 = b2Radius; 
                        fBallDY2 = -fBallDY2;
                    }
                    else if (fBallY2 + b2Radius > viewHeight) { 
                        fBallY2 = viewHeight - b2Radius; 
                        fBallDY2 = -fBallDY2; // Correctly reflects upward at the dynamic bottom edge
                    }

                    // Left Paddle Collision check for Ball 2
                    float leftPaddleRightEdge = startX_cached + 5.0f;
                    if (fBallX2 - b2Radius <= leftPaddleRightEdge && fBallX2 + b2Radius >= startX_cached && fBallDX2 < 0.0f) {
                        // Map the collision tracking straight to our normalized paddle position tracking bounds
                        if (fBallY2 >= fLeftPaddlePos - (paddleH / 2.0f) - 3.0f && fBallY2 <= fLeftPaddlePos + (paddleH / 2.0f) + 3.0f) {
                            fBallX2 = leftPaddleRightEdge + b2Radius;
                            fBallDX2 = -fBallDX2 * 1.15f; 
                        }
                    }

                    // Right Paddle Collision check for Ball 2
                    float rightPaddleLeftEdge = startX_cached + artworkWidth_cached - 5.0f;
                    if (fBallX2 + b2Radius >= rightPaddleLeftEdge && fBallX2 - b2Radius <= startX_cached + artworkWidth_cached && fBallDX2 > 0.0f) {
                        if (fBallY2 >= fRightPaddlePos - (paddleH / 2.0f) - 3.0f && fBallY2 <= fRightPaddlePos + (paddleH / 2.0f) + 3.0f) {
                            fBallX2 = rightPaddleLeftEdge - b2Radius;
                            fBallDX2 = -fBallDX2 * 1.15f;
                        }
                    }

   					// --- BALL 2 BONUS POINT OUT-OF-BOUNDS HANDLING ---
                    if (fBallX2 < startX_cached || fBallX2 > startX_cached + artworkWidth_cached) {
                        if (fBallX2 < startX_cached) {
                            fRightScore += 2; 
                        } else {
                            fLeftScore += 2; 
                        }
                        
                        fBonusFlashTick = 60.0f; 
                        fBonusFlashAlpha = 1.0f;
                        
                        fPongExplosionX = fBallX2;
                        fPongExplosionY = fBallY2;
                        fPongExplosionTick = 20.0f * dtScale;
                        fMotoCrashTicks = 15.0f * dtScale; 

                        fBallSize2 = 0.0f; // Despawns Ball 2 completely
                    }
                }
                // ------------------------------------------------------------

                // Update text opacity clock counter
                if (fBonusFlashTick > 0.0f) {
                    fBonusFlashTick -= dtScale;
                    fBonusFlashAlpha = fBonusFlashTick / 60.0f;
                    if (fBonusFlashTick < 0.0f) {
                        fBonusFlashTick = 0.0f;
                        fBonusFlashAlpha = 0.0f;
                    }
                }

                // Left Paddle Collision check (Ball 1)
                float leftPaddleRightEdge = startX_cached + 5.0f;
                if (fBallX - radius <= leftPaddleRightEdge && fBallX + radius >= startX_cached && fBallDX < 0.0f) {
                    if (fBallY >= fLeftPaddlePos - (paddleH / 2.0f) - 3.0f && fBallY <= fLeftPaddlePos + (paddleH / 2.0f) + 3.0f) {
                        fBallX = leftPaddleRightEdge + radius;
                        fBallDX = -fBallDX;
                        fBallDX *= 1.25f; // Apply fast arcade compounding bounce factor
                        
                        float relativeIntersectY = fLeftPaddlePos - fBallY;
                        float normalizedIntersectY = relativeIntersectY / (paddleH / 2.0f);
                        float randomFactor = ((rand() % 20) - 10) / 50.0f;
                        fBallDY = (-normalizedIntersectY * 3.5f) + randomFactor;

                        fLeftPaddleTargetOffset = (float)((rand() % 16) - 8); 
                    }
                }

                // Right Paddle Collision check (Ball 1)
                float rightPaddleLeftEdge = startX_cached + artworkWidth_cached - 5.0f;
                if (fBallX + radius >= rightPaddleLeftEdge && fBallX - radius <= startX_cached + artworkWidth_cached && fBallDX > 0.0f) {
                    if (fBallY >= fRightPaddlePos - (paddleH / 2.0f) - 3.0f && fBallY <= fRightPaddlePos + (paddleH / 2.0f) + 3.0f) {
                        fBallX = rightPaddleLeftEdge - radius;
                        fBallDX = -fBallDX;
                        fBallDX *= 1.25f; // Apply fast arcade compounding bounce factor
                        
                        float relativeIntersectY = fRightPaddlePos - fBallY;
                        float normalizedIntersectY = relativeIntersectY / (paddleH / 2.0f);
                        float randomFactor = ((rand() % 20) - 10) / 50.0f;
                        fBallDY = (-normalizedIntersectY * 3.5f) + randomFactor;
                    }
                }

                // ====================================================================
                // 3B. OUT OF BOUNDS RESET PATH HANDLER (ADAPTIVE SCORER SERVE ENGINE)
                // ====================================================================
                if (fBallX < startX_cached || fBallX > startX_cached + artworkWidth_cached) {
                    
                    static int lastScorerAnchor = 0;

                    // --- INVERTED SCORE ROUTING ALLOCATION & SIDE SNAP (Ball 1 Only = +1 Point) ---
                    if (fBallX < startX_cached) {
                        fRightScore++; 
                        lastScorerAnchor = 2; // Right player scored a point
                    } else {
                        fLeftScore++; 
                        lastScorerAnchor = 1; // Left player scored a point
                    }
                    // --------------------------------------------------

                    // 1. Snapshot the exact screen location of the ball's demise
                    fPongExplosionX = fBallX;
                    fPongExplosionY = fBallY;
                    
                    // 2. Start your animation tick and freeze countdown counters
                    fPongExplosionTick = 20.0f * dtScale;
                    fMotoCrashTicks = 25.0f * dtScale; 

                    // 3. ADAPTIVE SERVE LAYOUT CALCULATIONS
                    fBallY = viewHeight / 2.0f; // Unified center rendering target

                    // Determine the horizontal serve baseline position
                    if (lastScorerAnchor == 1) {
                        fBallX = startX_cached + 12.0f;
                        fBallDX = 5.5f; // Serve launches right toward human
                    } else if (lastScorerAnchor == 2) {
                        fBallX = startX_cached + artworkWidth_cached - 12.0f;
                        fBallDX = -5.5f; // Serve launches left toward computer
                    } else {
                        fBallX = startX_cached + (artworkWidth_cached / 2.0f);
                        fBallDX = ((rand() % 100) > 50) ? 5.5f : -5.5f;
                    }
                    fBallDY = ((rand() % 100) > 50) ? 4.0f : -4.0f;

                    // 4. RESET ANCHOR FOR ENTIRELY NEW COMPLETED GAMES
                    if (fLeftScore >= 10 || fRightScore >= 10) {
                        lastScorerAnchor = 0; 
                    }
                }
            } 
        } 




        // ====================================================================
        // 4. MODE 5 RAINDROPS PHYSICS UPDATES
        // ====================================================================
        if (fVisualizerMode == MODE_RAINDROPS) {
            for (int r = 0; r < 75; r++) {
                int freqIdx = (int)(fRainX[r] * 63.0f);
                // Scaled translation step via dtScale
                fRainY[r] += (fRainSpeed[r] * (1.0f + (fBarHeights[freqIdx] / viewHeight) * 2.5f)) * dtScale;
                if (fRainY[r] > 1.0f) {
                    fRainY[r] = 0.0f;
                    fRainX[r] = (float)(rand() % 1000) / 1000.0f;
                    fRainSpeed[r] = 0.01f + ((rand() % 100) / 10000.0f);
                }
            }
        }
        
		// ====================================================================
		// 4.5 MODE 4.5 MODE_REPLICA PHYSICS ROUTING @replicaengine
		// ====================================================================
		if (fVisualizerMode == MODE_REPLICA) {
    		if (Window() != nullptr && fReplicaWin != nullptr) {
        		// 1. Keep the independent floating canvas perfectly aligned with the main app bounds
        		fReplicaWin->MoveTo(Window()->Frame().LeftTop());
        		fReplicaWin->ResizeTo(Window()->Frame().Width(), Window()->Frame().Height());
        
        		// 2. FORWARD AUDIO DATA DIRECTLY DOWN THE CHANNEL
        		// We pass the raw data down. The ReplicaOverlayView class handles its own 
        		// internal heap allocations safely inside its own window loop context!
        		fReplicaWin->UpdateAudioPhysics(fBarHeights, dtScale, gVolumeScaleFactor);
    		}
		}

    	// ====================================================================
    	// 4.6 MODE 4.6 MODE_ACID_MELT MOVEMENT DISPATCHER
    	// ====================================================================
    	if (fVisualizerMode == MODE_ACID_MELT) {
        	if (Window() != nullptr && fAcidWin != nullptr) {
            	fAcidWin->MoveTo(Window()->Frame().LeftTop());
            	fAcidWin->ResizeTo(Window()->Frame().Width(), Window()->Frame().Height());
            
            	// PASS PALETTE HERE: Forwards your dynamic fArtworkPalette array directly into the overlay!
            	fAcidWin->UpdateAudioPhysics(fBarHeights, dtScale, gVolumeScaleFactor, fArtworkPalette);
        	}
    	}





        // ====================================================================
        // 4.5 MODE 6 NEON SIGN GAS PHYSICS ENGINE
        // ====================================================================
        if (fVisualizerMode == MODE_WERE_OPEN_NEON_SIGN) {
            float totalBass = 0.0f;
            float totalTreble = 0.0f;
            
            float dt = deltaTime; 
            float volumeScale = gVolumeScaleFactor;

            for (int b = 0; b < 12; b++)  totalBass += (fBarHeights[b] * volumeScale);
            for (int t = 35; t < 55; t++) totalTreble += (fBarHeights[t] * volumeScale);
            
            // --- 1. DETECT WINDOW STATE IN PHYSICS ENGINE ---
            bool isWindowInFullscreen = false;
            if (Window() != nullptr) {
                SuperMusicWindow* mainWin = dynamic_cast<SuperMusicWindow*>(Window());
                if (mainWin != nullptr) {
                    isWindowInFullscreen = mainWin->IsFullscreenActive();
                }
            }

            // --- 2. FIXED GAIN AUDIO CALIBRATION ANCHOR ---
            float targetBass = 0.0f;
            float targetTreble = 0.0f;

            if (isWindowInFullscreen) {
                // Fullscreen Mode: Lock denominator to a standard 150px baseline height target
                // This keeps your audio physics scales perfectly normalized on any resolution monitor!
                targetBass   = (totalBass / 12.0f) / 150.0f;
                targetTreble = (totalTreble / 20.0f) / 150.0f;
            } else {
                // Windowed Mode: Fall back to your original live layout bounds tracking
                float viewBoundsHeight = Bounds().Height();
                if (viewBoundsHeight <= 0.0f) viewBoundsHeight = 1.0f;

                targetBass   = (totalBass / 12.0f) / viewBoundsHeight;
                targetTreble = (totalTreble / 20.0f) / viewBoundsHeight;
            }

            if (targetBass > 1.0f)   targetBass = 1.0f;
            if (targetTreble > 1.0f) targetTreble = 1.0f;

            // Frame independent smoothing calculations
            float bassNeonLerp = 1.0f - powf(1.0f - 0.20f, dtScale);
            float trebleNeonLerp = 1.0f - powf(1.0f - 0.25f, dtScale);
            fNeonBassSmooth   += (targetBass - fNeonBassSmooth) * bassNeonLerp;

            fNeonTrebleSmooth += (targetTreble - fNeonTrebleSmooth) * trebleNeonLerp;

            // Tick down flicker duration timers using true time delta
            if (fNeonFlickerTimer1 > 0.0f) fNeonFlickerTimer1 -= dt;
            if (fNeonFlickerTimer2 > 0.0f) fNeonFlickerTimer2 -= dt;

            int noiseRoll = rand() % 100;

            // Trigger flicker probabilities corrected mathematically per step interval
            if (fNeonFlickerTimer1 <= 0.0f && targetTreble > 0.40f && noiseRoll > (100 - (10.0f * dtScale))) {
                fNeonFlickerTimer1 = 0.10f + ((rand() % 100) / 400.0f); 
            }

            if (fNeonFlickerTimer2 <= 0.0f && targetBass > 0.50f && noiseRoll > (100 - (6.0f * dtScale))) {
                fNeonFlickerTimer2 = 0.06f + ((rand() % 100) / 500.0f); 
            }
            Window()->Lock();
            Invalidate();
            Window()->Unlock();
        }



        // ====================================================================
        // 5. MODE 6: MULTI-OBSTACLE PHYSICS ENGINE, SCORE TRACKER & BACKFIRE
        // ====================================================================
        if (fVisualizerMode == MODE_MOTO_RIDER) {
            // Sample columns 2, 3, and 4 early to drive dynamic growing obstacles
            float lowBassPulse = (fBarHeights[2] + fBarHeights[3] + fBarHeights[4]) / 3.0f;
            float bassNormalized = (viewHeight > 0.0f) ? (lowBassPulse / viewHeight) : 0.0f;
            
            // --- 1. DETECT IF WINDOW IS IN FULLSCREEN MODE ---
            bool isWindowInFullscreen = false;
            if (Window() != nullptr) {
                SuperMusicWindow* mainWin = dynamic_cast<SuperMusicWindow*>(Window());
                if (mainWin != nullptr) {
                    isWindowInFullscreen = mainWin->IsFullscreenActive();
                }
            }
			//@motorspeed
            // --- 2. CONFIG SPEED MULTIPLIER FOR FULLSCREEN INSANITY ---
            float speedMultiplier = isWindowInFullscreen ? 1.5f : 1.0f;

            if (fMotoCrashTicks > 0) {
                fMotoCrashTicks -= dtScale;
                if (fMotoCrashTicks <= 0) { // Reset game pipeline on crash recovery loop
                    fMotoCrashTicks = 0;
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
                    // Scaled by our fullscreen speed multiplier variable
                    float baseObstacleSpeed = 5.8f * dtScale * speedMultiplier; 
                    fObsX[o] -= baseObstacleSpeed;                     
                    
                    // --- HAZARD 1: DYNAMIC HORIZONTAL MOVEMENT ---
                    if (o == 0 && fObsIsPit[o] == 0) {
                        float weaveTime = (float)system_time() / 1000000.0f;
                        // Frequency and amplitude are speed up respectively under extreme limits
                        fObsX[o] += (sinf(weaveTime * 6.5f * speedMultiplier) * 1.8f) * dtScale * speedMultiplier; 
                    } 
                    
                    // Recycle obstacle back to the right margins once it rolls left off-screen
                    if (fObsX[o] < -50.0f) { 
                        int otherIndex = (o == 0) ? 1 : 0;
                        fObsX[o] = max_c(artworkWidth_cached + 40.0f, fObsX[otherIndex] + 160.0f + (rand() % 60));                        
                        fObsIsPit[o] = rand() % 5; 
                        fObsHeightScale[o] = 0.6f + ((rand() % 8) / 10.0f);                        
                        fMotoScore++; // Successfully cleared a hazard!
                    }
                } 
                
                // ------------------------------------------------------------
                // --- BACKGROUND SCROLLING DOG SUBROUTINE (RUNNING LEFT TO RIGHT) ---
                // ------------------------------------------------------------
                static float dogVelocityY = 0.0f; 

                if (!fDogDrawActive) {
                    if ((rand() % 1000) < (3.0f * dtScale)) {
                        fDogDrawX = -30.0f; 
                        fDogDrawY = 0.0f;   
                        dogVelocityY = 0.0f;
                        fDogDrawActive = true;
                    }
                } else {
                    // Applied speed multiplier to horizontally charging dog asset movement
                    fDogDrawX += ((5.8f + 2.5f) * dtScale) * speedMultiplier; 

                    // --- JUMP SENSING AI DETECTOR ---
                    if (fDogDrawY <= 0.01f) { 
                        for (int o = 0; o < 2; o++) {
                            if (fObsX[o] > fDogDrawX && (fObsX[o] - fDogDrawX) < (40.0f * speedMultiplier)) {
                                if (fObsIsPit[o] == 0 || fObsIsPit[o] == 3 || fObsIsPit[o] == 4) {
                                    // Dog leaps higher/faster horizontally to clear intense speed obstacles
                                    dogVelocityY = 3.5f * speedMultiplier; 
                                    break;
                                }
                            }
                        }
                    }

                    // --- DOG GRAVITY PHYSICS ACCELERATION PASS ---
                    fDogDrawY += (dogVelocityY * dtScale);
                    if (fDogDrawY > 0.0f) {
                        // TWO-LINE Square the speedMultiplier on gravity to scale perfectly with the upward push
                        dogVelocityY -= (0.35f * dtScale) * (speedMultiplier * speedMultiplier); 
                    } else {
                        fDogDrawY = 0.0f;
                        dogVelocityY = 0.0f;
                    }


                    if (fDogDrawX > artworkWidth_cached + 30.0f) {
                        fDogDrawActive = false;
                    }
                }
                
                // ------------------------------------------------------------
                // Parallax scrolling speed tracking adjustments
                fMtnScrollX -= (0.95f * dtScale) * speedMultiplier; 
                
                // HIGH-RES MODIFICATION: Mirror drawing geometry bounds to prevent background tearing
                float currentMtnStepBounds = isWindowInFullscreen ? -540.0f : -240.0f;
                if (fMtnScrollX < currentMtnStepBounds) {
                    fMtnScrollX += fabsf(currentMtnStepBounds);
                } 

                for (int m = 0; m < 4; m++) {
                    if (fMtnHeightScale[m] <= 0.01f) {
                        fMtnHeightScale[m] = 0.7f + ((rand() % 7) / 5.0f); 
                    }
                }
                for (int t = 0; t < 4; t++) {
                    fTreeX[t] -= (1.85f * dtScale) * speedMultiplier; 
                    if (fTreeX[t] < -20.0f) {
                        fTreeX[t] = artworkWidth_cached + 10.0f + (rand() % 40);
                        fTreeHeight[t] = 12.0f + (rand() % 10);
                    }
                }
                for (int c = 0; c < 3; c++) {
                    fCloudX[c] -= (0.20f * dtScale) * speedMultiplier;
                    if (fCloudX[c] < -40.0f) {
                        fCloudX[c] = artworkWidth_cached + 10.0f + (rand() % 50);
                        
                        // ========================================================
                        // --- DYNAMIC FULLSCREEN CLOUD SCALING ---
                        // ========================================================
                        if (isWindowInFullscreen) {
                            // Expand size limits (e.g., range 40.0f to 75.0f) 
                            fCloudSize[c] = 40.0f + (rand() % 35);
                            
                            // Give them a deeper vertical spawn zone to match higher screens
                            fCloudY[c] = 8.0f + (rand() % 24);
                        } else {
                            // Classic Windowed Mode sizes (range 16.0f to 30.0f)
                            fCloudSize[c] = 16.0f + (rand() % 14);
                            fCloudY[c] = 4.0f + (rand() % 8);
                        }
                        // ========================================================
                    }
                } 

   
       			// Trigger a backfire burst when a heavy bass beat slams past an intense threshold
                if (fMotoCrashTicks == 0.0f && lowBassPulse > (100.0f * 0.52f)) {
                    for (int s = 0; s < 12; s++) {
                        if (fSparkLife[s] <= 0.0f) {
                            // Scaled particle lifetime up alongside our hyperspeed multiplier
                            fSparkLife[s] = (8 + (rand() % 8)) * dtScale * speedMultiplier;  
                            
                            // Pulled spawn coordinates forward from -12.0f to -2.0f 
                            // to align them directly with the physical tip of the tailpipe
                            fSparkX[s] = -2.0f; 
                            fSparkY[s] = 0.0f; 
                            
                            // Backward horizontal velocity is enhanced under hyperspeed bounds
                            fSparkDX[s] = (-0.8f - ((rand() % 10) / 10.0f)) * speedMultiplier;  
                            fSparkDY[s] = (-0.3f + ((rand() % 20) / 10.0f)) * speedMultiplier;
                        }
                    }
                } 

                // --- MOTO EXHAUST PARTICLE TRACKING PASS ---
                // ====================================================================
                // PURIFIED MATHEMATICAL PHYSICS UPDATES FOR BACKFIRE SPARKS (PULSE)
                // ====================================================================
                for (int s = 0; s < 12; s++) {
                    if (fSparkLife[s] > 0.0f) {
                        // Accelerated decay rates and delta movement vectors for full velocity matching
                        fSparkLife[s] -= dtScale * speedMultiplier; 
                        fSparkX[s] += fSparkDX[s] * dtScale; 
                        fSparkY[s] += fSparkDY[s] * dtScale; 
                        
                        fSparkDX[s] *= powf(0.88f, dtScale * speedMultiplier); 
                        fSparkDY[s] += 0.03f * dtScale * speedMultiplier; // Soft gravity drop pulling down
                    }
                } 
                // ====================================================================

                // --- RIDER MOVEMENT & GRAVITY ---
                fMotoY += fMotoVelocityY * dtScale * speedMultiplier;
                fMotoVelocityY -= 0.45f * dtScale * (speedMultiplier * speedMultiplier); // <-- FIXED


                if (fMotoY <= 0.0f) {
                    fMotoY = 0.0f; 
                    fMotoVelocityY = 0.0f;                    
                    fIsFlipping = false; 
                    fFlipRotation = 0.0f;
                } 

                if (fIsFlipping) {
                    // Scaled the rotational degree increments to accommodate the 25x frame velocities
                    fFlipRotation += 18.0f * speedMultiplier; 
                    if (fFlipRotation >= 360.0f) {
                        fIsFlipping = false; 
                        fFlipRotation = 0.0f;
                        fMotoScore += 5;                         
                        fStuntTextStr.SetTo("+5 STUNT!");
                        fStuntTextY = fMotoY + 22.0f; 
                        fStuntTextLife = 25;          
                    }
                } 
                if (fStuntTextLife > 0) {
                    fStuntTextLife--;      
                    fStuntTextY += 0.8f * speedMultiplier;   
                } 

                // ====================================================================
                // 5B. BACKFLIP ANIMATION STEP TRACKER & MULTI-FLIP BONUS CALCULATIONS
                // ====================================================================
                static int consecutiveFlipCount = 0; // Tracks flips within a single jump

                // --- RIDER MOVEMENT & GRAVITY ---
                // TWO-LINE Square the speedMultiplier on gravity to scale perfectly with the upward push
                fMotoY += fMotoVelocityY * dtScale * speedMultiplier;
                fMotoVelocityY -= 0.45f * dtScale * (speedMultiplier * speedMultiplier); 

                
                if (fMotoY <= 0.0f) {
                    fMotoY = 0.0f; 
                    fMotoVelocityY = 0.0f;                    
                    
                    // Safely terminate flip if the rider touches down
                    fIsFlipping = false; 
                    fFlipRotation = 0.0f;
                    consecutiveFlipCount = 0; // Reset landing tracker combo anchor
                } 

                if (fIsFlipping) {
                    // Spin velocity frame step scaled by our fullscreen speed multiplier
                    fFlipRotation += 18.0f * dtScale * speedMultiplier; 
                    
                    if (fFlipRotation >= 360.0f) {
                        fFlipRotation -= 360.0f; // Reset wheel loop rotation step cleanly
                        consecutiveFlipCount++;  // Increments: 1 for single, 2 for double!
                        
                        // Progressive scoring scaling rewards system (+5, +15, +30...)
                        int scoreBonus = 5 * consecutiveFlipCount;
                        fMotoScore += scoreBonus; 
                        
                        // --- RANDOM STUNT PHRASE GENERATOR ENGINE ---
                        const char* randomPhrases[10] = {
                            "Way To Go!",
                            "You Rock!",
                            "Front Flip Mania!",
                            "Excellent!",
                            "Wow!",
                            "Watch Out!",
                            "Go Go Go!",
                            "Non Stop!",
                            "No Way!",
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
                        
                        // Converted ticker frame bounds to time-slice units, scaled by speed multiplier so it doesn't linger at 25x game speeds
                        fStuntTextLife = 35.0f * dtScale * speedMultiplier; 
                    }
                } 

                if (fStuntTextLife > 0.0f) {
                    fStuntTextLife -= dtScale * speedMultiplier;      
                    fStuntTextY -= 0.4f * dtScale * speedMultiplier; // Float upward at speed matching velocity
                    if (fStuntTextLife < 0.0f) fStuntTextLife = 0.0f;
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
                            // Total frozen recovery steps locked by delta, keeping recovery independent of speedMultiplier
                            fMotoCrashTicks = 30.0f * dtScale; 
                        }
                    } else if (fObsIsPit[o] == 0) {  // SOLID ROCK (0)
                        float audioGrowthFactor = 1.0f + (bassNormalized * 2.0f); 
                        float currentObsHeight = 10.0f * fObsHeightScale[o] * audioGrowthFactor;                        
                        if (fObsX[o] >= bikeLeft && fObsX[o] <= bikeRight && fMotoY < currentObsHeight) {
                            fMotoCrashTicks = 30.0f * dtScale; 
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
                                    fMotoCrashTicks = 30.0f * dtScale; // Structural crash triggered
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



	void AddCubicSegment(BShape& shape, BPoint cp1, BPoint cp2, BPoint endPoint) {
    	// Optimized: In-place brace initialization minimizes stack allocations during high FPS drawing
    	BPoint controlArray[3] = { cp1, cp2, endPoint };
    	shape.BezierTo(controlArray);
	}





// @specdraw
virtual void Draw(BRect updateRect) override {       
    // 1. SAFE FULLSCREEN STATE MONITOR
    bool isWindowInFullscreen = false;
    if (Window() != nullptr) {
        SuperMusicWindow* mainWin = dynamic_cast<SuperMusicWindow*>(Window());
        if (mainWin != nullptr) {
            isWindowInFullscreen = mainWin->IsFullscreenActive();
        }
    }

    // 2. Dynamically track changing layout boundaries safely
    BRect currentBounds = Bounds();
    float height = currentBounds.Height() + 1.0f; 
    float width = currentBounds.Width() + 1.0f;

    // Fallback if layout engines are in-flight and bounds are uninitialized
    if (height <= 1.0f)  height = ExplicitMinSize().Height();
    if (width <= 1.0f)   width = ExplicitMinSize().Width();
    
    // Safety Gate: Abandon drawing pass if dimensions are completely flattened
    if (height <= 0.0f || width <= 0.0f) return;

    int numBars = 64;
    
    // ====================================================================
    // --- IMMERSIVE WIDESCREEN REAL ESTATE CALCULATOR ---
    // ====================================================================
    float artworkWidth = width; 
    float startX = 0.0f;

    if (isWindowInFullscreen) {
        // Fullscreen Mode: Add a elegant 40px outer margin buffer on monitor edges
        float screenPaddingEdge = 40.0f;
        artworkWidth = width - (screenPaddingEdge * 2.0f);
        startX = screenPaddingEdge;
    } else {
        // Windowed Mode: Let it naturally flush into your layout wrappers
        artworkWidth = width;
        startX = 0.0f;
    }
    // ====================================================================

    float barWidth = artworkWidth / (float)numBars; 

    artworkWidth_cached = artworkWidth;
    startX_cached = startX;

    BRect visualizerRegion(startX, 0.0f, startX + artworkWidth, height);

    // ====================================================================
    // --- UNIFIED BACKGROUND CLEARING ENGINE (WITH SCOPE RECOVERY) ---
    // ====================================================================
    rgb_color bgCol;
    if (cfg.uTheme == "Dark") {
        bgCol = (isWindowInFullscreen) ? rgb_color{0, 0, 0, 255} : rgb_color{40, 40, 40, 255}; 
        SetHighColor(bgCol);
        SetLowColor(bgCol);
        SetDrawingMode(B_OP_COPY);
        FillRect(Bounds()); 
    } else {
        bgCol = (Parent() != nullptr) ? Parent()->ViewColor() : ui_color(B_PANEL_BACKGROUND_COLOR);
        SetHighColor(bgCol);
        SetLowColor(bgCol);
        SetDrawingMode(B_OP_COPY);
        
        if (isWindowInFullscreen) {
            FillRect(Bounds());
        } else {
            FillRect(visualizerRegion);
        }
    }

    // 3. DISCONNECTED STATE REDIRECTOR
    if (!cfg.showSpectrumVisuals || !cfg.eqEnabled) {
        return;
    }
    
    // ====================================================================
    // --- RENDER MODES ---
    // ====================================================================
    if (fVisualizerMode == MODE_BARS) {
        SetDrawingMode(B_OP_ALPHA);
        
        const int fadeZoneWidth = 2; 
        const float bottomFadeHeight = 42.0f; 
        
        // ====================================================================
        // RE-DECLARE GEOMETRY CONSTANTS ADAPTED FOR WIDESCREEN REALESTATE
        // ====================================================================
        // Scale your layout padding dynamically so bars stay beautifully separated!
       // const float barPadding = isWindowInFullscreen ? 3.0f : 1.0f;
        const float barPadding = isWindowInFullscreen ? 6.0f : 2.0f;
        float barWidth = artworkWidth / (float)numBars;
        
        // HIGH-RESOLUTION EMBOSS METRICS
        // Dynamically assign 3D border thickness (4 pixels in fullscreen, 2 pixels in window mode)
        //float embossThickness = isWindowInFullscreen ? 3.0f : 1.0f;          
        //float peakIndicatorHeight = isWindowInFullscreen ? 2.0f : 0.0f;
        float embossThickness = isWindowInFullscreen ? 6.0f : 2.5f;
        float peakIndicatorHeight = isWindowInFullscreen ? 5.0f : 2.0f;
        // ====================================================================


        // Pull the safe volume tracking factor
        float volumeScale = gVolumeScaleFactor;

        // --- 2D ORGANIC SHIMMER ENGINE ---
        bigtime_t sysTime = system_time();
        const float basePeriodSeconds = 8.0f; 
        float timeSeconds = (float)sysTime / 1000000.0f;
        float wavePhase = (timeSeconds / basePeriodSeconds) * 2.0f * (float)M_PI;
        
        // Generate a multi-axis floating focal point
        float shimmerCenterX = (0.5f + 0.5f * sinf(wavePhase)) * (float)numBars;
        float shimmerCenterY = (0.5f + 0.5f * cosf(wavePhase * 1.4f)) * height;

        const float shimmerHalfWidthX = 12.0f;
        const float shimmerHalfWidthY = height * 0.4f; 

        for (int i = 0; i < numBars; i++) {
            // Your bar rendering loop can now safely execute using barWidth and barPadding!

            // --- STRICT READ-ONLY GEOMETRY ACCESS ---
            // Read-only variables calculated inside Pulse() to keep draw calls completely pure
            float finalBarHeight = fBarHeights[i] * volumeScale;
            float scaledPeakHeight = fPeakHeights[i] * volumeScale;

            if (finalBarHeight <= 0.0f) continue;
            
            // --- 1. Edge Fades ---
            float edgeFade = 1.0f;
            if (i < fadeZoneWidth) {
                edgeFade = (float)i / (float)fadeZoneWidth;
            } else if (i >= (numBars - fadeZoneWidth)) {
                edgeFade = (float)(numBars - 1 - i) / (float)fadeZoneWidth;
            }
            edgeFade = 0.5f * (1.0f - cosf(edgeFade * (float)M_PI));

            float baseBottomFade = 1.0f;
            if (finalBarHeight < bottomFadeHeight && bottomFadeHeight > 0.0f) {
                baseBottomFade = finalBarHeight / bottomFadeHeight;
                baseBottomFade = 0.5f * (1.0f - cosf(baseBottomFade * (float)M_PI));
            }
            float finalAlphaMultiplier = edgeFade * baseBottomFade;
            
            // --- 2. 2D Distance Blending ---
            float distX = fabsf((float)i - shimmerCenterX);
            float barTopY = height - finalBarHeight;
            
            float shimmerIntensity = 0.0f;
            if (distX < shimmerHalfWidthX) {
                float normX = distX / shimmerHalfWidthX;
                float intensityX = 0.5f * (1.0f + cosf(normX * (float)M_PI));

                float distY = fabsf(barTopY - shimmerCenterY);
                float intensityY = 0.0f;
                if (distY < shimmerHalfWidthY) {
                    float normY = distY / shimmerHalfWidthY;
                    intensityY = 0.5f * (1.0f + cosf(normY * (float)M_PI));
                }

                shimmerIntensity = intensityX * intensityY;
            }

            // --- 3. Geometry Setup ---
            float currentXStart = startX + (i * barWidth);
            float currentXEnd = startX + ((i + 1) * barWidth) - 1.0f - barPadding;
            if (currentXEnd < currentXStart) currentXEnd = currentXStart;

            // --- 4. Render Main Ambient Bar ---
            rgb_color barColor = fArtworkPalette[i];
            barColor.alpha = (uint8)(255.0f * finalAlphaMultiplier);
            SetHighColor(barColor);             
            FillRect(BRect(currentXStart, height - finalBarHeight, currentXEnd, height));

            // --- 5. Enhanced 2D Emboss Overlay ---
            // Ensure bar size is wide enough to accommodate the high-res borders safely
            if (currentXEnd - currentXStart >= (embossThickness * 2.0f + 1.0f)) {
                int16 lightBoost = 25 + (int16)(75.0f * shimmerIntensity);
                
                rgb_color leftEdgeColor = barColor;
                leftEdgeColor.red   = (uint8)min_c(255, leftEdgeColor.red + lightBoost);
                leftEdgeColor.green = (uint8)min_c(255, leftEdgeColor.green + lightBoost);
                leftEdgeColor.blue  = (uint8)min_c(255, leftEdgeColor.blue + lightBoost);
                
                rgb_color rightEdgeColor = barColor;
                rightEdgeColor.red   = (uint8)max_c(0, rightEdgeColor.red - 40);
                rightEdgeColor.green = (uint8)max_c(0, rightEdgeColor.green - 40);
                rightEdgeColor.blue  = (uint8)max_c(0, rightEdgeColor.blue - 40);
                
                // HIGH-RES IMPLEMENTATION: Thicker Left Light Edge Emboss
                SetHighColor(leftEdgeColor);
                FillRect(BRect(currentXStart, height - finalBarHeight, currentXStart + embossThickness, height));

                // HIGH-RES IMPLEMENTATION: Thicker Right Dark Shadow Edge Emboss
                SetHighColor(rightEdgeColor);
                FillRect(BRect(currentXEnd - embossThickness, height - finalBarHeight, currentXEnd, height));
                
                // HIGH-RES IMPLEMENTATION: Thicker Top Peak Light Bar Accent
                SetHighColor(leftEdgeColor);
                FillRect(BRect(currentXStart, height - finalBarHeight, currentXEnd, height - finalBarHeight + embossThickness));
            }

            // --- 6. Render Peak Indicators ---
            if (scaledPeakHeight > finalBarHeight && scaledPeakHeight > 2.0f) {
                rgb_color peakColor = fArtworkPalette[i];
                peakColor.red   = (uint8)min_c(255, peakColor.red + 50);
                peakColor.green = (uint8)min_c(255, peakColor.green + 50);
                peakColor.blue  = (uint8)min_c(255, peakColor.blue + 50);
                
                float peakBottomFade = 1.0f;
                if (scaledPeakHeight < bottomFadeHeight && bottomFadeHeight > 0.0f) {
                    peakBottomFade = scaledPeakHeight / bottomFadeHeight;
                    peakBottomFade = 0.5f * (1.0f - cosf(peakBottomFade * (float)M_PI));
                }
                
                peakColor.alpha = (uint8)(255.0f * edgeFade * peakBottomFade);
                SetHighColor(peakColor); 
                
                // HIGH-RES IMPLEMENTATION: Replace single line stroke with a clean thicker block in fullscreen
                FillRect(BRect(currentXStart, height - scaledPeakHeight - peakIndicatorHeight, 
                               currentXEnd, height - scaledPeakHeight));
            }
        }
        SetDrawingMode(B_OP_COPY);
    }

    
    
    
    else if (fVisualizerMode == MODE_LINE_WAVE) {
        SetDrawingMode(B_OP_ALPHA);
        float midY = height / 2.0f;

        // Pull the safe volume tracking factor
        float volumeScale = gVolumeScaleFactor;

        // --- 1. GLOBAL AUDIO ACTIVITY TRACKING ---
        float maxCurrentPeak = 0.0f;
        for (int i = 0; i < numBars; i++) {
            float scaledHeight = fBarHeights[i] * volumeScale;
            if (scaledHeight > maxCurrentPeak) {
                maxCurrentPeak = scaledHeight;
            }
        }

        float audioActivityAlpha = 1.0f;
        if (maxCurrentPeak < 3.0f) {
            audioActivityAlpha = maxCurrentPeak / 3.0f;
        }

        // Return early safely if silent to preserve desktop host CPU
        if (audioActivityAlpha <= 0.001f) {
            SetDrawingMode(B_OP_COPY);
            return;
        }

        // --- 2D ORGANIC SHIMMER ENGINE (MATCHED ENGINE SPEED) ---
        bigtime_t sysTime = system_time();
        const float basePeriodSeconds = 8.0f; 
        float timeSeconds = (float)sysTime / 1000000.0f;
        float wavePhase = (timeSeconds / basePeriodSeconds) * 2.0f * (float)M_PI;
        
        float shimmerCenterX = (0.5f + 0.5f * sinf(wavePhase)) * (float)numBars;
        float shimmerCenterY = (0.5f + 0.5f * cosf(wavePhase * 1.4f)) * height;

        const float shimmerHalfWidthX = 12.0f;
        const float shimmerHalfWidthY = height * 0.4f;

        BPoint points[64];
        for (int i = 0; i < numBars; i++) {
            float currentX = startX + (i * barWidth) + (barWidth / 2.0f);
            
            float fadeWindow = 1.0f;
            if (i < 8)  fadeWindow = (float)i / 8.0f;
            if (i > 55) fadeWindow = (float)(63 - i) / 8.0f;
            
            // --- SCALE THE WAVE AMPLITUDE BY USER VOLUME ---
            float offset = (fBarHeights[i] * volumeScale) * 0.5f * fadeWindow; 
            float currentY = (i % 2 == 0) ? (midY - offset) : (midY + offset);
            points[i] = BPoint(currentX, currentY);
        }

        // --- HIGH-RESOLUTION SPLINE SCALARS ---
        // Step 1: Up-sample rendering step density (16 subdivisions for crisp fullscreen curves)
        //const int dynamicSteps = isWindowInFullscreen ? 16 : 4; 
         const int dynamicSteps = isWindowInFullscreen ? 64 : 32; 

        // Step 2: Scale up layout pen sizes to match higher resolutions
        float baseFgPenWidth     = isWindowInFullscreen ? 5.5f : 2.5f;
        float baseShadowPenWidth = isWindowInFullscreen ? 7.5f : 3.5f;
       // float baseFgPenWidth     = isWindowInFullscreen ? 10.0f : 5.0f;
       // float baseShadowPenWidth = isWindowInFullscreen ? 14.0f : 7.0f;

        // Step 3: Scale drop-shadow pixel layout offset metrics
        //float shadowOffsetX = isWindowInFullscreen ? 2.5f : 1.0f;
        //float shadowOffsetY = isWindowInFullscreen ? 3.5f : 1.5f;
        float shadowOffsetX = isWindowInFullscreen ? 6.0f : 3.0f;
        float shadowOffsetY = isWindowInFullscreen ? 8.0f : 4.5f;

        for (int i = 0; i < numBars - 1; i++) {
            int i0 = (i == 0) ? 0 : i - 1; 
            int i1 = i; 
            int i2 = i + 1; 
            int i3 = (i + 2 >= numBars) ? numBars - 1 : i + 2;
            
            BPoint p0 = points[i0]; 
            BPoint p1 = points[i1]; 
            BPoint p2 = points[i2]; 
            BPoint p3 = points[i3];
            
            // Keep track of separate rendering points for the shadow and the highlight
            BPoint prevSegPointShadow = p1;
            BPoint prevSegmentPoint = p1;

            // Position offset for the 3D Drop-Shadow line using dynamic metrics
            prevSegPointShadow.x += shadowOffsetX;
            prevSegPointShadow.y += shadowOffsetY;

            for (int s = 1; s <= dynamicSteps; s++) {
                float t = (float)s / (float)dynamicSteps; 
                float t2 = t * t; 
                float t3 = t2 * t;
                
                float f1 = -0.5f * t3 + t2 - 0.5f * t; 
                float f2 = 1.5f * t3 - 2.5f * t2 + 1.0f; 
                float f3 = -1.5f * t3 + 2.0f * t2 + 0.5f * t; 
                float f4 = 0.5f * t3 - 0.5f * t2;
                
                BPoint currSegmentPoint(p0.x * f1 + p1.x * f2 + p2.x * f3 + p3.x * f4, 
                                        p0.y * f1 + p1.y * f2 + p2.y * f3 + p3.y * f4);
                
                // --- Calculate Shimmer Proximity for this sub-segment ---
                float currentSegmentIndex = (currSegmentPoint.x - startX) / barWidth;
                float distX = fabsf(currentSegmentIndex - shimmerCenterX);
                float distY = fabsf(currSegmentPoint.y - shimmerCenterY);
                
                float shimmerIntensity = 0.0f;
                if (distX < shimmerHalfWidthX && distY < shimmerHalfWidthY) {
                    float intensityX = 0.5f * (1.0f + cosf((distX / shimmerHalfWidthX) * (float)M_PI));
                    float intensityY = 0.5f * (1.0f + cosf((distY / shimmerHalfWidthY) * (float)M_PI));
                    shimmerIntensity = intensityX * intensityY;
                }

                // --- PASS 1: RENDER EMBOSS DROP SHADOW (Thicker Backing) ---
                rgb_color shadowColor = fArtworkPalette[i];
                shadowColor.red   = (uint8)max_c(0, shadowColor.red - 45);
                shadowColor.green = (uint8)max_c(0, shadowColor.green - 45);
                shadowColor.blue  = (uint8)max_c(0, shadowColor.blue - 45);
                shadowColor.alpha = (uint8)(160.0f * audioActivityAlpha); 
                
                SetHighColor(shadowColor);
                SetPenSize(baseShadowPenWidth); // Dynamically scaled shadow track thickness
                
                BPoint currSegPointShadow = currSegmentPoint;
                currSegPointShadow.x += shadowOffsetX;
                currSegPointShadow.y += shadowOffsetY;
                
                StrokeLine(prevSegPointShadow, currSegPointShadow);
                prevSegPointShadow = currSegPointShadow;

                // --- PASS 2: RENDER FOREGROUND SPLINE WITH SHIMMER LUMINANCE ---
                int16 lightBoost = 20 + (int16)(80.0f * shimmerIntensity);
                rgb_color litColor = fArtworkPalette[i];
                litColor.red   = (uint8)min_c(255, litColor.red + lightBoost);
                litColor.green = (uint8)min_c(255, litColor.green + lightBoost);
                litColor.blue  = (uint8)min_c(255, litColor.blue + lightBoost);
                litColor.alpha = (uint8)(255.0f * audioActivityAlpha);
                
                SetHighColor(litColor);
                SetPenSize(baseFgPenWidth); // Dynamically scaled foreground line thickness
                
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
            const int numNodes = 10; 
            BPoint nodes[10];
            
            // Pull the safe volume tracking factor
            float volumeScale = gVolumeScaleFactor;

            // --- 1. GLOBAL AUDIO ACTIVITY TRACKING ---
            float maxCurrentPeak = 0.0f;
            for (int i = 0; i < numBars; i++) {
                float scaledHeight = fBarHeights[i] * volumeScale;
                if (scaledHeight > maxCurrentPeak) {
                    maxCurrentPeak = scaledHeight;
                }
            }

            // Smooth interpolation threshold below 3 pixels to prevent sharp clipping pops
            float audioActivityAlpha = 1.0f;
            if (maxCurrentPeak < 3.0f) {
                audioActivityAlpha = maxCurrentPeak / 3.0f;
            }

            // If there's completely no audio active, skip calculations entirely to preserve CPU
            if (audioActivityAlpha <= 0.001f) {
                SetDrawingMode(B_OP_COPY);
                return;
            }
            
            // INSET BOUNDS: Margins pull endpoints safely inside the clipping region
            float innerWidth = artworkWidth - 4.0f;
            float adjustedStartX = startX + 2.0f;

            // --- 2D ORGANIC SHIMMER ENGINE (MATCHED ENGINE SPEED) ---
            bigtime_t sysTime = system_time();
            const float basePeriodSeconds = 8.0f; 
            float timeSeconds = (float)sysTime / 1000000.0f;
            float wavePhase = (timeSeconds / basePeriodSeconds) * 2.0f * (float)M_PI;
            
            float shimmerCenterX = (0.5f + 0.5f * sinf(wavePhase)) * (float)numBars;
            float shimmerCenterY = (0.5f + 0.5f * cosf(wavePhase * 1.4f)) * height;

            const float shimmerHalfWidthX = 12.0f;
            const float shimmerHalfWidthY = height * 0.4f;

            for (int i = 0; i < numNodes; i++) {
                nodes[i].x = adjustedStartX + (innerWidth * ((float)i / (float)(numNodes - 1)));
            }

            float peakAmplitudes[8] = { 0.0f };
            for (int chunk = 0; chunk < 8; chunk++) {
                float sum = 0.0f; 
                int startBar = chunk * 8;
                for (int sub = 0; sub < 8; sub++) { 
                    sum += fBarHeights[startBar + sub]; 
                }
                // --- SCALE CHUNKED AMPLITUDE BY LIVE USER VOLUME ---
                peakAmplitudes[chunk] = ((sum / 8.0f) * volumeScale) * 0.45f; 
            }

            nodes[0].y = midY; 
            nodes[9].y = midY;
            nodes[1].y = midY - peakAmplitudes[0]; 
            nodes[2].y = midY + peakAmplitudes[1]; 
            nodes[3].y = midY - peakAmplitudes[2]; 
            nodes[4].y = midY + peakAmplitudes[3]; 
            nodes[5].y = midY - peakAmplitudes[4]; 
            nodes[6].y = midY + peakAmplitudes[5]; 
            nodes[7].y = midY - peakAmplitudes[6]; 
            nodes[8].y = midY + peakAmplitudes[7]; 

          
        // --- PASS 1: GLOW SHADOW PASS ---
        // Dynamically scale the underlying glow pen size (15.0f in fullscreen, 6.0f in window mode)
        //SetPenSize(isWindowInFullscreen ? 15.0f : 6.0f); 
       // SetPenSize(isWindowInFullscreen ? 32.0f : 16.0f); 
         SetPenSize(isWindowInFullscreen ? 24.0f : 12.0f); 
        
        // Dynamically assign rendering step density (72 steps in fullscreen, 24 steps in window mode)
        // const int dynamicSteps = isWindowInFullscreen ? 72 : 64;
        const int dynamicSteps = isWindowInFullscreen ? 256 : 128;

        for (int glowMirror = 0; glowMirror < 2; glowMirror++) { 
            for (int i = 0; i < numNodes - 1; i++) {
                int i0 = (i == 0) ? 0 : i - 1; 
                int i1 = i; 
                int i2 = i + 1; 
                int i3 = (i + 2 >= numNodes) ? numNodes - 1 : i + 2;
                
                BPoint p0 = nodes[i0]; 
                BPoint p1 = nodes[i1]; 
                BPoint p2 = nodes[i2]; 
                BPoint p3 = nodes[i3];
                
                if (glowMirror == 1) { 
                    p0.y = midY + (midY - p0.y); 
                    p1.y = midY + (midY - p1.y); 
                    p2.y = midY + (midY - p2.y); 
                    p3.y = midY + (midY - p3.y); 
                }
                
                rgb_color colStart = fArtworkPalette[(int)(((float)i / (float)(numNodes - 1)) * 63.0f)];
                rgb_color colEnd = fArtworkPalette[(int)(((float)(i + 1) / (float)(numNodes - 1)) * 63.0f)];
                
                BPoint prevSegmentPoint(p1.x, p1.y);
                
                for (int s = 1; s <= dynamicSteps; s++) {
                    float t = (float)s / (float)dynamicSteps; 
                    float t2 = t * t; 
                    float t3 = t2 * t;
                    
                    float f1 = -0.5f * t3 + t2 - 0.5f * t; 
                    float f2 = 1.5f * t3 - 2.5f * t2 + 1.0f; 
                    float f3 = -1.5f * t3 + 2.0f * t2 + 0.5f * t; 
                    float f4 = 0.5f * t3 - 0.5f * t2;
                    
                    rgb_color glowColor; 
                    float rR = (colStart.red + (colEnd.red - colStart.red) * t) * 0.4f; 
                    float rG = (colStart.green + (colEnd.green - colStart.green) * t) * 0.4f; 
                    float rB = (colStart.blue + (colEnd.blue - colStart.blue) * t) * 0.4f;
                    
                    if (glowMirror == 1) { 
                        glowColor = { (uint8)(rR * 0.5f + bgCol.red * 0.5f), 
                                      (uint8)(rG * 0.5f + bgCol.green * 0.5f), 
                                      (uint8)(rB * 0.5f + bgCol.blue * 0.5f), 
                                      (uint8)(255.0f * audioActivityAlpha) }; 
                    } else { 
                        glowColor = { (uint8)rR, (uint8)rG, (uint8)rB, (uint8)(255.0f * audioActivityAlpha) }; 
                    }
                    
                    SetHighColor(glowColor);
                    BPoint curr(p0.x * f1 + p1.x * f2 + p2.x * f3 + p3.x * f4, 
                                p0.y * f1 + p1.y * f2 + p2.y * f3 + p3.y * f4);
                    StrokeLine(prevSegmentPoint, curr); 
                    prevSegmentPoint = curr;
                    SetDrawingMode(B_OP_ALPHA);
                }
            }
        }

        // --- PASS 2: CRISP FOREGROUND PASS WITH 3D SHADOW EMBOSS AND SHIMMER ---
        // Scale pen width variables proportionally for wide display canvases
        //float baseFgPenWidth     = isWindowInFullscreen ? 5.0f : 2.0f;
        //float baseShadowPenWidth = isWindowInFullscreen ? 8.0f : 4.0f;
        float baseFgPenWidth     = isWindowInFullscreen ? 8.0f : 4.0f;
        float baseShadowPenWidth = isWindowInFullscreen ? 12.0f : 6.0f;
        
        // Scale background drop-shadow coordinate displacement values
        //float shadowOffsetX = isWindowInFullscreen ? 2.5f : 1.0f;
        //float shadowOffsetY = isWindowInFullscreen ? 3.5f : 1.5f;
        float shadowOffsetX = isWindowInFullscreen ? 6.0f : 3.0f;
        float shadowOffsetY = isWindowInFullscreen ? 8.0f : 4.5f;

		//for (int fgMirror = 0; fgMirror < 4; fgMirror++) { // 4-way canvas symmetry
        for (int fgMirror = 0; fgMirror < 2; fgMirror++) { 
            for (int i = 0; i < numNodes - 1; i++) {
                int i0 = (i == 0) ? 0 : i - 1; 
                int i1 = i; 
                int i2 = i + 1; 
                int i3 = (i + 2 >= numNodes) ? numNodes - 1 : i + 2;
                
                BPoint p0 = nodes[i0]; 
                BPoint p1 = nodes[i1]; 
                BPoint p2 = nodes[i2]; 
                BPoint p3 = nodes[i3];
                
                if (fgMirror == 1) { 
                    p0.y = midY + (midY - p0.y); 
                    p1.y = midY + (midY - p1.y); 
                    p2.y = midY + (midY - p2.y); 
                    p3.y = midY + (midY - p3.y); 
                }
                
                rgb_color colStart = fArtworkPalette[(int)(((float)i / (float)(numNodes - 1)) * 63.0f)];
                rgb_color colEnd = fArtworkPalette[(int)(((float)(i + 1) / (float)(numNodes - 1)) * 63.0f)];
                
                BPoint prevSegmentPointShadow(p1.x, p1.y);
                BPoint prevSegmentPoint(p1.x, p1.y);
                
                // Offset initial shadow coordinates using dynamic metrics
                prevSegmentPointShadow.x += shadowOffsetX;
                prevSegmentPointShadow.y += shadowOffsetY;

                for (int s = 1; s <= dynamicSteps; s++) {
                    float t = (float)s / (float)dynamicSteps; 
                    float t2 = t * t; 
                    float t3 = t2 * t;
                    
                    float f1 = -0.5f * t3 + t2 - 0.5f * t; 
                    float f2 = 1.5f * t3 - 2.5f * t2 + 1.0f; 
                    float f3 = -1.5f * t3 + 2.0f * t2 + 0.5f * t; 
                    float f4 = 0.5f * t3 - 0.5f * t2;
                    
                    BPoint curr(p0.x * f1 + p1.x * f2 + p2.x * f3 + p3.x * f4, 
                                p0.y * f1 + p1.y * f2 + p2.y * f3 + p3.y * f4);

                    // --- Calculate Shimmer Intensity for this exact segment point ---
                    float currentSegmentIndex = (curr.x - startX) / (artworkWidth / 64.0f);
                    float distX = fabsf(currentSegmentIndex - shimmerCenterX);
                    float distY = fabsf(curr.y - shimmerCenterY);
                    
                    float shimmerIntensity = 0.0f;
                    if (distX < shimmerHalfWidthX && distY < shimmerHalfWidthY) {
                        float intensityX = 0.5f * (1.0f + cosf((distX / shimmerHalfWidthX) * (float)M_PI));
                        float intensityY = 0.5f * (1.0f + cosf((distY / shimmerHalfWidthY) * (float)M_PI));
                        shimmerIntensity = intensityX * intensityY;
                    }

                    // --- PASS 2A: DROP SHADOW ---
                    rgb_color shadowColor = colStart; 
                    shadowColor.red   = (uint8)max_c(0, shadowColor.red - 50);
                    shadowColor.green = (uint8)max_c(0, shadowColor.green - 50);
                    shadowColor.blue  = (uint8)max_c(0, shadowColor.blue - 50);
                    shadowColor.alpha = (uint8)(140.0f * audioActivityAlpha);
                    
                    SetHighColor(shadowColor);
                    SetPenSize(baseShadowPenWidth); // Use dynamic high-resolution pen width
                    
                    BPoint currShadow = curr;
                    currShadow.x += shadowOffsetX;
                    currShadow.y += shadowOffsetY;
                    StrokeLine(prevSegmentPointShadow, currShadow);
                    prevSegmentPointShadow = currShadow;

                    // --- PASS 2B: FOREGROUND LINE ---
                    int16 lightBoost = 15 + (int16)(85.0f * shimmerIntensity);
                    rgb_color litColor;
                    litColor.red   = (uint8)min_c(255, (colStart.red + (colEnd.red - colStart.red) * t) + lightBoost);
                    litColor.green = (uint8)min_c(255, (colStart.green + (colEnd.green - colStart.green) * t) + lightBoost);
                    litColor.blue  = (uint8)min_c(255, (colStart.blue + (colEnd.blue - colStart.blue) * t) + lightBoost);
                    litColor.alpha = (uint8)(255.0f * audioActivityAlpha);
                    
                    SetHighColor(litColor);
                    SetPenSize(baseFgPenWidth); // Use dynamic high-resolution pen width
                    StrokeLine(prevSegmentPoint, curr);
                    prevSegmentPoint = curr;
                }
            }
        }
        SetDrawingMode(B_OP_COPY);
        SetPenSize(1.0f);
    }


 else if (fVisualizerMode == MODE_PONG_BALLS) {
        SetDrawingMode(B_OP_ALPHA);
        
        // --- 1. DETECT IF WINDOW IS IN FULLSCREEN MODE INSIDE DRAW ---
        bool isWindowInFullscreen = false;
        if (Window() != nullptr) {
            SuperMusicWindow* mainWin = dynamic_cast<SuperMusicWindow*>(Window());
            if (mainWin != nullptr) {
                isWindowInFullscreen = mainWin->IsFullscreenActive();
            }
        }

        // --- 2. DYNAMIC PADDLE HEIGHT SCALE FACTOR ---
        // Dynamically matches the 72.0f size of your physics engine hitboxes
        float paddleH = isWindowInFullscreen ? 72.0f : 21.0f; 

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

        // Dynamically thicken the dash partition line in fullscreen mode
        SetPenSize(isWindowInFullscreen ? 3.0f : 1.5f);
        float verticalPadding = 6.0f; 
        float dashStep = isWindowInFullscreen ? 24.0f : 12.0f;
        float dashLength = isWindowInFullscreen ? 12.0f : 6.0f;

        for (float dY = verticalPadding; dY < (height - verticalPadding); dY += dashStep) {
            StrokeLine(BPoint(startX + (artworkWidth / 2.0f), dY), 
                       BPoint(startX + (artworkWidth / 2.0f), dY + dashLength));
        }
        
        // --- RETRO ARCADE SCORE TRACKING DISPLAY ---
        BFont scoreFont;
        GetFont(&scoreFont);
        
        // Scale the score numbers to 36.0f in fullscreen so they are easy to read from a distance
        scoreFont.SetSize(isWindowInFullscreen ? 36.0f : 14.0f); 
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
        
        // Drop the scores slightly lower in fullscreen to avoid hitting screen edge borders
        float scoreY = isWindowInFullscreen ? 45.0f : 18.0f; 
        float scoreOffset = isWindowInFullscreen ? 75.0f : 35.0f;
        float scoreRightOffset = isWindowInFullscreen ? 45.0f : 22.0f;

        DrawString(leftScoreStr.String(), BPoint(midPointX - scoreOffset, scoreY));
        DrawString(rightScoreStr.String(), BPoint(midPointX + scoreRightOffset, scoreY));

        // --- PROCEDURAL WHITE VECTOR DOG RENDERING OVERLAY ---
        if (fDogDrawActive) {
            SetDrawingMode(B_OP_ALPHA);
            SetHighColor(255, 255, 255, 230); // Clean white dog body
            
            // Set scale factor (2x larger in fullscreen mode)
            float proceduralDogScale = isWindowInFullscreen ? 2.0f : 1.0f;
            
            // Draw main body torso (Scaled uniformly)
            FillRect(BRect(
                fDogDrawX - (7.0f * proceduralDogScale), 
                fDogDrawY - (4.0f * proceduralDogScale), 
                fDogDrawX + (7.0f * proceduralDogScale), 
                fDogDrawY + (3.0f * proceduralDogScale)
            ));
            
            // Draw head block (Scaled uniformly)
            FillRect(BRect(
                fDogDrawX + (4.0f * proceduralDogScale), 
                fDogDrawY - (9.0f * proceduralDogScale), 
                fDogDrawX + (11.0f * proceduralDogScale), 
                fDogDrawY - (3.0f * proceduralDogScale)
            ));
            
            // Draw legs (Scaled uniformly)
            FillRect(BRect(fDogDrawX - (6.0f * proceduralDogScale), fDogDrawY + (3.0f * proceduralDogScale), fDogDrawX - (4.0f * proceduralDogScale), fDogDrawY + (8.0f * proceduralDogScale))); // Back Leg
            FillRect(BRect(fDogDrawX + (4.0f * proceduralDogScale), fDogDrawY + (3.0f * proceduralDogScale), fDogDrawX + (6.0f * proceduralDogScale), fDogDrawY + (8.0f * proceduralDogScale))); // Front Leg
            
            // Little wagging tail
            // Boost the pen size thickness in fullscreen to match the body mass
            SetPenSize(isWindowInFullscreen ? 3.0f : 1.5f);
            
            bigtime_t curTime = system_time();
            bool tailFlip = ((curTime / 150000) % 2 == 0);
            
            if (tailFlip) {
                StrokeLine(BPoint(fDogDrawX - (7.0f * proceduralDogScale), fDogDrawY - (2.0f * proceduralDogScale)), 
                           BPoint(fDogDrawX - (11.0f * proceduralDogScale), fDogDrawY - (6.0f * proceduralDogScale)));
            } else {
                StrokeLine(BPoint(fDogDrawX - (7.0f * proceduralDogScale), fDogDrawY - (2.0f * proceduralDogScale)), 
                           BPoint(fDogDrawX - (12.0f * proceduralDogScale), fDogDrawY - (2.0f * proceduralDogScale)));
            }
        }


     // --- VICTORY WIN MESSAGE SCREEN OVERLAY WITH COUNTDOWN ---
        if (fLeftScore >= 10 || fRightScore >= 10) {
            // Updated variable naming convention references to match private layout scope
            bigtime_t elapsed = system_time() - fWinStartTime; 
            int secondsLeft = 3 - (int)(elapsed / 1000000);
            if (secondsLeft < 1) secondsLeft = 1; 

            BFont winFont;
            GetFont(&winFont);
            winFont.SetSize(16.0f); 
            winFont.SetFace(B_BOLD_FACE);
            SetFont(&winFont);
            fBallSize2 = 0.0f;

            // DYNAMIC CONTRAST CHECK: Read current background / ViewColor to determine theme state
            rgb_color bgCol = ViewColor();
            // If the view background is set to transparent or uninitialized, fallback to standard dark check
            if (bgCol == B_TRANSPARENT_COLOR) {
                bgCol = ui_color(B_PANEL_BACKGROUND_COLOR); // Fetch Haiku system panel color standard
            }
            
            // Calculate perceived brightness using standard digital luma coefficients
            float bgLuminance = (bgCol.red * 0.299f) + (bgCol.green * 0.587f) + (bgCol.blue * 0.114f);

            if (bgLuminance > 128.0f) {
                // LIGHT THEME DETECTED: Apply a sharp, highly visible dark purple
                SetHighColor(75, 0, 130, 255); 
            } else {
                // DARK THEME DETECTED: Default to your classic radiant golden yellow
                SetHighColor(255, 215, 0, 255); 
            }
            
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

            // Dynamically tweak subtext color slightly for readability on bright screens too
            if (bgLuminance > 128.0f) {
                SetHighColor(80, 80, 80, 255); // Dark slate charcoal gray subtext
            } else {
                SetHighColor(200, 200, 200, 200); // Muted silver overlay
            }
            
            BString countStr;
            countStr.SetToFormat("Restarting in %d...", secondsLeft);
            DrawString(countStr.String(), BPoint(midPointX - 42.0f, (height / 2.0f) + 12.0f));
            
            // Revert font adjustments back to defaults for remaining passes
            SetFont(&scoreFont);
        } else {
            // Updated class member references to clean trailing variable mismatches
            fDrawTimerStarted = false;
            fDrawWinStartTime = 0;
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
        
        // Dynamically scale thickness (12.0f in fullscreen, 5.0f in window mode)
        float paddleWidth = isWindowInFullscreen ? 12.0f : 5.0f;
        
        // Render left paddle utilizing dynamic width variables
        FillRect(BRect(startX, fLeftPaddlePos - (paddleH / 2.0f), startX + paddleWidth, fLeftPaddlePos + (paddleH / 2.0f)));

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
        
        // Mirrors the right-side starting coordinate to perfectly align thicker paddles
        float rightPaddleStartX = startX + artworkWidth - paddleWidth;
        FillRect(BRect(rightPaddleStartX, fRightPaddlePos - (paddleH / 2.0f), startX + artworkWidth, fRightPaddlePos + (paddleH / 2.0f)));

        // --- LAYER: PROCEDURAL RADIAL ARC SHOCKWAVE EXPLOSION ---
        if (fPongExplosionTick > 0.0f) {
            // Normalize timeline progress safely against the 20-frame baseline to prevent scaling distortion
            float maxTicksBaseline = 20.0f * fDtScaleCached;
            if (maxTicksBaseline <= 0.0f) maxTicksBaseline = 1.0f;
            
            float animationProgress = 1.0f - (fPongExplosionTick / maxTicksBaseline);
            if (animationProgress < 0.0f) animationProgress = 0.0f;
            if (animationProgress > 1.0f) animationProgress = 1.0f;

            float maxRadiusBounds = 21.0f * 1.8f;
            float currentRadius = animationProgress * maxRadiusBounds;
            
            SetPenSize(2.0f); // Bold vector edges
            
            // Calculate fading alpha multiplier based on remaining animation lifetime
            uint8 alphaFade = (uint8)((fPongExplosionTick / maxTicksBaseline) * 240.0f);

            for (int ring = 0; ring < 3; ring++) {
                float ringRadius = currentRadius - (ring * 4.0f);
                if (ringRadius < 1.0f) continue;

                // Flash between flame-orange and neon-electric cyan
                if (ring % 2 == 0) {
                    SetHighColor(255, 90, 0, alphaFade); 
                } else {
                    SetHighColor(0, 240, 255, alphaFade);
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

        // --- PASS A: RENDER PRIMARY ACTIVE BALL 1 ---
        if (fMotoCrashTicks <= 0.0f) {
            // Multiply the baseline diameter by 2x if the window is currently in fullscreen
            float dynamicBallSize1 = isWindowInFullscreen ? (fBallSize * 2.0f) : fBallSize;

            rgb_color glowColor = fArtworkPalette[10]; 
            SetHighColor(glowColor.red, glowColor.green, glowColor.blue, 120);
            
            // Adjust radial glow calculations to use the up-scaled size bounds
            FillEllipse(BPoint(fBallX, fBallY), (dynamicBallSize1 / 2.0f) + 3.0f, (dynamicBallSize1 / 2.0f) + 3.0f);

            SetHighColor(255, 255, 255, 255);
            FillEllipse(BPoint(fBallX, fBallY), dynamicBallSize1 / 2.0f, dynamicBallSize1 / 2.0f);
        }

        // --- PASS B: RENDER CHAOTIC HAZARD BALL 2 ---
        if (fBallSize2 > 0.0f) {
            // Multiply the baseline diameter by 2x if the window is currently in fullscreen
            float dynamicBallSize2 = isWindowInFullscreen ? (fBallSize2 * 2.0f) : fBallSize2;

            rgb_color glowColor2 = fArtworkPalette[45]; 
            SetHighColor(glowColor2.red, glowColor2.green, glowColor2.blue, 120);
            
            // Adjust radial glow calculations to use the up-scaled size bounds
            FillEllipse(BPoint(fBallX2, fBallY2), (dynamicBallSize2 / 2.0f) + 3.0f, (dynamicBallSize2 / 2.0f) + 3.0f);

            SetHighColor(255, 0, 180, 255); // Radiant high-heat Magenta core
            FillEllipse(BPoint(fBallX2, fBallY2), dynamicBallSize2 / 2.0f, dynamicBallSize2 / 2.0f);
        }


        // --- PASS C: BONUS POINT ALPHA TEXT ALERT ---
        if (fBonusFlashTick > 0.0f) {
            SetDrawingMode(B_OP_ALPHA);
            
            rgb_color pinkColor = { 255, 0, 180, (uint8)(fBonusFlashAlpha * 255) };
            SetHighColor(pinkColor);
            
            float scale = be_plain_font->Size() / 12.0f;
            SetFont(be_bold_font);
            SetFontSize(14.0f * scale); 
            
            const char* bonusText = "+2 Bonus Ball Activatd!!";
            float textWidth = StringWidth(bonusText);
            
            float drawBoundsHeight = Bounds().Height();
            
            BPoint textPos;
            textPos.x = startX_cached + ((artworkWidth_cached - textWidth) / 2.0f);
            textPos.y = (drawBoundsHeight / 2.0f) - (25.0f * scale); 
            
            DrawString(bonusText, textPos);
        }

        SetDrawingMode(B_OP_COPY);
        SetPenSize(1.0f);
    }





 else if (fVisualizerMode == MODE_RAINDROPS) {
        // Mode 5: Audio-Reactive Falling Particle Rain Drops
        SetDrawingMode(B_OP_ALPHA);
        
        // HIGH-RES MODIFICATION: Dynamic sizing scalar for lines and points
        float rainScale = isWindowInFullscreen ? 2.5f : 1.0f;
        //float rainScale = isWindowInFullscreen ? 5.0f : 2.5f;
        SetPenSize(1.8f * rainScale);

        // Pull the safe volume tracking factor
        float volumeScale = gVolumeScaleFactor;

        float systemTimeSec = (float)system_time() / 1000000.0f;
        float pulseWave = (sinf(systemTimeSec * 4.5f) + 1.0f) / 2.0f; 
        float dynamicOpacityPct = 0.35f + (pulseWave * 0.50f); 

        for (int i = 0; i < 75; i++) {
            int frequencyIndex = (int)(fRainX[i] * 63.0f);
            if (frequencyIndex < 0) frequencyIndex = 0;
            if (frequencyIndex > 63) frequencyIndex = 63;
            
            // --- STRICT READ-ONLY DRIVE METRICS ---
            float audioDrive = (fBarHeights[frequencyIndex] / height) * volumeScale; 

            float currentX = startX + (fRainX[i] * artworkWidth);
            float currentY = fRainY[i] * height;

            rgb_color dropColor = fArtworkPalette[frequencyIndex];

            rgb_color transparentBlendedColor;
            transparentBlendedColor.red   = (uint8)(dropColor.red   * dynamicOpacityPct + bgCol.red   * (1.0f - dynamicOpacityPct));
            transparentBlendedColor.green = (uint8)(dropColor.green * dynamicOpacityPct + bgCol.green * (1.0f - dynamicOpacityPct));
            transparentBlendedColor.blue  = (uint8)(dropColor.blue  * dynamicOpacityPct + bgCol.blue  * (1.0f - dynamicOpacityPct));
            transparentBlendedColor.alpha = 255;

            // HIGH-RES MODIFICATION: Scale the rain drizzle tail length proportionally for larger screen heights
            float tailLength = (4.0f * rainScale) + (audioDrive * 12.0f * rainScale); 
            SetHighColor(transparentBlendedColor);
            StrokeLine(BPoint(currentX, currentY - tailLength), BPoint(currentX, currentY));
        }
        
        // --- LAYER: RENDER ACTIVE DETONATION SPARK PARTICLES ---
        SetPenSize(2.2f * rainScale); 
        float dynamicSparkSize = 2.0f * rainScale;

        for (int s = 0; s < 12; s++) {
            if (fSparkLife[s] > 0.0f) {
                // Flash high-contrast orange vs neon bright yellow spark clusters
                if (rand() % 100 > 45) {
                    SetHighColor(255, 65, 0, 255);   // High-heat Vermilion
                } else {
                    SetHighColor(255, 225, 10, 255);  // Retro Arcade Yellow
                }

                // Render directly onto coordinates now that math is uncoupled
                float sx = fSparkX[s];
                float sy = fSparkY[s];
                
                // HIGH-RES MODIFICATION: Expand pixel splash widths uniformly to remain punchy in fullscreen
                FillRect(BRect(sx, sy, sx + dynamicSparkSize, sy + dynamicSparkSize));
            }
        }
        
        SetDrawingMode(B_OP_COPY);
        SetPenSize(1.0f);
    }
    
    
    
    
    
		//@replicadraw @ACID_MELT
		else if (fVisualizerMode == MODE_REPLICA || fVisualizerMode == MODE_ACID_MELT) {
            // ================================================================
            // --- THEME-AWARE BASE REPLICANT CLEARING ENGINE ---
            // ================================================================
            // If the dark theme is disabled, use native Haiku panel gray instead 
            // of forcing a hardcoded dark rectangle backbuffer.
            if (cfg.uTheme == "Dark") {
                SetHighColor(40, 40, 40, 255);
            } else {
                SetHighColor(ui_color(B_PANEL_BACKGROUND_COLOR));
            }
            
            SetLowColor(HighColor());
            SetDrawingMode(B_OP_COPY);
            FillRect(updateRect);
        }





    
    // @neon
else if (fVisualizerMode == MODE_WERE_OPEN_NEON_SIGN) {
        SetDrawingMode(B_OP_ALPHA);

        float centerWindowX = startX + (artworkWidth / 2.0f);
        float centerWindowY = height / 2.0f;
        
        // --- 1. DETECT IF WINDOW IS IN FULLSCREEN MODE ---
        bool isWindowInFullscreen = false;
        if (Window() != nullptr) {
            SuperMusicWindow* mainWin = dynamic_cast<SuperMusicWindow*>(Window());
            if (mainWin != nullptr) {
                isWindowInFullscreen = mainWin->IsFullscreenActive();
            }
        }

        // --- 2. THEME-SAFE EXPLICIT CINEMATIC MULTIPLIER ---
        float signScaleFactor = 1.0f;
        
        if (isWindowInFullscreen) {
            // Fullscreen Mode: Calculate an absolute ratio based on screen size!
            signScaleFactor = (artworkWidth / 210.0f); 
            if (signScaleFactor > 8.05f) signScaleFactor = 8.5f; // Giant upper boundary for 4K setups
        } else {
            // Windowed Mode: Fall back safely to your original compact rules
            signScaleFactor = (artworkWidth / 320.0f);
            if (signScaleFactor > 1.2f)  signScaleFactor = 1.2f;
            if (signScaleFactor < 0.65f) signScaleFactor = 0.65f;
        }

        // Layout metrics for vector tube boxes
        float letterWidth = 40.0f * signScaleFactor;
        float letterPadding = 12.0f * signScaleFactor;
        float totalOpenWidth = (letterWidth * 4.0f) + (letterPadding * 3.0f);
        
        // Re-centered layout using our expanded dynamic vector metrics
        BPoint posOpen(centerWindowX - (totalOpenWidth / 2.0f), centerWindowY - (30.0f * signScaleFactor));

        // --- 2D ORGANIC SHIMMER ENGINE ---
        bigtime_t sysTime = system_time();
        const float basePeriodSeconds = 8.0f; 
        float timeSeconds = (float)sysTime / 1000000.0f;
        float wavePhase = (timeSeconds / basePeriodSeconds) * 2.0f * (float)M_PI;
        
        float shimmerCenterX = (0.5f + 0.5f * sinf(wavePhase)) * (float)numBars;
        float shimmerCenterY = (0.5f + 0.5f * cosf(wavePhase * 1.4f)) * height;

        const float shimmerHalfWidthX = 12.0f;
        const float shimmerHalfWidthY = height * 0.4f; 

        float bassDrive = fNeonBassSmooth;
        
        // Decoupled random visual noise ticks
        bool gasFlicker2 = (fNeonFlickerTimer2 > 0.0f) && ((rand() % 100) > 45);
        float auraPulseIntensity = 0.35f + (bassDrive * 0.65f);

        PushState();
        SetFlags(Flags() | B_SUBPIXEL_PRECISE);
        SetLineMode(B_ROUND_CAP, B_ROUND_JOIN);
        
        float openPulse = auraPulseIntensity * (gasFlicker2 ? 0.35f : 1.0f);

        // ================================================================
        // --- LAYER 0: PHYSICAL UNLIT "DEAD" GLASS TUBE SILHOUETTE ---
        // ================================================================
        SetPenSize(9.5f * signScaleFactor); 
        SetLineMode(B_ROUND_CAP, B_ROUND_JOIN); // HIGH-DEF LOCK

        for (int i = 0; i < 4; i++) {
            float letterX = posOpen.x + (i * (letterWidth + letterPadding));
            float letterCenterX = letterX + (letterWidth / 2.0f);

            float openSegmentIndex = (letterCenterX - startX) / (artworkWidth / (float)numBars);
            int openPaletteIdx = (int)((openSegmentIndex / (float)numBars) * 63.0f);
            if (openPaletteIdx < 0) openPaletteIdx = 0;
            if (openPaletteIdx >= numBars) openPaletteIdx = numBars - 1;
            rgb_color openLetterColor = fArtworkPalette[openPaletteIdx];

            rgb_color deadTubeColor;
            deadTubeColor.red   = (uint8)(openLetterColor.red   * 0.12f);
            deadTubeColor.green = (uint8)(openLetterColor.green * 0.12f);
            deadTubeColor.blue  = (uint8)(openLetterColor.blue  * 0.12f);
            deadTubeColor.alpha = 110; 

            SetHighColor(deadTubeColor);
            BShape letterShape = GenerateNeonLetterShape(i, BPoint(letterX, posOpen.y), signScaleFactor);
            StrokeShape(&letterShape);
        }
        
        // ================================================================
        // --- LAYER 1: BACKDROP AMBIENT WALL GLOW ---
        // ================================================================            
        SetPenSize(26.0f * signScaleFactor); 
        SetLineMode(B_ROUND_CAP, B_ROUND_JOIN); // HIGH-DEF LOCK

        for (int i = 0; i < 4; i++) {
            float letterX = posOpen.x + (i * (letterWidth + letterPadding));
            float letterCenterX = letterX + (letterWidth / 2.0f);
            float letterCenterY = posOpen.y + (30.0f * signScaleFactor);

            float horizontalRatio = (letterCenterX - startX) / artworkWidth;
            if (horizontalRatio < 0.0f) horizontalRatio = 0.0f;
            if (horizontalRatio > 1.0f) horizontalRatio = 1.0f;
            
            int openPaletteIdx = (int)(horizontalRatio * 63.0f);
            rgb_color openLetterColor = fArtworkPalette[openPaletteIdx];

            float openSegmentIndex = horizontalRatio * (float)numBars;
            float openDistX = fabsf(openSegmentIndex - shimmerCenterX);
            float openDistY = fabsf(letterCenterY - shimmerCenterY);
            float openShimmerIntensity = 0.0f;

            if (openDistX < shimmerHalfWidthX && openDistY < shimmerHalfWidthY) {
                float normX = openDistX / shimmerHalfWidthX;
                float intensityX = 0.5f * (1.0f + cosf(normX * (float)M_PI));
                float normY = openDistY / shimmerHalfWidthY;
                float intensityY = 0.5f * (1.0f + cosf(normY * (float)M_PI));
                openShimmerIntensity = intensityX * intensityY;
            }
            int16 openLightBoost = 25 + (int16)(75.0f * openShimmerIntensity);

            rgb_color letterAuraColor = openLetterColor;
            letterAuraColor.red   = (uint8)min_c(255, letterAuraColor.red   + (openLightBoost / 2));
            letterAuraColor.green = (uint8)min_c(255, letterAuraColor.green + (openLightBoost / 2));
            letterAuraColor.blue  = (uint8)min_c(255, letterAuraColor.blue  + (openLetterColor.blue  * 0.0f)); 
            letterAuraColor.alpha = (uint8)(50.0f * openPulse);

            SetHighColor(letterAuraColor);
            BShape letterShape = GenerateNeonLetterShape(i, BPoint(letterX, posOpen.y), signScaleFactor);
            StrokeShape(&letterShape);
        }

        // ================================================================
        // --- LAYER 2: INTERMEDIATE MAIN COLOR GAS TUBE ---
        // ================================================================
        SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
        SetPenSize(9.5f * signScaleFactor); 
        SetLineMode(B_ROUND_CAP, B_ROUND_JOIN); // HIGH-DEF LOCK
        
        for (int i = 0; i < 4; i++) {
            float letterX = posOpen.x + (i * (letterWidth + letterPadding));
            float letterCenterX = letterX + (letterWidth / 2.0f);
            float letterCenterY = posOpen.y + (30.0f * signScaleFactor);

            float horizontalRatio = (letterCenterX - startX) / artworkWidth;
            if (horizontalRatio < 0.0f) horizontalRatio = 0.0f;
            if (horizontalRatio > 1.0f) horizontalRatio = 1.0f;
            
            int openPaletteIdx = (int)(horizontalRatio * 63.0f);
            rgb_color openLetterColor = fArtworkPalette[openPaletteIdx];

            float openSegmentIndex = horizontalRatio * (float)numBars;
            float openDistX = fabsf(openSegmentIndex - shimmerCenterX);
            float openDistY = fabsf(letterCenterY - shimmerCenterY);
            float openShimmerIntensity = 0.0f;

            if (openDistX < shimmerHalfWidthX && openDistY < shimmerHalfWidthY) {
                float normX = openDistX / shimmerHalfWidthX;
                float intensityX = 0.5f * (1.0f + cosf(normX * (float)M_PI));
                float normY = openDistY / shimmerHalfWidthY;
                float intensityY = 0.5f * (1.0f + cosf(normY * (float)M_PI));
                openShimmerIntensity = intensityX * intensityY;
            }
            int16 openLightBoost = 25 + (int16)(75.0f * openShimmerIntensity);

            rgb_color letterCoreColor = openLetterColor;
            letterCoreColor.red   = (uint8)min_c(255, letterCoreColor.red   + openLightBoost);
            letterCoreColor.green = (uint8)min_c(255, letterCoreColor.green + openLightBoost);
            letterCoreColor.blue  = (uint8)min_c(255, letterCoreColor.blue  + openLightBoost);
            letterCoreColor.alpha = gasFlicker2 ? (uint8)45 : (uint8)215;

            SetHighColor(letterCoreColor);
            BShape letterShape = GenerateNeonLetterShape(i, BPoint(letterX, posOpen.y), signScaleFactor);
            StrokeShape(&letterShape);
        }

        // ================================================================
        // --- LAYER 3: INNER HOT CENTER FILAMENTS ---
        // ================================================================
        SetPenSize(2.8f * signScaleFactor); 
        SetLineMode(B_ROUND_CAP, B_ROUND_JOIN); // HIGH-DEF LOCK
        uint8 whiteAlphaValue = gasFlicker2 ? 80 : 190;

        for (int i = 0; i < 4; i++) {
            float letterX = posOpen.x + (i * (letterWidth + letterPadding));
            float letterCenterX = letterX + (letterWidth / 2.0f);

            float horizontalRatio = (letterCenterX - startX) / artworkWidth;
            if (horizontalRatio < 0.0f) horizontalRatio = 0.0f;
            if (horizontalRatio > 1.0f) horizontalRatio = 1.0f;
            
            int openPaletteIdx = (int)(horizontalRatio * 63.0f);
            rgb_color openLetterColor = fArtworkPalette[openPaletteIdx];

            rgb_color blendedFilament;
            blendedFilament.red   = (uint8)((openLetterColor.red   * 0.35f) + (255.0f * 0.65f));
            blendedFilament.green = (uint8)((openLetterColor.green * 0.35f) + (255.0f * 0.65f));
            blendedFilament.blue  = (uint8)((openLetterColor.blue  * 0.35f) + (255.0f * 0.65f));
            blendedFilament.alpha = whiteAlphaValue;

            SetHighColor(blendedFilament);
            BShape letterShape = GenerateNeonLetterShape(i, BPoint(letterX, posOpen.y), signScaleFactor);
            StrokeShape(&letterShape);
        }

        PopState();
        SetDrawingMode(B_OP_COPY);
        
        SetLineMode(B_BUTT_CAP, B_MITER_JOIN, B_DEFAULT_MITER_LIMIT);
        SetPenSize(1.0f);
    }


    
    
    
 else if (fVisualizerMode == MODE_MOTO_RIDER) {			
        // Mode 6: Endless Motorcycle Runner with Parallax & Scoreboard Display
        SetDrawingMode(B_OP_ALPHA);
        float baselineY = height - 2.0f; 
        float bgBrightness = (bgCol.red * 0.299f) + (bgCol.green * 0.587f) + (bgCol.blue * 0.114f); 
        bool isDarkBg = (bgBrightness < 100.0f);
        
        // Fetch live audio vars to match the physics thread scaling calculations exactly
        float lowBassPulse = (fBarHeights[2] + fBarHeights[3] + fBarHeights[4]) / 3.0f; 
        float bassNormalized = (height > 0.0f) ? (lowBassPulse / height) : 0.0f;  

        // HIGH-RES MODIFICATION: Scale global world thickness and landscape layout widths
        float worldScale = isWindowInFullscreen ? 2.2f : 1.0f;
        float mtnStepWidth = isWindowInFullscreen ? 540.0f : 240.0f;
        float mtnHalfWidth = mtnStepWidth / 2.0f;
                   
        // --- LAYER 0: SKY RESIDENT LAYER (Drifting Clouds) ---
        SetPenSize(isWindowInFullscreen ? 2.5f : 1.0f); 
        SetHighColor(isDarkBg ? rgb_color{110, 125, 140, 120} : rgb_color{200, 205, 210, 150});
        for (int c = 0; c < 3; c++) {
            // Scale out the cloud tracking index speed dynamically on massive screens
            float cx = startX + (fCloudX[c] * (isWindowInFullscreen ? 1.5f : 1.0f)); 
            float cy = fCloudY[c] * (isWindowInFullscreen ? 1.8f : 1.0f); // Push clouds higher up in fullscreen
            float cw = fCloudSize[c] * worldScale;
            
            FillEllipse(BPoint(cx, cy), cw / 2.0f, 3.5f * worldScale); 
            FillEllipse(BPoint(cx + (cw * 0.2f), cy - (2.0f * worldScale)), cw / 3.0f, 3.0f * worldScale); 
            FillEllipse(BPoint(cx - (cw * 0.2f), cy - (1.0f * worldScale)), cw / 3.5f, 2.5f * worldScale);
        }   
                  
        // --- LAYER 1: DISTANT PARALLAX MOUNTAINS (DYNAMIC STEPS FOR WIDESCREEN) ---
        for (int m = 0; m < 4; m++) {
            // Read custom sizing profile scalar set inside your math loop
            float currentMtnScale = (fMtnHeightScale[m] > 0.01f) ? fMtnHeightScale[m] : (0.8f + (m * 0.15f));
            float peakHeight = 26.0f * currentMtnScale * worldScale;    
                        
            // Dynamic width step prevents mountains from tearing apart or showing gaps in fullscreen
            float mx = fMtnScrollX + (m * mtnStepWidth);                
            BPoint triangle[3] = {
                BPoint(startX + mx, baselineY),
                BPoint(startX + mx + mtnHalfWidth, baselineY - peakHeight), 
                BPoint(startX + mx + mtnStepWidth, baselineY)
            };                
            // Base Solid Mountain Color
            SetHighColor(isDarkBg ? rgb_color{55, 68, 82, 255} : rgb_color{190, 198, 205, 255}); 
            FillPolygon(triangle, 3);      
                       
            // Outer Structural Depth Accent Edging Line
            SetHighColor(isDarkBg ? rgb_color{85, 100, 115, 255} : rgb_color{165, 175, 185, 255}); 
            SetPenSize(1.2f * worldScale); 
            StrokePolygon(triangle, 3);                
        }
                     
        // --- LAYER 2: MIDGROUND LAYER (Random Green Stick Trees) ---
        SetHighColor(35, 155, 75, 255); 
        SetPenSize(1.5f * worldScale);   
        for (int t = 0; t < 4; t++) {
            // Adjust scrolling width tracking step for fullscreen
            float tx = startX + (fTreeX[t] * (isWindowInFullscreen ? 1.5f : 1.0f)); 
            float th = fTreeHeight[t] * worldScale;
            
            StrokeLine(BPoint(tx, baselineY), BPoint(tx, baselineY - th));
            StrokeLine(BPoint(tx, baselineY - th), BPoint(tx - (4.0f * worldScale), baselineY - th + (5.0f * worldScale))); 
            StrokeLine(BPoint(tx, baselineY - th), BPoint(tx + (4.0f * worldScale), baselineY - th + (5.0f * worldScale)));
            StrokeLine(BPoint(tx, baselineY - th + (4.0f * worldScale)), BPoint(tx - (6.0f * worldScale), baselineY - th + (10.0f * worldScale))); 
            StrokeLine(BPoint(tx, baselineY - th + (4.0f * worldScale)), BPoint(tx + (6.0f * worldScale), baselineY - th + (10.0f * worldScale)));
        }
                         
        // --- LAYER 3: LIVE GAME GROUND RUNNER horizon tracks ---
        SetHighColor(isDarkBg ? rgb_color{80, 90, 100, 255} : rgb_color{180, 185, 190, 255}); 
        SetPenSize(2.0f * worldScale);  
        StrokeLine(BPoint(startX, baselineY), BPoint(startX + artworkWidth, baselineY)); 
        SetHighColor(bgCol); 
        for (int o = 0; o < 2; o++) {
            float dynamicObsX = startX + (fObsX[o] * (isWindowInFullscreen ? 1.5f : 1.0f));
            // If the obstacle is either a pit (1) or water pocket (2), carve out the dynamic gap width
            if (fObsIsPit[o] == 1 || fObsIsPit[o] == 2) {
                StrokeLine(BPoint(dynamicObsX + 1.0f, baselineY), BPoint(dynamicObsX + (23.0f * worldScale), baselineY));
            }
        }

                    
        // --- LAYER 4: MULTI-HAZARD DRAW ENGINES (Rocks, Pits, Water Blue Pools, & Spikes) ---
        int32 themeColorIndex = 20; 
        
        // HIGH-RES MODIFICATION: Dynamic sizing scalar for obstacles
        float hazardScale = isWindowInFullscreen ? 2.5f : 1.0f;
        float hazardLinePen = isWindowInFullscreen ? 2.5f : 1.0f;

        for (int o = 0; o < 2; o++) {
            // Scale horizontal movement metric to handle the expanded display width
            float dynamicObsX = startX + (fObsX[o] * (isWindowInFullscreen ? 1.5f : 1.0f));

            if (fObsIsPit[o] == 0) {
                // --- HAZARD TYPE 0: SOLID ROCK BLOCK ---
                float audioGrowthFactor = 1.0f;
                if (o == 1) { 
                    audioGrowthFactor += (bassNormalized * 1.6f); 
                }                    
                float finalObsHeight = 10.0f * fObsHeightScale[o] * audioGrowthFactor * hazardScale; 
                float baseWidth = (fObsHeightScale[o] < 0.9f) ? 7.0f : ((fObsHeightScale[o] > 1.2f) ? 5.0f : 10.0f);                    
                float obsWidth = baseWidth * hazardScale;

                BRect rockBounds(dynamicObsX, baselineY - finalObsHeight, dynamicObsX + obsWidth, baselineY);  
                SetHighColor(fArtworkPalette[themeColorIndex]);
                FillRect(rockBounds);                       
                if (fObsHeightScale[o] > 1.2f) {
                    SetHighColor(240, 70, 70, 255); 
                    FillRect(BRect(dynamicObsX, baselineY - finalObsHeight, dynamicObsX + obsWidth, baselineY - finalObsHeight + (2.0f * hazardScale)));
                } 
                                   
                // CRITICAL VISIBILITY CORRECTION: Thicken safety lines on big screens
                SetPenSize(hazardLinePen);
                SetHighColor(isDarkBg ? rgb_color{255, 255, 255, 220} : rgb_color{0, 0, 0, 220});
                StrokeRect(rockBounds);                    
            } else if (fObsIsPit[o] == 1) {
                // --- HAZARD TYPE 1: EMPTY GROUND PIT GAP ---
                SetHighColor(fArtworkPalette[themeColorIndex]);
                FillRect(BRect(dynamicObsX - (2.0f * hazardScale), baselineY - (3.0f * hazardScale), dynamicObsX, baselineY));
                FillRect(BRect(dynamicObsX + (24.0f * hazardScale), baselineY - (3.0f * hazardScale), dynamicObsX + (26.0f * hazardScale), baselineY));   
                                 
                // Add high contrast neon warning trim to edges
                SetPenSize(hazardLinePen * 1.5f);
                SetHighColor(255, 60, 60, 255);
                StrokeLine(BPoint(dynamicObsX, baselineY), BPoint(dynamicObsX + (24.0f * hazardScale), baselineY));                    
            } else if (fObsIsPit[o] == 2) {
                // --- HAZARD TYPE 2: NEON WATER POOL (BLUE OBSTACLE) ---
                BRect waterBounds(dynamicObsX, baselineY + 1.0f, dynamicObsX + (24.0f * hazardScale), baselineY + (6.0f * hazardScale));                    
                SetHighColor(0, 130, 255, 255); 
            
                // Rich deep hazard blue pool fill
                FillRect(waterBounds);                    
                SetHighColor(0, 240, 255, 255); 
            
                // Radiant glowing surface layer line
                SetPenSize(hazardLinePen);
                StrokeLine(BPoint(dynamicObsX, baselineY + 1.0f), BPoint(dynamicObsX + (24.0f * hazardScale), baselineY + 1.0f));                    
            
                // Safety shoreline markers
                SetHighColor(isDarkBg ? rgb_color{255, 255, 255, 180} : rgb_color{0, 0, 0, 180});
                StrokeLine(BPoint(dynamicObsX, baselineY), BPoint(dynamicObsX, baselineY + (4.0f * hazardScale)));
                StrokeLine(BPoint(dynamicObsX + (24.0f * hazardScale), baselineY), BPoint(dynamicObsX + (24.0f * hazardScale), baselineY + (4.0f * hazardScale)));
            } else if (fObsIsPit[o] == 3 || fObsIsPit[o] == 4) {
                // --- HAZARD TYPE 3 & 4: PULSING SHARP SHOCK-SPIKE BLADES ---
                int spikeCount = (fObsIsPit[o] == 3) ? 4 : 5;
                float spikeWidth = 8.0f * hazardScale;
                float spikeAudioScale = 1.0f + (bassNormalized * 1.2f);

                for (int sIdx = 0; sIdx < spikeCount; sIdx++) {
                    float leftX = dynamicObsX + (sIdx * spikeWidth);
                    float rightX = leftX + spikeWidth;
                    float centerX = leftX + (spikeWidth / 2.0f);

                    // Match physical sizing structure profile equations precisely
                    float sizeVariation = 0.7f + (((sIdx * 3) % 5) / 6.0f);
                    float currentSpikeHeight = 7.0f * fObsHeightScale[o] * sizeVariation * spikeAudioScale * hazardScale;
                    float tipY = baselineY - currentSpikeHeight;

                    // Solid Semi-Translucent Triangle Body Fill
                    SetHighColor(255, 90, 0, 95); 
                    BPoint spikeTri[3] = { BPoint(leftX, baselineY), BPoint(centerX, tipY), BPoint(rightX, baselineY) };
                    FillPolygon(spikeTri, 3);

                    // High Contrast Solid Accent Border Outlines
                    SetPenSize(hazardLinePen);
                    SetHighColor(isDarkBg ? rgb_color{255, 110, 50, 240} : rgb_color{210, 40, 10, 240});
                    StrokeLine(BPoint(leftX, baselineY), BPoint(centerX, tipY));
                    StrokeLine(BPoint(centerX, tipY), BPoint(rightX, baselineY));
                }
            }
        }
                    

		// --- LAYER 5: SCOREBOARD TRACKING DISPLAY TEXT ---
		BFont scoreFont;   
		GetFont(&scoreFont);
		
		// HIGH-RES Scale font size up (22.0f in fullscreen, 11.0f in window mode)
		float dynamicScoreSize = isWindowInFullscreen ? 22.0f : 11.0f;
		scoreFont.SetSize(dynamicScoreSize); 
		SetFont(&scoreFont);            
		
		SetHighColor(isDarkBg ? rgb_color{0, 240, 255, 200} : rgb_color{50, 60, 70, 220});
		BString scoreStr;
		scoreStr.SetToFormat("SCORE: %" B_PRId32, fMotoScore);
		
		// HIGH-RES Move layout margins out from screen boundaries depending on fullscreen states
		float scoreDrawX = startX + artworkWidth - (isWindowInFullscreen ? 140.0f : 68.0f);
		float scoreDrawY = isWindowInFullscreen ? 30.0f : 15.0f;
		DrawString(scoreStr.String(), BPoint(scoreDrawX, scoreDrawY));  

		// --- LAYER 5B: UNCLIPPED CENTER-SCREEN STUNT POPUP ---
		if (fStuntTextLife > 0.0f) {
    		BFont stuntFont;
    		GetFont(&stuntFont);
            
    		// HIGH-RES Scale the bold stunt popups cleanly up to 26.0f in fullscreen
    		stuntFont.SetSize(isWindowInFullscreen ? 26.0f : 13.0f); 
    		stuntFont.SetFace(B_BOLD_FACE);
    		SetFont(&stuntFont);
    
    		// --- THEME-ADAPTIVE HIGH-CONTRAST COLOR SWITCH pass ---
    		float maxStuntTextLife = 35.0f * fDtScaleCached;
    		if (maxStuntTextLife <= 0.0f) maxStuntTextLife = 1.0f;
    		float alphaFadePct = fStuntTextLife / maxStuntTextLife;
    
    		if (isDarkBg) {
        		SetHighColor(255, 215, 0, (uint8)(alphaFadePct * 252.0f)); 
    		} else {
        		SetHighColor(0, 185, 20, (uint8)(alphaFadePct * 252.0f)); 
    		}

    		// Mathematical String Width Center Offset Calculation
    		float stringWidth = stuntFont.StringWidth(fStuntTextStr.String());
    		float screenCenterX = startX + (artworkWidth / 2.0f);
    
    		// HIGH-RES Scale vertical trajectory coordinate position mapping to prevent overlap
    		float dynamicStuntY = isWindowInFullscreen ? (fStuntTextY * 2.2f) : fStuntTextY;
    		DrawString(fStuntTextStr.String(), BPoint(screenCenterX - (stringWidth / 2.0f), dynamicStuntY));
    
    		// Restore font to clear trailing canvas modifications
    		SetFont(&scoreFont);
		}

		// --- LAYER 5C: BACKGROUND SCROLLING VECTOR DOG RENDERING (FACING RIGHT) ---
		if (fDogDrawActive) {
    		SetDrawingMode(B_OP_ALPHA);
    		SetHighColor(255, 255, 255, 210); 
    
    		// HIGH-RES Introduce a 2.5x spatial structural scale factor in fullscreen
    		float dogScale = isWindowInFullscreen ? 2.5f : 1.0f;
    		float dogFloorY = baselineY - (5.0f * dogScale) - (fDogDrawY * dogScale); 
    		float dynamicDogX = startX + (fDogDrawX * (isWindowInFullscreen ? 1.5f : 1.0f));

    		// Draw main body torso (Multiplied by dogScale coordinates)
    		FillRect(BRect(
                dynamicDogX - (7.0f * dogScale), 
                dogFloorY - (4.0f * dogScale), 
                dynamicDogX + (7.0f * dogScale), 
                dogFloorY + (3.0f * dogScale)
            ));
    
    		// FIXED ORIENTATION: Draw head block on the RIGHT because he's running right!
    		FillRect(BRect(
                dynamicDogX + (4.0f * dogScale), 
                dogFloorY - (9.0f * dogScale), 
                dynamicDogX + (11.0f * dogScale), 
                dogFloorY - (3.0f * dogScale)
            ));
    
    		// Draw legs
    		FillRect(BRect(dynamicDogX - (5.0f * dogScale), dogFloorY + (3.0f * dogScale), dynamicDogX - (3.0f * dogScale), dogFloorY + (8.0f * dogScale))); // Back Leg
    		FillRect(BRect(dynamicDogX + (3.0f * dogScale), dogFloorY + (3.0f * dogScale), dynamicDogX + (5.0f * dogScale), dogFloorY + (8.0f * dogScale))); // Front Leg
    
    		// Animated fast wagging tail (now pointing LEFT since he runs right)
    		SetPenSize(isWindowInFullscreen ? 3.5f : 1.5f);
    
    		bigtime_t currentClock = system_time();
    		bool tailOscillation = ((currentClock / 120000) % 2 == 0); // Fast 120ms real-world tail wag frequency
    
    		if (tailOscillation) {
        		StrokeLine(BPoint(dynamicDogX - (7.0f * dogScale), dogFloorY - (2.0f * dogScale)), BPoint(dynamicDogX - (11.0f * dogScale), dogFloorY - (6.0f * dogScale)));
    		} else {
        		StrokeLine(BPoint(dynamicDogX - (7.0f * dogScale), dogFloorY - (2.0f * dogScale)), BPoint(dynamicDogX - (12.0f * dogScale), dogFloorY - (2.0f * dogScale)));
    		}
		}

        // --- LAYER 6: MOTORCYCLE RIDER VEHICLE BODY & FLIP MECHANIC ---
        // HIGH-RES MODIFICATION: Line weights and overall sprite scaling transformations
        float bikeScale = isWindowInFullscreen ? 2.5f : 1.0f;
        SetPenSize(isWindowInFullscreen ? 3.5f : 1.5f);

        // Adjust anchor rendering targets so the larger model floats cleanly above ground tracks
        float riderX = startX + (isWindowInFullscreen ? 112.0f : 45.0f); 
        float riderY = baselineY - (fMotoY * bikeScale) - (isWindowInFullscreen ? 15.0f : 6.0f);

        if (fMotoCrashTicks > 0.0f) {
            SetHighColor(240, 70, 70, 255);
            StrokeLine(BPoint(riderX - (8.0f * bikeScale), baselineY - (4.0f * bikeScale)), BPoint(riderX + (8.0f * bikeScale), baselineY - (8.0f * bikeScale)));
            StrokeLine(BPoint(riderX - (4.0f * bikeScale), baselineY - (10.0f * bikeScale)), BPoint(riderX + (6.0f * bikeScale), baselineY - (4.0f * bikeScale)));
        } else {
            // Compute rotation transformation matrices if mid-air stunt is active
            float rad = fFlipRotation * (M_PI / 180.0f);
            float cosR = cosf(rad);
            float sinR = sinf(rad);

            SetHighColor(isDarkBg ? rgb_color{255, 255, 255, 255} : rgb_color{0, 0, 0, 255});

            // Calculate relative rotated offsets for bike frame points scaled to core body center
            BPoint frameLeft(riderX + ((-10.0f * cosR - 0.0f * sinR) * bikeScale), riderY + ((-10.0f * sinR + 0.0f * cosR) * bikeScale));
            BPoint frameRight(riderX + ((10.0f * cosR - (-2.0f) * sinR) * bikeScale), riderY + ((10.0f * sinR + (-2.0f) * cosR) * bikeScale));
            StrokeLine(frameLeft, frameRight);
            
            BPoint neckBase(riderX + ((6.0f * cosR - (-2.0f) * sinR) * bikeScale), riderY + ((6.0f * sinR + (-2.0f) * cosR) * bikeScale));
            BPoint handlebars(riderX + ((8.0f * cosR - (-9.0f) * sinR) * bikeScale), riderY + ((8.0f * sinR + (-9.0f) * cosR) * bikeScale));
            StrokeLine(neckBase, handlebars);

            // Sparks stream dynamically out from the rotated exhaust tailpipe placement
            BPoint tailpipe(riderX + ((-10.0f * cosR - 1.0f * sinR) * bikeScale), riderY + ((-10.0f * sinR + 1.0f * cosR) * bikeScale));
                
            // ====================================================================
            // FIXED: PURE RENDERING ENGINE DRAW PASS FOR BACKFIRE SPARKS (DRAW)
            // ====================================================================
            float sparkDotSize = isWindowInFullscreen ? 3.0f : 1.5f;
            for (int s = 0; s < 12; s++) {
                if (fSparkLife[s] > 0.0f) {
                    // Flashes random high-contrast arcade colors
                    SetHighColor(255, rand() % 80 + 150, (rand() % 100 > 50) ? 0 : 255, 255); 
                    
                    // Explicitly factor in the scaled and rotated tailpipe coordinates
                    float sx = tailpipe.x + (fSparkX[s] * bikeScale);
                    float sy = tailpipe.y + (fSparkY[s] * bikeScale);
                    
                    FillRect(BRect(sx, sy, sx + sparkDotSize, sy + sparkDotSize));
                }
            }
            // ====================================================================
            
            // Rotated Wheel Positions (Scaled dynamically)
            BPoint frontWheel(riderX + ((8.0f * cosR - 4.0f * sinR) * bikeScale), riderY + ((8.0f * sinR + 4.0f * cosR) * bikeScale));
            BPoint backWheel(riderX + ((-8.0f * cosR - 4.0f * sinR) * bikeScale), riderY + ((-8.0f * sinR + 4.0f * cosR) * bikeScale));

            float dynamicWheelRadius = 4.0f * bikeScale;
            SetHighColor(fArtworkPalette[4]);
            StrokeEllipse(backWheel, dynamicWheelRadius, dynamicWheelRadius);                
            SetHighColor(fArtworkPalette[58]);
            StrokeEllipse(frontWheel, dynamicWheelRadius, dynamicWheelRadius); 
            
            // Rotated Driver Head and Extremities
            BPoint driverHead(riderX + ((0.0f * cosR - (-14.0f) * sinR) * bikeScale), riderY + ((0.0f * sinR + (-14.0f) * cosR) * bikeScale));
            BPoint driverSpine(riderX + ((0.0f * cosR - (-11.0f) * sinR) * bikeScale), riderY + ((0.0f * sinR + (-11.0f) * cosR) * bikeScale));
            BPoint driverHip(riderX + ((-2.0f * cosR - (-4.0f) * sinR) * bikeScale), riderY + ((-2.0f * sinR + (-4.0f) * cosR) * bikeScale));
            
            SetHighColor(isDarkBg ? rgb_color{0, 240, 255, 255} : rgb_color{20, 30, 40, 255});
            FillEllipse(driverHead, 2.5f * bikeScale, 2.5f * bikeScale); 
            StrokeLine(driverSpine, driverHip); 
            
            BPoint driverFoot(riderX + ((-6.0f * cosR - 0.0f * sinR) * bikeScale), riderY + ((-6.0f * sinR + 0.0f * cosR) * bikeScale));
            StrokeLine(driverHip, driverFoot); 
            
            BPoint handlebarsGrip(riderX + ((6.0f * cosR - (-7.0f) * sinR) * bikeScale), riderY + ((6.0f * sinR + (-7.0f) * cosR) * bikeScale));
            StrokeLine(driverHip, handlebarsGrip); 
        }
        
        SetDrawingMode(B_OP_COPY);
        SetPenSize(1.0f);
    }
}


void UpdateData(const uint8* data, size_t size) {
    if (!cfg.showSpectrumVisuals || !cfg.eqEnabled) return;
    memcpy(frequencyData, data, size > 64 ? 64 : size);
    // Explicitly thread-safe Invalidate handles view updates independently from your Pulse calculations
    Invalidate();
}

private:
    // New members for MODE_REPLICA
    BBitmap* fMeltOffsetsBitmap; // Pointer to safely back up bRimage if needed
    BBitmap* bRimage;            // The snapshot image pointer

    double    fCurrentLevel; 
    uint8     frequencyData[64]; 
    float     fBarHeights[64];   
    float     fBarVelocities[64];
    float     fPeakHeights[64];  
    float     fPeakHold[64];     // Changed from int to float for smooth delta-time decay
    rgb_color fArtworkPalette[64];
    bigtime_t fLastDataTime;    
    int32     fVisualizerMode; 
    
    // Cached Layout Geometry Variables
    float     fCachedWidth;
    float     fCachedStartX;

    // Pong Engine State Storage Values
    float     fBallX;
    float     fBallY;
    float     fBallDX;
    float     fBallDY;
    float     fBallSize;
    
    float     fBallX2;
    float     fBallY2;
    float     fBallDX2;
    float     fBallDY2;
    float     fBallSize2;
    
    
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
    float     fMotoCrashTicks; 

    // Obstacle Trackers (Array Size 2)
    float     fObsX[2];
    int       fObsIsPit[2]; 
    float     fObsHeightScale[2];
    float     fMtnHeightScale[4]; 
    
    // Scoreboard Tracker
    int32     fMotoScore;

    // Parallax Background Storage Objects
    float     fMtnScrollX;
    float     fTreeX[4];
    float     fTreeHeight[4];
    float     fCloudX[3];
    float     fCloudY[3];
    float     fCloudSize[3];

    float     fSparkX[12];       
    float     fSparkY[12];
    float     fSparkDX[12];      
    float     fSparkDY[12];      
    float     fSparkLife[12];   
    
    // Latency cache fix
    double    fLevelHistory[512];
    bigtime_t fTimeHistory[512];
    int       fHistoryHead = 0;
    int       fHistoryTail = 0;
    bigtime_t fAudioHardwareDelayUs = 130000; 
    bigtime_t fManualSyncOffsetUs = 0; 
	
    float     fPongExplosionTick = 0.0f;
    float     fPongExplosionX = 0.0f;
    float     fPongExplosionY = 0.0f;
	
    bool      fIsFlipping;
    float     fFlipRotation;
    bigtime_t fLastClickTime;
    
    float     fStuntTextY;       
    float     fStuntTextLife;    
    BString   fStuntTextStr;   

    bool      fDogDrawActive;
    float     fDogDrawX;
    float     fDogDrawY;

    float     fNeonBassSmooth;    
    float     fNeonTrebleSmooth;  
    float     fNeonFlickerTimer1; 
    float     fNeonFlickerTimer2; 
    
    bigtime_t fWinStartTime = 0;       // Maps back to game victory sequence anchor
    bool      fDrawTimerStarted = false; 
    bigtime_t fDrawWinStartTime = 0;
    
    float fDtScaleCached = 1.0f; // Stores the current frame timing step factor globally

    bigtime_t fPrevFrameTime = 0; // Track system clock across frames for dynamic delta-time calculations
    
    // Added persistent tracking state members for the +2 Bonus Ball alert system
    float fBonusFlashTick;
    float fBonusFlashAlpha;
    ReplicaOverlayWindow* fReplicaWin; 
    AcidMeltingWindow*    fAcidWin;   
};


// @spectrum
SpectrumView* FindSpectrumViewRecursive(BView* parent) {
    if (parent == nullptr) return nullptr;
    
    // Attempt to dynamically cast the view to your specific class type
    SpectrumView* specView = dynamic_cast<SpectrumView*>(parent);
    if (specView != nullptr) return specView;
    
    // Walk down the child tree elements recursively
    int32 count = parent->CountChildren();
    for (int32 i = 0; i < count; i++) {
        SpectrumView* found = FindSpectrumViewRecursive(parent->ChildAt(i));
        if (found != nullptr) return found;
    }
    return nullptr;
}

class RadialVolumeControl : public BControl {
public:
    RadialVolumeControl(BRect frame, const char* name, BMessage* msg, int32 multiplier = 2)
        : BControl(frame, name, NULL, msg, B_FOLLOW_NONE, B_WILL_DRAW | B_NAVIGABLE),
          fMultiplier(multiplier),
          fVolCenterIcon(NULL),
          fTickIcon(NULL),
          fIsMuted(false),
          fIsHovered(false),      
          fHoverAlpha(0.0f),      
          fLastTime(system_time()),
          fHasAlbumArtPalette(false) // <-- Initialize flag
    {
        SetViewColor(B_TRANSPARENT_COLOR);
        SetValue(100);
        
        SetFlags(Flags() | B_POINTER_EVENTS);

        // Center View Icon (Rendered clean at 32x32)      
        fVolCenterIcon = new BBitmap(BRect(0, 0, 40, 40), B_RGBA32);
        if (BIconUtils::GetVectorIcon(kIconSpeaker, kIconSpeakerSize, fVolCenterIcon) != B_OK) {
            delete fVolCenterIcon; 
            fVolCenterIcon = NULL;
        }



        // Initialize default backup palette colors
        ResetPalette();
    }

    virtual ~RadialVolumeControl() {
        delete fVolCenterIcon;
        delete fTickIcon;
    }

    // --- ALBUM ART COLOR ADAPTATION FUNCTION ---
    void AdaptToAlbumArt(BBitmap* artBitmap) {
        if (artBitmap == nullptr || artBitmap->InitCheck() != B_OK) {
            ResetPalette();
            return;
        }        
        
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
            
            if (targetPixelX >= width) targetPixelX = width - 1;
            if (targetPixelX < 0) targetPixelX = 0;

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
        
        fHasAlbumArtPalette = true;
        Invalidate();
    }



    // Helper to snap back to the classic high-visibility green look
    void ResetPalette() {
        fHasAlbumArtPalette = false;
        for (int i = 0; i < 64; i++) {
            fArtworkPalette[i] = {65, 255, 75, 255}; // Classic neonLime base
        }
        Invalidate();
    }

    virtual BSize MinSize() override { return BSize(76, 76); }
    virtual BSize MaxSize() override { return BSize(88, 88); }
    virtual BSize PreferredSize() override { return BSize(80, 80); }

    BRect CenterIconRect() const {
        if (!fVolCenterIcon) return BRect();
        float cx = Bounds().Width() / 2.0f;
        float cy = Bounds().Height() / 2.0f;
        BRect knobBounds = fVolCenterIcon->Bounds();
        float ix = cx - (knobBounds.Width() / 2.0f);
        float iy = cy - (knobBounds.Height() / 2.0f);
        return BRect(ix, iy, ix + knobBounds.Width(), iy + knobBounds.Height());
    }

    virtual void MouseMoved(BPoint point, uint32 transit, const BMessage* message) override {
        BControl::MouseMoved(point, transit, message);
        bool wasHovered = fIsHovered;
        if (transit == B_ENTERED_VIEW) fIsHovered = true;
        else if (transit == B_EXITED_VIEW) fIsHovered = false;
        if (wasHovered != fIsHovered) Invalidate();
    }

    virtual void AttachedToWindow() override {
        BView::AttachedToWindow();    
        if (Window() != nullptr) {
            Window()->SetPulseRate(33333); 
        }
    }

    virtual void Draw(BRect updateRect) override {
        rgb_color bgCol = (Parent() != nullptr) ? Parent()->ViewColor() : ui_color(B_PANEL_BACKGROUND_COLOR);

        SetHighColor(bgCol);
        SetDrawingMode(B_OP_COPY);
        FillRect(updateRect);
        
        float cx = Bounds().Width() / 2.0f;
        float cy = Bounds().Height() / 2.0f;
        float radius = std::min(cx, cy) - 2.0f; 
        float markerRadius = radius * 0.78f; 

        float startAngle = 135.0f;
        float totalSweep = 270.0f;

        bigtime_t currentTime = system_time();
        float deltaTime = (float)(currentTime - fLastTime) / 1000000.0f;
        fLastTime = currentTime;
        if (deltaTime > 0.1f) deltaTime = 0.1f;

        const float fadeSpeed = 1.8f; 
        if (fIsHovered) {
            fHoverAlpha += deltaTime * fadeSpeed;
            if (fHoverAlpha > 1.0f) fHoverAlpha = 1.0f;
        } else {
            fHoverAlpha -= deltaTime * fadeSpeed;
            if (fHoverAlpha < 0.0f) fHoverAlpha = 0.0f;
        }

        if (fHoverAlpha > 0.0f && fHoverAlpha < 1.0f) {
            Invalidate();
        }



        PushState();
        SetFlags(Flags() | B_SUBPIXEL_PRECISE);
        
        SetDrawingMode(B_OP_ALPHA);
        SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);

        rgb_color neonLime  = {65, 255, 75, 255};   
        rgb_color neonCyan  = {0, 220, 255, 255};    

        // Check if the background is a light theme (high RGB values)
        bool isLightTheme = (bgCol.red > 150 && bgCol.green > 150 && bgCol.blue > 150);

        for (int i = 0; i <= 100; i += 5) {
            bool isLit = !fIsMuted && (Value() > 0 && i <= Value());
            if (!isLit) continue;

            float angleDeg = startAngle + (totalSweep * (i / 100.0f));
            float angleRad = angleDeg * (M_PI / 180.0f);

            BAffineTransform t;
            t.RotateBy(BPoint(cx, cy), angleRad);
            SetTransform(t);

            // --- 1. DETERMINING TARGET TICK COLOR ---
            rgb_color finalNeon;
            if (fHasAlbumArtPalette) {
                int32 paletteIndex = (int32)((i / 100.0f) * 63.0f);
                if (paletteIndex < 0)  paletteIndex = 0;
                if (paletteIndex > 63) paletteIndex = 63;
                finalNeon = fArtworkPalette[paletteIndex];
            } else {
                float mixRatio = (float)i / 100.0f;
                finalNeon = neonLime;
                if (mixRatio > 0.5f) {
                    float factor = (mixRatio - 0.5f) * 2.0f;
                    finalNeon.red   = (uint8)(neonLime.red * (1.0f - factor) + neonCyan.red * factor);
                    finalNeon.green = (uint8)(neonLime.green * (1.0f - factor) + neonCyan.green * factor);
                    finalNeon.blue  = (uint8)(neonLime.blue * (1.0f - factor) + neonCyan.blue * factor);
                }
            }

            // --- 2. LIGHT THEME CONTRAST FILTER ---
            if (isLightTheme) {
                // Calculate perceived luminance using the standard ITU-R formula
                float luminance = (0.2126f * finalNeon.red) + (0.7152f * finalNeon.green) + (0.0722f * finalNeon.blue);
                
                // If the color is too bright/white, scale down channels to create a crisp jewel tone
                if (luminance > 140.0f) {
                    float darkenFactor = 120.0f / luminance; // Scale targets to high-readability threshold
                    finalNeon.red   = (uint8)(finalNeon.red * darkenFactor);
                    finalNeon.green = (uint8)(finalNeon.green * darkenFactor);
                    finalNeon.blue  = (uint8)(finalNeon.blue * darkenFactor);
                }
            }

            // --- 3. GENERATE BSHAPE METRICS ---
            BShape tickShape;
            tickShape.Clear();

            float startX = cx + markerRadius;
            float endX   = startX + 8.0f; // Tick length span (8 pixels)

            // Trace out the exact sharp radial blade shape
            tickShape.MoveTo(BPoint(startX, cy - 1.5f)); 
            tickShape.LineTo(BPoint(endX, cy - 0.75f));  
            tickShape.LineTo(BPoint(endX, cy + 0.75f));  
            tickShape.LineTo(BPoint(startX, cy + 1.5f)); 
            tickShape.Close();

            // --- 4. RENDER LAYER 1: AMBIENT NEON GLOW HALO ---
            // Create a semi-transparent, wider outline base to act as an illuminated bloom
            rgb_color glowColor = finalNeon;
            glowColor.alpha = 75; // 30% opacity ambient light emission
            
            SetHighColor(glowColor);
            SetPenSize(2.5f); // Thicker outline stroke
            StrokeShape(&tickShape);

            // --- 5. RENDER LAYER 2: SOLID CORE SHAPE ---
            // Render the solid, crisp color geometric core right over the center of the bloom
            SetHighColor(finalNeon);
            FillShape(&tickShape);

            SetTransform(BAffineTransform()); 
        }

        PopState(); 


        // --- 3. RENDER HOVER GLOW LAYER (Around Center Knob Only) ---
        if (fHoverAlpha > 0.0f && IsEnabled() && fVolCenterIcon) {
            SetDrawingMode(B_OP_ALPHA);
            SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
            rgb_color glowColor = isLightTheme ? rgb_color{40, 40, 45, 255} : rgb_color{235, 235, 240, 255};

            float baseRadius = (fVolCenterIcon->Bounds().Width() / 2.0f) + 1.0f;
            const int glowSteps = 6;
            float maxGlowSpread = fVolCenterIcon->Bounds().Width() * 0.125f; 

            for (int step = 0; step < glowSteps; step++) {
                float progress = (float)step / (float)glowSteps;
                float gRadius = baseRadius + (progress * maxGlowSpread);
                
                float alphaFactor = 0.30f * (1.0f - cosf((1.0f - progress) * (float)M_PI)) * fHoverAlpha;
                glowColor.alpha = (uint8)(255.0f * alphaFactor);
                
                SetHighColor(glowColor);
                StrokeEllipse(BPoint(cx, cy), gRadius, gRadius);
            }
        }

        // --- 4. DRAW CENTER SPEAKER KNOB ---
        if (fVolCenterIcon) {
            if (fIsMuted) {
                SetDrawingMode(B_OP_BLEND);
            } else {
                SetDrawingMode(B_OP_ALPHA);
                SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
            }
            
            BRect knobBounds = fVolCenterIcon->Bounds();
            BRect destinationRect = CenterIconRect();
            
            DrawBitmap(fVolCenterIcon, knobBounds, destinationRect, B_FILTER_BITMAP_BILINEAR);
        }
        
        SetDrawingMode(B_OP_COPY);
    }



    virtual void MouseDown(BPoint point) override {
        if (CenterIconRect().Contains(point)) {
            fIsMuted = !fIsMuted;
            
            BMessage muteMsg(MSG_MUTE_TOGGLED);
            muteMsg.AddBool("muted", fIsMuted);
            InvokeNotify(&muteMsg, B_CONTROL_MODIFIED);
            
            Invalidate(); 
            return;
        }
        BControl::MouseDown(point);
    }

    virtual void MessageReceived(BMessage* msg) override {
        if (msg->what == B_MOUSE_WHEEL_CHANGED) {
            float dy;
            if (msg->FindFloat("be:wheel_delta_y", &dy) == B_OK) {
                if (fIsMuted) {
                    fIsMuted = false;
                    BMessage muteMsg(MSG_MUTE_TOGGLED);
                    muteMsg.AddBool("muted", false);
                    InvokeNotify(&muteMsg, B_CONTROL_MODIFIED);
                }

                int32 delta = (int32)(dy * fMultiplier); 
                int32 newValue = Value() - delta;
                if (newValue < 0) newValue = 0;
                if (newValue > 100) newValue = 100;
                if (newValue != Value()) {
                    SetValue(newValue);
                    Invoke();
                    Invalidate();
                }
            }
        } else {
            BControl::MessageReceived(msg);
        }
    }

    bool IsMuted() const { return fIsMuted; }
    void SetMuted(bool mute) { 
        if (fIsMuted != mute) {
            fIsMuted = mute;
            Invalidate();
        }
    }

private:
    int32 	  fMultiplier;
    BBitmap*  fVolCenterIcon;
    BBitmap*  fTickIcon;
    bool 	  fIsMuted;
    
    bool 	  fIsHovered;
    float 	  fHoverAlpha;       
    bigtime_t fLastTime; 
    
    rgb_color fArtworkPalette[64];    
    bool      fHasAlbumArtPalette; 
};




class SongLabel : public BTextView {
public:
    SongLabel(const char* name) : BTextView(name) {
        MakeEditable(false);
        MakeSelectable(false);
        SetWordWrap(true);
        SetAlignment(B_ALIGN_CENTER);
        SetInsets(2, 2, 2, 2); 
   
        fScrollOffset = 0.0f;
        fWaitTicks = 0;
        fIsWrapped = true;
        fRawText = "";
    }
    
    BSize MinSize() override {
        float scale = be_plain_font->Size() / 12.0f;
        if (!cfg.compactMode) {
            // Give it a flexible width, but clamp the height to 2 lines of text
            return BSize(150.0f, 32.0f * scale); 
        }
        return BSize(150.0f, 16.0f * scale); 
    }

    BSize PreferredSize() override {
        float scale = be_plain_font->Size() / 12.0f;
        if (!cfg.compactMode) {
            // Let the text wrapping engine estimate native needs, 
            // but clamp it safely to a 32dp container if uncalculated
            BSize nativePref = BTextView::PreferredSize();
            return BSize(nativePref.width, 32.0f * scale); 
        }
        return BSize(150.0f, 20.0f * scale);
    }

    BSize MaxSize() override {
        float scale = be_plain_font->Size() / 12.0f;
        if (!cfg.compactMode) {
            //  Allow unlimited expansion horizontally, 
            // but strictly limit the maximum vertical height to 2 lines!
            return BSize(B_SIZE_UNLIMITED, 36.0f * scale); 
        }
        return BSize(B_SIZE_UNLIMITED, 24.0f * scale); 
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
    
    fScrollOffset = 0.0f;
    fWaitTicks = 0;

    if (!enabled) {
        fIsWrapped = true;
        SetWordWrap(true);
        SetAlignment(B_ALIGN_CENTER);            
        // REMOVED: Do not set static TextRect here; let FrameResized handle it.
    } else {
        fIsWrapped = false;
        SetWordWrap(false);
        SetAlignment(B_ALIGN_CENTER);            
        BRect r = Bounds();
        r.left = 2; 
        r.right = 99999.0f; 
        SetTextRect(r);
    }
    BTextView::SetText(""); 
    BTextView::SetText(currentText.String()); 
    
    // FORCE LAYOUT UPDATE: Tells the Haiku API to recalculate text height requirements
    InvalidateLayout(); 
    Invalidate();
}

void FrameResized(float width, float height) override {
    BTextView::FrameResized(width, height);
    BRect r = BRect(0, 0, width, height); // Use the new dimensions explicitly
    r.InsetBy(2, 2);
    
    if (!cfg.compactMode) {
        SetTextRect(r); // Safely sets the correct wrapping width dynamically
    } else {
        r.right = 99999.0f;
        SetTextRect(r);
    }
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






// Static proxy redirect function bridges native Haiku C-style threads back to C++ class methods
static status_t DownloadThreadProxy(void* cookie) {
    if (cookie != nullptr) {
        ((SuperMusicWindow*)cookie)->DoDownloadLoop();
    }
    return B_OK;
}

void SuperMusicWindow::DownloadStationIcons() {
    if (fIsQuitting) return;
    
    // Spawn a native, trackable Haiku background thread system
    fDownloadThreadID = spawn_thread(DownloadThreadProxy, "StationIconDownloader", 
                                     B_LOW_PRIORITY, this);
                                     
    if (fDownloadThreadID >= 0) {
        resume_thread(fDownloadThreadID); // Kick off the background thread loop pipeline
    }
}

// The safe, synchronized worker execution loop method
void SuperMusicWindow::DoDownloadLoop() {
    snooze(100000); 

    int32 mainCount = 0;
    if (Lock()) {
        mainCount = fStationList->CountItems();
        Unlock();
    }

    for (int32 i = 0; i < mainCount; i++) {
        // Safe Early-Exit Point: Terminate instantly if window is shutting down
        if (fIsQuitting) break; 

        StationItem* mainItem = nullptr;
        if (Lock()) {
            mainItem = (StationItem*)fStationList->ItemAt(i);
            Unlock();
        }
        if (!mainItem) continue;
        
        Channel chan = mainItem->GetChannel();
        if (chan.image.empty()) continue;

        // Verify cache allocations under a brief thread-safe lock block
        bool hitCache = false;
        if (Lock()) {
            if (fIconCache.count(chan.id) > 0) {
                mainItem->SetIcon(new BBitmap(fIconCache[chan.id]));
                fStationList->InvalidateItem(i);
                
                for (int32 j = 0; j < fFavList->CountItems(); j++) {
                    StationItem* favItem = (StationItem*)fFavList->ItemAt(j);
                    if (favItem && favItem->GetChannel().id == chan.id) {
                        favItem->SetIcon(new BBitmap(fIconCache[chan.id]));
                        fFavList->InvalidateItem(j);
                    }
                }
                hitCache = true;
            }
            Unlock();
        }
        if (hitCache) continue;

        // Setup Curl pipeline connections
        CURL* curl = curl_easy_init();
        std::string buffer;
        if (curl) {
            curl_easy_setopt(curl, CURLOPT_URL, chan.image.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
            curl_easy_setopt(curl, CURLOPT_USERAGENT, "SuperMusicThingy/1.0");
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

            if (curl_easy_perform(curl) == CURLE_OK && !buffer.empty() && !fIsQuitting) {
                // FIXED THREAD SAFETY SECURE BLOCK:
                // We request a window Lock *before* using the Translation Kit decoder utilities.
                // This prevents multi-threaded race conditions on translation catalogs.
                if (Lock()) {
                    // Double-verify that the application hasn't started close down paths
                    if (!fIsQuitting) {
                        BMemoryIO mem(buffer.data(), buffer.size());
                        BBitmap* icon = BTranslationUtils::GetBitmap(&mem);
                        
                        if (icon && icon->InitCheck() == B_OK) {
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
                        }
                    }
                    Unlock();
                }
            }
            curl_easy_cleanup(curl);
        }
    }
    
    // Clear out thread handle ID value once worker concludes cleanly
    fDownloadThreadID = -1;
}



class ColorItem : public BStringItem {
public:
    ColorItem(const char* text) : BStringItem(text) {}

    virtual void DrawItem(BView* owner, BRect frame, bool complete = false) {
        if (cfg.uTheme == "Dark") {
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





// @ApplyTheme
void SuperMusicWindow::ApplyTheme() {
    rgb_color bgVal;
    rgb_color bg2Val;
    rgb_color txtVal;

    // --- LOCK THE WINDOW ONCE TO PREVENT DEADLOCKS ---
    if (Lock()) {
        // Run a linear two-pass execution to shake loose Haiku's color caches 
        int totalPasses = (cfg.uTheme == "Default") ? 2 : 1;

        for (int pass = 1; pass <= totalPasses; pass++) {
            if (cfg.uTheme == "Dark" || (cfg.uTheme == "Default" && pass == 1)) {
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

            if (fTabView) {
                fTabView->SetViewColor(bgVal);
        
                for (int32 i = 0; i < fTabView->CountTabs(); i++) {
                    BTab* tab = fTabView->TabAt(i);
                    if (tab == nullptr) continue; 

                    BView* tabView = tab->View(); 
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
        } 

        // Rebuild the tabs to force the top navigation buttons to snap into place
        if (!cfg.compactMode && fTabView) {
        	
        	// --- 1A. SMART CONTEXT RETENTION ENGINE ---
            std::string savedTabName = "Radio"; // Safe fallback default
            
            // If an explicit programmatic override is active, use that!
            // Otherwise, safely fallback to tracking whatever tab the user was looking at.
            if (!fOverrideTabTarget.IsEmpty()) {
                savedTabName = fOverrideTabTarget.String();
            } else {
                int32 currentSelection = fTabView->Selection();
                if (currentSelection >= 0) {
                    BTab* activeTab = fTabView->TabAt(currentSelection);
                    if (activeTab && activeTab->Label()) {
                        savedTabName = activeTab->Label();
                    }
                }
            }
        	 
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

            // --- 3. PROGRAMMATIC TARGET RESOLUTION ---
            const char* targetTabLabel = fIsStartingUp ? "Radio" : savedTabName.c_str(); 
            
            for (int32 j = 0; j < fTabView->CountTabs(); j++) {
                BTab* currentTab = fTabView->TabAt(j);
                if (currentTab && currentTab->Label() && strcmp(currentTab->Label(), targetTabLabel) == 0) {
                    fTabView->Select(j);
                    break;
                }
            }
            
            // Consume the override token so subsequent standard checkbox clicks track naturally
            fOverrideTabTarget = "";
   
            // 4. Force the inner controls to blend seamlessly with the panel backgrounds

            rgb_color bgDarkColor = rgb_color{40, 40, 40, 255}; 
            bool useDarkColors = (cfg.uTheme == "Dark");

            // Style the 15-band EQ sliders array
            for (int i = 0; i < 15; i++) {
                if (fEQSliders[i]) {
                    if (useDarkColors) {
                        fEQSliders[i]->SetViewColor(bgDarkColor);
                        fEQSliders[i]->SetLowColor(bgDarkColor);
                    } else {
                        fEQSliders[i]->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
                        fEQSliders[i]->SetLowColor(ui_color(B_PANEL_BACKGROUND_COLOR));
                    }
                    fEQSliders[i]->Invalidate();
                }
            }

            // Style the limiter controllers
            BSlider* limiterSliders[] = { fLimitInput, fLimitLimit, fLimitRelease };
            
            for (int s = 0; s < 3; s++) {
                if (limiterSliders[s]) {
                    if (useDarkColors) {
                        limiterSliders[s]->SetViewColor(bgDarkColor);
                        limiterSliders[s]->SetLowColor(bgDarkColor);
                        limiterSliders[s]->SetHighColor(255, 255, 255, 255); // Force labels to white
                    } else {
                        limiterSliders[s]->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
                        limiterSliders[s]->SetLowColor(ui_color(B_PANEL_BACKGROUND_COLOR));
                        limiterSliders[s]->SetHighColor(0, 0, 0, 255); // Default black text
                    }
                    
                    // Recolor any internal view wrappers inside the components
                    for (int32 c = 0; c < limiterSliders[s]->CountChildren(); c++) {
                        BView* child = limiterSliders[s]->ChildAt(c);
                        if (child) {
                            if (useDarkColors) {
                                child->SetViewColor(bgDarkColor);
                                child->SetLowColor(bgDarkColor);
                                child->SetHighColor(255, 255, 255, 255);
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

            // --- EXCLUSIVE GROUP LAYOUT RESET FIX ---
            if (fConfigGroup) {
                fConfigGroup->InvalidateLayout(true);
                fConfigGroup->Layout(true);
                fConfigGroup->Invalidate();
            }
            if (fStationGroup) fStationGroup->InvalidateLayout(true);
            if (fFavGroup)     fFavGroup->InvalidateLayout(true);
            if (fAboutGroup)   fAboutGroup->InvalidateLayout(true);
        }

        this->InvalidateLayout(true);
        this->Layout(true);
        
       
        // --- FINALLY SAFE TO UNLOCK WINDOW THREAD ---
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
    
    for (const auto& ch : channels) {
        if (ch.title == currentStation) {
            currentUrl = BASE_URL + ch.id + ".pls";
            break;
        }
    }

    if (currentUrl.empty()) {
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

    if (!isDuplicate) {
        std::ofstream outfile(path, std::ios_base::app);
        if (outfile.is_open()) {
            outfile << currentUrl << std::endl;
            outfile.close();
        }
    }
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

    fade_volume(mpv, 0, 250);
    currentSong = "Loading Favorite...";
    if (gGuiWindow && gGuiWindow->Lock()) {
        gGuiWindow->UpdateStatus(currentDesc.c_str(), currentSong.c_str());
        gGuiWindow->Unlock();
    }

    const char *cmd[] = {"loadfile", finalUrl.c_str(), NULL};
    mpv_command(mpv, cmd);
    
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

    fade_volume(mpv, 0, 250); 

    currentSong = "Buffering...";
    UpdateStatus(currentStation.c_str(), currentSong.c_str());

    std::string url = get_quality_url(chan); 
    const char *cmd[] = {"loadfile", url.c_str(), NULL};
    mpv_command(mpv, cmd);    

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
    
    for (const auto& ch : channels) {
        if (ch.title == currentStation) {
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
        if (line != currentUrl && !line.empty()) {
            remaining.push_back(line);
        } else {
            removed = true;
        }
    }
    infile.close();

    if (removed) {
        std::ofstream outfile(path, std::ios::trunc);
        for (const auto& f : remaining) {
            outfile << f << "\n";
        }
    }
}

    
void play_random() {
    if (channels.empty()) return;
    fade_volume(mpv, 0, 250);

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



void PopulatePresetList(BListView* list, const char* folderPath) {
    if (list == nullptr) return;
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
        
        // --- THE HIBERNATION HOOK ---
        if (visualWin) {
            uint32_t flags = SDL_GetWindowFlags(visualWin);
            
            // If the window is hidden via SDL_HideWindow, pause rendering and capture
            if (!(flags & SDL_WINDOW_SHOWN)) {
                snooze(100000); // Sleep for 100ms to keep CPU usage at 0%
                
                // Keep polling background events so SDL stays responsive to unhide actions
                SDL_Event e;
                while (SDL_PollEvent(&e)) {
                    if (e.type == SDL_QUIT || (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE)) {
                        visualsRunning = false;
                    }
                }
                continue; // Skip projectM processing and jump back to top of loop
            }
        }
    
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
                static short buffer[2048]; 
                alcCaptureSamples(alcCaptureDevice, (ALCvoid*)buffer, 1024);
                
                static float floatBuffer[2048];
                for (int i = 0; i < 2048; ++i) floatBuffer[i] = buffer[i] / 32768.0f;
                projectm_pcm_add_float(pm, floatBuffer, 1024, PROJECTM_STEREO);
            }
        }
        
        // This break guard now strictly catches application termination requests
        if (!visualsRunning) {
            break; 
        }

        uint32_t currentTime = SDL_GetTicks();
        if (cfg.autoShuffleVisuals && (currentTime - lastPresetChange >= PRESET_DURATION)) {
            load_random_preset(pm);
            lastPresetChange = currentTime;
        }

        // --- Render and Swap ---
        projectm_opengl_render_frame(pm);
        SDL_GL_SwapWindow(visualWin);

        // --- Standard Event Polling ---
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            // Window Close / Quit
            if (e.type == SDL_QUIT || (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE)) {
                if (gGuiWindow != nullptr) {
                    // Send a message to uncheck the checkbox and hide the window
                    gGuiWindow->PostMessage(MSG_HIDE_VISUALS_REQUEST); 
                }
                   if (visualWin) {
       					 SDL_HideWindow(visualWin);
    					}
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
                // INJECTION POINT: Safely notify the main thread of a scroll wheel volume change
                if (gGuiWindow != nullptr) {
                    BMessage volStepMsg(MSG_VOL_STEP_REQUEST);
                    // Pass 1 for volume up, -1 for volume down
                    volStepMsg.AddInt32("direction", (e.wheel.y > 0) ? 1 : -1);
                    
                    gGuiWindow->PostMessage(&volStepMsg);
                }
            }  

            else if (e.type == SDL_MOUSEBUTTONDOWN) {
                if (e.button.button == SDL_BUTTON_MIDDLE) {
                    if (gGuiWindow != nullptr) {
                        gGuiWindow->PostMessage(MSG_MUTE_TOGGLED);
                    }
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

    if (visualWin) {
        SDL_GL_MakeCurrent(visualWin, NULL);
    }
    
	
    if (cfg.debugEnable) printf("[DEBUG SDL] VisualsThread loop exited cleanly.\n");
	#endif
    return B_OK;
   
}


// @Visuals
void SuperMusicWindow::ReallyStopVisuals() {
    #ifdef USE_PROJECTM
    if (visualWin == nullptr && glContext == nullptr && pm == nullptr) {
        return; 
    }
    
    if (cfg.debugEnable) printf("[DEBUG Visual] Force-terminating visual subsystems...\n");
    
    // Break loop flag just in case
    visualsRunning = false; 

    // Clean up OpenAL audio captures safely
    cleanup_capture_device();
    
    // Clear the OpenGL pipeline context
    if (glContext && visualWin) { 
        // Unbind the context from the main thread first
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
    
    if (cfg.debugEnable) printf("[DEBUG Visual] Visuals cleanup sequence finalized successfully.\n");
    #endif
}



void SuperMusicWindow::StartVisuals() {
    #ifdef USE_PROJECTM
    if (!visualWin) {
        // First-time setup: allocate everything ONCE
        init_visuals(); 
        visualsRunning = true;
        fVisualsThreadID = spawn_thread(VisualsThread, "VisualsLoop", B_NORMAL_PRIORITY, NULL);
        resume_thread(fVisualsThreadID);
    } else {
        // Subsquent toggles: simple un-hide the window and resume drawing!
        visualsRunning = true;
        SDL_ShowWindow(visualWin);
    }
    #endif
}


void SuperMusicWindow::StopVisuals() {
    #ifdef USE_PROJECTM
    if (!visualsRunning) return; 
    
    // Hide the window out of sight instead of deleting it!
    if (visualWin) {
        SDL_HideWindow(visualWin);
    }
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
    IconView(BBitmap* bitmap) // Leaf Icons
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




struct SnowParticle {
    float x;
    float y;
    float speedY;
    float swaySpeed;
    float swayAmplitude;
    float swayPhase;
    float size; // Controls the radius of the snowflake
};

class CreditsSlider : public BView {
public:
    CreditsSlider(const char* name) : BView(name, B_WILL_DRAW | B_PULSE_NEEDED | B_FRAME_EVENTS) {
        SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
        fScrollY = 0.0f;
        
        // Populate text matrix lines
        fLines.Add("Special Thanks For Testing");
        fLines.Add("&");
        fLines.Add("Suggestions");
        fLines.Add("cocobean");
        fLines.Add("Starcrasher");
        fLines.Add("kim1963");
        fLines.Add("");
        fLines.Add("");
         fLines.Add("Powered By");
        fLines.Add("SomaFM (Radio Service)");
        fLines.Add("MPV (Playback Core)");
        fLines.Add("nlohmann/json (The Data)");
        fLines.Add("Haiku Interface Kit (The GUI)");
        fLines.Add("libsdl / projectM / OpenGL (The Visuals)");
        fLines.Add("SVGear (Scalable Vector Graphics)");
        fLines.Add("libcurl (Network/Streaming)");
        fLines.Add("");
        fLines.Add("");
        fLines.Add("");
        fLines.Add("");
        fLines.Add("");
        fLines.Add("");
        fLines.Add("");
        fLines.Add("");
    }

    ~CreditsSlider() {
    }
    
    void AttachedToWindow() override {
        BView::AttachedToWindow();
        if (Parent()) SetViewColor(Parent()->ViewColor());
        
        // Define the global window pulse interval in microseconds (35000 µs = 35ms)
        if (Window()) Window()->SetPulseRate(35000);
    }

    // --- POPULATE SNOW ONCE LAYOUT ENGINE ASSIGNS TRUE DIMENSIONS ---
    void FrameResized(float newWidth, float newHeight) override {
        BView::FrameResized(newWidth, newHeight);
        static bool sSnowInitialized = false;
        if (!sSnowInitialized && newWidth > 5.0f && newHeight > 5.0f) {
            for (int i = 0; i < kMaxSnow; i++) {
                ResetSnowflake(i, true); // Randomizes snow across the whole view area
            }
            sSnowInitialized = true;
        }
    }
    
    void Pulse() override {
        fScrollY += 0.28f; 
        
        BFont textFont(be_plain_font);
        font_height fh;
        textFont.GetHeight(&fh);
        float lineHeight = fh.ascent + fh.descent + fh.leading + 6.0f;
        float textBlockHeight = (fLines.CountStrings() * lineHeight);

        float viewHeight = Bounds().Height() > 0 ? Bounds().Height() : 250.0f;

        // Loop instantly right when the last line finishes passing the upper limit
        if (fScrollY > textBlockHeight) {
            fScrollY = 0.0f; 
        }

        // --- 2. UPDATE SNOWFLAKE POSITIONS ---
        float viewWidth = Bounds().Width() > 0 ? Bounds().Width() : 400.0f;
        for (int i = 0; i < kMaxSnow; i++) {
            fSnow[i].y += fSnow[i].speedY;
            fSnow[i].swayPhase += fSnow[i].swaySpeed;

            if (fSnow[i].y > viewHeight + 10.0f || fSnow[i].x < -10.0f || fSnow[i].x > viewWidth + 10.0f) {
                ResetSnowflake(i, false); 
            }
        }

        Invalidate();
    }

    BSize MinSize() override { return BSize(150.0f, 100.0f); }
    BSize PreferredSize() override { return BSize(150.0f, B_SIZE_UNSET); }
    BSize MaxSize() override { return BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED); }

    void Draw(BRect updateRect) override {
        PushState();
        
        rgb_color bgColor = ViewColor();
        SetLowColor(bgColor);
        FillRect(Bounds(), B_SOLID_LOW);

        float viewWidth = Bounds().Width();
        float viewHeight = Bounds().Height();

        // =================================================================
        // PHASE 1: DRAW BACKGROUND PROCEDURAL SNOWFLAKES
        // =================================================================
        SetDrawingMode(B_OP_ALPHA);
        
        rgb_color snowColor = make_color(255, 255, 255, 75); // Semi-transparent white
        SetHighColor(snowColor);

        for (int i = 0; i < kMaxSnow; i++) {
            float currentX = fSnow[i].x + (sinf(fSnow[i].swayPhase) * fSnow[i].swayAmplitude);
            float currentY = fSnow[i].y;
            float r = fSnow[i].size;

            // Draw six-pointed geometric crosses
            StrokeLine(BPoint(currentX, currentY - r), BPoint(currentX, currentY + r));
            
            float xOffset1 = r * 0.866f; // cos(30)
            float yOffset1 = r * 0.500f; // sin(30)
            StrokeLine(BPoint(currentX - xOffset1, currentY - yOffset1), BPoint(currentX + xOffset1, currentY + yOffset1));
            StrokeLine(BPoint(currentX - xOffset1, currentY + yOffset1), BPoint(currentX + xOffset1, currentY - yOffset1));
        }

        // =================================================================
        // PHASE 2: DRAW FOREGROUND SCROLLING TEXTS
        // =================================================================
        BFont plainFont(be_plain_font);
        BFont boldFont(be_bold_font);
        font_height fh;
        plainFont.GetHeight(&fh);
        float lineHeight = fh.ascent + fh.descent + fh.leading + 6.0f;

        // Start scrolling upward directly from the bottom edge of the view container
        float startY = viewHeight - fScrollY; 

        SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_OVERLAY);

        for (int32 i = 0; i < fLines.CountStrings(); i++) {
            float textY = startY + (i * lineHeight);
            
            // Culling bounds protection
            if (textY < -20.0f || textY > viewHeight + 20.0f)
                continue;

            BString currentLine = fLines.StringAt(i);
            const char* currentText = currentLine.String();

            bool isHeader = (currentLine.FindFirst("Testing") != B_ERROR) ||
                            (currentLine.FindFirst("&") != B_ERROR) ||
                            (currentLine.FindFirst("Suggestions") != B_ERROR) ||
                            (currentLine.FindFirst("Powered By") != B_ERROR);
            
            rgb_color textColor;
            rgb_color headerColor;
            
            // Fixed: Assuming external configuration structure 'cfg' exists globally
            if (cfg.uTheme == "Dark") {
                textColor = make_color(255, 255, 255, 255);
                headerColor = make_color(240, 180, 40, 255); 
            } else {
                textColor = ui_color(B_DOCUMENT_TEXT_COLOR);
                headerColor = ui_color(B_MENU_SELECTED_ITEM_TEXT_COLOR);
            }

            if (isHeader) {
                boldFont.SetSize(13.0f);
                SetFont(&boldFont);
                SetHighColor(headerColor); 
            } else {
                plainFont.SetSize(12.0f);
                SetFont(&plainFont);
                SetHighColor(textColor);
            }

            float textWidth = isHeader ? boldFont.StringWidth(currentText) : plainFont.StringWidth(currentText);
            float textX = (viewWidth - textWidth) / 2.0f; 
            
            DrawString(currentText, BPoint(textX, textY + fh.ascent));
        }

        PopState();
    }

private:
    void ResetSnowflake(int index, bool randomizeInitialY) {
        float viewWidth = Bounds().Width() > 5.0f ? Bounds().Width() : 400.0f;
        float viewHeight = Bounds().Height() > 5.0f ? Bounds().Height() : 250.0f;

        fSnow[index].x = (float)(rand() % (int)viewWidth);
        fSnow[index].y = randomizeInitialY ? (float)(rand() % (int)viewHeight) : -10.0f;
        
        fSnow[index].speedY = 0.2f + ((rand() % 100) / 100.0f) * 0.4f; 
        fSnow[index].swaySpeed = 0.01f + ((rand() % 100) / 100.0f) * 0.03f;
        fSnow[index].swayAmplitude = 2.0f + (rand() % 5); 
        fSnow[index].swayPhase = ((rand() % 100) / 100.0f) * 6.28f;
        fSnow[index].size = 2.0f + (rand() % 4);
    }

    static const int kMaxSnow = 25; 
    SnowParticle     fSnow[kMaxSnow];
    float            fScrollY;
    BStringList      fLines;
};


class SnowyHeaderView : public BView {
public:
    SnowyHeaderView(const char* name) : BView(name, B_WILL_DRAW | B_PULSE_NEEDED) {
        SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
    }

    void AttachedToWindow() override {
        BView::AttachedToWindow();
        if (Parent()) SetViewColor(Parent()->ViewColor());
        
        // Match the exact same timing rate as your Crawler view
        for (int i = 0; i < kMaxSnow; i++) {
            ResetSnowflake(i, true);
        }
    }

    void Pulse() override {
        float viewWidth = Bounds().Width() > 0 ? Bounds().Width() : 400.0f;
        float viewHeight = Bounds().Height() > 0 ? Bounds().Height() : 400.0f;

        for (int i = 0; i < kMaxSnow; i++) {
            fSnow[i].y += fSnow[i].speedY;
            fSnow[i].swayPhase += fSnow[i].swaySpeed;

            if (fSnow[i].y > viewHeight + 10.0f || fSnow[i].x < -10.0f || fSnow[i].x > viewWidth + 10.0f) {
                ResetSnowflake(i, false);
            }
        }
        Invalidate();
    }

	void Draw(BRect updateRect) {
    	PushState();
    	rgb_color bgColor = ViewColor();
    	SetLowColor(bgColor);
    	FillRect(Bounds(), B_SOLID_LOW);

    	// Snow setup rendering layer
    	SetDrawingMode(B_OP_ALPHA);

    	rgb_color snowColor;

        snowColor = make_color(255, 255, 255, 75); // Semi-transparent white

    	SetHighColor(snowColor);

    	for (int i = 0; i < kMaxSnow; i++) {
        	float currentX = fSnow[i].x + (sinf(fSnow[i].swayPhase) * fSnow[i].swayAmplitude);
        	float currentY = fSnow[i].y;
        	float r = fSnow[i].size;

        	StrokeLine(BPoint(currentX, currentY - r), BPoint(currentX, currentY + r));
        	float xOffset1 = r * 0.866f;
        	float yOffset1 = r * 0.500f;
        	StrokeLine(BPoint(currentX - xOffset1, currentY - yOffset1), BPoint(currentX + xOffset1, currentY + yOffset1));
        	StrokeLine(BPoint(currentX - xOffset1, currentY + yOffset1), BPoint(currentX + xOffset1, currentY - yOffset1));
    	}
    	PopState();
	}


private:
    void ResetSnowflake(int index, bool randomizeInitialY) {
        float viewWidth = Bounds().Width() > 5.0f ? Bounds().Width() : 400.0f;
        float viewHeight = Bounds().Height() > 5.0f ? Bounds().Height() : 250.0f;

        fSnow[index].x = (float)(rand() % (int)viewWidth);
        fSnow[index].y = randomizeInitialY ? (float)(rand() % (int)viewHeight) : -10.0f;
        fSnow[index].speedY = 0.2f + ((rand() % 100) / 100.0f) * 0.4f; 
        fSnow[index].swaySpeed = 0.01f + ((rand() % 100) / 100.0f) * 0.03f;
        fSnow[index].swayAmplitude = 2.0f + (rand() % 5); 
        fSnow[index].swayPhase = ((rand() % 100) / 100.0f) * 6.28f;
        fSnow[index].size = 2.0f + (rand() % 4);
    }

    static const int kMaxSnow = 20; 
    SnowParticle     fSnow[kMaxSnow];
};

class SnowyFooterView : public BView {
public:
    SnowyFooterView(const char* name) : BView(name, B_WILL_DRAW | B_PULSE_NEEDED) {
        SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
    }

    void AttachedToWindow() override {
        BView::AttachedToWindow();
        if (Parent()) SetViewColor(Parent()->ViewColor());
        
        for (int i = 0; i < kMaxSnow; i++) {
            ResetSnowflake(i, true);
        }
    }

    void Pulse() override {
        float viewWidth = Bounds().Width() > 0 ? Bounds().Width() : 400.0f;
        float viewHeight = Bounds().Height() > 0 ? Bounds().Height() : 250.0f;

        for (int i = 0; i < kMaxSnow; i++) {
            fSnow[i].y += fSnow[i].speedY;
            fSnow[i].swayPhase += fSnow[i].swaySpeed;

            if (fSnow[i].y > viewHeight + 10.0f || fSnow[i].x < -10.0f || fSnow[i].x > viewWidth + 10.0f) {
                ResetSnowflake(i, false);
            }
        }
        Invalidate();
    }

    void Draw(BRect updateRect) override {
        PushState();
        rgb_color bgColor = ViewColor();
        SetLowColor(bgColor);
        FillRect(Bounds(), B_SOLID_LOW);

        SetDrawingMode(B_OP_ALPHA);
    	rgb_color snowColor;
        snowColor = make_color(255, 255, 255, 75); // Semi-transparent white
        SetHighColor(snowColor);

        for (int i = 0; i < kMaxSnow; i++) {
            float currentX = fSnow[i].x + (sinf(fSnow[i].swayPhase) * fSnow[i].swayAmplitude);
            float currentY = fSnow[i].y;
            float r = fSnow[i].size;

            StrokeLine(BPoint(currentX, currentY - r), BPoint(currentX, currentY + r));
            float xOffset1 = r * 0.866f;
            float yOffset1 = r * 0.500f;
            StrokeLine(BPoint(currentX - xOffset1, currentY - yOffset1), BPoint(currentX + xOffset1, currentY + yOffset1));
            StrokeLine(BPoint(currentX - xOffset1, currentY + yOffset1), BPoint(currentX + xOffset1, currentY - yOffset1));
        }
        PopState();
    }

private:
    void ResetSnowflake(int index, bool randomizeInitialY) {
        float viewWidth = Bounds().Width() > 5.0f ? Bounds().Width() : 400.0f;
        float viewHeight = Bounds().Height() > 5.0f ? Bounds().Height() : 250.0f;

        fSnow[index].x = (float)(rand() % (int)viewWidth);
        fSnow[index].y = randomizeInitialY ? (float)(rand() % (int)viewHeight) : -10.0f;
        fSnow[index].speedY = 0.2f + ((rand() % 100) / 100.0f) * 0.4f; 
        fSnow[index].swaySpeed = 0.01f + ((rand() % 100) / 100.0f) * 0.03f;
        fSnow[index].swayAmplitude = 2.0f + (rand() % 5); 
        fSnow[index].swayPhase = ((rand() % 100) / 100.0f) * 6.28f;
        fSnow[index].size = 2.0f + (rand() % 4);
    }

    static const int kMaxSnow = 10; // Lower density since this is just a small buffer zone
    SnowParticle     fSnow[kMaxSnow];
};


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
    : BWindow(BRect(100, 100, 350, 250), "SuperMusicThingy", B_TITLED_WINDOW, 
    B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS | B_QUIT_ON_WINDOW_CLOSE)
    
{

	SetPulseRate(50000); 
    fAlbumArt = nullptr;
    fIsStartingUp = true;
    fIsQuitting = false;
	fDownloadThreadID = -1;
    
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
    // ====================================================================
    // RIGID LAYOUT CONSTRAINT FIX
    // ====================================================================
    if (cfg.showSpectrumVisuals) { 
		float stackHeight = 125.0f * scale; // Default baseline fallback
        stackHeight = 125.0f;    
        fSpectrum = new SpectrumView(BRect(0, 0, 375 * scale, stackHeight), "spectrum"); 
        // Lock both boundaries to the exact same size.
        // This forces Haiku's engine to respect a strict, unstretchable box.
        fSpectrum->SetExplicitMinSize(BSize(375 * scale, stackHeight));
        fSpectrum->SetExplicitMaxSize(BSize(375 * scale, stackHeight));
    } else {
        // Safe, flat initialization when spectrum visuals are toggled off
        fSpectrum = new SpectrumView(BRect(0, 0, 375, 0), "spectrum");         
        fSpectrum->SetExplicitMinSize(BSize(375, 0));
        fSpectrum->SetExplicitMaxSize(BSize(375, 0));
    }
    // ====================================================================

	
	
    
    BBitmap* heartIcon = GetVectorIcon(kIconFav, kIconFavSize, 40);
	fBtnAddFav = new IconButton("btn_add_fav", heartIcon, new BMessage(MSG_ADD_FAV));	
	
	BBitmap* pauseIcon = GetVectorIcon(kIconPause, kIconPauseSize, 40);
    fPauseBtn = new IconButton("btn_pause", pauseIcon, new BMessage(MSG_PAUSE));
	
	BBitmap* playIcon = GetVectorIcon(kIconPlay, kIconPlaySize, 40);
    fPlayBtn = new IconButton("btn_play", playIcon, new BMessage(MSG_PLAY));
    
    BBitmap* stopIcon = GetVectorIcon(kIconStop, kIconStopSize, 40);
    fStopBtn = new IconButton("btn_stop", stopIcon, new BMessage(MSG_STOP));
	
	BBitmap* shuffleIcon = GetVectorIcon(kIconShuffle, kIconShuffleSize, 40);
   	fShuffleBtn = new IconButton("btn_shuffle", shuffleIcon, new BMessage(MSG_SHUFFLE));
	
	
    BRect volRect(0, 0, 64, 64); 
    fVolumeSlider = new RadialVolumeControl(volRect, "Volume", new BMessage(MSG_VOL_CHANGED), 5);
    fVolumeSlider->SetTarget(this);

// @MainLayout

// --- SETUP THE RIGHT SIDE CONTAINER FIRST ---
fRightSideControlGroup = new BGroupView(B_HORIZONTAL, 5);
BLayoutBuilder::Group<>(fRightSideControlGroup, B_HORIZONTAL, 0)
    .Add(fBtnAddFav) // Heart icon safely managed inside this group container
.End();

// --- LAYOUT BUILDER FOR PLAYER TAB (STABLE MASTER BLUEPRINT) ---
fControlStack = new BGroupView(B_HORIZONTAL, 5); 
BLayoutBuilder::Group<>(fControlStack, B_HORIZONTAL, 5)
    // Add B_ALIGN_VERTICAL_CENTER to explicitly pull each button to the middle
    .Add(fStopBtn, B_ALIGN_VERTICAL_CENTER)
    .Add(fPauseBtn, B_ALIGN_VERTICAL_CENTER)
    .Add(fPlayBtn, B_ALIGN_VERTICAL_CENTER)
    .Add(fShuffleBtn, B_ALIGN_VERTICAL_CENTER)    
.End();


// Create distinct, persistent structural placeholder nodes
fNormalControlsWrapper = new BGroupView(B_HORIZONTAL, 0);
fCompactControlsWrapper = new BGroupView(B_HORIZONTAL, 0);
fCompactSpectrumWrapper = new BGroupView(B_VERTICAL, 0); // Dedicated compact placeholder

// This stack serves as the default vertical home for normal mode text and visualizer
BGroupView* fMetaAndSpectrumStack = new BGroupView(B_VERTICAL, 0);
BLayoutBuilder::Group<>(fMetaAndSpectrumStack, B_VERTICAL, 0)
    .Add(fDescView)
    .Add(fSongView)  
    .Add(fSpectrum) // Starts inside the text stack for normal view orientation
.End();

// --- MAIN PLAYER GROUP ---
BLayoutBuilder::Group<>(fPlayerGroup, B_VERTICAL, 5)
    .SetInsets(0)
    .Add(fArtView, B_ALIGN_HORIZONTAL_CENTER) 
    
    // Group the text stack and our spectrum wrapper placeholder sequentially 
    // This allows fluid reflow under the labels when switching to horizontal mode
    .AddGroup(B_VERTICAL, 0)               
        .Add(fMetaAndSpectrumStack)         
        .Add(fCompactSpectrumWrapper)      
    .End()
    
    .AddGlue()              
    .AddGroup(B_HORIZONTAL, 5) 
        // Left Side: Stats and checkboxes
        .AddGroup(B_VERTICAL, 0) 
            .SetInsets(20, 0, 0, 0) 
            .AddGlue()
            .Add(fListenersView)
            .AddStrut(6.0f * scale)
            .Add(fquality)
            .AddStrut(6.0f * scale)
            .Add(fCompactModeRadio) 
            .AddGlue()              
         .End()          
         .AddGlue(2)

         .Add(fVolumeSlider, B_ALIGN_HORIZONTAL_CENTER | B_ALIGN_VERTICAL_CENTER) 
         .AddGlue() 
         
         // Right side container isolates layout elements and sets a clear 20px padding edge
         .AddGroup(B_HORIZONTAL, 0)
             .SetInsets(0, 0, 20, 0) 
             .Add(fRightSideControlGroup, B_ALIGN_VERTICAL_CENTER)
             .Add(fCompactControlsWrapper, B_ALIGN_VERTICAL_CENTER)
         .End()
     .End()        
   
   // Bottom Row: Fixed structural placeholder node for normal mode controls
   .Add(fNormalControlsWrapper);
  





    // ==========================================
    // TAB 2: STATIONS VIEW (The Directory)
    // ==========================================
    fStationGroup = new BGroupView(B_VERTICAL, 0);
    fStationGroup->SetName("Stations"); 
    // Set container color to prevent bleed-through
    fStationGroup->SetViewColor(ui_color(B_LIST_BACKGROUND_COLOR));
    
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
    
    fChkTheme = new BCheckBox("chk_theme", "Dark Theme", new BMessage(MSG_CFG_THEME));
    fChkTheme->SetValue(cfg.uTheme == "Dark" ? B_CONTROL_ON : B_CONTROL_OFF);    
    
    fChkDebug = new BCheckBox("chk_debug", "Debug Mode [?]", new BMessage(MSG_CFG_DEBUG));
    fChkDebug->SetValue(cfg.debugEnable ? B_CONTROL_ON : B_CONTROL_OFF); 
    
    fChkDebug->SetToolTip("Unhides options that are not 100% working in the app.\nShows debug info if app is started from terminal.");   
    
    fChkTitle = new BCheckBox("fChkTitle_toggle", "Song Titles", new BMessage(MSG_SHOW_TITLE));
    fChkTitle->SetValue(cfg.enableTitles ? B_CONTROL_ON : B_CONTROL_OFF);

    
    fChkSong = new BCheckBox("fChkSong_toggle", "Station Descriptions", new BMessage(MSG_SHOW_DESC));
    fChkSong->SetValue(cfg.enableDescriptions ? B_CONTROL_ON : B_CONTROL_OFF);    
    

    fPresetList = new PresetListView("preset_list");
	fPresetList->SetSelectionMessage(new BMessage(MSG_PRESET_SELECTED));
    
    fPresetScroll = new BScrollView("preset_scroll", fPresetList, 0, true, true, B_NO_BORDER);
    fPresetScroll->Hide();
    fPresetScroll->SetExplicitMinSize(BSize(B_SIZE_UNSET, 150));
	fPresetScroll->SetExplicitMaxSize(BSize(B_SIZE_UNSET, 300));
	
	fPresetToggle = new BCheckBox("preset_toggle", "MilkDrop Presets [?]:", new BMessage(MSG_TOGGLE_PRESETS));
	fPresetToggle->SetValue(B_CONTROL_OFF);

	fPresetToggle->SetToolTip("Add more milk presets here -> $HOME/config/settings/SuperMusicThingy/milk_presets");

    fVisualsCheckbox = new BCheckBox("visuals_toggle", "projectM Visualizer", new BMessage(MSG_TOGGLE_VISUALS));
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

	fEQToggle = new BCheckBox("eq_toggle", "15-Band EQ [?]", new BMessage(MSG_TOGGLE_EQ));
	fEQToggle->SetValue(cfg.eqEnabled ? B_CONTROL_ON : B_CONTROL_OFF);
	
	fEQToggle->SetToolTip("15-Band EQ\nMust be enabled for navtive spectrum visualizers to work.");

	
    fEnableSpectrum = new BCheckBox("chk_spectrum", "Spectrum Visualizer [?]", new BMessage(MSG_TOGGLE_Spectrum));
	fEnableSpectrum->SetValue(cfg.showSpectrumVisuals ? B_CONTROL_ON : B_CONTROL_OFF); 
	
	fEnableSpectrum->SetToolTip("Show or hide native spectrum visualizers.\nRight mouse click on the spectrum to toggle through all modes.\nDouble left click to toggle fullscreen.\nMotorcycle spectrum: use center mouse wheel to toggle fullscreen,\nleft click to jump, and double left click to do front flip.");   

    
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
	

    
    // --- CHECK FOR MESA OPENGL DRIVER CAPABILITY ---
    // If the vendor file is missing, the player cannot render advanced visual presets.
    struct stat mesaBuffer;
    bool hasMesaDriver = (stat("/boot/system/add-ons/opengl/egl_vendor.d/libEGL_mesa.so", &mesaBuffer) == 0);




//@main2
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
    .Add(fChkShuffle)
    .Add(fShuffleFavsCheckbox)
    .Add(fCompactModeConfig)
    .Add(fChkSong)
    .Add(fChkTitle)
    .Add(fChkTheme)
    .Add(fChkDebug)  
    .Add(fChkSysTray)
   	.Add(fEQToggle)
   	.Add(fEnableladspa)  
   	.Add(fEnableSpectrum)
   	.Add(fEQContainer)
     #ifdef USE_PROJECTM
    .AddStrut(5)
    .Add(fVisualsCheckbox)
    .Add(fChkPresetTimer)
    .Add(fPresetToggle)    
    .Add(fPresetScroll)       
     #endif
    .AddGlue()
.End();

    if (!cfg.showNotifications) fSizeContainer->Hide();	
	if (!cfg.eqEnabled) {
    	fEQContainer->Hide();
    	fEnableladspa->Hide();
    	fEnableSpectrum->Hide();
	}
	if (!cfg.debugEnable) {
		fChkSysTray->Hide();
		fEnableladspa->Hide();	
	}
    if (!hasMesaDriver) {
    	if (fVisualsCheckbox)  fVisualsCheckbox->Hide();
        if (fChkPresetTimer)   fChkPresetTimer->Hide();
        if (fPresetToggle)     fPresetToggle->Hide();
        if (fPresetScroll)     fPresetScroll->Hide();   
    }
    if (!cfg.showVisuals) {    	
         fPresetToggle->Hide(); 
         fChkPresetTimer->Hide(); 
    } else {

        StartVisuals();
    }

    


    // ==========================================
    // TAB 4: ABOUT VIEW
    // ==========================================
	fAboutGroup = new BGroupView(B_VERTICAL, 4);
	fAboutGroup->SetName("About");
    fAboutGroup->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

    // Header Styles
    BFont titleFont(be_bold_font);
    titleFont.SetSize(26.0);

    BFont boldFont(be_bold_font);
    boldFont.SetSize(14.0);
    
    // Year calc
    std::time_t t = std::time(nullptr);
    std::tm* now = std::localtime(&t);
    int currentYear = now->tm_year + 1900; 

    // Icon and Text Components
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
    
    BString copyString;
    copyString.SetToFormat("MIT License | Copyright " B_UTF8_COPYRIGHT " %d Kris Beazley", currentYear);

    BStringView* txtCopy = new BStringView("abt_copy", copyString.String());
    txtCopy->SetAlignment(B_ALIGN_CENTER);
    
    BStringView* txtEmail = new BStringView("abt_mail", "supermusicthingy@epluribusunix.net");
    txtEmail->SetAlignment(B_ALIGN_CENTER);
    
    BStringView* txtAI = new BStringView("abtAI", "AI Assisted");
    txtAI->SetAlignment(B_ALIGN_CENTER);

    // --- INSTANTIATE THE NEW BACKGROUND ENGINE VIEW ---
    SnowyHeaderView* animatedHeaderBackground = new SnowyHeaderView("animated_top");

    // --- BUILD STATIC ITEMS INSIDE THE ANIMATED BACKGROUND VIEW CONTAINER ---
    BLayoutBuilder::Group<>(animatedHeaderBackground, B_VERTICAL, 4)
        .SetInsets(0)
        .Add(appLogo) // Place the leaf logo inside the background layer so it floats over snow
        .AddStrut(5)
        .Add(titleApp)
        .Add(txtVer)
        .Add(txturl)
        .AddGroup(B_HORIZONTAL, 150) 
            .AddGlue()        
            .Add(iconLink)	
            .AddGlue()          
        .End()
        .AddStrut(12) 
        .Add(txtCopy)        
        .Add(txtEmail)
        .Add(txtAI)
    .End();

    // Layout Crawler Setup
    CreditsSlider* creditCrawler = new CreditsSlider("about_crawler");

    // --- INSTANTIATE THE NEW BACKGROUND FOOTER VIEW ---
    SnowyFooterView* animatedFooterBackground = new SnowyFooterView("animated_bottom");
    
    animatedFooterBackground->SetExplicitMinSize(BSize(B_SIZE_UNSET, 250.0f));
    animatedFooterBackground->SetExplicitMaxSize(BSize(B_SIZE_UNSET, 250.0f));

    // --- ASSEMBLE MASTER TAB CONTAINER ---
    BLayoutBuilder::Group<>(fAboutGroup, B_VERTICAL, 0)
        .SetInsets(10)
        .Add(animatedHeaderBackground, 0.0f) // Top headers + snow
        .AddStrut(20)                        // Middle open spacing
        .Add(creditCrawler, 1.0f)            // Crawler centered zone (flexible size)
        .Add(animatedFooterBackground, 0.0f) // 150px Snow Footer
    .End();


//----------- End About Tab





    // Attach only the Player Group Tab initially
    fTabView->AddTab(fPlayerGroup);
    
    // Set up the dynamic pointers but do NOT manually AddTab here.
    // The MSG_COMPACTM_CHANGED handler block will build or remove them based on config!
    fStationTab = nullptr;
    fFavTab = nullptr;
    fConfigTab = nullptr;
    fAboutTab = nullptr;

    // Final Window Layout 
    BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
        .SetInsets(0)
        .Add(fTabView)
    .End();
    
    PopulateStationList();   
    UpdateFavButtons();
    DownloadStationIcons();    
    RefreshFavorites();



    std::string configPath = std::string(getenv("HOME")) + "/config/settings/SuperMusicThingy/milk_presets/";
    PopulatePresetList(fPresetList, configPath.c_str());


	if (fVolumeSlider) {
    	fVolumeSlider->SetValue(static_cast<int32>(cfg.currentVolume));
	}
	if (mpv) {
    	double vol = static_cast<double>(cfg.currentVolume);
    	mpv_set_property(mpv, "volume", MPV_FORMAT_DOUBLE, &vol);
	}


    // =================================================================
    // UNIFY INITIALIZATION BY CALLING THE COMPACT MODE HANDLER
    // =================================================================
    BMessage setupLayoutMsg(MSG_COMPACTM_CHANGED);
    // Tell the handler that 'this' window is the source so it syncs UI parameters accurately
    setupLayoutMsg.AddPointer("source", this); 
    this->PostMessage(&setupLayoutMsg);   

}

void SuperMusicWindow::ResizeWindowToFit() {
	// 1. Force the layout engine to calculate its exact needed limits right now
	this->Layout(true);

	// 2. Query what the minimum and preferred constraints are for the whole tab layout
	float minWidth, maxWidth, minHeight, maxHeight;
	GetSizeLimits(&minWidth, &maxWidth, &minHeight, &maxHeight);

	// 3. Set the new explicit bounds so the user can't shrink it smaller than contents
	SetSizeLimits(minWidth, maxWidth, minHeight, maxHeight);

	// 4. Smoothly snap the window width and height to match its exact minimum footprint
	ResizeTo(minWidth, minHeight);
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
		

      
         case MSG_CFG_THEME: {
            BCheckBox* chk = dynamic_cast<BCheckBox*>(FindView("chk_theme"));
            if (chk) {
                // If we are currently starting up, preserve the user's existing config file theme!
                // Otherwise, update the theme variable based on the live user checkbox interaction click.
                if (!fIsStartingUp) {
                    cfg.uTheme = (chk->Value() == B_CONTROL_ON) ? "Dark" : "Default";
                    save_config();
                }
                
                ApplyTheme(); 
                
                // Rebuild the tabs to force the top navigation buttons to snap into place
                if (!cfg.compactMode && fTabView) {
                    
                    // --- SMART CONTEXT RETENTION ENGINE ---
                    std::string savedTabName = "Radio"; // Safe fallback default
                    
                    // If an explicit programmatic override is active (like unhiding), use that!
                    // Otherwise, safely fallback to tracking whatever tab the user was looking at.
                    if (!fOverrideTabTarget.IsEmpty()) {
                        savedTabName = fOverrideTabTarget.String();
                    } else {
                        int32 currentSelection = fTabView->Selection();
                        if (currentSelection >= 0) {
                            BTab* activeTab = fTabView->TabAt(currentSelection);
                            if (activeTab && activeTab->Label()) {
                                savedTabName = activeTab->Label();
                            }
                        }
                    }

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

                    // --- PROGRAMMATIC TARGET RESOLUTION ---
                    const char* targetTabLabel = fIsStartingUp ? "Radio" : savedTabName.c_str(); 

                    for (int32 j = 0; j < fTabView->CountTabs(); j++) {
                        BTab* currentTab = fTabView->TabAt(j);
                        if (currentTab && currentTab->Label() && strcmp(currentTab->Label(), targetTabLabel) == 0) {
                            fTabView->Select(j);
                            break;
                        }
                    }

                    // Consume the override token so subsequent standard checkbox clicks track naturally
                    fOverrideTabTarget = "";

                    // 4. Force the inner controls to blend seamlessly with the panel backgrounds
                    rgb_color bgDarkColor = rgb_color{40, 40, 40, 255}; 
                    bool useDarkColors = (cfg.uTheme == "Dark");

                    // Style the 15-band EQ sliders array
                    for (int i = 0; i < 15; i++) {
                        if (fEQSliders[i]) {
                            if (useDarkColors) {
                                fEQSliders[i]->SetViewColor(bgDarkColor);
                                fEQSliders[i]->SetLowColor(bgDarkColor);
                            } else {
                                fEQSliders[i]->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
                                fEQSliders[i]->SetLowColor(ui_color(B_PANEL_BACKGROUND_COLOR));
                            }
                            fEQSliders[i]->Invalidate();
                        }
                    }

                    // Style the limiter controllers
                    BSlider* limiterSliders[] = { fLimitInput, fLimitLimit, fLimitRelease };
                    
                    for (int s = 0; s < 3; s++) {
                        if (limiterSliders[s]) {
                            if (useDarkColors) {
                                limiterSliders[s]->SetViewColor(bgDarkColor);
                                limiterSliders[s]->SetLowColor(bgDarkColor);
                                limiterSliders[s]->SetHighColor(255, 255, 255, 255); // Force labels to white
                            } else {
                                limiterSliders[s]->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
                                limiterSliders[s]->SetLowColor(ui_color(B_PANEL_BACKGROUND_COLOR));
                                limiterSliders[s]->SetHighColor(0, 0, 0, 255); // Default black text
                            }
                            
                            // Recolor any internal view wrappers inside the components
                            for (int32 c = 0; c < limiterSliders[s]->CountChildren(); c++) {
                                BView* child = limiterSliders[s]->ChildAt(c);
                                if (child) {
                                    if (useDarkColors) {
                                        child->SetViewColor(bgDarkColor);
                                        child->SetLowColor(bgDarkColor);
                                        child->SetHighColor(255, 255, 255, 255);
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

                    // --- 64-BIT EXCLUSIVE GROUP LAYOUT RESET FIX ---
                    if (fConfigGroup) {
                        fConfigGroup->InvalidateLayout(true);
                        fConfigGroup->Layout(true);
                        fConfigGroup->Invalidate();
                    }
                    if (fStationGroup) fStationGroup->InvalidateLayout(true);
                    if (fFavGroup)     fFavGroup->InvalidateLayout(true);
                    if (fAboutGroup)   fAboutGroup->InvalidateLayout(true);
                }

                this->InvalidateLayout(true);
                this->Layout(true);
                fIsStartingUp = false; 
            }
            break;            
        }





		case MSG_SHOW_TITLE: {
			BCheckBox* chk = dynamic_cast<BCheckBox*>(FindView("fChkTitle_toggle"));
			if (chk) {
				bool targetState = (chk->Value() == B_CONTROL_ON);
				cfg.enableTitles = targetState;
				save_config();
				
				// --- ASYNC DOUBLE-PUMP HACK FOR TITLES ---
				// 1. Send the inverted state to shake up the layout engine topology
				cfg.enableTitles = !targetState; 
				BMessage invertMessage(MSG_COMPACTM_CHANGED);
				invertMessage.AddPointer("source", this);
				this->PostMessage(&invertMessage);

				// 2. Immediately send the actual true target state right behind it
				cfg.enableTitles = targetState;
				BMessage refreshMessage(MSG_COMPACTM_CHANGED);
				refreshMessage.AddPointer("source", this); 
				this->PostMessage(&refreshMessage);
				
			}
			break;
		}
        
		case MSG_SHOW_DESC: {
			BCheckBox* chk = dynamic_cast<BCheckBox*>(FindView("fChkSong_toggle"));
			if (chk) {
				bool targetState = (chk->Value() == B_CONTROL_ON);
				cfg.enableDescriptions = targetState;            	
				save_config();
				
				// --- ASYNC DOUBLE-PUMP HACK FOR DESCRIPTIONS ---
				// 1. Send the inverted state to shake up the layout engine topology
				cfg.enableDescriptions = !targetState;
				BMessage invertMessage(MSG_COMPACTM_CHANGED);
				invertMessage.AddPointer("source", this);
				this->PostMessage(&invertMessage);

				// 2. Immediately send the actual true target state right behind it
				cfg.enableDescriptions = targetState;            	
				BMessage refreshMessage(MSG_COMPACTM_CHANGED);
				refreshMessage.AddPointer("source", this); 
				this->PostMessage(&refreshMessage);
				
			}
			break;
		}








	case B_COLORS_UPDATED: {
		// 1. Refresh internal color definitions without modifying structural nodes
		ApplyTheme();
		
		// 2. Force the view hierarchy layers to recalculate and redraw with the new system colors
		if (fPlayerGroup) {
			fPlayerGroup->InvalidateLayout(true);
			fPlayerGroup->Invalidate(true);
		}
		
		// 3. Inform the window's structural framework to update its presentation layout tree
		this->InvalidateLayout(true);
		
		// 4. Break early to protect layout constraints and tab states
		break;
	}
	
	
	    	
	case MSG_TOGGLE_EQ: {
		if (cfg.debugEnable) printf("[DEBUG] [MSG_TOGGLE_EQ] Event hook triggered.\n");
		BCheckBox* chk = dynamic_cast<BCheckBox*>(FindView("eq_toggle"));
		if (chk) {
			// Directly parse state using explicit ternary bounds
			cfg.eqEnabled = (chk->Value() == B_CONTROL_ON) ? 1 : 0;            
			if (cfg.debugEnable) printf("[DEBUG] [MSG_TOGGLE_EQ] Checkbox evaluated state: %d (Target config: cfg.eqEnabled)\n", cfg.eqEnabled);
	
			if (cfg.eqEnabled) {
				if (cfg.debugEnable) printf("[DEBUG] [MSG_TOGGLE_EQ] Showing associated EQ panel wrappers.\n");
				if (fEnableladspa) fEnableladspa->Show();
				if (fEnableSpectrum) fEnableSpectrum->Show();
				if (fChkShuffle) fChkShuffle->Show();  
				if (fEQContainer) fEQContainer->Show();
			} else {
				if (cfg.debugEnable) printf("[DEBUG] [MSG_TOGGLE_EQ] Hiding associated EQ panel wrappers.\n");
				if (fEnableladspa) fEnableladspa->Hide();  
				if (fEnableSpectrum) fEnableSpectrum->Hide();
				if (fEQContainer) fEQContainer->Hide();
			}
			
			if (cfg.debugEnable) printf("[DEBUG] [MSG_TOGGLE_EQ] Persisting config, updating mpv filters, and applying themes.\n");
			save_config();
			UpdateMPVFilters();
			//ApplyTheme(); 

			// --- DELEGATE STRUCTURAL LAYOUT TO MASTER ENGINE ---
			if (cfg.debugEnable) printf("[DEBUG] [MSG_TOGGLE_EQ] Allocating and queuing master layout payload (MSG_COMPACTM_CHANGED).\n");
			BMessage* layoutRefresh = new BMessage(MSG_COMPACTM_CHANGED);
			status_t ptrStatus = layoutRefresh->AddPointer("source", chk); // Pass check control as source pointer
			status_t postStatus = this->PostMessage(layoutRefresh);
			
			if (cfg.debugEnable) printf("[DEBUG] [MSG_TOGGLE_EQ] Thread Messaging Report -> Payload pointer bind status: %d, PostMessage status: %d\n", 
				(int)ptrStatus, (int)postStatus);
		} else {
			if (cfg.debugEnable) printf("[DEBUG] [MSG_TOGGLE_EQ] [WARNING] Failed to locate \"eq_toggle\" via FindView lookup!\n");
		}
		break;
	}

	case MSG_TOGGLE_Spectrum: {
		if (cfg.debugEnable) printf("[DEBUG] [MSG_TOGGLE_Spectrum] Event hook triggered.\n");
		if (fEnableSpectrum) {
			cfg.showSpectrumVisuals = (fEnableSpectrum->Value() == B_CONTROL_ON) ? 1 : 0;
			if (cfg.debugEnable) printf("[DEBUG] [MSG_TOGGLE_Spectrum] Checkbox evaluated state: %d\n", cfg.showSpectrumVisuals);
		} else {
			if (cfg.debugEnable) printf("[DEBUG] [MSG_TOGGLE_Spectrum] [WARNING] fEnableSpectrum pointer is null!\n");
		}
		
		save_config();
		this->UpdateMPVFilters();
		//ApplyTheme();
		
		// --- DIRECT VISIBILITY DOUBLE-PUMP RECONSTRUCTION ---
		if (fSpectrum) {
			// Save the final state we want the spectrum view to settle on
			bool finalTargetState = (cfg.showSpectrumVisuals == 1);

			// PUMP 1: Force change the target view to its inverse state directly 
			if (finalTargetState) {
				fSpectrum->Hide(); // If we want to show it, force hide it first
			} else {
				fSpectrum->Show(); // If we want to hide it, force show it first
			}
			
			// Let the layout container know its tree has violently shifted
			if (fSpectrum->Parent()) fSpectrum->Parent()->InvalidateLayout(true);

			// Post a layout refresh pass while the view is flipped upside down
			BMessage* invertMsg = new BMessage(MSG_COMPACTM_CHANGED);
			invertMsg->AddPointer("source", fEnableSpectrum);
			this->PostMessage(invertMsg);

			// PUMP 2: Immediately restore the real target visibility configuration
			if (finalTargetState) {
				fSpectrum->Show();
			} else {
				fSpectrum->Hide();
			}

			// Invalidate structural hierarchies up to root again
			if (fSpectrum->Parent()) fSpectrum->Parent()->InvalidateLayout(true);

			// Post the final clean configuration execution update message
			BMessage* finalMsg = new BMessage(MSG_COMPACTM_CHANGED);
			finalMsg->AddPointer("source", fEnableSpectrum);
			this->PostMessage(finalMsg);

			if (cfg.debugEnable) {
				printf("[DEBUG] [MSG_TOGGLE_Spectrum] Fixed Double-pump visibility cycle applied cleanly.\n");
			}
		} else {
			BMessage* layoutRefresh = new BMessage(MSG_COMPACTM_CHANGED);
			layoutRefresh->AddPointer("source", nullptr);
			this->PostMessage(layoutRefresh);
		}
		
		break;
	}



	
	// @Fullscreen
	case MSG_TOGGLE_FULLSCREEN: {
    	static bool sIsFullscreen = false;

    	static BRect sSavedWindowFrame;
    	static uint32 sSavedLook;
    	static uint32 sSavedFeel;
    	static BView* sOriginalParent = nullptr;

    	sIsFullscreen = !sIsFullscreen;
    	this->fFullscreenActive = sIsFullscreen;

    	if (sIsFullscreen) {
        	// --- 1. HIDE THE MOUSE CURSOR ---
        	be_app->HideCursor();

        	sSavedWindowFrame = Frame();
        	sSavedLook = Look();
        	sSavedFeel = Feel();

        	BScreen screen(this);
        	BRect screenFrame = screen.Frame();

            if (fSpectrum) {
                sOriginalParent = fSpectrum->Parent();
                fSpectrum->RemoveSelf();
                
                fSpectrum->SetExplicitMinSize(BSize(screenFrame.Width(), screenFrame.Height()));
                fSpectrum->SetExplicitMaxSize(BSize(screenFrame.Width(), screenFrame.Height()));
                fSpectrum->SetExplicitPreferredSize(BSize(screenFrame.Width(), screenFrame.Height()));
                
                this->AddChild(fSpectrum);
                fSpectrum->ResizeTo(screenFrame.Width(), screenFrame.Height());
                fSpectrum->MoveTo(0, 0);
                fSpectrum->Show();
            }

            if (fTabView) fTabView->Hide();

            SetLook(B_NO_BORDER_WINDOW_LOOK);
            SetFeel(B_MODAL_ALL_WINDOW_FEEL); 
            MoveTo(screenFrame.left, screenFrame.top);
            ResizeTo(screenFrame.Width(), screenFrame.Height());
            
            this->Layout(true);

    	} else {
        	// --- 2. RESTORE THE MOUSE CURSOR ---
        	be_app->ShowCursor();

        	if (fSpectrum) {
            	fSpectrum->RemoveSelf();
            fSpectrum->RemoveSelf();
            fSpectrum->SetExplicitMinSize(BSize(B_SIZE_UNSET, B_SIZE_UNSET));
            fSpectrum->SetExplicitMaxSize(BSize(B_SIZE_UNSET, B_SIZE_UNSET));
            fSpectrum->SetExplicitPreferredSize(BSize(B_SIZE_UNSET, B_SIZE_UNSET));
        	}

        	SetLook((window_look)sSavedLook);
        	SetFeel((window_feel)sSavedFeel);
            
            // Re-apply original pre-fullscreen bounding frames
        	MoveTo(sSavedWindowFrame.left, sSavedWindowFrame.top);
        	ResizeTo(sSavedWindowFrame.Width(), sSavedWindowFrame.Height());

        	if (fTabView) fTabView->Show();

        	if (fSpectrum && sOriginalParent) {
            	sOriginalParent->AddChild(fSpectrum);
        	}

        // --- REBUILT COMPACT MESSAGE PIPELINE ---
        BMessage refreshLayout(MSG_COMPACTM_CHANGED);
        refreshLayout.AddPointer("source", this);
        refreshLayout.AddBool("force_compact_state", cfg.compactMode);
        refreshLayout.AddBool("initial_boot_pass", true); 
        
        // Call MessageReceived directly so layout modifications 
        // finish executing before we command the layout engine to refresh.
        this->MessageReceived(&refreshLayout); 
        
        // Now recalculate the layout tree with the clean state applied
        this->Layout(true); 
    	}
    
    	if (fSpectrum) fSpectrum->Invalidate(); 
    	break;
	}





		
		// @Modes
		case MSG_COMPACTM_CHANGED: {
			if (cfg.debugEnable) printf("[DEBUG] MSG_COMPACTM_CHANGED received!\n");
			


			void* source = nullptr;
			message->FindPointer("source", &source);    
			bool newState = cfg.compactMode;		
			
			// Intercept payload flags to ensure asynchronous passes execute correctly
			bool forcedState = false;
			status_t forceFindStatus = message->FindBool("force_compact_state", &forcedState);
			
			if (cfg.debugEnable) printf("[DEBUG] Init State - source ptr: %p, config compactMode: %d, forced flag status: %d, forcedState val: %d\n", source, cfg.compactMode, (int)forceFindStatus, forcedState);


			// Track if this pass is an intentional structural mode transition (Normal <-> Compact)
			bool isRealModeTransition = false;

			if (forceFindStatus == B_OK) {
				if (cfg.compactMode != forcedState) isRealModeTransition = true;
				cfg.compactMode = forcedState;
				newState = forcedState;
				if (cfg.debugEnable) printf("[DEBUG] Forced state payload applied. New compact state: %d\n", newState);
			} else {
				if (source == fCompactModeConfig || source == fCompactModeRadio) {
					newState = (source == fCompactModeConfig) ? (fCompactModeConfig->Value() == B_CONTROL_ON) 
																			: (fCompactModeRadio->Value() == B_CONTROL_ON);
					if (cfg.compactMode != newState) isRealModeTransition = true;
					cfg.compactMode = newState;
					save_config();				
					if (fCompactModeRadio) fCompactModeRadio->SetValue(newState ? B_CONTROL_ON : B_CONTROL_OFF);
					if (fCompactModeConfig) fCompactModeConfig->SetValue(newState ? B_CONTROL_ON : B_CONTROL_OFF);
					if (cfg.debugEnable) printf("[DEBUG] Layout mode changed by user. Evaluated compact state: %d\n", newState);
				} else {
					if (fCompactModeConfig) fCompactModeConfig->SetValue(cfg.compactMode ? B_CONTROL_ON : B_CONTROL_OFF);
					if (fCompactModeRadio) fCompactModeRadio->SetValue(cfg.compactMode ? B_CONTROL_ON : B_CONTROL_OFF);
					if (cfg.debugEnable) printf("[DEBUG] Pure layout refresh pass (Checkbox toggle). Retaining active compact state: %d\n", cfg.compactMode);
				}
			}			
			
			// Safety Check: Stop if critical views are missing
			if (fPlayerGroup == nullptr || fControlStack == nullptr)
				break;

			// --- 1. GLOBAL LAYOUT SCALE METRICS ---
			float scale = be_plain_font->Size() / 12.0f; 
			float artSize = cfg.compactMode ? (180 * scale) : (350 * scale);
			float btnSize = cfg.compactMode ? (40 * scale)  : (75 * scale);
			float favSize = cfg.compactMode ? (40 * scale)  : (75 * scale);
			float VolSize = cfg.compactMode ? (40 * scale)  : (75 * scale);

			// Enforce explicit base properties on primary layout nodes
			fPlayerGroup->GroupLayout()->SetOrientation(cfg.compactMode ? B_HORIZONTAL : B_VERTICAL);
			fPlayerGroup->GroupLayout()->SetSpacing(cfg.compactMode ? 5 : 10);
			fControlStack->GroupLayout()->SetOrientation(cfg.compactMode ? B_VERTICAL : B_HORIZONTAL);    
			 
			if (fDescView) ((SongLabel*)fDescView)->SetCompactMode(cfg.compactMode);
			if (fSongView) ((SongLabel*)fSongView)->SetCompactMode(cfg.compactMode);
		
			
			// --- ROBUST STRUCTURAL UPDATE FOR DESCRIPTION VIEW ---
			if (fDescView) {
				// 1. Sanitize the view leaf pointer against memory corruption
				if ((uintptr_t)fDescView > 0x1000) {
					// Query local structural visibility state instead of inherited window state
					bool wasLocallyHidden = fDescView->IsHidden(fDescView);
					bool targetHiddenState = !cfg.enableDescriptions;

					if (wasLocallyHidden != targetHiddenState) {
						if (!targetHiddenState) {
							fDescView->Show();
							if (cfg.debugEnable) printf("[DEBUG] Descriptions structural visibility -> SHOW.\n");
						} else {
							fDescView->Hide();
							if (cfg.debugEnable) printf("[DEBUG] Descriptions structural visibility -> HIDE.\n");
						}

						// 2. Safely bubble layout invalidation context up to the top structural ancestor
						BView* parentNode = fDescView->Parent();
						if (parentNode && (uintptr_t)parentNode > 0x1000) {
							
							// Flush the immediate parent layout container deeply
							parentNode->InvalidateLayout(true);
							
							// Walk layout hierarchy upwards if nestled inside multiple layout containers
							while (parentNode->Parent() != nullptr) {
								BView* nextParent = parentNode->Parent();
								
								// Stop loop immediately if pointer wraps around or corrupts
								if (!nextParent || (uintptr_t)nextParent <= 0x1000)
									break;
									
								parentNode = nextParent;
								parentNode->InvalidateLayout(false); // Clear parent element matrix caches
							}
							
							// 3. Anchor the root layout container explicitly to force an App Server redraw
							if (parentNode->GetLayout()) {
								parentNode->GetLayout()->InvalidateLayout(true);
							}
						}

						// 4. Fixed: Force synchronous top-down coordinate reconciliation directly.
						// No window looper locking required because 'this' is already the BWindow loop.
						this->Layout(true); 
					}
				}
			}


			// --- ROBUST STRUCTURAL UPDATE FOR SONG VIEW ---
			if (fSongView) {
				// 1. Sanitize the view leaf pointer against memory corruption
				if ((uintptr_t)fSongView > 0x1000) {
					// Query local structural visibility state instead of inherited window state
					bool wasLocallyHidden = fSongView->IsHidden(fSongView);
					bool targetHiddenState = !cfg.enableTitles;

					if (wasLocallyHidden != targetHiddenState) {
						if (!targetHiddenState) {
							fSongView->Show();
							if (cfg.debugEnable) printf("[DEBUG] Titles structural visibility -> SHOW.\n");
						} else {
							fSongView->Hide();
							if (cfg.debugEnable) printf("[DEBUG] Titles structural visibility -> HIDE.\n");
						}

						// 2. Safely bubble layout invalidation context up to the top structural ancestor
						BView* parentNode = fSongView->Parent();
						if (parentNode && (uintptr_t)parentNode > 0x1000) {
							
							// Flush the immediate parent layout container deeply
							parentNode->InvalidateLayout(true);
							
							// Walk layout hierarchy upwards if nestled inside multiple layout containers
							while (parentNode->Parent() != nullptr) {
								BView* nextParent = parentNode->Parent();
								
								// Stop loop immediately if pointer wraps around or corrupts
								if (!nextParent || (uintptr_t)nextParent <= 0x1000)
									break;
									
								parentNode = nextParent;
								parentNode->InvalidateLayout(false); // Clear parent element matrix caches
							}
							
							// 3. Anchor the root layout container explicitly to force an App Server redraw
							if (parentNode->GetLayout()) {
								parentNode->GetLayout()->InvalidateLayout(true);
							}
						}

						// 4. Force synchronous top-down coordinate reconciliation directly.
						// Running natively in BWindow::MessageReceived means looper is already locked.
						this->Layout(true); 
					}
				}
			}


		
			

			
			
			// --- 2. COMPACT MODE GEOMETRY BRANCH ---
			if (cfg.compactMode) {
				
				/* Old Double Pump Logic
				static bool sInitialSyncDone = false;
				if (!sInitialSyncDone && (source == this || source == nullptr)) {
					sInitialSyncDone = true;
					BMessage* normalMsg = new BMessage(MSG_COMPACTM_CHANGED);
					normalMsg->AddPointer("source", source);
					normalMsg->AddBool("force_compact_state", false);
					this->PostMessage(normalMsg);

					BMessage* compactMsg = new BMessage(MSG_COMPACTM_CHANGED);
					compactMsg->AddPointer("source", source);
					compactMsg->AddBool("force_compact_state", true);
					this->PostMessage(compactMsg);
					break; 
				}
				*/				
								
				
				// New soft pump logic
				// Updated robust soft pump execution block
				static bool sInitialSyncDone = false;
                
                // Read the restoration flag out of the incoming loop message
                bool forceRebuildPass = false;
                if (message) message->FindBool("initial_boot_pass", &forceRebuildPass);

                // Run the pump if it is the first boot OR an intentional layout refresh request
				if ((!sInitialSyncDone || forceRebuildPass) && (source == this || source == nullptr)) {
    			    sInitialSyncDone = true;
    
    			    BMessage* initMsg = new BMessage(MSG_COMPACTM_CHANGED);
    			    initMsg->AddPointer("source", source);
    			    initMsg->AddBool("force_compact_state", cfg.compactMode);
    			    initMsg->AddBool("initial_boot_pass", false); // Clear flag to prevent infinite loops
    			    this->PostMessage(initMsg);
    			    break; 
				}


				



				// --- ROBUST STRUCTURAL UPDATE FOR COMPACT MODE BRANCH ---
				
				float expandedWidth = 375.0f * scale; 
				float sliderWidth = (btnSize * 2.4f) + 10.0f;		
				
				float labelHeight = 0.0f; 
				if (cfg.enableTitles) labelHeight += (24.0f * scale);
				if (cfg.enableDescriptions)  labelHeight += (24.0f * scale);
				
				float specWidth = expandedWidth;
				float specHeight = 10.0f * scale; 
				
				bool featuresActive = (cfg.showSpectrumVisuals && cfg.eqEnabled);

				if (cfg.showSpectrumVisuals) {
					specWidth = 400.0f * scale;
					specHeight = (labelHeight == 0.0f) ? 150.0f * scale : 100.0f * scale;
				}
				
				// Size checks		
				
				if ((cfg.showSpectrumVisuals) && (cfg.enableDescriptions && cfg.enableTitles)) { 
   					 artSize = 170.0f * scale; 
				}
				if ((cfg.showSpectrumVisuals) && (cfg.enableDescriptions || cfg.enableTitles)) { 
   					 artSize = 170.0f * scale; 
				}
				
				if ((!cfg.showSpectrumVisuals || !cfg.eqEnabled) && (cfg.enableDescriptions && cfg.enableTitles)) { 
   					 artSize = 115.0f * scale; 
				}
				
				if ((!cfg.showSpectrumVisuals || !cfg.eqEnabled) && (!cfg.enableDescriptions && cfg.enableTitles)) { 
   					 artSize = 115.0f * scale; 
				}
				if ((!cfg.showSpectrumVisuals || !cfg.eqEnabled) && (cfg.enableDescriptions && !cfg.enableTitles)) { 
   					 artSize = 115.0f * scale; 
				}
				
				if ((!cfg.showSpectrumVisuals || !cfg.eqEnabled) && (!cfg.enableDescriptions && !cfg.enableTitles)) { 
   					 artSize = 100.0f * scale; 
				}
				
				
				
				float finalWidth; 		
				if ((!cfg.showSpectrumVisuals || !cfg.eqEnabled) && (cfg.enableDescriptions || cfg.enableTitles))  {
						finalWidth = 250.0f * scale;
					} else {
						finalWidth = B_SIZE_UNSET;
						
				}


				// 1. UPDATE SPECTRUM CORE VIEW
				if (fSpectrum) {
					bool wasLocallyHidden = fSpectrum->IsHidden(fSpectrum);
					bool targetHiddenState = !featuresActive;
					
					

					if (featuresActive) {
						if (cfg.debugEnable) {
							printf("[DEBUG] Compact Mode -> Setting Explicit Spectrum Size: %.2f x %.2f\n", 
								specWidth, specHeight);
						}
						fSpectrum->SetExplicitMinSize(BSize(specWidth, specHeight));
						fSpectrum->SetExplicitMaxSize(BSize(specWidth, specHeight));
						fSpectrum->SetExplicitPreferredSize(BSize(specWidth, specHeight));
					} else {
						fSpectrum->SetExplicitMinSize(BSize(0, 0));
						fSpectrum->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 0));
						fSpectrum->SetExplicitPreferredSize(BSize(0, 0));
					}

					// Evaluate visibility change and bubble invalidation up the tree
					if (wasLocallyHidden != targetHiddenState) {
						if (!targetHiddenState) {
							fSpectrum->Show();
							if (cfg.debugEnable) printf("[DEBUG] Compact Spectrum structural visibility -> SHOW.\n");
						} else {
							fSpectrum->Hide();
							if (cfg.debugEnable) printf("[DEBUG] Compact Spectrum structural visibility -> HIDE.\n");
						}

						BView* parentNode = fSpectrum->Parent();
						if (parentNode) {
							parentNode->InvalidateLayout(true);
							while (parentNode->Parent() != nullptr) {
								parentNode = parentNode->Parent();
								parentNode->InvalidateLayout(false);
							}
						}
					}
				}
				
				// 2. UPDATE METADATA & SPECTRUM STACK CONTAINER
				if (fMetaAndSpectrumStack != nullptr && (uintptr_t)fMetaAndSpectrumStack > 0x1000) {  
					if (featuresActive) {
						// Floating layout protection logic to prevent layout expansion
						fMetaAndSpectrumStack->SetExplicitMinSize(BSize(B_SIZE_UNSET, B_SIZE_UNSET));
						fMetaAndSpectrumStack->SetExplicitPreferredSize(BSize(B_SIZE_UNSET, B_SIZE_UNSET));
						fMetaAndSpectrumStack->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET)); 
						
						if (fMetaAndSpectrumStack->IsHidden()) fMetaAndSpectrumStack->Show();
					} else {
						// Collapse completely to 0 width/height when visualizer is inactive
						fMetaAndSpectrumStack->SetExplicitMinSize(BSize(0, 0));
						fMetaAndSpectrumStack->SetExplicitPreferredSize(BSize(0, 0));
						fMetaAndSpectrumStack->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 0)); 
						fMetaAndSpectrumStack->Hide();
					}
				}

				// 3. MAINTAIN COMPACT SPECTRUM WRAPPER WITH FLOATING LOGIC
				if (fCompactSpectrumWrapper) {
					if (featuresActive) {
						// Bulletproof floating logic keeps window bounds flexible
						fCompactSpectrumWrapper->SetExplicitMinSize(BSize(B_SIZE_UNSET, B_SIZE_UNSET));
						fCompactSpectrumWrapper->SetExplicitPreferredSize(BSize(B_SIZE_UNSET, B_SIZE_UNSET));
						fCompactSpectrumWrapper->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
						
						if (fCompactSpectrumWrapper->IsHidden()) fCompactSpectrumWrapper->Show();
					} else {
						fCompactSpectrumWrapper->SetExplicitMinSize(BSize(finalWidth, 0));
						fCompactSpectrumWrapper->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 0));
						fCompactSpectrumWrapper->SetExplicitPreferredSize(BSize(finalWidth, 0));
					}
				}


				// ONLY modify structural tree during a true compact mode swap pass
				// ====================================================================
				// --- STRUCTURAL HIERARCHY VERIFICATION PASS ---
				// ====================================================================
				// Ensure fControlStack is detached from Normal and attached to Compact wrapper
				if (fNormalControlsWrapper && fNormalControlsWrapper->GroupLayout() && fControlStack->Parent() == fNormalControlsWrapper) {
					fNormalControlsWrapper->GroupLayout()->RemoveView(fControlStack);
				}
				
				if (fCompactControlsWrapper && fCompactControlsWrapper->GroupLayout() && fControlStack->Parent() != fCompactControlsWrapper) {
					fCompactControlsWrapper->GroupLayout()->RemoveView(fControlStack);
					
					// 1. CLEAR THE INNER ITEMS ARRAY COMPLETELY BEFORE BUILDING
					if (fControlStack->GroupLayout()) {
						while (fControlStack->GroupLayout()->CountItems() > 0) {
							fControlStack->GroupLayout()->RemoveItem((int32)0);
						}
					}
					
					// 2. REBUILD THE 2x2 GRID POPULATED WITH RE-LINKED BUTTON POINTERS
					BLayoutBuilder::Group<>(fControlStack, B_VERTICAL, 4)
						.AddGlue()
						.AddGroup(B_HORIZONTAL, 4)
							.Add(fStopBtn)
							.Add(fPauseBtn)
						.End()
						.AddGroup(B_HORIZONTAL, 4)
							.Add(fPlayBtn)
							.Add(fShuffleBtn)
						.End()
						.AddGlue() 
					.End();

					fCompactControlsWrapper->GroupLayout()->AddView(fControlStack, B_ALIGN_VERTICAL_CENTER);
				}

				// Enforce safe structural attachments for your spectrum visualizer
				if (fCompactSpectrumWrapper && fCompactSpectrumWrapper->GroupLayout() && fSpectrum && fSpectrum->Parent() != fCompactSpectrumWrapper) {
					if (fSpectrum->Parent()) fSpectrum->RemoveSelf();
					
					fSpectrum->InvalidateLayout(true);
					fSpectrum->SetExplicitMinSize(BSize(finalWidth, specHeight));
					fSpectrum->SetExplicitMaxSize(BSize(finalWidth, specHeight));
					fSpectrum->SetExplicitPreferredSize(BSize(finalWidth, specHeight));
					
					fCompactSpectrumWrapper->GroupLayout()->AddView(fSpectrum);
				}

				// Handle dynamic tab strip extraction on every configuration event loop
				if (fTabView && fTabView->CountTabs() > 1) {
					for (int32 i = fTabView->CountTabs() - 1; i >= 0; i--) {
						BTab* tab = fTabView->TabAt(i);
						if (tab == fStationTab || tab == fFavTab || tab == fConfigTab || tab == fAboutTab) {
							fTabView->RemoveTab(i);
						}
					}
				}

				// --- LOCK COMPACT SPECTRUM WRAPPER BOUNDS ---
				if (fCompactSpectrumWrapper) {
					fCompactSpectrumWrapper->SetExplicitMinSize(BSize(finalWidth, B_SIZE_UNSET));
					fCompactSpectrumWrapper->SetExplicitMaxSize(BSize(finalWidth, B_SIZE_UNSET));
					fCompactSpectrumWrapper->SetExplicitPreferredSize(BSize(finalWidth, B_SIZE_UNSET));
				}

				// ====================================================================
				// --- RESIZING CONTROL PARAMETERS ---
				// ====================================================================
				if (fStopBtn && fPauseBtn && fPlayBtn && fShuffleBtn && fBtnAddFav) {

					BSize compactBtnSize(btnSize, btnSize);					
					fStopBtn->SetExplicitMinSize(compactBtnSize); fStopBtn->SetExplicitMaxSize(compactBtnSize); fStopBtn->SetExplicitPreferredSize(compactBtnSize);
					fPauseBtn->SetExplicitMinSize(compactBtnSize); fPauseBtn->SetExplicitMaxSize(compactBtnSize); fPauseBtn->SetExplicitPreferredSize(compactBtnSize);
					fPlayBtn->SetExplicitMinSize(compactBtnSize); fPlayBtn->SetExplicitMaxSize(compactBtnSize); fPlayBtn->SetExplicitPreferredSize(compactBtnSize);
					fShuffleBtn->SetExplicitMinSize(compactBtnSize); fShuffleBtn->SetExplicitMaxSize(compactBtnSize); fShuffleBtn->SetExplicitPreferredSize(compactBtnSize);
					fBtnAddFav->SetExplicitMinSize(compactBtnSize); fBtnAddFav->SetExplicitMaxSize(compactBtnSize); fBtnAddFav->SetExplicitPreferredSize(compactBtnSize);
				}
				if (fControlStack) {
					fControlStack->SetExplicitMinSize(BSize(sliderWidth, B_SIZE_UNSET));
					fControlStack->SetExplicitMaxSize(BSize(sliderWidth, B_SIZE_UNSET));
					fControlStack->SetExplicitPreferredSize(BSize(sliderWidth, B_SIZE_UNSET));
				}
				

			
				
			} else { // --- 3. NORMAL MODE GEOMETRY BRANCH ---
			
			
				/*
				float finalHeight; 		
				if ((!cfg.showSpectrumVisuals || !cfg.eqEnabled) && (cfg.enableDescriptions || cfg.enableTitles))  {
						finalHeight = 125.0f * scale;
					} else {
						finalHeight = B_SIZE_UNSET;
						
				}
				*/
				
				if (cfg.debugEnable) printf("[DEBUG] Entering Normal Mode Geometry Branch.\n");

				BSize unlimited(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED);
				BSize unset(B_SIZE_UNSET, B_SIZE_UNSET);
				


				if (fVolumeSlider) {
					fVolumeSlider->SetExplicitMaxSize(unlimited);
					fVolumeSlider->SetExplicitPreferredSize(unset);
				}
				
			// --- ROBUST STRUCTURAL UPDATE FOR SpectrumVisuals VIEW ---
			if (fSpectrum) {
				// Query local structural visibility state instead of inherited window state
				bool wasLocallyHidden = fSpectrum->IsHidden(fSpectrum);
				
				// Determine target hidden state based on both features being active
				bool featuresActive = (cfg.showSpectrumVisuals && cfg.eqEnabled);
				bool targetHiddenState = !featuresActive;

				// 1. APPLY EXPLICIT DIMENSIONS AND WRAPPER CONFIGURATIONS
				if (featuresActive) {
					float normalSpecWidth = 375.0f * scale;
					float normalSpecHeight = 125.0f * scale; 
					
					if (cfg.debugEnable) {
						printf("[DEBUG] Normal Mode -> Setting Explicit Spectrum Size: %.2f x %.2f\n", 
							normalSpecWidth, normalSpecHeight);
					}
					fSpectrum->SetExplicitMinSize(BSize(normalSpecWidth, normalSpecHeight));
					fSpectrum->SetExplicitMaxSize(BSize(normalSpecWidth, normalSpecHeight));
					fSpectrum->SetExplicitPreferredSize(BSize(normalSpecWidth, normalSpecHeight));
				} else {
					if (cfg.debugEnable) {
						printf("[DEBUG] Spectrum Disabled -> Collapsing explicit size dimensions to zero.\n");
					}
					fSpectrum->SetExplicitMinSize(BSize(0, 0));
					fSpectrum->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 0));
					fSpectrum->SetExplicitPreferredSize(BSize(0, 0));
				}

				// Apply dynamic constraints to the Metadata and Spectrum container stack
				if (fMetaAndSpectrumStack && (uintptr_t)fMetaAndSpectrumStack > 0x1000) {
					BSize unsetSize(B_SIZE_UNSET, B_SIZE_UNSET);
					BSize unlimitedSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED);

					if (featuresActive) {
						fMetaAndSpectrumStack->SetExplicitMinSize(unsetSize);
						fMetaAndSpectrumStack->SetExplicitMaxSize(unlimitedSize);
						fMetaAndSpectrumStack->SetExplicitPreferredSize(unsetSize);
					} else {
						fMetaAndSpectrumStack->SetExplicitMinSize(BSize(0, 0));
						fMetaAndSpectrumStack->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
						fMetaAndSpectrumStack->SetExplicitPreferredSize(BSize(0, 0));
					}
				}

				// Maintain safety wrapper sizing limits if present in the tree
				if (fCompactSpectrumWrapper) {
					if (featuresActive) {
								
						// Leave width UNSET so it adapts to child size changes without expanding the window
						fCompactSpectrumWrapper->SetExplicitMinSize(BSize(B_SIZE_UNSET, B_SIZE_UNSET));
						fCompactSpectrumWrapper->SetExplicitPreferredSize(BSize(B_SIZE_UNSET, B_SIZE_UNSET));
						fCompactSpectrumWrapper->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
						
						if (fCompactSpectrumWrapper->IsHidden()) fCompactSpectrumWrapper->Show();
					} else {
						// Completely collapse all dimensions to zero when inactive
						fCompactSpectrumWrapper->SetExplicitMinSize(BSize(0, 0));
						fCompactSpectrumWrapper->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 0));
						fCompactSpectrumWrapper->SetExplicitPreferredSize(BSize(0, 0));
					}
				}


				// 2. EVALUATE VISIBILITY AND BUBBLE LAYOUT INVALIDATION
				if (wasLocallyHidden != targetHiddenState) {
					if (!targetHiddenState) {
						fSpectrum->Show();
						if (cfg.debugEnable) printf("[DEBUG] SpectrumVisuals structural visibility -> SHOW.\n");
					} else {
						fSpectrum->Hide();
						if (cfg.debugEnable) printf("[DEBUG] SpectrumVisuals structural visibility -> HIDE.\n");
					}

					// Bubble layout invalidation context up to the top structural ancestor
					BView* parentNode = fSpectrum->Parent();
					if (parentNode) {
						parentNode->InvalidateLayout(true);
						
						// Walk layout hierarchy upwards if nestled inside multiple layout containers
						while (parentNode->Parent() != nullptr) {
							parentNode = parentNode->Parent();
							parentNode->InvalidateLayout(false); // Clear parent element matrix caches
						}
					}
				}
			}

				

				// ONLY manipulate layout blocks if actively changing from Compact to Normal mode
				if (isRealModeTransition) {
					if (cfg.debugEnable) printf("[DEBUG] Structural Change -> Rebuilding tree for Normal Mode.\n");
					
					if (fTabView) {
						if (cfg.debugEnable) printf("[DEBUG] Cleaning up dynamic group panels from fTabView hierarchy cleanly.\n");
						
						// Stop the loop at > 0 so index 0 (your Radio Tab) is NEVER deleted!
						int32 initialTabsCount = fTabView->CountTabs();
						for (int32 i = initialTabsCount - 1; i > 0; i--) {
							fTabView->RemoveTab(i);
						}

						// Physically separate only your 4 dynamic panels from the window layer tree
						if (fStationGroup && fStationGroup->Parent()) fStationGroup->RemoveSelf();
						if (fFavGroup && fFavGroup->Parent()) fFavGroup->RemoveSelf();
						if (fConfigGroup && fConfigGroup->Parent()) fConfigGroup->RemoveSelf();
						if (fAboutGroup && fAboutGroup->Parent()) fAboutGroup->RemoveSelf();

						// Reset dynamic tab item trackers so they can be re-allocated safely
						fStationTab = nullptr;
						fFavTab = nullptr;
						fConfigTab = nullptr;
						fAboutTab = nullptr;
					}

					fNormalControlsWrapper->GroupLayout()->RemoveView(fControlStack);
					fCompactControlsWrapper->GroupLayout()->RemoveView(fControlStack);
					
					if (fRightSideControlGroup && fRightSideControlGroup->GroupLayout()) {
						fRightSideControlGroup->GroupLayout()->RemoveView(fBtnAddFav);
					}

					BView* allButtons[] = { fStopBtn, fPauseBtn, fPlayBtn, fShuffleBtn, fBtnAddFav };
					for (BView* btn : allButtons) {
						if (btn && btn->Parent()) btn->RemoveSelf();
					}

					if (fControlStack->GroupLayout()) {
						while (fControlStack->GroupLayout()->CountItems() > 0) {
							fControlStack->GroupLayout()->RemoveItem((int32)0);
						}
					}

					// Re-build horizontal control layout
					BLayoutBuilder::Group<>(fControlStack, B_HORIZONTAL, 5)
					    
						.Add(fStopBtn)
						.Add(fPauseBtn)
						.Add(fPlayBtn)
						.Add(fShuffleBtn)
					.End();

					fNormalControlsWrapper->GroupLayout()->AddView(fControlStack);
					
					if (fRightSideControlGroup) {
						if (fRightSideControlGroup->GroupLayout()->CountItems() > 0) fRightSideControlGroup->GroupLayout()->RemoveItem((int32)0);
						fRightSideControlGroup->GroupLayout()->AddView(fBtnAddFav);
					}
					
					if (fMetaAndSpectrumStack && fSpectrum) {
						if (fSpectrum->Parent() != nullptr) fSpectrum->RemoveSelf();
						fMetaAndSpectrumStack->GroupLayout()->AddView(fSpectrum);
					}
					
					    // --- CRITICAL WRAPPER COMPRESSION FIX ---
                        // Right here! This locks down the outer wrapper layout footprint.
                        fCompactSpectrumWrapper->SetExplicitMinSize(BSize(B_SIZE_UNSET, B_SIZE_UNSET));
                        fCompactSpectrumWrapper->SetExplicitMaxSize(BSize(B_SIZE_UNSET, B_SIZE_UNSET));
                        fCompactSpectrumWrapper->SetExplicitPreferredSize(BSize(B_SIZE_UNSET, B_SIZE_UNSET));
					
				}


				// The attachments block below handles rebuilding views on every pass
				if (fNormalControlsWrapper && fNormalControlsWrapper->GroupLayout() && fControlStack->Parent() != fNormalControlsWrapper) {
					if (cfg.debugEnable) printf("[DEBUG] Ensuring fControlStack is attached to fNormalControlsWrapper.\n");
					fNormalControlsWrapper->GroupLayout()->AddView(fControlStack);
				}
				
				if (fRightSideControlGroup && fRightSideControlGroup->GroupLayout() && fBtnAddFav && fBtnAddFav->Parent() != fRightSideControlGroup) {
					if (fRightSideControlGroup->GroupLayout()->CountItems() > 0) fRightSideControlGroup->GroupLayout()->RemoveItem((int32)0);
					fRightSideControlGroup->GroupLayout()->AddView(fBtnAddFav);
				}
				
				if (fMetaAndSpectrumStack && fMetaAndSpectrumStack->GroupLayout() && fSpectrum && fSpectrum->Parent() != fMetaAndSpectrumStack) {
					if (fSpectrum->Parent() != nullptr) fSpectrum->RemoveSelf();
					fMetaAndSpectrumStack->GroupLayout()->AddView(fSpectrum);
				}

				BView* playbackButtons[] = { fStopBtn, fPauseBtn, fPlayBtn, fShuffleBtn };
				BSize standardSize(75.0f * scale, 75.0f * scale);
				for (BView* btn : playbackButtons) {
					if (btn) {
						btn->SetExplicitMinSize(standardSize);
						btn->SetExplicitMaxSize(standardSize);
						btn->SetExplicitPreferredSize(standardSize);
					}
				}

				if (fBtnAddFav) {
					BSize favNormalSize(75.0f * scale, 75.0f * scale);
					fBtnAddFav->SetExplicitMinSize(favNormalSize);
					fBtnAddFav->SetExplicitMaxSize(favNormalSize);
					fBtnAddFav->SetExplicitPreferredSize(favNormalSize);
				}

				if (fControlStack) {
					fControlStack->SetExplicitMinSize(unset);
					fControlStack->SetExplicitPreferredSize(unset);
					fControlStack->SetExplicitMaxSize(unlimited);
					fControlStack->SetExplicitAlignment(BAlignment(B_ALIGN_HORIZONTAL_CENTER, B_ALIGN_BOTTOM));
				}

				fPlayerGroup->GroupLayout()->SetInsets(3);	
				if (fCompactModeRadio) fCompactModeRadio->Show();
				if (fVolumeSlider) fVolumeSlider->SetTarget(this);		
				for (int i = 0; i < 15; i++) {
					if (fEQSliders[i]) fEQSliders[i]->SetTarget(this);
				}

			// Reallocate fresh tab wrappers onto the cleared TabView
			if (isRealModeTransition || fTabView->CountTabs() < 4) {
					BGroupView* groups[] = { fStationGroup, fFavGroup, fConfigGroup, fAboutGroup };
					const char* labels[] = { "Stations", "Fav", "Config", "About" };
					BTab** dynamicTabs[] = { &fStationTab, &fFavTab, &fConfigTab, &fAboutTab };
					
					if (cfg.debugEnable) printf("[DEBUG] Syncing Normal dynamic tab assignments onto clean container.\n");
					for (int i = 0; i < 4; i++) {
						if (groups[i] == nullptr || dynamicTabs[i] == nullptr) continue;

						bool found = false;
						int32 currentTabs = fTabView->CountTabs();
						for (int32 j = 0; j < currentTabs; j++) {
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
			} // 👈 CLOSE THE IS_REAL_MODE_TRANSITION WRAPPER HERE!

			// --- MOVE THIS OUTSIDE THE WRAPPER SO IT RUNS EVERY TIME ---
			// This forces regular mode footprints back onto objects right after exiting full screen.
			if (fSpectrum) {
				fSpectrum->InvalidateLayout(true);
				
				// Enforce regular mode bounding preferences back onto the core object
				fSpectrum->SetExplicitMinSize(BSize(375.0f * scale, 125.0f * scale));
				fSpectrum->SetExplicitMaxSize(BSize(375.0f * scale, 125.0f * scale));
				fSpectrum->SetExplicitPreferredSize(BSize(375.0f * scale, 125.0f * scale));
			}

			// Lock down the parent container stack to protect the vertical footprint
			if (fMetaAndSpectrumStack) {
				fMetaAndSpectrumStack->InvalidateLayout(true);
				fMetaAndSpectrumStack->SetExplicitMinSize(BSize(B_SIZE_UNSET, 125.0f * scale));
				fMetaAndSpectrumStack->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
				fMetaAndSpectrumStack->SetExplicitPreferredSize(BSize(B_SIZE_UNSET, 125.0f * scale));
			}
			
		} 
		
		
		// --- END OF NORMAL MODE GEOMETRY BRANCH ---





			// --- EXECUTE MANDATORY UI RE-FLOW CALCULATIONS ---
			if (fNormalControlsWrapper && fNormalControlsWrapper->GroupLayout()) fNormalControlsWrapper->GroupLayout()->InvalidateLayout(true);
			if (fCompactControlsWrapper && fCompactControlsWrapper->GroupLayout()) fCompactControlsWrapper->GroupLayout()->InvalidateLayout(true);
			if (fControlStack && fControlStack->GroupLayout()) fControlStack->GroupLayout()->InvalidateLayout(true);
			if (fRightSideControlGroup && fRightSideControlGroup->GroupLayout()) fRightSideControlGroup->GroupLayout()->InvalidateLayout(true);
			if (fCompactSpectrumWrapper && fCompactSpectrumWrapper->GroupLayout()) fCompactSpectrumWrapper->GroupLayout()->InvalidateLayout(true);
			
			if (fTabView) fTabView->InvalidateLayout(true);
			if (fStationGroup && fStationGroup->GroupLayout()) fStationGroup->GroupLayout()->InvalidateLayout(true);
			if (fFavGroup && fFavGroup->GroupLayout()) fFavGroup->GroupLayout()->InvalidateLayout(true);
			if (fConfigGroup && fConfigGroup->GroupLayout()) fConfigGroup->GroupLayout()->InvalidateLayout(true);
			if (fAboutGroup && fAboutGroup->GroupLayout()) fAboutGroup->GroupLayout()->InvalidateLayout(true);
			
			if (fPlayerGroup) fPlayerGroup->InvalidateLayout(true);
			if (fMetaAndSpectrumStack && (uintptr_t)fMetaAndSpectrumStack > 0x1000) fMetaAndSpectrumStack->InvalidateLayout(true);
			InvalidateLayout(true);

			// --- 4. EXECUTE FINAL GEOMETRY AND SIZING OPERATIONS ---
			if (fArtView) {
				fArtView->SetExplicitMinSize(BSize(artSize, artSize));
				fArtView->SetExplicitMaxSize(BSize(artSize, artSize));
				fArtView->SetExplicitPreferredSize(BSize(artSize, artSize));
			}

			float paddedVolumeSliderSize = cfg.compactMode ? VolSize + (12.0f * scale) : VolSize + (16.0f * scale);
			float paddedBtnSize = cfg.compactMode ? btnSize + (12.0f * scale) : btnSize + (16.0f * scale);
			float paddedFavSize = cfg.compactMode ? favSize + (12.0f * scale) : btnSize + (16.0f * scale);	
						
			if (fVolumeSlider)   fVolumeSlider->SetExplicitSize(BSize(paddedVolumeSliderSize, B_SIZE_UNSET));
			if (fBtnAddFav)  	 fBtnAddFav->SetExplicitSize(BSize(paddedFavSize, paddedFavSize));
			if (fStopBtn)     	 fStopBtn->SetExplicitSize(BSize(paddedBtnSize, paddedBtnSize));
			if (fPauseBtn)    	 fPauseBtn->SetExplicitSize(BSize(paddedBtnSize, paddedBtnSize));
			if (fPlayBtn)     	 fPlayBtn->SetExplicitSize(BSize(paddedBtnSize, paddedBtnSize)); 
			if (fShuffleBtn)  	 fShuffleBtn->SetExplicitSize(BSize(paddedBtnSize, paddedBtnSize));
			
			if (fquality)       fquality->Show();
			if (fListenersView) fListenersView->Show();
			
		
			if (fSpectrum) {
				if (cfg.showSpectrumVisuals && cfg.eqEnabled) fSpectrum->Show(); else fSpectrum->Hide();
			}
			
			if (fPlayerGroup) fPlayerGroup->InvalidateLayout(true);
			this->InvalidateLayout(true);
			
			if (LockLooper()) {
				this->SetSizeLimits(0, B_SIZE_UNLIMITED, 0, B_SIZE_UNLIMITED);
				this->Layout(true); 
				this->ResizeToPreferred(); 
				UnlockLooper();
			}
			

			ApplyTheme();

			// --- 5. DEFERRED SELECTION MESSAGE PROCESSOR ---
			BString deferredSelect;
			if (message->FindString("deferred_select", &deferredSelect) == B_OK && fTabView) {
				fTabView->InvalidateLayout();
				fTabView->Layout(true);		
				const char* matchLabel = "Radio";
				int32 fallbackIndex = 0;
				if (deferredSelect == "stations") { matchLabel = "Stations"; fallbackIndex = 1; }
				else if (deferredSelect == "favorites") { matchLabel = "Fav"; fallbackIndex = 2; }
				else if (deferredSelect == "eq") { matchLabel = "Config"; fallbackIndex = 3; }
				
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

			if (LockLooper()) {
				// 1. Flush individual item caches down the line
				if (fMetaAndSpectrumStack && (uintptr_t)fMetaAndSpectrumStack > 0x1000) fMetaAndSpectrumStack->InvalidateLayout(true);
				if (fCompactSpectrumWrapper && fCompactSpectrumWrapper->GroupLayout()) fCompactSpectrumWrapper->InvalidateLayout(true);
				if (fPlayerGroup) fPlayerGroup->InvalidateLayout(true);
				if (fTabView) fTabView->InvalidateLayout(true);

				// 2. If a full structural switch happened, force a total window layout flush!
				// This mimics a fresh start by forcing Haiku to dump all cached component sizes.
				if (isRealModeTransition) {
					if (cfg.debugEnable) printf("[DEBUG] Real Mode Transition -> Executing deep tree layout flush.\n");
					if (this->GetLayout()) {
						this->GetLayout()->InvalidateLayout(true);
					}
				}

				// 3. Temporarily reset window bounds floor constraints to 0
				this->SetSizeLimits(0, B_SIZE_UNLIMITED, 0, B_SIZE_UNLIMITED);
				this->Layout(true); 
				
				// 4. Query the fresh bottom-up layout height requirements 
				BSize preferred = this->GetLayout()->PreferredSize();
				if (cfg.debugEnable) printf("[DEBUG] Computed Master Layout Preferred Size - Width: %.2f, Height: %.2f\n", preferred.width, preferred.height);
				
				// Update the window dimensions to match the updated calculations
				this->ResizeTo(preferred.width, preferred.height);
				
				if (cfg.debugEnable) printf("[DEBUG] Triggering ResizeWindowToFit()...\n");
				this->ResizeWindowToFit();
				
				// 5. Force the window server to lock the new limits down safely
				this->UpdateSizeLimits();
				
				UnlockLooper();
				if (cfg.debugEnable) printf("[DEBUG] Looper Lock Sequence Pass 2 completed.\n");
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
    	

    	
        case 'mcyc': {
            // Look through all top-level window controls and sub-views recursively
            SpectrumView* mainSpectrumView = nullptr;
            int32 count = CountChildren();
            for (int32 i = 0; i < count; i++) {
                mainSpectrumView = FindSpectrumViewRecursive(ChildAt(i));
                if (mainSpectrumView != nullptr) break;
            }

            if (mainSpectrumView != nullptr) {
                mainSpectrumView->Window()->PostMessage('drep', mainSpectrumView);            
            }
            break;
        }




    	
    	//@debug
    	case MSG_CFG_DEBUG: {
        BCheckBox* chk = dynamic_cast<BCheckBox*>(FindView("chk_debug"));
        if (chk) {
            cfg.debugEnable = (chk->Value() == B_CONTROL_ON);            
            if (cfg.debugEnable) {
            	if (fChkSysTray) fChkSysTray->Show();
				if (fEnableladspa) fEnableladspa->Show();
    			if (fVisualsCheckbox) fVisualsCheckbox->Show(); 
    	 		if (fChkPresetTimer)  fChkPresetTimer->Show(); 
    	 		if (fPresetToggle) fPresetToggle->Show();
 	
            } else {      
            	if (fChkSysTray) fChkSysTray->Hide();
            	if (fEnableladspa) fEnableladspa->Hide();        
    			if (fVisualsCheckbox) fVisualsCheckbox->Hide();
            }
            InvalidateLayout();
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
                        if (cfg.debugEnable) printf("[Visualizer] Found icon in cache for %s. Extracting colors...\n", chan.title.c_str());
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
                // Swap out runtime dynamic_cast for an explicit static pointer conversion
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
           
                    // 1. Existing working spectrum adaptation execution block
                    if (targetArt != nullptr && this->fSpectrum != nullptr) {
                        this->fSpectrum->AdaptToAlbumArt(targetArt);
                    }

                    // --- 2. THE MISSING LINK: INJECT VOLUME CONTROL ADAPTATION HERE ---
                    if (targetArt != nullptr && this->fVolumeSlider != nullptr) {
                        // Cast the view pointer to our specific subclass layout type
                        RadialVolumeControl* radialKnob = dynamic_cast<RadialVolumeControl*>(this->fVolumeSlider);
                        if (radialKnob != nullptr) {
                            radialKnob->AdaptToAlbumArt(targetArt);
                        }
                    } else if (targetArt == nullptr && this->fVolumeSlider != nullptr) {
                        // Safe fallback: Reset to classic green if no artwork is available
                        RadialVolumeControl* radialKnob = dynamic_cast<RadialVolumeControl*>(this->fVolumeSlider);
                        if (radialKnob != nullptr) {
                            radialKnob->ResetPalette();
                        }
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
            
            
            
        case MSG_MUTE_TOGGLED: {
            // 1. Cycle the mute state safely on the main thread
            mpv_command_string(mpv, "cycle mute");            
            
            int is_muted = 0;
            mpv_get_property(mpv, "mute", MPV_FORMAT_FLAG, &is_muted);          
            
 
            // 2. Synchronize the standard and custom view states
            if (fVolumeSlider != nullptr) {
                // Keep the standard enabled state in sync
                fVolumeSlider->SetEnabled(is_muted ? false : true);
                
                // 3. Cast and inject the state change into your custom drawing engine
                RadialVolumeControl* radialKnob = dynamic_cast<RadialVolumeControl*>(fVolumeSlider);
                if (radialKnob != nullptr) {
                    radialKnob->SetMuted(is_muted == 1); 
                }
                
                // 4. Force a top-level paint flash down the hierarchy line
                fVolumeSlider->Invalidate(); 
            }
            break;
        }




            
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
    		
    		// Use proper Haiku FindString API pattern to avoid null assignment
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

            
    		
        case MSG_VOL_CHANGED: {
            if (fVolumeSlider) {
                int32 value = fVolumeSlider->Value();
                
                cfg.currentVolume = static_cast<float>(value);
                
                double vol = (double)value;
                mpv_set_property(mpv, "volume", MPV_FORMAT_DOUBLE, &vol);
                save_config(); 
            }
            break;
        }
  
//@vcase  
//--------------------------------- Projectm         
        #ifdef USE_PROJECTM         
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
    		InvalidateLayout(true);
    		ResizeToPreferred();
    		break;
		}

//--------------------------------- Projectm   
        
        case MSG_TOGGLE_VISUALS: {
            bool currentVisualsState = (fVisualsCheckbox->Value() == B_CONTROL_ON);            
            if (currentVisualsState) {
                if (fChkPresetTimer) fChkPresetTimer->Show();          
                if (fPresetToggle) fPresetToggle->Show();
               
                cfg.showVisuals = true;
                save_config();
                StartVisuals(); 
            } else { 
                bool show = (fPresetToggle->Value() == B_CONTROL_ON);    
    				if (show) {
        				fPresetToggle->SetValue(B_CONTROL_OFF);
        				fPresetToggle->Invoke(); 
    				}    	
                if (fChkPresetTimer) fChkPresetTimer->Hide();
                if (fPresetToggle) fPresetToggle->Hide();               
                cfg.showVisuals = false;
                save_config();
                StopVisuals();
            }

            this->SetSizeLimits(0, B_SIZE_UNLIMITED, 0, B_SIZE_UNLIMITED);
            InvalidateLayout(true);
            ResizeToPreferred();
            
            if (this->GetLayout() != nullptr) {
                BSize minSize = this->GetLayout()->MinSize();
                this->SetSizeLimits(minSize.width, B_SIZE_UNLIMITED, minSize.height, B_SIZE_UNLIMITED);
            }
            break;
        }
        
//--------------------------------- Projectm   
		
		case MSG_HIDE_VISUALS_REQUEST: {
    		if (fVisualsCheckbox != nullptr && fVisualsCheckbox->Value() == B_CONTROL_ON) {
        		// 1. Uncheck the UI element visually
        		fVisualsCheckbox->SetValue(B_CONTROL_OFF);
        
        		// 2. Safely fire the checkbox's assigned message (MSG_TOGGLE_VISUALS) 
        		// into the main loop to execute all the layout & pipeline cleanup.
        		fVisualsCheckbox->Invoke(); 
    		}
    		break;
		}


//--------------------------------- Projectm   

        case MSG_VOL_STEP_REQUEST: {
            int32 direction = 0;
            if (message->FindInt32("direction", &direction) == B_OK && fVolumeSlider != nullptr) {
                // 1. Calculate the new volume target step limits
                int32 currentVal = fVolumeSlider->Value();
                int32 stepAmount = 5; // Change volume by 5% increments per tick
                int32 newVal = currentVal + (direction * stepAmount);
                
                // Enforce safety floor and ceiling bounds constraints
                if (newVal < 0) newVal = 0;
                if (newVal > 100) newVal = 100;
                
                // 2. Mechanically advance the layout slider widget 
                fVolumeSlider->SetValue(newVal);
                
                // 3. Force sync execution to update the core MPV layer properties instantly
                double vol = (double)newVal;
                mpv_set_property(mpv, "volume", MPV_FORMAT_DOUBLE, &vol);
            }
            break;
        }



//--------------------------------- Projectm  		
		#endif
//--------------------------------- Projectm  
//@vcase   

		
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
			// --- STEP 1: UNHIDE / ACTIVATE WINDOW IMMEDIATELY ---
			if (IsHidden()) {
				if (fSpectrum != nullptr) {
					BMessage teardownMsg('tdwn'); 
					fSpectrum->MessageReceived(&teardownMsg);
				}
				Show();
			} else {
				Activate(true);
			}

			// --- STEP 2: PARSE TARGET TAB AND MANAGE COMPACT STATE ---
			BString targetTab;
			if (message->FindString("target_tab", &targetTab) == B_OK) {
				
				// Only break out of compact mode if navigating to an incompatible tab
				if (cfg.compactMode && (targetTab == "stations" || targetTab == "favorites" || targetTab == "eq")) {
					// --- CODE INJECTION: TOGGLE COMPACT VISUAL CONTROLS OFF ---
					// Uncheck the controls to drive the state machine through natural user emulation
					if (fCompactModeConfig != nullptr) {
						fCompactModeConfig->SetValue(B_CONTROL_OFF);
					}
					if (fCompactModeRadio != nullptr) {
						fCompactModeRadio->SetValue(B_CONTROL_OFF);
					}

					BMessage compactMsg(MSG_COMPACTM_CHANGED);
					
					// Set the source to one of your controls so the layout engine processes the toggle
					if (fCompactModeConfig != nullptr) {
						compactMsg.AddPointer("source", fCompactModeConfig);
					} else {
						compactMsg.AddPointer("source", this);
					}
					
					// Force the mode switch payload to Normal Mode (false)
					compactMsg.AddBool("force_compact_state", false);
					
					// Pipe the target tab parameter through to the layout loop
					compactMsg.AddString("deferred_select", targetTab);
					
					this->PostMessage(&compactMsg);
					break; // Exit early; let MSG_COMPACTM_CHANGED handle tab selection
				}

				// --- STEP 3: DIRECT OVERRIDE APPLIER ---
				// (Runs if NOT in compact mode, OR if in compact mode but choosing "radio")
				if (targetTab == "stations")       fOverrideTabTarget = "Stations";
				else if (targetTab == "favorites") fOverrideTabTarget = "Fav";
				else if (targetTab == "eq")        fOverrideTabTarget = "Config";
				else                               fOverrideTabTarget = "Radio";

				// Directly verify and update UI tab selectors for instant compliance
				if (fTabView) {
					for (int32 i = 0; i < fTabView->CountTabs(); i++) {
						BTab* tab = fTabView->TabAt(i);
						if (tab && tab->Label() && strcmp(tab->Label(), fOverrideTabTarget.String()) == 0) {
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
    // ====================================================================
    // EMERGENCY THREAD TERMINATION 
    // ====================================================================
    // 1. Immediately flag any background loops to halt execution iterations
    fIsQuitting = true;
    
    // 2. Force the main window thread to wait until the downloader thread safely parks.
    // This guarantees no code is reading/writing to fIconCache or tabs while we delete them.
    if (fDownloadThreadID >= 0) {
        status_t exitStatus;
        wait_for_thread(fDownloadThreadID, &exitStatus);
        fDownloadThreadID = -1; // Reset handle state
    }
    // ====================================================================
    
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
    
	ReallyStopVisuals();

}



class SuperMusicApp : public BApplication {
public:
    SuperMusicApp() : BApplication("application/x-vnd.HaikuSuperMusicThingy") {}
	virtual void MessageReceived(BMessage* message);
	virtual void ReadyToRun() {
    	load_config();
    	fetch_channels();
    	init_mpv();

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


    #ifdef USE_PROJECTM
    if (gGuiWindow != nullptr) {
        thread_id activeVisuals = gGuiWindow->VisualsThreadID();
        
        if (activeVisuals > 0) {
            if (cfg.debugEnable) printf("[DEBUG App] Explicitly halting visualizer thread: %" B_PRId32 "\n", activeVisuals);
            
            visualsRunning = false; // 1. Signal background loop exit instantly
            
            // 2. SYNCHRONOUS WAIT: Force the app thread to wait until the thread is 100% gone
            status_t threadExitStatus = B_OK;
            status_t waitResult = wait_for_thread_etc(activeVisuals, B_RELATIVE_TIMEOUT, 800000, &threadExitStatus); // 800ms timeout
            
            if (waitResult == B_TIMED_OUT) {
                if (cfg.debugEnable) printf("[DEBUG Visuals] Visualizer blocked on driver return. Forcing kill_thread.\n");
                kill_thread(activeVisuals);
            }
        }
    }
    #endif

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
            fade_duration_us = 546000;         // 780ms 
            //fade_duration_us = 160000;         // 160ms 
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

    			// --- TRACK VOLUME PERCENTAGE RATIO ---
    			// Scale against 100.0 (or your app's max volume capacity)
    			// This scales the spectrum down to 0 when muted, and up to full height at 100% volume
    			gVolumeScaleFactor = (float)(v / 100.0);
    
    			if (gVolumeScaleFactor < 0.0f) gVolumeScaleFactor = 0.0f;
    			if (gVolumeScaleFactor > 1.0f) gVolumeScaleFactor = 1.0f;
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
    
    ReallyStopVisuals();

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

    // ====================================================================
    // --- ENFORCE EXPLICIT ROUTING ON SYSTEM LEFT CLICK ---
    // ====================================================================
    if (buttons & B_PRIMARY_MOUSE_BUTTON) {
        if (appMessenger.IsValid()) {
            // Explicitly build a message envelope with a "radio" string payload
            BMessage showPlayerMsg(MSG_ACTIVATE_APP);
            showPlayerMsg.AddString("target_tab", "radio");
            
            appMessenger.SendMessage(&showPlayerMsg);
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
        popup->AddItem(new BMenuItem("Config", eqMessage));
        
        popup->AddSeparatorItem();
        
        // 5. Playback Controls
        popup->AddItem(new BMenuItem("Shuffle", new BMessage(MSG_SHUFFLE)));
        popup->AddItem(new BMenuItem("Pause", new BMessage(MSG_PAUSE)));
        popup->AddItem(new BMenuItem("Stop", new BMessage(MSG_STOP))); 
        
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
        case MSG_SHUFFLE:
        case MSG_PAUSE:
        case MSG_STOP: { 
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

void SuperMusicWindow::Show() {
    // 1. Let the window show and boot up naturally
    BWindow::Show();
    
    // 2. Queue up the theme processing to run on the next frame loop pass
   
    if (Lock()) {
    PostMessage(MSG_CFG_THEME);
    Unlock();
	}
	
}



int main() {
	std::srand(std::time(nullptr)); 
	ensure_config_dir();
    SuperMusicApp app;   
    app.Run();    
    return 0;
}
