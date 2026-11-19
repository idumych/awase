/*
 * Copyright 2026, Ian Dumych.
 * SPDX-License-Identifier: MIT
 */
#ifndef AWASE_MAIN_WINDOW_H
#define AWASE_MAIN_WINDOW_H

#include <Messenger.h>
#include <Window.h>

#include "DemoPool.h"
#include "LivePool.h"
#include "Settings.h"
#include "Views.h"
#include "VmService.h"
#include "VmSetup.h"


class BMenuBar;
class BMenuItem;
class BMessageRunner;


// The frame moved and shrunk as needed to be all on screen, its tab and
// border too.
BRect				FitOnScreen(BRect frame, BRect screen);


class MainWindow : public BWindow {
public:
								MainWindow(const Settings& settings);
	virtual						~MainWindow();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();
	virtual	void				MenusBeginning();

	// How the build is going, for the Deskbar replicant
			void				DeskbarState(BMessage& reply) const;

private:
			void				_Tick();
			void				_SetLimits(const awase::Limits& limits);
			void				_UseCoordinator(bool use);
			void				_ShowConnect();
			void				_UpdateMenu();
			void				_SaveSettings();
			void				_ShowVms();
			// Shrinks the window to the pocket view, or back
			void				_SetMini(bool mini);
			// Starts or stops building in the x86 VM to match the settings
			void				_UpdateVm();

			Settings			fSettings;
			awase::DemoPool		fPool;
			awase::LivePool		fLive;
			awase::VmSetup		fVmSetup;
			awase::VmService	fVm;
			BMessenger			fVmWindow;
			Model				fModel;
			BMessageRunner*		fTicker;
			bigtime_t			fLastTick;
			int					fFrame;

			bool				fMini;
			BRect				fFullFrame;	// while mini
			BPoint				fMiniAt;	// while full

			BMenuBar*			fMenuBar;
			BMenuItem*			fMiniItem;
			BMenuItem*			fDeskbarItem;
			BMenuItem*			fPauseItem;
			BMenuItem*			fDemoItem;
			BMenuItem*			fCoordinatorItem;
			HeaderView*			fHeader;
			PoolView*			fPoolView;
			NowView*			fNowView;
			StatsView*			fStatsView;
			ActivityView*		fActivityView;
			LimitsView*			fLimitsView;
			PanelColumn*		fColumn;
			PocketView*			fPocketView;
};


#endif	// AWASE_MAIN_WINDOW_H
