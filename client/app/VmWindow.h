/*
 * Copyright 2026, Ian Dumych.
 * SPDX-License-Identifier: MIT
 */
#ifndef AWASE_VM_WINDOW_H
#define AWASE_VM_WINDOW_H

// The Virtual machines window: what this machine offers, setting up the x86
// build VM with progress, and turning building in it on or off. Awase sets
// up a VM only with NVMM, and only for x86, the one platform NVMM runs.

#include <Window.h>

#include <string>
#include <vector>

#include "Ink.h"
#include "VmHost.h"
#include "VmService.h"
#include "VmSetup.h"


class BTextControl;


enum {
	kMsgShowVms = 'shvm',
	kMsgVmSetup = 'vmsu',		// "client", "secret" (strings)
	kMsgVmCancelSetup = 'vmcn',
	kMsgVmBuild = 'vmbd',		// "on" (bool)
	kMsgVmEnabled = 'vmen',		// to the window: "on" (bool)
};


class VmView : public ink::InkView {
public:
								VmView(awase::VmSetup& setup,
									awase::VmService& service,
									const std::string& vmDir,
									const std::string& client,
									const std::string& secret, bool enabled);

	virtual	void				AttachedToWindow();
	virtual	void				AllAttached();
	virtual	void				MouseDown(BPoint where);
	virtual	void				MouseMoved(BPoint where, uint32 transit,
									const BMessage* drag);
	virtual	void				Pulse();
	virtual	void				MessageReceived(BMessage* message);

			void				SetEnabled(bool enabled);

protected:
	virtual	void				Paint(BView* canvas, BRect bounds);

private:
			enum Page { kChoose, kDeclined, kProgress };

			struct Button {
				BRect			frame;
				uint32			what;
				std::string		label;
				bool			primary;
				bool			enabled;
			};

			Page				_Page();
			void				_Check();
			void				_ShowFields(bool show);
			void				_Header(BView* canvas, BRect bounds,
									const char* label, rgb_color labelColor,
									const char* title, const char* text);
			float				_Item(BView* canvas, float x, float y,
									float width, int icon, const char* title,
									const std::string& detail);
			void				_Footer(BView* canvas, BRect bounds,
									const char* note);
			void				_AddButton(BView* canvas, float right,
									float top, uint32 what, const char* label,
									bool primary, bool enabled, float* left);
			void				_PaintChoose(BView* canvas, BRect bounds);
			void				_PaintDeclined(BView* canvas, BRect bounds);
			void				_PaintProgress(BView* canvas, BRect bounds);
			void				_Do(uint32 what);

			awase::VmSetup&		fSetup;
			awase::VmService&	fService;
			std::string			fDir;
			awase::HostCheck	fHost;
			awase::VmSetupSnapshot fSnapshot;
			bool				fReady;
			bool				fEnabled;
			bool				fShowResult;	// a setup started here
			bool				fX86;
			BTextControl*		fClient;
			BTextControl*		fSecret;
			std::vector<Button>	fButtons;
			int					fHover;
			BRect				fToggle;
			bigtime_t			fDoneAt[awase::kStepCount];	// for the sparks
};


class VmWindow : public BWindow {
public:
								VmWindow(BWindow* owner, awase::VmSetup& setup,
									awase::VmService& service,
									const std::string& vmDir,
									const std::string& client,
									const std::string& secret, bool enabled);

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

private:
			BWindow*			fOwner;
			VmView*				fView;
};


#endif	// AWASE_VM_WINDOW_H
