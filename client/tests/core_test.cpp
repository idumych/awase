/*
 * Copyright 2026, Ian Dumych, idumych@gmail.com.
 * SPDX-License-Identifier: MIT
 */

// The pool's rules, checked on the host: c++ -Icore tests/core_test.cpp
// core/*.cpp && ./a.out

#include "DemoPool.h"
#include "Pool.h"

#include <stdio.h>
#include <stdlib.h>

using namespace awase;

static int sFailures = 0;

#define CHECK(x) \
	do { \
		if (!(x)) { \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); \
			sFailures++; \
		} \
	} while (0)


static void
TwoMatchingBuildsSeal()
{
	Pool pool;
	int you = pool.AddVolunteer("you", 1, true);
	int a = pool.AddVolunteer("a", 1, false);
	int zlib = pool.AddPackage("zlib", "1.3.1", "x86_64", std::vector<int>(), 5);
	CHECK(pool.Packages()[zlib].state == kReady);

	pool.Assign(zlib, you, 5);
	pool.Assign(zlib, a, 5);
	pool.Finish(you, "h1");
	CHECK(pool.Packages()[zlib].state == kBuilding);
	CHECK(pool.YourStats().builds == 1);
	CHECK(pool.YourStats().accepted == 0);
	pool.Finish(a, "h1");
	CHECK(pool.Packages()[zlib].state == kSealed);
	CHECK(pool.YourStats().accepted == 1);
	CHECK(pool.AllSealed());
}


static void
MismatchStartsOver()
{
	Pool pool;
	int you = pool.AddVolunteer("you", 1, true);
	int a = pool.AddVolunteer("a", 1, false);
	int p = pool.AddPackage("ffmpeg7", "7.1", "x86_64", std::vector<int>(), 5);
	pool.Assign(p, you, 5);
	pool.Assign(p, a, 5);
	pool.Finish(you, "h1");
	pool.Finish(a, "evil");
	CHECK(pool.Packages()[p].state == kReady);
	CHECK(pool.Packages()[p].results.empty());
	CHECK(pool.Packages()[p].mismatches == 1);
	CHECK(pool.YourStats().mismatched == 1);
	CHECK(pool.YourStats().accepted == 0);
	CHECK(pool.Events().back().kind == kEventMismatch);
}


static void
NoTwoCopiesOnOneMachine()
{
	Pool pool;
	int you = pool.AddVolunteer("you", 1, true);
	int a = pool.AddVolunteer("a", 1, false);
	int b = pool.AddVolunteer("b", 1, false);
	int p = pool.AddPackage("gcc", "13.3.0", "x86_64", std::vector<int>(), 5);
	pool.Assign(p, you, 5);
	pool.Finish(you, "h");
	// You can't supply the second build of your own package.
	CHECK(!pool.CanBuild(p, you));
	CHECK(pool.CanBuild(p, a));
	pool.Assign(p, a, 5);
	// Two copies out or in: no third.
	CHECK(!pool.CanBuild(p, b));
}


static void
DependenciesGate()
{
	Pool pool;
	int a = pool.AddVolunteer("a", 1, false);
	int b = pool.AddVolunteer("b", 1, false);
	int zlib = pool.AddPackage("zlib", "1", "x86_64", std::vector<int>(), 5);
	std::vector<int> deps(1, zlib);
	int png = pool.AddPackage("libpng16", "1", "x86_64", deps, 5);
	CHECK(pool.Packages()[png].state == kWaiting);
	CHECK(pool.Packages()[png].layer == 1);
	CHECK(!pool.CanBuild(png, a));
	pool.Assign(zlib, a, 5);
	pool.Assign(zlib, b, 5);
	pool.Finish(a, "z");
	pool.Finish(b, "z");
	CHECK(pool.Packages()[png].state == kReady);
}


static void
PauseStopsYourWork()
{
	DemoPool demo(7);
	Limits limits = demo.GetLimits();
	limits.paused = true;
	demo.SetLimits(limits);
	for (int i = 0; i < 600; i++)
		demo.Step(0.1);
	CHECK(demo.GetPool().YourStats().builds == 0);
	CHECK(demo.GetPool().YourStats().buildSeconds == 0);
	CHECK(!demo.YouActive());
}


static void
IdleOnlyWaits()
{
	DemoPool demo(7);
	Limits limits = demo.GetLimits();
	limits.idleOnly = true;
	demo.SetLimits(limits);
	demo.SetIdle(false);
	CHECK(demo.WaitingForIdle());
	for (int i = 0; i < 100; i++)
		demo.Step(0.1);
	CHECK(demo.GetPool().YourStats().buildSeconds == 0);
	demo.SetIdle(true);
	for (int i = 0; i < 100; i++)
		demo.Step(0.1);
	CHECK(demo.GetPool().YourStats().buildSeconds > 0);
}


static void
DemoRoundCompletes()
{
	DemoPool demo(42);
	Limits limits = demo.GetLimits();
	limits.cores = limits.maxCores;
	demo.SetLimits(limits);
	int round = demo.Round();
	// Ten simulated minutes is plenty for a round of the catalog.
	for (int i = 0; i < 6000 && demo.Round() == round; i++)
		demo.Step(0.1);
	CHECK(demo.Round() == round + 1);
	const Stats& stats = demo.GetPool().YourStats();
	CHECK(stats.builds > 0);
	CHECK(stats.accepted + stats.mismatched <= stats.builds);
	printf("round took %.0f s; you built %d, %d accepted\n",
		demo.GetPool().Now(), stats.builds, stats.accepted);
}


int
main()
{
	TwoMatchingBuildsSeal();
	MismatchStartsOver();
	NoTwoCopiesOnOneMachine();
	DependenciesGate();
	PauseStopsYourWork();
	IdleOnlyWaits();
	DemoRoundCompletes();
	if (sFailures == 0)
		printf("all core tests passed\n");
	return sFailures == 0 ? 0 : 1;
}
