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

// --- Haiku Storage Kit ---
#include <Path.h>
#include <FindDirectory.h>
#include <Directory.h> 

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

// --- Local Header ---
#include "haiku-supermusicthingy.h"



namespace fs = std::filesystem;
const std::string BASE_URL = "https://somafm.com/";
extern void play_random();
extern void set_volume(char direction);
extern mpv_handle* mpv;
extern void init_mpv();

std::string statusMsg = "";
std::time_t statusExpiry = 0;

class SuperMusicWindow; 

enum {
    MSG_SHUFFLE = 'shuf',
    MSG_STOP    = 'stop',
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
    MSG_CFG_NOTIFY       = 'c_nt',
    MSG_CFG_QUALITY      = 'c_qu',
    MSG_CFG_THEME        = 'c_th' 
};

bool mpvthread_running = true;
SuperMusicWindow* gGuiWindow = nullptr; 
int32 mpv_loop_thread(void* data);


using json = nlohmann::json;
std::vector<std::string> favUrls;
std::vector<std::string> helpMenu;
int selectedhelp = 0;
int scrollhelpOffset = 0;
int selectedFav = 0;
int scrollOffset = 0;
bool showFavorites = false;
bool showHelp = false;
bool showNotifications = false;
bool showConfig = false;




std::string configPath = getenv("HOME") + std::string("/config/settings/SuperMusicThingy/config.txt");

void ensure_config_dir() {
    BPath path;

    if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
        path.Append("SuperMusicThingy");
        if (create_directory(path.Path(), 0755) == B_OK) {
        } else {            
        }
    }
}



struct Config {
    bool showNotifications = true;
    bool showVisuals = false;
    bool autoShuffle = false;
    bool autoShuffleVisuals = false;
    bool autoVsync = false;
    int defaultVolume = 75;
    std::string updateTheme = "Dark";
    std::string quality = "Highest";
} cfg;

int selectedConfig = 0;

enum MenuState { NONE, FAVORITES, HELP, CONFIG };
MenuState currentMenu = NONE;

void download_art(const std::string& url) {
    if (url.empty()) return;
    
    CURL* curl = curl_easy_init();
    if(curl) {
        FILE* fp = fopen("/tmp/somafm_art.png", "wb");     
        if (fp) {
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
            curl_easy_perform(curl);
            fclose(fp);
            if (gGuiWindow) {
                gGuiWindow->PostMessage(new BMessage(MSG_UPDATE_ART));
            }
        }
        curl_easy_cleanup(curl);
    }
}


struct Channel {
    std::string title;
    std::string id;
    std::string desc;
    std::string listeners;
    std::string largeimage;
};

mpv_handle *mpv = nullptr;
std::vector<Channel> channels;

std::string pendingSong = "";
std::time_t notifyTimer = 0;

std::string currentSong = "None";
std::string currentDesc = "None";
std::string currentStation = "";
std::string currentListeners = "";
std::string currentAlbumArtUrl = "";



static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

// --- Logic Functions ---

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
                        ch.value("largeimage", "")
                    });
                }
            } catch(...) {}
        }
        curl_easy_cleanup(curl);
    }
}




void save_config() {
    json j;
    j["quality"] = cfg.quality;
    j["updateTheme"] = cfg.updateTheme;
    j["showNotifications"] = cfg.showNotifications;
    j["autoShuffle"] = cfg.autoShuffle;
    j["autoShuffleVisuals"] = cfg.autoShuffleVisuals;
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
            } catch(...) {

            }
        }
    }
}



void init_mpv() {
        mpv = mpv_create();
        if (!mpv) exit(1);
        #ifdef __HAIKU__
        mpv_set_option_string(mpv, "ao", "openal");
        #else
        mpv_set_option_string(mpv, "ao", "pulse");
        #endif

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
        // Ensure we hit the exact target
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
        #ifdef __HAIKU__
        std::string dir = home + "/config/settings/SuperMusicThingy";
        std::string path = dir + "/favorites.txt";
        #else
        std::string dir = home + "/.config/SuperMusicThingy";
        std::string path = dir + "/favorites.txt";
        #endif

        mkdir(dir.c_str(), 0755);

        // 1. Determine the URL for the current station
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

        // 2. Check if URL already exists in the file
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

        // 3. Save only if it's NOT a duplicate
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
        #ifdef __HAIKU__
        std::string path = home + "/config/settings/SuperMusicThingy/favorites.txt";
        #else
        std::string path = home + "/.config/SuperMusicThingy/favorites.txt";
        #endif

        std::ifstream infile(path);
        std::vector<std::string> favs;
        std::string line;
        while (std::getline(infile, line)) if (!line.empty()) favs.push_back(line);

        if (favs.empty()) {
            statusMsg = "No favorites saved!";
            statusExpiry = std::time(nullptr) + 2;
            return;
        }

        std::string url = favs[rand() % favs.size()];

        // Extract ID from URL to update global state correctly
        // URL format: https://somafm.com
        size_t lastSlash = url.find_last_of('/');
        size_t lastDot = url.find_last_of('.');
        if (lastSlash != std::string::npos && lastDot != std::string::npos) {
            std::string id = url.substr(lastSlash + 1, lastDot - lastSlash - 1);
            for (const auto& ch : channels) {
                if (ch.id == id) {
                    currentStation = ch.title;
                    currentDesc = ch.desc;
                    currentListeners = ch.listeners;
                    currentAlbumArtUrl = ch.largeimage;

                    if (!currentAlbumArtUrl.empty()) {
                        std::thread([url = currentAlbumArtUrl]() {
                            download_art(url);
                        }).detach();
                    }

                    break;
                }
            }
        }

        double original_vol;
        mpv_get_property(mpv, "volume", MPV_FORMAT_DOUBLE, &original_vol);
        fade_volume(mpv, 0, 300);

        currentSong = "Loading Favorite...";
        const char *cmd[] = {"loadfile", url.c_str(), NULL};
        mpv_command(mpv, cmd);
        fade_volume(mpv, original_vol, 500);
}


void play_specific_url(std::string url) {
    if (url.empty()) return;

    // 1. Extract ID from URL to find Station Info
    // (Logic copied from your play_favorite)
    size_t lastSlash = url.find_last_of('/');
    size_t lastDot = url.find_last_of('.');
    
    if (lastSlash != std::string::npos && lastDot != std::string::npos) {
        std::string id = url.substr(lastSlash + 1, lastDot - lastSlash - 1);
        
        for (const auto& ch : channels) {
            if (ch.id == id) {
                // Update Global State
                currentStation = ch.title;
                currentDesc = ch.desc;
                currentListeners = ch.listeners;
                currentAlbumArtUrl = ch.largeimage; 

                // Trigger Art Download
                if (!currentAlbumArtUrl.empty()) {
                    std::thread([url = currentAlbumArtUrl]() {
                        download_art(url);
                    }).detach();
                }
                break;
            }
        }
    }

    // 2. Send Command to MPV
    double original_vol;
    mpv_get_property(mpv, "volume", MPV_FORMAT_DOUBLE, &original_vol);
    fade_volume(mpv, 0, 200); // Quick fade out

    currentSong = "Loading Favorite...";
    const char *cmd[] = {"loadfile", url.c_str(), NULL};
    mpv_command(mpv, cmd);
    
    fade_volume(mpv, original_vol, 500); // Fade in
}




// Delete Station from favorites list while listening
void delete_favorite() {
        std::string home = getenv("HOME") ? getenv("HOME") : ".";
        #ifdef __HAIKU__
        std::string path = home + "/config/settings/SuperMusicThingy/favorites.txt";
        #else
        std::string path = home + "/.config/SuperMusicThingy/favorites.txt";
        #endif

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

        // 1.
        double original_vol;
        mpv_get_property(mpv, "volume", MPV_FORMAT_DOUBLE, &original_vol);
        fade_volume(mpv, 0, 300);

        // 2.
        int idx = rand() % channels.size();
        currentStation = channels[idx].title;
        currentDesc = channels[idx].desc;
        currentListeners = channels[idx].listeners;
        currentSong = "Buffering...";
        currentAlbumArtUrl = channels[idx].largeimage;

        if (!currentAlbumArtUrl.empty()) {
            std::thread([url = currentAlbumArtUrl]() {
                download_art(url);
            }).detach();
        }

        // USE THE HELPER
        std::string url = get_quality_url(channels[idx].id);

        const char *cmd[] = {"loadfile", url.c_str(), NULL};
        mpv_command(mpv, cmd);

        // 3.
        fade_volume(mpv, original_vol, 500);
}


    
bool is_favorite() {
    BPath path;
    // 1. Get the standard settings path
    if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK) return false;
    path.Append("SuperMusicThingy/favorites.txt");
    
    std::ifstream infile(path.Path());
    
    // 2. Reconstruct current station URL
    std::string currentUrl = "";
    // Ensure 'channels' and 'currentStation' are accessible here
    for(const auto& ch : channels) {
        if(ch.title == currentStation) {
            currentUrl = BASE_URL + ch.id + ".pls";
            break;
        }
    }

    if (currentUrl.empty()) return false;

    // 3. Check file for match
    if (infile.is_open()) {
        std::string line;
        while (std::getline(infile, line)) {
            if (line == currentUrl) return true;
        }
    }
    
 return false;
}


class AlbumArtView : public BView {
public:
    AlbumArtView() : BView("art_view", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE) {}

virtual void Draw(BRect updateRect) {
    // Look at the class member inside the window instance
    if (gGuiWindow && gGuiWindow->fAlbumArt) {
        DrawBitmap(gGuiWindow->fAlbumArt, Bounds());
    } else {
        SetHighColor(30, 30, 30);
        FillRect(Bounds());
    }
}

};

SuperMusicWindow::SuperMusicWindow()
    : BWindow(BRect(100, 100, 500, 300), "SuperMusicThingy", B_TITLED_WINDOW, 
              B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS | B_QUIT_ON_WINDOW_CLOSE)
{
    fAlbumArt = nullptr;

    // 1. Fonts
    BFont largeFont(be_bold_font);
    largeFont.SetSize(24.0); 
    BFont smallFont(be_bold_font);
    smallFont.SetSize(12.0); 

    // 2. Create the Tab Container (Root View)
    fTabView = new BTabView("tab_container");
    fTabView->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

    // ==========================================
    // TAB 1: PLAYER VIEW (The "Radio" Interface)
    // ==========================================
    BGroupView* playerGroup = new BGroupView(B_VERTICAL, 10);
    playerGroup->SetName("Radio"); 

    // Text Labels
    fStationView = new BStringView("station", "Press Shuffle to Start");
    fStationView->SetFont(&largeFont);
    fStationView->SetAlignment(B_ALIGN_LEFT);

    fSongView = new BStringView("song", "");
    fSongView->SetFont(&smallFont);
    fSongView->SetAlignment(B_ALIGN_LEFT);
    
    fquality = new BStringView("quality", "Quality: --");
    fquality->SetFont(&smallFont);
    
    fListenersView = new BStringView("listeners", "Listeners: --");
    fListenersView->SetFont(&smallFont);
    
    // Album Art
    fArtView = new AlbumArtView();
    fArtView->SetExplicitMinSize(BSize(256, 256)); 

    // NEW: Fav Buttons
    fBtnAddFav = new BButton("add_fav", "Add Fav", new BMessage(MSG_ADD_FAV));
    fBtnDelFav = new BButton("del_fav", "Del Fav", new BMessage(MSG_DEL_FAV));
    
    // Standard Controls
    fShuffleBtn = new BButton("shuffle", "Shuffle", new BMessage(MSG_SHUFFLE));
    BButton* stopBtn = new BButton("stop", "Stop", new BMessage(MSG_STOP));
    
    fVolumeSlider = new BSlider("volume", "Volume", new BMessage(MSG_VOL_CHANGE), 
                                0, 100, B_HORIZONTAL);
    fVolumeSlider->SetValue(100);

    // --- LAYOUT BUILDER FOR PLAYER TAB ---
    BLayoutBuilder::Group<>(playerGroup, B_VERTICAL, 10)
        .SetInsets(10)
        .Add(fArtView)      
        .Add(fStationView) 
        .Add(fSongView)
        // This nested Group creates the "Split Row" from your mock
        .AddGroup(B_HORIZONTAL, 0) 
            .AddGroup(B_VERTICAL, 0) // LEFT: Info
                .Add(fListenersView)
                .Add(fquality)
            .End()
            .AddGlue() // Pushes the buttons to the far right
            .AddGroup(B_VERTICAL, 5) // RIGHT: Buttons
                .Add(fBtnAddFav)
                .Add(fBtnDelFav)
            .End()
        .End()
        // End Split Row
        .AddGlue()
        .Add(fVolumeSlider)
        .AddGroup(B_HORIZONTAL, 10)
            .Add(stopBtn)
            .Add(fShuffleBtn)
        .End();

    // ==========================================
    // TAB 2: FAVORITES VIEW (The List)
    // ==========================================
    BGroupView* favGroup = new BGroupView(B_VERTICAL, 10);
    favGroup->SetName("Fav"); 

    fFavList = new BListView("fav_list");
    fFavList->SetInvocationMessage(new BMessage(MSG_PLAY_FAV)); 
    
    BLayoutBuilder::Group<>(favGroup, B_VERTICAL, 0)
        .SetInsets(10)
        .Add(new BScrollView("fav_scroll", fFavList, 0, false, true))
    .End();

    // ==========================================
    // TAB 3: CONFIG VIEW (Placeholder)
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

	// Set current selection based on cfg
	BMenuItem* selectedItem = qualityMenu->FindItem(cfg.quality.c_str());
	if (selectedItem) selectedItem->SetMarked(true);

    // FIX STARTS HERE:
    // 1. Create a standalone label (This will respect our Dark Theme)
    BStringView* qualityLabel = new BStringView("lbl_qual", "Audio Quality:");
    
    // 2. Create the menu field WITHOUT a built-in label (NULL)
    BMenuField* qualityField = new BMenuField("quality_field", NULL, qualityMenu);

    // --- Checkboxes ---
    BCheckBox* chkShuffle = new BCheckBox("chk_shuffle", "Auto Shuffle", new BMessage(MSG_CFG_AUTO_SHUFFLE));
    chkShuffle->SetValue(cfg.autoShuffle ? B_CONTROL_ON : B_CONTROL_OFF);

    BCheckBox* chkNotify = new BCheckBox("chk_notify", "Show Notifications", new BMessage(MSG_CFG_NOTIFY));
    chkNotify->SetValue(cfg.showNotifications ? B_CONTROL_ON : B_CONTROL_OFF);
    
    BCheckBox* chkTheme = new BCheckBox("chk_theme", "Dark Theme", new BMessage(MSG_CFG_THEME));
    chkTheme->SetValue(cfg.updateTheme == "Dark" ? B_CONTROL_ON : B_CONTROL_OFF);

    // --- Layout ---
    BLayoutBuilder::Group<>(configGroup, B_VERTICAL, 10)
        .SetInsets(20)
        .AddGroup(B_HORIZONTAL, 5) // Add 5px spacing between Label and Menu
            .Add(qualityLabel)     // Add the label first
            .Add(qualityField)     // Then the dropdown
            .AddGlue()
        .End()
        .Add(chkShuffle)
        .Add(chkNotify)
        .Add(chkTheme)
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

    BStringView* txtVer = new BStringView("abt_ver", "Version 1.0 (Haiku)");
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
    BStringView* c3 = new BStringView("c3", "nlohmann/json");
    BStringView* c4 = new BStringView("c4", "Haiku Interface Kit");
    BStringView* c5 = new BStringView("c5", "Some AI Assistance");
    
    // Center the credits
    c1->SetAlignment(B_ALIGN_CENTER);
    c2->SetAlignment(B_ALIGN_CENTER);
    c3->SetAlignment(B_ALIGN_CENTER);
    c4->SetAlignment(B_ALIGN_CENTER);
    c5->SetAlignment(B_ALIGN_CENTER);

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
    fTabView->AddTab(favGroup);
    fTabView->AddTab(configGroup);
    fTabView->AddTab(aboutGroup); 

    // 4. Final Window Layout (No Insets, so tabs touch the edges)
    BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
        .SetInsets(0)
        .Add(fTabView)
    .End();

    // Load the list immediately
    RefreshFavorites();
    UpdateFavButtons();
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



void SuperMusicWindow::MessageReceived(BMessage* message)
{
    switch (message->what) {
        
        // --- FAVORITES LOGIC ---
        case MSG_ADD_FAV:
            save_favorite(); 
            RefreshFavorites(); 
            UpdateFavButtons(); 
            break;

        case MSG_DEL_FAV:
            delete_favorite();
            RefreshFavorites();
            UpdateFavButtons(); 
            break;

		case MSG_PLAY_FAV: {
    		int32 index = message->GetInt32("index", -1);
    		if (index < 0 && fFavList) {
        		index = fFavList->CurrentSelection();
   			 }

    		if (index >= 0) {
        		BStringItem* item = dynamic_cast<BStringItem*>(fFavList->ItemAt(index));
        		if (item) {
            		play_specific_url(item->Text());
            	if (fStationView) fStationView->SetText(currentStation.c_str());
           		if (fSongView) fSongView->SetText("Buffering...");
            
            	BString lStr("Listeners: ");
            	lStr << currentListeners.c_str();
            	if (fListenersView) fListenersView->SetText(lStr.String());
        		}
    		}
    		UpdateFavButtons(); 
    		break;
			}


        // --- SHUFFLE LOGIC ---
        case MSG_SHUFFLE: {
            play_random();
            if (fStationView) fStationView->SetText(currentStation.c_str());
            if (fSongView) fSongView->SetText("Buffering...");
            
            BString qStr("Quality: ");
            qStr << cfg.quality.c_str() << " (" << get_bitrate_text().c_str() << ")";
            if (fquality) fquality->SetText(qStr.String());

            BString lStr("Listeners: ");
            lStr << currentListeners.c_str();
            if (fListenersView) fListenersView->SetText(lStr.String());
            
            UpdateFavButtons(); 
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

    	case MSG_CFG_NOTIFY: {
        BCheckBox* chk = dynamic_cast<BCheckBox*>(FindView("chk_notify"));
        	if (chk) {
            	cfg.showNotifications = (chk->Value() == B_CONTROL_ON);
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
            qStr << cfg.quality.c_str() << " (" << get_bitrate_text().c_str() << ")";
            if (fquality) fquality->SetText(qStr.String());
        	}
        	break;
    	}
 
       case MSG_CFG_THEME: {
        BCheckBox* chk = dynamic_cast<BCheckBox*>(FindView("chk_theme"));
        if (chk) {
            // Toggle between "Dark" and "Default"
            cfg.updateTheme = (chk->Value() == B_CONTROL_ON) ? "Dark" : "Default";
            save_config();
  			ApplyTheme(); 
        	}
        	break;
    	}
            

        case MSG_UPDATE_ART: {
            BBitmap* newArt = BTranslationUtils::GetBitmap("/tmp/somafm_art.png");
            if (newArt) {
                if (Lock()) {
                    delete fAlbumArt; 
                    fAlbumArt = newArt;
                    if (fArtView) fArtView->Invalidate();
                    Unlock();
                }
            }
            break;
        }

        case MSG_STOP:
            mpv_command_string(mpv, "stop");
            if (fSongView) fSongView->SetText("Stopped");
            break;

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
                if (!line.empty()) {
                     fFavList->AddItem(new FavItem(line.c_str())); 
                }
            }
            infile.close();
        }
    }
}


void SuperMusicWindow::UpdateFavButtons() {
    bool isFav = is_favorite();
    
    if (fBtnAddFav) fBtnAddFav->SetEnabled(!isFav); 
    if (fBtnDelFav) fBtnDelFav->SetEnabled(isFav); 
}


void RecursiveColorApply(BView* view, rgb_color bg, rgb_color txt) {
    if (!view) return;
    view->SetViewColor(bg);
    view->SetLowColor(bg);   
    view->SetHighColor(txt);
    view->Invalidate();
    for (int32 i = 0; i < view->CountChildren(); i++) {
        RecursiveColorApply(view->ChildAt(i), bg, txt);
    }
}


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

    if (Lock()) {
        if (fTabView) {
            fTabView->SetViewColor(bgVal);
            
            for (int32 i = 0; i < fTabView->CountTabs(); i++) {
                BView* tabView = fTabView->ViewForTab(i);
                RecursiveColorApply(tabView, bgVal, txtVal);
            }
            fTabView->Invalidate();
        }
        if (fFavList) {
            fFavList->SetViewColor(bgVal);
            fFavList->SetLowColor(bgVal);
            fFavList->Invalidate(); 
        }        
        Unlock();
    }
}



SuperMusicWindow::~SuperMusicWindow()
{
    if (fAlbumArt != nullptr) {
        delete fAlbumArt;
        fAlbumArt = nullptr;
    }
}


class SuperMusicApp : public BApplication {
public:
    SuperMusicApp() : BApplication("application/x-vnd.SuperMusicThingy") {}

    virtual void ReadyToRun() {
        load_config();
        fetch_channels();
        init_mpv();


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


int main() {
	std::srand(std::time(nullptr)); 
	ensure_config_dir();
    SuperMusicApp app;   
    app.Run();    
    return 0;
}

