/*
 * Copyright 2026, ablyss supermusicthingy@epluribusunix.net
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <app/Application.h>
#include <app/Roster.h>
#include <Deskbar.h>
#include <interface/Bitmap.h>
#include <interface/ControlLook.h>
#include <interface/MenuItem.h>
#include <interface/PopUpMenu.h>
#include <interface/View.h>
#include <interface/Window.h>
#include <storage/MimeType.h>
#include <storage/NodeInfo.h>
#include <support/Archivable.h>
#include <Message.h>

enum {
    kMsgActivateApp = 'atry',
    kMsgShuffle     = 'shuf',
    kMsgPause       = 'paus',
    kMsgStop        = 'stop'
};

static const char* kMainAppSignature = "application/x-vnd.HaikuSuperMusicThingy";
static const char* kMySignature      = "application/x-vnd.SuperMusicTrayIconLibrary";

class MyIcon : public BView {
public:
    MyIcon(BRect frame) 
        : BView(frame, "SuperMusicTrayIcon", B_FOLLOW_NONE, 
                B_WILL_DRAW | B_FRAME_EVENTS | B_FULL_UPDATE_ON_RESIZE) {
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

        archive->AddString("class", "MyIcon");
        // Always write the legacy helper signature for this 32-bit compilation unit
        archive->AddString("add_on", kMySignature);

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
        int32 buttons = 0;
        if (Window() && Window()->CurrentMessage())
            Window()->CurrentMessage()->FindInt32("buttons", &buttons);

        BMessenger appMessenger(kMainAppSignature);
        if (!appMessenger.IsValid()) {
            status_t launchErr = be_roster->Launch(kMainAppSignature);
            if (launchErr == B_OK || launchErr == B_ALREADY_RUNNING)
                appMessenger = BMessenger(kMainAppSignature);
        }

        if (buttons & B_PRIMARY_MOUSE_BUTTON) {
            if (appMessenger.IsValid()) {
                BMessage showPlayerMsg(kMsgActivateApp);
                showPlayerMsg.AddString("target_tab", "radio");
                appMessenger.SendMessage(&showPlayerMsg);
            }
        } else if (buttons & B_SECONDARY_MOUSE_BUTTON) {
            BPopUpMenu* popup = new BPopUpMenu("tray_popup", false, false);

            BMessage* showMsg = new BMessage(kMsgActivateApp);
            showMsg->AddString("target_tab", "radio");
            popup->AddItem(new BMenuItem("Show Player", showMsg));
            popup->AddSeparatorItem();

            BMessage* stationsMsg = new BMessage(kMsgActivateApp);
            stationsMsg->AddString("target_tab", "stations");
            popup->AddItem(new BMenuItem("Stations", stationsMsg));

            BMessage* favsMsg = new BMessage(kMsgActivateApp);
            favsMsg->AddString("target_tab", "favorites");
            popup->AddItem(new BMenuItem("Favorites", favsMsg));

            BMessage* eqMsg = new BMessage(kMsgActivateApp);
            eqMsg->AddString("target_tab", "eq");
            popup->AddItem(new BMenuItem("Config", eqMsg));

            popup->AddSeparatorItem();
            popup->AddItem(new BMenuItem("Shuffle", new BMessage(kMsgShuffle)));
            popup->AddItem(new BMenuItem("Pause", new BMessage(kMsgPause)));
            popup->AddItem(new BMenuItem("Stop", new BMessage(kMsgStop)));
            popup->AddSeparatorItem();
            popup->AddItem(new BMenuItem("Quit", new BMessage(B_QUIT_REQUESTED)));

            if (appMessenger.IsValid())
                popup->SetTargetForItems(appMessenger);
            else
                popup->SetTargetForItems(this);

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
        if (size < 16.0f) size = 16.0f;

        fIcon = new BBitmap(BRect(0, 0, size - 1, size - 1), B_RGBA32);

        entry_ref ref;
        if (be_roster->FindApp(kMainAppSignature, &ref) == B_OK) {
            if (BNodeInfo::GetTrackerIcon(&ref, fIcon, (icon_size)size) != B_OK) {
                BMimeType type(kMainAppSignature);
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

// ------------------- Exported Symbols -------------------

extern "C" {

_EXPORT BView* instantiate_deskbar_item(void) {
    float size = be_control_look->ComposeIconSize(B_MINI_ICON).Width();
    if (size < 16.0f) size = 16.0f;
    return new MyIcon(BRect(0, 0, size - 1, size - 1));
}

} // extern "C"

class TrayLibApp : public BApplication {
public:
    TrayLibApp() : BApplication(kMySignature) {}
};

int main() {
    TrayLibApp app;
    return 0;
}
