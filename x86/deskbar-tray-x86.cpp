/*
 * Copyright 2026, ablyss supermusicthingy@epluribusunix.net
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <app/Application.h>
#include <app/Roster.h>
#include <Deskbar.h>
#include <interface/Bitmap.h>
#include <interface/ControlLook.h>
#include <interface/IconUtils.h>
#include <interface/MenuItem.h>
#include <interface/PopUpMenu.h>
#include <interface/View.h>
#include <interface/Window.h>
#include <kernel/fs_attr.h>
#include <storage/MimeType.h>
#include <storage/Mime.h>
#include <storage/Node.h>
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

        // View footprint stays B_MINI_ICON-derived -- see the matching note
        // in the main binary's _LoadIcon(): a B_LARGE_ICON-sized frame was
        // confirmed to make Deskbar discard this replicant right after
        // adding it. Only the drawn icon's quality improves below.
        float size = be_control_look->ComposeIconSize(B_MINI_ICON).Width();
        if (size < 16.0f) size = 16.0f;

        fIcon = new BBitmap(BRect(0, 0, size - 1, size - 1), B_RGBA32);

        entry_ref ref;
        if (be_roster->FindApp(kMainAppSignature, &ref) != B_OK)
            return;

        // Prefer rasterizing the app's own vector ("BEOS:ICON" HVIF) icon
        // directly at our exact small target size -- sharper at any UI
        // scale than the legacy fixed-size raster icon path, since vector
        // data has no native resolution to up/downscale from.
        bool loadedVector = false;
        BNode node(&ref);
        if (node.InitCheck() == B_OK) {
            attr_info info;
            if (node.GetAttrInfo("BEOS:ICON", &info) == B_OK && info.size > 0) {
                uint8* buffer = new uint8[info.size];
                if (node.ReadAttr("BEOS:ICON", B_VECTOR_ICON_TYPE, 0, buffer, info.size)
                        == (ssize_t)info.size) {
                    loadedVector = (BIconUtils::GetVectorIcon(buffer, info.size, fIcon) == B_OK);
                }
                delete[] buffer;
            }
        }

        if (!loadedVector) {
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

// See the matching, longer comment in the main binary: Deskbar calls this
// symbol through a BView* (*)(float maxWidth, float maxHeight) function
// pointer (the real, documented convention -- see NetworkStatusView's own
// instantiate_deskbar_item() in Haiku's source), passing the max size it
// will actually allow. Sizing to maxHeight directly, instead of guessing
// independently via ComposeIconSize(), tracks Deskbar's own font-scaled
// tray height and avoids requesting a size Deskbar might reject.
_EXPORT BView* instantiate_deskbar_item(float maxWidth, float maxHeight) {
    return new MyIcon(BRect(0, 0, maxHeight - 1, maxHeight - 1));
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
