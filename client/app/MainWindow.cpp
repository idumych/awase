/*
 * Copyright 2026, Ian Dumych.
 * SPDX-License-Identifier: MIT
 */


#include "MainWindow.h"

#include "ConnectWindow.h"
#include "DeskbarView.h"
#include "VmWindow.h"

#include <Application.h>
#ifdef AWASE_DESKBAR
#include <Deskbar.h>
#endif
#include <InterfaceDefs.h>
#include <LayoutBuilder.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <Roster.h>
#include <Screen.h>

#include <stdio.h>


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
	fFrame(0),
	fMini(false),
	fFullFrame(settings.frame),
	fMiniAt(settings.miniAt)
{
	fModel.demo = &fPool;
	fModel.live = false;
	fPool.SetLimits(settings.limits);
	fLive.SetLimits(settings.limits);
	fLive.SetTotals(settings.builds, settings.succeeded, settings.failed,
		settings.buildSeconds);
	fModel.snapshot = fLive.Snapshot();
	fModel.vmReady = settings.VmReady();
	fModel.vmOn = settings.vmEnabled;
	fModel.vmState = awase::kVmOff;
	fModel.vmFrozen = false;
	fModel.vmSettingUp = false;
	fModel.vmSetupFraction = 0;
	fModel.vmSnapshot = fVm.Snapshot();
	fVm.SetLimits(settings.limits);

	BMenuBar* menuBar = fMenuBar = new BMenuBar("menu");
	BMenu* app = new BMenu("Awase");
	app->AddItem(new BMenuItem("About Awase" B_UTF8_ELLIPSIS,
		new BMessage(B_ABOUT_REQUESTED)));
	app->AddSeparatorItem();
	app->AddItem(new BMenuItem("Quit", new BMessage(B_QUIT_REQUESTED), 'Q'));
	menuBar->AddItem(app);

	BMenu* view = new BMenu("View");
	fMiniItem = new BMenuItem("Mini mode", new BMessage(kMsgToggleMini), 'M');
	view->AddItem(fMiniItem);
	fDeskbarItem = NULL;
#ifdef AWASE_DESKBAR
	fDeskbarItem = new BMenuItem("Show in Deskbar",
		new BMessage(kMsgToggleDeskbar));
	view->AddItem(fDeskbarItem);
#endif
	menuBar->AddItem(view);

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
	if (kHasVm) {
		pool->AddItem(new BMenuItem("Virtual machines" B_UTF8_ELLIPSIS,
			new BMessage(kMsgShowVms)));
	}
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
	fColumn = new PanelColumn(fNowView, fStatsView, fActivityView);
	fPocketView = new PocketView(fModel);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(menuBar)
		.Add(fHeader)
		.AddGroup(B_HORIZONTAL, 0)
			.Add(fPoolView, 3)
			.Add(fColumn, 2)
		.End()
		.Add(fLimitsView)
		.Add(fPocketView);
	fPocketView->Hide();

	_SetLimits(fPool.GetLimits());
	_UseCoordinator(settings.useCoordinator && settings.CoordinatorReady());
	_SetMini(settings.mini);

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

		case kMsgToggleMini:
			_SetMini(!fMini);
			break;

#ifdef AWASE_DESKBAR
		case kMsgToggleDeskbar:
		{
			// Added by its file, so Deskbar keeps it after a restart
			BDeskbar deskbar;
			app_info info;
			if (deskbar.HasItem(kDeskbarItemName))
				deskbar.RemoveItem(kDeskbarItemName);
			else if (be_app->GetAppInfo(&info) == B_OK)
				deskbar.AddItem(&info.ref);
			break;
		}
#endif

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

		case kMsgShowVms:
			if (kHasVm)
				_ShowVms();
			break;

		case kMsgVmSetup:
		{
			const char* value;
			if (message->FindString("client", &value) == B_OK)
				fSettings.vmClientId = value;
			if (message->FindString("secret", &value) == B_OK)
				fSettings.vmSecret = value;
			_SaveSettings();
			awase::VmSetupConfig config
				= awase::VmSetup::DefaultConfig(fSettings.VmDir());
			config.vm.cores = fSettings.vmCores;
			config.vm.memoryMB = fSettings.vmMemoryMB;
			// A VM left running from before can't share its disk with setup
			fVm.Stop();
			fVmSetup.Start(config);
			break;
		}

		case kMsgVmCancelSetup:
			fVmSetup.RequestCancel();
			break;

		case kMsgVmBuild:
		{
			bool on;
			if (message->FindBool("on", &on) == B_OK) {
				fSettings.vmEnabled = on;
				_SaveSettings();
				_UpdateVm();
				BMessage state(kMsgVmEnabled);
				state.AddBool("on", on);
				fVmWindow.SendMessage(&state);
			}
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
	fVmSetup.Cancel();
	fVm.Stop();		// gives its build back too, and powers the VM off
	_SaveSettings();
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


void
MainWindow::MenusBeginning()
{
	BWindow::MenusBeginning();
#ifdef AWASE_DESKBAR
	fDeskbarItem->SetMarked(BDeskbar().HasItem(kDeskbarItemName));
#endif
}


void
MainWindow::DeskbarState(BMessage& reply) const
{
	rgb_color dot;
	reply.AddString("status", StatusText(fModel, dot));
	reply.AddBool("paused", fPool.GetLimits().paused);
	bool building = false;
	float progress = -1;
	std::string port;
	char figure[64];
	if (fModel.live) {
		const awase::LiveSnapshot& live = fModel.snapshot;
		bool uploading = live.state == awase::kLiveUploading
			|| live.state == awase::kLiveReporting;
		building = live.hasJob && (uploading
			|| live.state == awase::kLiveBuilding);
		if (live.hasJob)
			port = live.job.port;
		if (uploading && live.toUpload > 0)
			progress = (float)live.uploaded / live.toUpload;
		snprintf(figure, sizeof(figure), "%d built", live.succeeded);
	} else {
		const awase::Pool& pool = fPool.GetPool();
		int package = pool.YourPackage();
		building = package >= 0 && !fPool.GetLimits().paused;
		if (package >= 0) {
			port = pool.Packages()[package].name;
			progress = (float)pool.YourProgress();
		}
		snprintf(figure, sizeof(figure), "%d sealed (demo pool)",
			pool.YourStats().accepted);
	}
	reply.AddBool("building", building);
	reply.AddFloat("progress", progress);
	reply.AddString("port", port.c_str());
	reply.AddString("figure", figure);
}


void
MainWindow::_Tick()
{
	bigtime_t now = system_time();
	double seconds = (now - fLastTick) / 1000000.0;
	fLastTick = now;

	bool idle = idle_time() >= kIdleAfter;
	// The x86 VM: its setup, and building in it
	if (fFrame % 3 == 0) {
		awase::VmSetupSnapshot setup = fVmSetup.Snapshot();
		bool wasSettingUp = fModel.vmSettingUp;
		fModel.vmSettingUp = setup.running;
		fModel.vmSetupFraction = setup.fraction;
		if (wasSettingUp && !setup.running) {
			fModel.vmReady = fSettings.VmReady();
			_UpdateVm();
		}
		fVm.SetIdle(idle);
		fVm.Update();
		fModel.vmState = fVm.State();
		fModel.vmFrozen = fVm.Frozen();
		fModel.vmMessage = fVm.Message();
		fModel.vmOn = fSettings.vmEnabled;
		fModel.vmSnapshot = fVm.Snapshot();
	}
	if (fModel.live) {
		fLive.SetIdle(idle);
		fModel.snapshot = fLive.Snapshot();
	} else {
		fPool.SetIdle(idle);
		fPool.Step(seconds);
	}

	if (fMini) {
		fPocketView->Invalidate();
		fFrame++;
		return;
	}

	// The pool and the current build move every frame; the rest is cheap
	// enough to redraw a few times a second.
	fPoolView->Invalidate();
	fNowView->Invalidate();
	fHeader->Invalidate();		// the rings around the status dot
	if (fFrame % 2 == 0)
		fActivityView->Invalidate();	// the newest event fading
	if (fFrame++ % 6 == 0) {
		fStatsView->Invalidate();
		fLimitsView->Invalidate();	// the temperature, the VM's state
	}
}


void
MainWindow::_SetLimits(const awase::Limits& limits)
{
	fPool.SetLimits(limits);
	fLive.SetLimits(fPool.GetLimits());
	fVm.SetLimits(fPool.GetLimits());
	fPauseItem->SetLabel(fPool.GetLimits().paused
		? "Resume building" : "Pause building");
	fHeader->Invalidate();
	fNowView->Invalidate();
	fLimitsView->Invalidate();
	fPocketView->Invalidate();
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
	_UpdateVm();
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
MainWindow::_ShowVms()
{
	if (fVmWindow.IsValid()) {
		BLooper* looper = NULL;
		fVmWindow.Target(&looper);
		BWindow* window = dynamic_cast<BWindow*>(looper);
		if (window != NULL && window->Lock()) {
			window->Activate();
			window->Unlock();
		}
		return;
	}
	VmWindow* window = new VmWindow(this, fVmSetup, fVm, fSettings.VmDir(),
		fSettings.vmClientId, fSettings.vmSecret, fSettings.vmEnabled);
	fVmWindow = BMessenger(window);
	window->Show();
}


void
MainWindow::_SetMini(bool mini)
{
	if (mini == fMini)
		return;
	if (mini)
		fFullFrame = Frame();
	else
		fMiniAt = Frame().LeftTop();
	fMini = mini;

	BView* full[] = { fMenuBar, fHeader, fPoolView, fColumn, fLimitsView };
	for (BView* view : full) {
		if (mini)
			view->Hide();
		else
			view->Show();
	}
	if (mini)
		fPocketView->Show();
	else
		fPocketView->Hide();
	const uint32 fixed = B_NOT_RESIZABLE | B_NOT_ZOOMABLE;
	SetFlags(mini ? Flags() | fixed : Flags() & ~fixed);
	UpdateSizeLimits();

	BRect frame = fFullFrame;
	if (mini) {
		BSize size = fPocketView->ExplicitMinSize();
		frame = BRect(fMiniAt, fMiniAt + BPoint(size.width, size.height));
	}
	frame = FitOnScreen(frame, BScreen(this).Frame());
	MoveTo(frame.LeftTop());
	ResizeTo(frame.Width(), frame.Height());
	fMiniItem->SetMarked(mini);
}


void
MainWindow::_UpdateVm()
{
	bool want = kHasVm && fModel.live && fSettings.vmEnabled
		&& fSettings.VmReady()
		&& !fSettings.vmClientId.empty() && !fSettings.vmSecret.empty()
		&& !fVmSetup.Running();
	if (want && !fVm.Active()) {
		awase::VmConfig vm = awase::VmSetup::DefaultConfig(
			fSettings.VmDir()).vm;
		vm.cores = fSettings.vmCores;
		vm.memoryMB = fSettings.vmMemoryMB;
		awase::LiveConfig live = fSettings.coordinator;
		live.clientId = fSettings.vmClientId;
		live.secret = fSettings.vmSecret;
		live.workDir = fSettings.VmDir() + "/work";
		fVm.SetLimits(fPool.GetLimits());
		fVm.Start(vm, live);
	} else if (!want && fVm.Active())
		fVm.StopAsync();
	fModel.vmOn = fSettings.vmEnabled;
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
	fSettings.frame = fMini ? fFullFrame : Frame();
	fSettings.mini = fMini;
	fSettings.miniAt = fMini ? Frame().LeftTop() : fMiniAt;
	fSettings.limits = fPool.GetLimits();
	awase::LiveSnapshot live = fLive.Snapshot();
	fSettings.builds = live.builds;
	fSettings.succeeded = live.succeeded;
	fSettings.failed = live.failed;
	fSettings.buildSeconds = live.buildSeconds;
	fSettings.Save();
}


BRect
FitOnScreen(BRect frame, BRect screen)
{
	// Room for the window's border, and its tab above it
	BRect usable(screen.left + 5, screen.top + 28, screen.right - 5,
		screen.bottom - 5);
	if (frame.Width() > usable.Width())
		frame.right = frame.left + usable.Width();
	if (frame.Height() > usable.Height())
		frame.bottom = frame.top + usable.Height();
	if (frame.right > usable.right)
		frame.OffsetBy(usable.right - frame.right, 0);
	if (frame.bottom > usable.bottom)
		frame.OffsetBy(0, usable.bottom - frame.bottom);
	if (frame.left < usable.left)
		frame.OffsetBy(usable.left - frame.left, 0);
	if (frame.top < usable.top)
		frame.OffsetBy(0, usable.top - frame.top);
	return frame;
}
