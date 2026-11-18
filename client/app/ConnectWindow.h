/*
 * Copyright 2026, Ian Dumych.
 * SPDX-License-Identifier: MIT
 */
#ifndef AWASE_CONNECT_WINDOW_H
#define AWASE_CONNECT_WINDOW_H

#include <Messenger.h>
#include <Window.h>

#include "LivePool.h"


class BTextControl;


enum {
	kMsgConnect = 'cnct',	// "server", "client", "secret", "command",
							// "folder" (strings)
};


// Where to build for: the coordinator's address, this machine's build client
// id and secret (from the coordinator's Clients page), and the build command.
class ConnectWindow : public BWindow {
public:
								ConnectWindow(BRect frame,
									const awase::LiveConfig& config,
									BMessenger target);

	virtual	void				MessageReceived(BMessage* message);

private:
			BTextControl*		fServer;
			BTextControl*		fClient;
			BTextControl*		fSecret;
			BTextControl*		fCommand;
			BTextControl*		fFolder;
			BMessenger			fTarget;
};


#endif	// AWASE_CONNECT_WINDOW_H
