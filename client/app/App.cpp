/*
 * Copyright 2026, Ian Dumych, idumych@gmail.com.
 * SPDX-License-Identifier: MIT
 */


#include <Alert.h>
#include <Application.h>
#include <Screen.h>

#include "Ink.h"
#include "MainWindow.h"
#include "Settings.h"


static const char* kSignature = "application/x-vnd.Awase";


class App : public BApplication {
public:
	App()
		:
		BApplication(kSignature)
	{
	}

	virtual void ReadyToRun()
	{
		ink::InitFonts();
		Settings settings;
		settings.Load();

		// Keep the window on screen if the screen got smaller.
		BRect screen = BScreen().Frame();
		BRect frame = settings.frame;
		if (!screen.Contains(frame.LeftTop()) || frame.Width() > screen.Width()
			|| frame.Height() > screen.Height()) {
			frame.OffsetTo(screen.left + 40, screen.top + 40);
			if (frame.Width() > screen.Width() - 80)
				frame.right = frame.left + screen.Width() - 80;
			if (frame.Height() > screen.Height() - 80)
				frame.bottom = frame.top + screen.Height() - 80;
		}
		settings.frame = frame;
		(new MainWindow(settings))->Show();
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
};


int
main()
{
	App app;
	app.Run();
	return 0;
}
