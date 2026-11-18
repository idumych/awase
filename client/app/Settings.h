/*
 * Copyright 2026, Ian Dumych.
 * SPDX-License-Identifier: MIT
 */
#ifndef AWASE_SETTINGS_H
#define AWASE_SETTINGS_H

#include <Rect.h>

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

			BRect				frame;
			awase::Limits		limits;

			bool				useCoordinator;
			awase::LiveConfig	coordinator;

			int					builds;
			int					succeeded;
			int					failed;
			double				buildSeconds;
};


#endif	// AWASE_SETTINGS_H
