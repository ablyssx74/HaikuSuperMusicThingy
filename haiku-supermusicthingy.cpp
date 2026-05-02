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
    MSG_CFG_THEME        = 'c_th',
    MSG_TOGGLE_VISUALS   = 'tvis'
 
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
        double original_vol;
        
        mpv_get_property(mpv, "volume", MPV_FORMAT_DOUBLE, &original_vol);
        fade_volume(mpv, 0, 300);


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
        
        std::string url = get_quality_url(channels[idx].id);
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
    #ifdef __HAIKU__
    std::string configPath = std::string(home) + "/config/settings/SuperMusicThingy/milk_presets/";
    #else
    std::string configPath = std::string(home) + "/.config/SuperMusicThingy/milk_presets/";
    #endif

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
        // Set default font for new text
        SetFontAndColor(font); 
        // Force redraw
        Invalidate();
    }
};



SuperMusicWindow::SuperMusicWindow()
    : BWindow(BRect(100, 100, 500, 300), "SuperMusicThingy", B_TITLED_WINDOW, 
              B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS | B_QUIT_ON_WINDOW_CLOSE)
{
    fAlbumArt = nullptr;


    BFont largeFont(be_bold_font);
    largeFont.SetSize(24.0); 
    BFont smallFont(be_bold_font);
    smallFont.SetSize(12.0); 

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
    fStationView->SetAlignment(B_ALIGN_CENTER);

    fSongView = new SongLabel("song_view");
    fSongView->SetFontAndColor(&smallFont);
    fSongView->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));


    
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

        .AddGroup(B_HORIZONTAL, 0) 
            .AddGroup(B_VERTICAL, 0) 
                .Add(fListenersView)
                .Add(fquality)
            .End()
            .AddGlue() 
            .AddGroup(B_VERTICAL, 5) 
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

	BMenuItem* selectedItem = qualityMenu->FindItem(cfg.quality.c_str());
	if (selectedItem) selectedItem->SetMarked(true);

    BStringView* qualityLabel = new BStringView("lbl_qual", "Audio Quality:"); 

    BMenuField* qualityField = new BMenuField("quality_field", NULL, qualityMenu);

    // --- Checkboxes ---
    BCheckBox* fVisualsCheckbox = new BCheckBox(BRect(10, 10, 200, 30), "visuals_toggle", 
    "Enable Visualizer", new BMessage(MSG_TOGGLE_VISUALS));
    fVisualsCheckbox->SetValue(cfg.showVisuals ? B_CONTROL_ON : B_CONTROL_OFF);
    
    BCheckBox* chkShuffle = new BCheckBox("chk_shuffle", "Auto Shuffle", new BMessage(MSG_CFG_AUTO_SHUFFLE));
    chkShuffle->SetValue(cfg.autoShuffle ? B_CONTROL_ON : B_CONTROL_OFF);

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
    fTabView->AddTab(favGroup);
    fTabView->AddTab(configGroup);
    fTabView->AddTab(aboutGroup); 

    // 4. Final Window Layout 
    BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
        .SetInsets(0)
        .Add(fTabView)
    .End();


    RefreshFavorites();
    UpdateFavButtons();
    ApplyTheme(); 
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

    BTextView* textView = dynamic_cast<BTextView*>(view);
    if (textView) {
        textView->SetFontAndColor(NULL, B_FONT_ALL, &txt);
    }

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
    StopVisuals();
    snooze(50000); 
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

