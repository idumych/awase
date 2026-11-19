/*
 * Copyright 2026, Ian Dumych, idumych@gmail.com.
 * SPDX-License-Identifier: MIT
 */


#include <Alert.h>
#include <Application.h>
#include <Screen.h>

#include "DeskbarView.h"
#include "Ink.h"
#include "MainWindow.h"
#include "Settings.h"
#include "Views.h"


static const char* kSignature = AWASE_SIGNATURE;


class App : public BApplication {
public:
	App()
		:
		BApplication(kSignature),
		fWindow(NULL)
	{
	}

	virtual void ReadyToRun()
	{
		ink::InitFonts();
		Settings settings;
		settings.Load();

		// Keep the window on screen if the screen is smaller than it. The
		// window's size limits keep it from getting too small.
		settings.frame = FitOnScreen(settings.frame, BScreen().Frame());
		fWindow = new MainWindow(settings);
		fWindow->Show();
	}

	// The Deskbar replicant asks how the build is going, shows the window
	// and pauses.
	virtual void MessageReceived(BMessage* message)
	{
		switch (message->what) {
			case kMsgDeskbarQuery:
			{
				BMessage reply(kMsgDeskbarState);
				if (fWindow != NULL && fWindow->LockWithTimeout(100000) == B_OK) {
					fWindow->DeskbarState(reply);
					fWindow->Unlock();
					message->SendReply(&reply, (BHandler*)NULL, 100000);
				}
				break;
			}
			case kMsgDeskbarOpen:
				if (fWindow != NULL && fWindow->LockWithTimeout(100000) == B_OK) {
					if (fWindow->IsMinimized())
						fWindow->Minimize(false);
					fWindow->Activate();
					fWindow->Unlock();
				}
				break;
			case kMsgTogglePause:
				if (fWindow != NULL)
					fWindow->PostMessage(kMsgTogglePause);
				break;
			default:
				BApplication::MessageReceived(message);
		}
	}

	virtual void AboutRequested()
	{
		BAlert* alert = new BAlert("About Awase",
			"Awase\n\n"
			"Lend your computer's idle time to build Renku OS packages.\n\n"
			"It builds for a RenkuOSPorts coordinator (Pool \xe2\x80\xba "
			"Coordinator\xe2\x80\xa6), which publishes each package from "
			"one build. The demo pool shows the plan of having every package "
			"built twice and shipped only when both builds match; nothing is "
			"built or uploaded there.\n\n"
			"Copyright 2026, Ian Dumych. MIT licence.",
			"OK");
		alert->Go(NULL);
	}

private:
	MainWindow*	fWindow;
};


int
main()
{
	App app;
	app.Run();
	return 0;
}
