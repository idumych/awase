/*
 * Copyright 2026, Ian Dumych.
 * SPDX-License-Identifier: MIT
 */


#include "MainWindow.h"

#include "ConnectWindow.h"

#include <Application.h>
#include <InterfaceDefs.h>
#include <LayoutBuilder.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <Roster.h>


static const bigtime_t kFrameInterval = 33333;	// 30 frames a second
static const bigtime_t kIdleAfter = 60000000;	// a minute without input
static const char* kWebsite = "https://idumych.github.io/awase/";

enum {
	kMsgUseDemo = 'demo',
	kMsgUseCoordinator = 'live',
	kMsgShowConnect = 'shcn',
};


MainWindow::MainWindow(const Settings& settings)
	:
	BWindow(settings.frame, "Awase", B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS),
	fSettings(settings),
	fTicker(NULL),
	fLastTick(system_time()),
	fFrame(0)
{
	fModel.demo = &fPool;
	fModel.live = false;
	fPool.SetLimits(settings.limits);
	fLive.SetLimits(settings.limits);
	fLive.SetTotals(settings.builds, settings.succeeded, settings.failed,
		settings.buildSeconds);
	fModel.snapshot = fLive.Snapshot();

	BMenuBar* menuBar = new BMenuBar("menu");
	BMenu* app = new BMenu("Awase");
	app->AddItem(new BMenuItem("About Awase" B_UTF8_ELLIPSIS,
		new BMessage(B_ABOUT_REQUESTED)));
	app->AddSeparatorItem();
	app->AddItem(new BMenuItem("Quit", new BMessage(B_QUIT_REQUESTED), 'Q'));
	menuBar->AddItem(app);

	BMenu* pool = new BMenu("Pool");
	fPauseItem = new BMenuItem("Pause building", new BMessage(kMsgTogglePause),
		'P');
	pool->AddItem(fPauseItem);
	pool->AddSeparatorItem();
	fDemoItem = new BMenuItem("Demo pool", new BMessage(kMsgUseDemo));
	fCoordinatorItem = new BMenuItem("Build for the coordinator",
		new BMessage(kMsgUseCoordinator));
	pool->AddItem(fDemoItem);
	pool->AddItem(fCoordinatorItem);
	pool->AddItem(new BMenuItem("Coordinator" B_UTF8_ELLIPSIS,
		new BMessage(kMsgShowConnect), ',', B_SHIFT_KEY));
	pool->AddSeparatorItem();
	pool->AddItem(new BMenuItem("Open the Awase website",
		new BMessage(kMsgWebsite)));
	menuBar->AddItem(pool);

	fHeader = new HeaderView(fModel);
	fPoolView = new PoolView(fModel);
	fNowView = new NowView(fModel);
	fStatsView = new StatsView(fModel);
	fActivityView = new ActivityView(fModel);
	fLimitsView = new LimitsView(fModel);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(menuBar)
		.Add(fHeader)
		.AddGroup(B_HORIZONTAL, 0)
			.Add(fPoolView, 3)
			.AddGroup(B_VERTICAL, 0, 2)
				.Add(fNowView)
				.Add(fStatsView)
				.Add(fActivityView)
			.End()
		.End()
		.Add(fLimitsView);

	_SetLimits(fPool.GetLimits());
	_UseCoordinator(settings.useCoordinator && settings.CoordinatorReady());

	BMessage tick(kMsgTick);
	fTicker = new BMessageRunner(BMessenger(this), &tick, kFrameInterval);
}


MainWindow::~MainWindow()
{
	delete fTicker;
}


void
MainWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgTick:
			_Tick();
			break;

		case kMsgTogglePause:
		{
			awase::Limits limits = fPool.GetLimits();
			limits.paused = !limits.paused;
			_SetLimits(limits);
			break;
		}

		case kMsgLimits:
		{
			awase::Limits limits = fPool.GetLimits();
			int32 value;
			bool idle;
			if (message->FindInt32("cores", &value) == B_OK)
				limits.cores = value;
			if (message->FindInt32("temperature", &value) == B_OK)
				limits.maxTemperature = value;
			if (message->FindBool("idle", &idle) == B_OK)
				limits.idleOnly = idle;
			_SetLimits(limits);
			break;
		}

		case kMsgUseDemo:
			_UseCoordinator(false);
			break;

		case kMsgUseCoordinator:
			if (fSettings.CoordinatorReady())
				_UseCoordinator(true);
			else
				_ShowConnect();
			break;

		case kMsgShowConnect:
			_ShowConnect();
			break;

		case kMsgConnect:
		{
			const char* value;
			if (message->FindString("server", &value) == B_OK)
				fSettings.coordinator.server = value;
			if (message->FindString("client", &value) == B_OK)
				fSettings.coordinator.clientId = value;
			if (message->FindString("secret", &value) == B_OK)
				fSettings.coordinator.secret = value;
			if (message->FindString("command", &value) == B_OK)
				fSettings.coordinator.buildCommand = value;
			if (message->FindString("folder", &value) == B_OK)
				fSettings.coordinator.workDir = value;
			_SaveSettings();
			_UseCoordinator(fSettings.CoordinatorReady());
			break;
		}

		case kMsgWebsite:
		{
			const char* argv[] = { kWebsite, NULL };
			be_roster->Launch("application/x-vnd.Be.URL.https", 1, argv);
			break;
		}

		case B_ABOUT_REQUESTED:
			be_app->PostMessage(B_ABOUT_REQUESTED);
			break;

		default:
			BWindow::MessageReceived(message);
	}
}


bool
MainWindow::QuitRequested()
{
	// Stopping gives a build in progress back to the coordinator, so it can
	// hand the job out again at once instead of waiting out the lease.
	fLive.Stop();
	_SaveSettings();
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


void
MainWindow::_Tick()
{
	bigtime_t now = system_time();
	double seconds = (now - fLastTick) / 1000000.0;
	fLastTick = now;

	bool idle = idle_time() >= kIdleAfter;
	if (fModel.live) {
		fLive.SetIdle(idle);
		fModel.snapshot = fLive.Snapshot();
	} else {
		fPool.SetIdle(idle);
		fPool.Step(seconds);
	}

	// The pool and the current build move every frame; the rest is cheap
	// enough to redraw a few times a second.
	fPoolView->Invalidate();
	fNowView->Invalidate();
	if (fFrame++ % 6 == 0) {
		fHeader->Invalidate();
		fStatsView->Invalidate();
		fActivityView->Invalidate();
		if (fModel.live)
			fLimitsView->Invalidate();	// the temperature reading
	}
}


void
MainWindow::_SetLimits(const awase::Limits& limits)
{
	fPool.SetLimits(limits);
	fLive.SetLimits(fPool.GetLimits());
	fPauseItem->SetLabel(fPool.GetLimits().paused
		? "Resume building" : "Pause building");
	fHeader->Invalidate();
	fNowView->Invalidate();
	fLimitsView->Invalidate();
}


void
MainWindow::_UseCoordinator(bool use)
{
	fSettings.useCoordinator = use;
	if (use) {
		fLive.Start(fSettings.coordinator);
	} else
		fLive.Stop();
	fModel.live = use;
	fModel.snapshot = fLive.Snapshot();
	_UpdateMenu();
	fHeader->Invalidate();
	fStatsView->Invalidate();
	fActivityView->Invalidate();
}


void
MainWindow::_ShowConnect()
{
	BRect frame(0, 0, 480, 220);
	frame.OffsetTo(Frame().left + 120, Frame().top + 120);
	(new ConnectWindow(frame, fSettings.coordinator, BMessenger(this)))
		->Show();
}


void
MainWindow::_UpdateMenu()
{
	fDemoItem->SetMarked(!fModel.live);
	fCoordinatorItem->SetMarked(fModel.live);
}


void
MainWindow::_SaveSettings()
{
	fSettings.frame = Frame();
	fSettings.limits = fPool.GetLimits();
	awase::LiveSnapshot live = fLive.Snapshot();
	fSettings.builds = live.builds;
	fSettings.succeeded = live.succeeded;
	fSettings.failed = live.failed;
	fSettings.buildSeconds = live.buildSeconds;
	fSettings.Save();
}
