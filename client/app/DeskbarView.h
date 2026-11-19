/*
 * Copyright 2026, Ian Dumych, idumych@gmail.com.
 * SPDX-License-Identifier: MIT
 */
#ifndef AWASE_DESKBAR_VIEW_H
#define AWASE_DESKBAR_VIEW_H

// The Deskbar replicant: a small ring that shows how the build is going, as
// in the Pocket mockup. Deskbar loads the app's own executable as an add-on
// to make it, and the replicant asks the running app for its state.
// Only on Renku and Haiku: Cosmoe and VitruvianOS have no Deskbar.

#include <SupportDefs.h>

#if defined(__HAIKU__) && !defined(APP_COSMOE) && !defined(APP_VOS)
#define AWASE_DESKBAR 1
#endif


#define AWASE_SIGNATURE "application/x-vnd.Awase"

static const char* const kDeskbarItemName = "Awase";

enum {
	kMsgDeskbarQuery = 'dbqr',	// replicant to app; the reply is the state:
								// "status", "port" (strings), "progress"
								// (float, < 0 for unknown), "building",
								// "paused" (bools), "figure" (string)
	kMsgDeskbarState = 'dbst',
	kMsgDeskbarOpen = 'dbop',	// replicant to app: show the window
};


#ifdef AWASE_DESKBAR

#include <String.h>
#include <View.h>


class BMessageRunner;


class DeskbarView : public BView {
public:
								DeskbarView(BRect frame);
								DeskbarView(BMessage* archive);
	virtual						~DeskbarView();

	static	DeskbarView*		Instantiate(BMessage* archive);
	virtual	status_t			Archive(BMessage* archive,
									bool deep = true) const;

	virtual	void				AttachedToWindow();
	virtual	void				DetachedFromWindow();
	virtual	void				Draw(BRect updateRect);
	virtual	void				MessageReceived(BMessage* message);
	virtual	void				MouseDown(BPoint where);

private:
			void				_Init();
			void				_Poll();
			void				_SetToolTip(const BString& text);
			void				_ShowMenu(BPoint where);

			BMessageRunner*		fRunner;
			bool				fRunning;
			bool				fBuilding;
			bool				fPaused;
			float				fProgress;
			float				fTurn;		// degrees, while nobody knows
			BString				fToolTip;
};


extern "C" _EXPORT BView* instantiate_deskbar_item(float maxWidth,
	float maxHeight);

#endif	// AWASE_DESKBAR


#endif	// AWASE_DESKBAR_VIEW_H
