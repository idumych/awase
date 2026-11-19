/*
 * Copyright 2026, Ian Dumych.
 * SPDX-License-Identifier: MIT
 */
#ifndef AWASE_SETTINGS_H
#define AWASE_SETTINGS_H

#include <Rect.h>

#include <string>

#include "LivePool.h"
#include "Pool.h"


// The window's place, your limits, the coordinator to build for and this
// machine's totals, kept in ~/config/settings/Awase (readable only by you,
// since it holds the client's secret).
struct Settings {
								Settings();

			status_t			Load();
			status_t			Save() const;

			bool				CoordinatorReady() const;
			// The x86 build VM's folder, and whether its setup finished
			std::string			VmDir() const;
			bool				VmReady() const;

			BRect				frame;
			awase::Limits		limits;

			bool				useCoordinator;
			awase::LiveConfig	coordinator;

			// Building for x86 in a VM (QEMU with NVMM), as its own client
			bool				vmEnabled;
			std::string			vmClientId;
			std::string			vmSecret;
			int					vmCores;
			int					vmMemoryMB;
			int					builds;
			int					succeeded;
			int					failed;
			double				buildSeconds;
};


#endif	// AWASE_SETTINGS_H
