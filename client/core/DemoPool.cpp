/*
 * Copyright 2026, Ian Dumych, idumych@gmail.com.
 * SPDX-License-Identifier: MIT
 */


#include "DemoPool.h"

#include <stdio.h>


namespace awase {


const double DemoPool::kTimeScale = 60.0;

static const double kYourFullSpeed = 1.4;
static const double kMismatchChance = 0.06;
static const double kRoundPause = 5.0;
static const double kBucketSeconds = 20.0;


struct CatalogEntry {
	const char*	name;
	const char*	version;
	const char*	arch;
	double		cost;
	const char*	deps[5];
};


// Real ports, in an order their dependencies allow. Costs are in demo
// seconds on a speed-1 machine, roughly in proportion to the real builds.
static const CatalogEntry kCatalog[] = {
	{ "binutils", "2.43", "x86_64", 14, { NULL } },
	{ "gcc", "13.3.0", "x86_64", 30, { NULL } },
	{ "zlib", "1.3.1", "x86_64", 5, { NULL } },
	{ "ncurses6", "6.5", "x86_64", 7, { NULL } },
	{ "openssl3", "3.4.1", "x86_64", 12, { "zlib", NULL } },
	{ "libpng16", "1.6.47", "x86_64", 5, { "zlib", NULL } },
	{ "libffi", "3.4.6", "arm64", 4, { NULL } },
	{ "readline", "8.2", "x86_64", 4, { "ncurses6", NULL } },
	{ "expat", "2.6.4", "x86_64", 4, { NULL } },
	{ "freetype", "2.13.3", "x86_64", 8, { "libpng16", "zlib", NULL } },
	{ "glib2", "2.82.4", "x86_64", 14, { "libffi", "zlib", NULL } },
	{ "curl", "8.12.1", "x86_64", 9, { "openssl3", "zlib", NULL } },
	{ "python3.12", "3.12.9", "x86_64", 18,
		{ "openssl3", "libffi", "readline", NULL } },
	{ "sqlite", "3.49.1", "arm64", 6, { "readline", NULL } },
	{ "fontconfig", "2.16.0", "x86_64", 6, { "freetype", "expat", NULL } },
	{ "harfbuzz", "10.3.0", "x86_64", 10, { "freetype", "glib2", NULL } },
	{ "libsdl2", "2.32.10", "x86_64", 10, { "libpng16", NULL } },
	{ "ffmpeg7", "7.1", "x86_64", 22, { "openssl3", "libsdl2", NULL } },
	{ "llvm19", "19.1.7", "x86_64", 34, { "python3.12", "zlib", NULL } },
	{ "cairo", "1.18.2", "x86_64", 9, { "fontconfig", "libpng16", NULL } },
	{ "qt6_base", "6.8.2", "x86_64", 30,
		{ "harfbuzz", "glib2", "openssl3", "fontconfig", NULL } },
	{ "sdl2_mixer", "2.8.1", "arm64", 6, { "libsdl2", NULL } },
	{ "mpv", "0.39.0", "x86_64", 12, { "ffmpeg7", "libsdl2", NULL } },
};

static const struct {
	const char*	name;
	double		speed;
} kVolunteers[] = {
	{ "ampere-01", 1.6 },
	{ "ryzen-tower", 1.5 },
	{ "pi5-garage", 0.7 },
	{ "thinkpad-x13", 0.9 },
	{ "nuc-shelf", 1.0 },
	{ "xeon-basement", 1.3 },
	{ "mini-pc", 0.8 },
	{ "steamdeck", 0.8 },
	{ "old-optiplex", 0.6 },
};


DemoPool::DemoPool(uint32_t seed)
	:
	fIdle(true),
	fNow(0),
	fRoundDoneAt(-1),
	fRound(0),
	fSeed(seed != 0 ? seed : 1)
{
	fLimits.cores = 4;
	fLimits.maxCores = 8;
	fLimits.idleOnly = false;
	fLimits.maxTemperature = 85;
	fLimits.paused = false;

	fPool.AddVolunteer("you", _YourSpeed(), true);
	for (size_t i = 0; i < sizeof(kVolunteers) / sizeof(kVolunteers[0]); i++)
		fPool.AddVolunteer(kVolunteers[i].name, kVolunteers[i].speed, false);
	_NewRound();
}


void
DemoPool::SetLimits(const Limits& limits)
{
	fLimits = limits;
	if (fLimits.maxCores < 1)
		fLimits.maxCores = 1;
	if (fLimits.cores < 1)
		fLimits.cores = 1;
	if (fLimits.cores > fLimits.maxCores)
		fLimits.cores = fLimits.maxCores;
	fPool.SetSpeed(fPool.You(), _YourSpeed());
}


bool
DemoPool::YouActive() const
{
	return !fLimits.paused && (!fLimits.idleOnly || fIdle);
}


bool
DemoPool::WaitingForIdle() const
{
	return !fLimits.paused && fLimits.idleOnly && !fIdle;
}


void
DemoPool::Step(double seconds)
{
	if (seconds <= 0)
		return;
	if (seconds > 0.5)
		seconds = 0.5;

	fNow += seconds;
	fPool.SetTime(fNow);

	if (fPool.AllSealed()) {
		if (fRoundDoneAt < 0)
			fRoundDoneAt = fNow;
		else if (fNow - fRoundDoneAt > kRoundPause)
			_NewRound();
		return;
	}

	const std::vector<Volunteer>& volunteers = fPool.Volunteers();
	const std::vector<Package>& packages = fPool.Packages();
	int you = fPool.You();

	// Hand out work: the lowest layer first, so the pool never starves.
	for (size_t v = 0; v < volunteers.size(); v++) {
		if (volunteers[v].package >= 0)
			continue;
		if ((int)v == you && !YouActive())
			continue;
		int best = -1;
		int count = 0;
		for (size_t p = 0; p < packages.size(); p++) {
			if (!fPool.CanBuild((int)p, (int)v))
				continue;
			if (best < 0 || packages[p].layer < packages[best].layer) {
				best = (int)p;
				count = 1;
			} else if (packages[p].layer == packages[best].layer
				&& _Random() < 1.0 / ++count) {
				best = (int)p;
			}
		}
		// Others pick up work at their own pace, not all on the same tick.
		if (best >= 0 && ((int)v == you || _Random() < seconds * 2.5)) {
			fPool.Assign(best, (int)v,
				packages[best].cost * (0.85 + 0.3 * _Random()));
		}
	}

	// Work, and finish what's done.
	for (size_t v = 0; v < volunteers.size(); v++) {
		int index = volunteers[v].package;
		if (index < 0)
			continue;
		double speed = volunteers[v].speed;
		if ((int)v == you) {
			if (!YouActive())
				continue;
			speed = _YourSpeed();
			fPool.AddWorkingTime(seconds * kTimeScale, kBucketSeconds);
		}
		fPool.Work((int)v, seconds * speed);

		const Package& package = packages[index];
		for (size_t c = 0; c < package.copies.size(); c++) {
			const Copy& copy = package.copies[c];
			if (copy.volunteer != (int)v || copy.done < copy.needed)
				continue;
			std::string hash = "sha256:" + package.name + "-" + package.version;
			if ((int)v != you && _Random() < kMismatchChance) {
				char odd[24];
				snprintf(odd, sizeof(odd), "sha256:odd-%08x",
					(unsigned)(_Random() * 4294967295.0));
				hash = odd;
			}
			fPool.Finish((int)v, hash);
			break;
		}
	}
}


void
DemoPool::_NewRound()
{
	fRound++;
	fRoundDoneAt = -1;
	fPool.Clear();

	size_t count = sizeof(kCatalog) / sizeof(kCatalog[0]);
	std::vector<std::string> names;
	for (size_t i = 0; i < count; i++) {
		const CatalogEntry& entry = kCatalog[i];
		std::vector<int> deps;
		for (int d = 0; d < 5 && entry.deps[d] != NULL; d++) {
			for (size_t n = 0; n < names.size(); n++) {
				if (names[n] == entry.deps[d])
					deps.push_back((int)n);
			}
		}
		char version[48];
		snprintf(version, sizeof(version), "%s-%d", entry.version, fRound);
		fPool.AddPackage(entry.name, version, entry.arch, deps, entry.cost);
		names.push_back(entry.name);
	}
}


double
DemoPool::_Random()
{
	// xorshift32: small, and the same on every platform for a given seed
	fSeed ^= fSeed << 13;
	fSeed ^= fSeed >> 17;
	fSeed ^= fSeed << 5;
	return (fSeed & 0xffffff) / (double)0x1000000;
}


double
DemoPool::_YourSpeed() const
{
	return kYourFullSpeed * fLimits.cores / fLimits.maxCores;
}


}	// namespace awase
