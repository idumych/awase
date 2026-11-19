/*
 * Copyright 2026, Ian Dumych, idumych@gmail.com.
 * SPDX-License-Identifier: MIT
 */


#include "DeskbarView.h"

#ifdef AWASE_DESKBAR

#include "Views.h"

#include <Deskbar.h>
#include <Entry.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <Messenger.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <Window.h>
#include <image.h>

#include <algorithm>
#include <math.h>


enum {
	kMsgPoll = 'dbpl',
	kMsgRemove = 'dbrm',
};

static const bigtime_t kPollInterval = 500000;

static const rgb_color kFireColor = { 255, 106, 19, 255 };
static const rgb_color kSealColor = { 216, 67, 42, 255 };
static const rgb_color kGoldColor = { 232, 178, 74, 255 };


// The file Deskbar loaded this replicant from, so a click starts that copy
// of Awase rather than whichever one the roster finds for the signature.
static status_t
OwnRef(entry_ref* ref)
{
	addr_t here = (addr_t)&instantiate_deskbar_item;
	image_info info;
	int32 cookie = 0;
	while (get_next_image_info(B_CURRENT_TEAM, &cookie, &info) == B_OK) {
		addr_t text = (addr_t)info.text;
		if (here >= text && here < text + info.text_size)
			return get_ref_for_path(info.name, ref);
	}
	return B_ENTRY_NOT_FOUND;
}


DeskbarView::DeskbarView(BRect frame)
	:
	BView(frame, kDeskbarItemName, B_FOLLOW_LEFT | B_FOLLOW_TOP,
		B_WILL_DRAW)
{
	_Init();
}


DeskbarView::DeskbarView(BMessage* archive)
	:
	BView(archive)
{
	_Init();
}


DeskbarView::~DeskbarView()
{
	delete fRunner;
}


void
DeskbarView::_Init()
{
	fRunner = NULL;
	fRunning = false;
	fBuilding = false;
	fPaused = false;
	fProgress = -1;
	fTurn = 0;
}


DeskbarView*
DeskbarView::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, "DeskbarView"))
		return NULL;
	return new DeskbarView(archive);
}


status_t
DeskbarView::Archive(BMessage* archive, bool deep) const
{
	status_t status = BView::Archive(archive, deep);
	if (status == B_OK)
		status = archive->AddString("add_on", AWASE_SIGNATURE);
	if (status == B_OK)
		status = archive->AddString("class", "DeskbarView");
	return status;
}


void
DeskbarView::AttachedToWindow()
{
	BView::AttachedToWindow();
	// The tray draws behind it; its colour is what the greys are tinted from
	SetLowColor(Parent() != NULL ? Parent()->ViewColor()
		: ui_color(B_MENU_BACKGROUND_COLOR));
	SetViewColor(B_TRANSPARENT_COLOR);

	BMessage poll(kMsgPoll);
	fRunner = new BMessageRunner(BMessenger(this), &poll, kPollInterval);
	_Poll();
}


void
DeskbarView::DetachedFromWindow()
{
	delete fRunner;
	fRunner = NULL;
	BView::DetachedFromWindow();
}


// The seal in a ring: the ring fills as the build goes, or a short arc
// turns while nobody knows how far it is. Grey when Awase isn't running,
// gold when it's paused.
void
DeskbarView::Draw(BRect updateRect)
{
	BRect bounds = Bounds();
	float size = std::min(bounds.Width(), bounds.Height()) + 1;
	BPoint center(bounds.left + bounds.Width() / 2,
		bounds.top + bounds.Height() / 2);
	float pen = std::max(1.5f, size / 9);
	float radius = size / 2 - pen / 2 - 0.5f;
	rgb_color panel = LowColor();
	bool dark = panel.Brightness() < 128;
	rgb_color track = tint_color(panel, dark ? B_LIGHTEN_2_TINT
		: B_DARKEN_2_TINT);

	SetDrawingMode(B_OP_ALPHA);
	SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	SetPenSize(pen);
	SetHighColor(fRunning && fPaused ? kGoldColor : track);
	StrokeEllipse(center, radius, radius);
	if (fRunning && !fPaused) {
		SetHighColor(kFireColor);
		if (fProgress > 0.002f)
			StrokeArc(center, radius, radius, 90,
				-360 * std::min(1.0f, fProgress));
		else if (fBuilding)
			StrokeArc(center, radius, radius, 90 - fTurn, -100);
	}
	SetPenSize(1);

	float half = std::max(2.5f, size * 0.2f);
	BRect seal(center.x - half, center.y - half, center.x + half,
		center.y + half);
	SetHighColor(fRunning ? kSealColor : tint_color(panel,
		dark ? B_LIGHTEN_1_TINT : B_DARKEN_3_TINT));
	FillRoundRect(seal, half * 0.4f, half * 0.4f);
	(void)updateRect;
}


void
DeskbarView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgPoll:
			_Poll();
			break;

		case kMsgDeskbarState:
		{
			fRunning = true;
			bool flag;
			fBuilding = message->FindBool("building", &flag) == B_OK && flag;
			fPaused = message->FindBool("paused", &flag) == B_OK && flag;
			float progress;
			fProgress = message->FindFloat("progress", &progress) == B_OK
				? progress : -1;
			if (fBuilding && fProgress < 0)
				fTurn = fmodf(fTurn + 36, 360);

			BString status = message->GetString("status", "");
			BString port = message->GetString("port", "");
			BString tip("Awase: ");
			if (fBuilding && port.Length() > 0) {
				tip << "building " << port;
				if (fProgress >= 0)
					tip << ", " << (int32)(fProgress * 100) << "%";
			} else {
				status.ToLower();
				tip << status;
			}
			BString figure = message->GetString("figure", "");
			if (figure.Length() > 0)
				tip << "\n" << figure;
			_SetToolTip(tip);
			Invalidate();
			break;
		}

		case kMsgDeskbarOpen:
		{
			BMessenger app(AWASE_SIGNATURE);
			BMessage open(kMsgDeskbarOpen);
			entry_ref ref;
			if (app.IsValid())
				app.SendMessage(&open, (BHandler*)NULL, 100000);
			else if (OwnRef(&ref) == B_OK)
				be_roster->Launch(&ref);
			else
				be_roster->Launch(AWASE_SIGNATURE);
			break;
		}

		case kMsgTogglePause:
		{
			BMessenger app(AWASE_SIGNATURE);
			BMessage pause(kMsgTogglePause);
			app.SendMessage(&pause, (BHandler*)NULL, 100000);
			_Poll();
			break;
		}

		case kMsgRemove:
			BDeskbar().RemoveItem(kDeskbarItemName);
			break;

		default:
			BView::MessageReceived(message);
	}
}


void
DeskbarView::MouseDown(BPoint where)
{
	int32 buttons = B_PRIMARY_MOUSE_BUTTON;
	if (Window()->CurrentMessage() != NULL)
		Window()->CurrentMessage()->FindInt32("buttons", &buttons);
	if ((buttons & B_SECONDARY_MOUSE_BUTTON) != 0)
		_ShowMenu(where);
	else {
		BMessage open(kMsgDeskbarOpen);
		MessageReceived(&open);
	}
}


// Asks the app how it's doing; the answer comes back as kMsgDeskbarState.
void
DeskbarView::_Poll()
{
	BMessenger app(AWASE_SIGNATURE);
	BMessage query(kMsgDeskbarQuery);
	if (app.IsValid() && app.SendMessage(&query, this, 100000) == B_OK)
		return;
	if (fRunning || fToolTip.Length() == 0) {
		fRunning = false;
		_SetToolTip("Awase isn't running. Click to start it.");
		Invalidate();
	}
}


void
DeskbarView::_SetToolTip(const BString& text)
{
	if (text == fToolTip)
		return;
	fToolTip = text;
	SetToolTip(text.String());
}


void
DeskbarView::_ShowMenu(BPoint where)
{
	BPopUpMenu* menu = new BPopUpMenu("Awase", false, false);
	menu->SetFont(be_plain_font);
	menu->AddItem(new BMenuItem(fRunning ? "Show Awase" : "Start Awase",
		new BMessage(kMsgDeskbarOpen)));
	BMenuItem* pause = new BMenuItem(fPaused ? "Resume building"
		: "Pause building", new BMessage(kMsgTogglePause));
	pause->SetEnabled(fRunning);
	menu->AddItem(pause);
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Remove from Deskbar",
		new BMessage(kMsgRemove)));
	menu->SetTargetForItems(this);
	ConvertToScreen(&where);
	menu->Go(where, true, true, true);
}


extern "C" _EXPORT BView*
instantiate_deskbar_item(float maxWidth, float maxHeight)
{
	float size = std::min(maxWidth, maxHeight);
	return new DeskbarView(BRect(0, 0, size - 1, size - 1));
}

#endif	// AWASE_DESKBAR
