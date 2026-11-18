/*
 * Copyright 2026, Ian Dumych, idumych@gmail.com.
 * SPDX-License-Identifier: MIT
 */
#ifndef AWASE_LIVE_POOL_H
#define AWASE_LIVE_POOL_H

// Building for a real coordinator: the RenkuOSPorts portal. A thread claims
// a job, runs the build command, uploads the packages it made, and reports
// the job done or failed, always within the volunteer's limits. The window
// reads a copy of the state with Snapshot().

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Builder.h"
#include "Pool.h"
#include "Portal.h"


namespace awase {


enum LiveState {
	kLiveOff,			// not configured, or stopped
	kLiveConnecting,
	kLiveIdle,			// waiting for the next poll
	kLiveBuilding,
	kLiveUploading,
	kLiveReporting,
	kLivePaused,
	kLiveWaitingIdle,
	kLiveError,			// retrying after a delay
};


enum LiveEventKind {
	kLiveStarted,
	kLiveSucceeded,
	kLiveFailed,
	kLiveInfo,
	kLiveProblem,
};


struct LiveEvent {
	double				time;
	LiveEventKind		kind;
	std::string			text;
};


struct LiveConfig {
	std::string			server;
	std::string			clientId;
	std::string			secret;
	std::string			buildCommand;
	std::string			workDir;
	int					pollSeconds;
};


struct LiveSnapshot {
	LiveState			state;
	std::string			message;
	std::string			platform;
	std::string			server;

	bool				hasJob;
	PortalJob			job;
	BuildPhase			phase;
	double				elapsed;		// building time, pauses excluded
	std::string			lastLine;
	int					uploaded;
	int					toUpload;

	int					builds;			// finished by this machine
	int					succeeded;
	int					failed;
	double				buildSeconds;
	std::vector<double>	history;		// build seconds per 5 minutes

	std::vector<LiveEvent> events;
	std::vector<PortalPackage> recent;	// published, newest first
	std::vector<std::string> published;	// ports this machine published
	double				now;
	double				nextPoll;		// seconds until the next poll
};


class LivePool {
public:
								LivePool();
								~LivePool();

			void				Start(const LiveConfig& config);
			// Stops the thread. A build in progress is stopped and reported
			// as failed with "client stopped", as the portal's client does.
			void				Stop();
			bool				Running() const { return fThread.joinable(); }

			void				SetLimits(const Limits& limits);
			void				SetIdle(bool idle);
			LiveSnapshot		Snapshot();

			// Counters kept across runs by the app.
			void				SetTotals(int builds, int succeeded, int failed,
									double buildSeconds);

private:
			void				_Run();
			bool				_Wait(double seconds);
			void				_Event(LiveEventKind kind,
									const std::string& text);
			void				_SetState(LiveState state,
									const std::string& message);
			bool				_Allowed();
			void				_Build(const PortalJob& job);
			void				_Finish(const PortalJob& job, Builder& builder,
									const std::string& outDir);
			void				_AddBuildTime(double seconds);
			double				_Now() const;

			LiveConfig			fConfig;
			PortalClient		fPortal;
			std::thread			fThread;
			std::mutex			fLock;
			std::condition_variable fWake;
			bool				fQuit;
			Limits				fLimits;
			bool				fIdle;
			LiveSnapshot		fState;
			double				fStart;
			double				fBucketStart;
};


}	// namespace awase


#endif	// AWASE_LIVE_POOL_H
