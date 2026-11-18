/*
 * Copyright 2026, Ian Dumych.
 * SPDX-License-Identifier: MIT
 */


#include "ConnectWindow.h"

#include <Button.h>
#include <LayoutBuilder.h>
#include <StringView.h>
#include <TextControl.h>


static const uint32 kMsgOK = 'okok';


ConnectWindow::ConnectWindow(BRect frame, const awase::LiveConfig& config,
	BMessenger target)
	:
	BWindow(frame, "Coordinator", B_TITLED_WINDOW,
		B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS
			| B_CLOSE_ON_ESCAPE),
	fTarget(target)
{
	fServer = new BTextControl("Coordinator:", config.server.c_str(), NULL);
	fClient = new BTextControl("Client ID:", config.clientId.c_str(), NULL);
	fSecret = new BTextControl("Secret:", config.secret.c_str(), NULL);
	fSecret->TextView()->HideTyping(true);
	fCommand = new BTextControl("Build command:", config.buildCommand.c_str(),
		NULL);
	fFolder = new BTextControl("Work folder:", config.workDir.c_str(), NULL);
	fServer->SetExplicitMinSize(BSize(380, B_SIZE_UNSET));

	BStringView* help = new BStringView("help",
		"Create a build client on the coordinator's Clients page; its "
		"secret is shown once.");
	BFont small(be_plain_font);
	small.SetSize(small.Size() * 0.9f);
	help->SetFont(&small);

	BButton* cancel = new BButton("Cancel", new BMessage(B_QUIT_REQUESTED));
	BButton* connect = new BButton("Connect", new BMessage(kMsgOK));

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_SPACING)
		.AddGrid()
			.AddTextControl(fServer, 0, 0)
			.AddTextControl(fClient, 0, 1)
			.AddTextControl(fSecret, 0, 2)
			.AddTextControl(fCommand, 0, 3)
			.AddTextControl(fFolder, 0, 4)
		.End()
		.Add(help)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(cancel)
			.Add(connect)
		.End();

	SetDefaultButton(connect);
	fServer->MakeFocus(true);
}


void
ConnectWindow::MessageReceived(BMessage* message)
{
	if (message->what != kMsgOK) {
		BWindow::MessageReceived(message);
		return;
	}
	BMessage connect(kMsgConnect);
	connect.AddString("server", fServer->Text());
	connect.AddString("client", fClient->Text());
	connect.AddString("secret", fSecret->Text());
	connect.AddString("command", fCommand->Text());
	connect.AddString("folder", fFolder->Text());
	fTarget.SendMessage(&connect);
	Quit();
}
