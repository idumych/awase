/*
 * Copyright 2026, Ian Dumych, idumych@gmail.com.
 * SPDX-License-Identifier: MIT
 */
#ifndef AWASE_POOL_H
#define AWASE_POOL_H

// The pool as the client sees it: packages in dependency order, the builds
// of them in flight, and the rule that a package is accepted (sealed) only
// when two volunteers' builds of it match.
//
// Pool holds the state and the rules. DemoPool (DemoPool.h) drives it with
// simulated volunteers until a real pool server exists. Nothing here uses
// Haiku headers, so the same code builds on Linux and in the unit tests.

#include <stdint.h>

#include <string>
#include <vector>


namespace awase {


enum PackageState {
	kWaiting,		// a dependency isn't sealed yet
	kReady,			// can be handed out
	kBuilding,		// one or two copies handed out
	kSealed,		// two builds matched
};


enum BuildPhase {
	kFetching,
	kConfiguring,
	kCompiling,
	kPackaging,
	kSigning,
	kAwaitingMatch,	// finished; waiting for the second build
};


struct Result {
	int					volunteer;
	std::string			hash;
};


struct Copy {
	int					volunteer;
	double				done;		// seconds of work done
	double				needed;		// seconds of work needed
};


struct Package {
	std::string			name;
	std::string			version;
	std::string			arch;
	int					layer;
	std::vector<int>	deps;
	PackageState		state;
	std::vector<Copy>	copies;		// in flight
	std::vector<Result>	results;	// finished, not yet matched
	double				cost;		// seconds of work on a speed-1 machine
	double				sealedAt;
	double				mismatchAt;
	int					mismatches;
};


struct Volunteer {
	std::string			name;
	double				speed;		// work-seconds per second
	bool				you;
	int					package;	// -1 when idle
};


enum EventKind {
	kEventSealed,
	kEventMismatch,
	kEventYouStarted,
	kEventYouFinished,
	kEventRound,
};


struct Event {
	double				time;
	EventKind			kind;
	int					package;
	std::string			text;
};


struct Limits {
	int					cores;		// cores lent, 1..maxCores
	int					maxCores;
	bool				idleOnly;
	int					maxTemperature;	// °C; reading sensors is to do
	bool				paused;
};


struct Stats {
	int					builds;		// your finished builds
	int					accepted;	// of those, sealed with your result
	int					mismatched;	// of those, in a mismatch
	double				buildSeconds;	// your working time
	std::vector<double>	history;	// working seconds per bucket, oldest first
};


class Pool {
public:
								Pool();

			// Setup
			int					AddVolunteer(const std::string& name,
									double speed, bool you);
			int					AddPackage(const std::string& name,
									const std::string& version,
									const std::string& arch,
									const std::vector<int>& deps,
									double cost);
			void				Clear();

			// The rules
			bool				CanBuild(int package, int volunteer) const;
			void				Assign(int package, int volunteer,
									double needed);
			void				Work(int volunteer, double seconds);
			void				Finish(int volunteer, const std::string& hash);
			void				Revoke(int volunteer);

			void				SetSpeed(int volunteer, double speed)
									{ fVolunteers[volunteer].speed = speed; }
			void				SetTime(double now) { fNow = now; }
			double				Now() const { return fNow; }

			// Reading
			const std::vector<Package>&	Packages() const { return fPackages; }
			const std::vector<Volunteer>& Volunteers() const
									{ return fVolunteers; }
			const std::vector<Event>& Events() const { return fEvents; }
			int					You() const { return fYou; }
			bool				AllSealed() const;

			// Your current build, if any
			int					YourPackage() const;
			double				YourProgress() const;	// 0..1
			BuildPhase			YourPhase() const;
			double				YourSecondsLeft() const;
			int					LastYouBuilt() const { return fLastYours; }

			const Stats&		YourStats() const { return fStats; }
			void				AddWorkingTime(double seconds,
									double bucketLength);

	static	BuildPhase			PhaseFor(double progress);

private:
			void				_UpdateReadiness();
			void				_Event(EventKind kind, int package,
									const std::string& text);
			int					_CopyIndex(const Package& package,
									int volunteer) const;

			std::vector<Package> fPackages;
			std::vector<Volunteer> fVolunteers;
			std::vector<Event>	fEvents;
			Stats				fStats;
			double				fNow;
			double				fBucketStart;
			int					fYou;
			int					fLastYours;
};


}	// namespace awase


#endif	// AWASE_POOL_H
