#include <View.h>
#include <Bitmap.h>
#include <Deskbar.h>
#include <Roster.h>
#include <NodeInfo.h>
#include <Mime.h>
#include <PopUpMenu.h>
#include <MenuItem.h>
#include <ControlLook.h>
#include <Window.h>
#include <Message.h>
#include <Entry.h>
#include <string.h>

// Define application messages manually since you don't want to mix headers
#define MSG_ACTIVATE_APP 'mACT'
#define MSG_SHUFFLE      'mSHF'
#define MSG_PAUSE        'mPAS'
#define MSG_STOP         'mSTP'

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
    
    // GCC2 Requires standard _EXPORT behavior for runtime binding
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

        archive->AddString("class", "MyIcon");
        
        // This file is built exclusively as the 32-bit library or 64-bit helper addon
        #ifndef IS_HAIKU_32BIT
        archive->AddString("add_on", "application/x-vnd.HaikuSuperMusicThingy"); 
        #else
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
        if (Parent()) {
            SetLowColor(Parent()->ViewColor());
            FillRect(updateRect, B_SOLID_LOW);
        }

        if (fIcon) {
            SetDrawingMode(B_OP_ALPHA);
            SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
            
            BRect bounds = Bounds();
            float iconSize = fIcon->Bounds().Width();
            float x = (bounds.Width() - iconSize) / 2.0f;
            float y = (bounds.Height() - iconSize) / 2.0f;
            
            DrawBitmap(fIcon, BPoint(x, y));
        } else {
            SetHighColor(ui_color(B_NAVIGATION_BASE_COLOR));
            FillRect(Bounds());
        }
    }

    virtual void MouseDown(BPoint point) {
        int32 buttons;
        if (Window()->CurrentMessage()->FindInt32("buttons", &buttons) != B_OK)
            return;

        BMessenger appMessenger("application/x-vnd.HaikuSuperMusicThingy");

        if (!appMessenger.IsValid()) {
            status_t launchErr = be_roster->Launch("application/x-vnd.HaikuSuperMusicThingy");
            if (launchErr == B_OK || launchErr == B_ALREADY_RUNNING) {
                appMessenger = BMessenger("application/x-vnd.HaikuSuperMusicThingy");
            }
        }

        if (buttons & B_PRIMARY_MOUSE_BUTTON) {
            if (appMessenger.IsValid()) {
                BMessage showPlayerMsg(MSG_ACTIVATE_APP);
                showPlayerMsg.AddString("target_tab", "radio");
                appMessenger.SendMessage(&showPlayerMsg);
            }
        } else if (buttons & B_SECONDARY_MOUSE_BUTTON) {
            BPopUpMenu *popup = new BPopUpMenu("tray_popup", false, false);        
            
            BMessage* showMsg = new BMessage(MSG_ACTIVATE_APP);
            showMsg->AddString("target_tab", "radio");
            popup->AddItem(new BMenuItem("Show Player", showMsg));
            
            popup->AddSeparatorItem();

            BMessage* stationsMsg = new BMessage(MSG_ACTIVATE_APP);
            stationsMsg->AddString("target_tab", "stations");
            popup->AddItem(new BMenuItem("Stations", stationsMsg));

            BMessage* favsMsg = new BMessage(MSG_ACTIVATE_APP);
            favsMsg->AddString("target_tab", "favorites");
            popup->AddItem(new BMenuItem("Favorites", favsMsg));
            
            BMessage* eqMessage = new BMessage(MSG_ACTIVATE_APP);
            eqMessage->AddString("target_tab", "eq"); 
            popup->AddItem(new BMenuItem("Config", eqMessage));
            
            popup->AddSeparatorItem();
            
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

        float size = be_control_look->ComposeIconSize(B_MINI_ICON).Width();
        
        if (Bounds().Width() > 0 && Bounds().Width() < size) {
            size = Bounds().Width();
        }

        fIcon = new BBitmap(BRect(0, 0, size - 1, size - 1), B_RGBA32);

        entry_ref ref;
        if (be_roster->FindApp("application/x-vnd.HaikuSuperMusicThingy", &ref) == B_OK) {
            if (BNodeInfo::GetTrackerIcon(&ref, fIcon, (icon_size)size) != B_OK) {
                BMimeType type("application/x-vnd.HaikuSuperMusicThingy");
                type.GetIcon(fIcon, (icon_size)size);
            }
        }
    }
    BBitmap* fIcon;
};

// ====================================================================
// --- EXPORTED GLOBAL INSTANTIATION SYMBOL ---
// ====================================================================
extern "C" _EXPORT BArchivable* Instantiate(BMessage* data) {
    if (!validate_instantiation_bits(data, "MyIcon"))
        return NULL;
    return new MyIcon(data);
}
