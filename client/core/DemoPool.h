/*
 * Copyright 2026, Ian Dumych, idumych@gmail.com.
 * SPDX-License-Identifier: MIT
 */
#ifndef AWASE_DEMO_POOL_H
#define AWASE_DEMO_POOL_H

// A simulated pool, until a real pool server exists: other volunteers'
// machines, real port names in dependency order, and now and then a pair of
// builds that disagree. Your machine works only within your limits.
// Time runs fast: one second of demo time counts as a minute of build time.

#include "Pool.h"


namespace awase {


class DemoPool {
public:
	static const double			kTimeScale;		// build seconds per second

								DemoPool(uint32_t seed = 0x5eed);

			void				Step(double seconds);

			const Pool&			GetPool() const { return fPool; }
			void				SetLimits(const Limits& limits);
			const Limits&		GetLimits() const { return fLimits; }
			void				SetIdle(bool idle) { fIdle = idle; }

			// Whether your machine is building now, and if not, why
			bool				YouActive() const;
			bool				WaitingForIdle() const;
			int					Round() const { return fRound; }

private:
			void				_NewRound();
			double				_Random();
			double				_YourSpeed() const;

			Pool				fPool;
			Limits				fLimits;
			bool				fIdle;
			double				fNow;
			double				fRoundDoneAt;
			int					fRound;
			uint32_t			fSeed;
};


}	// namespace awase


#endif	// AWASE_DEMO_POOL_H
