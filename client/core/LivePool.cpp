/*
 * Copyright 2026, Ian Dumych, idumych@gmail.com.
 * SPDX-License-Identifier: MIT
 */


#include "LivePool.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>


namespace awase {


static const size_t kMaxEvents = 50;
static const size_t kHistoryBuckets = 24;
static const double kBucketSeconds = 300;
static const double kPackagesEvery = 60;


static bool
MakeDirectories(const std::string& path)
{
	std::string sofar;
	for (size_t i = 0; i <= path.size(); i++) {
		if (i == path.size() || path[i] == '/') {
			if (!sofar.empty() && mkdir(sofar.c_str(), 0755) != 0
				&& errno != EEXIST)
				return false;
		}
		if (i < path.size())
			sofar += path[i];
	}
	return true;
}


static std::vector<std::string>
PackagesIn(const std::string& directory)
{
	std::vector<std::string> found;
	DIR* dir = opendir(directory.c_str());
	if (dir == NULL)
		return found;
	struct dirent* entry;
	while ((entry = readdir(dir)) != NULL) {
		std::string name = entry->d_name;
		if (name.size() > 5 && name.compare(name.size() - 5, 5, ".hpkg") == 0)
			found.push_back(directory + "/" + name);
	}
	closedir(dir);
	std::sort(found.begin(), found.end());
	return found;
}


static void
EmptyDirectory(const std::string& directory)
{
	std::vector<std::string> files;
	DIR* dir = opendir(directory.c_str());
	if (dir == NULL)
		return;
	struct dirent* entry;
	while ((entry = readdir(dir)) != NULL) {
		if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
			files.push_back(directory + "/" + entry->d_name);
	}
	closedir(dir);
	for (size_t i = 0; i < files.size(); i++)
		unlink(files[i].c_str());
	rmdir(directory.c_str());
}


LivePool::LivePool()
	:
	fQuit(false),
	fIdle(true),
	fStart(0),
	fBucketStart(0)
{
	fLimits.cores = 1;
	fLimits.maxCores = 1;
	fLimits.idleOnly = false;
	fLimits.maxTemperature = 85;
	fLimits.paused = false;

	fState.state = kLiveOff;
	fState.hasJob = false;
	fState.phase = kFetching;
	fState.elapsed = 0;
	fState.uploaded = 0;
	fState.toUpload = 0;
	fState.builds = 0;
	fState.succeeded = 0;
	fState.failed = 0;
	fState.buildSeconds = 0;
	fState.history.push_back(0);
	fState.now = 0;
	fState.nextPoll = 0;
	fStart = _Now();
}


LivePool::~LivePool()
{
	Stop();
}


double
LivePool::_Now() const
{
	using namespace std::chrono;
	return duration_cast<duration<double> >(
		steady_clock::now().time_since_epoch()).count();
}


void
LivePool::Start(const LiveConfig& config)
{
	Stop();
	std::lock_guard<std::mutex> lock(fLock);
	fConfig = config;
	if (fConfig.pollSeconds < 5)
		fConfig.pollSeconds = 5;
	fQuit = false;
	fState.server = config.server;
	fState.state = kLiveConnecting;
	fState.message = "Connecting to " + config.server;
	fThread = std::thread(&LivePool::_Run, this);
}


void
LivePool::Stop()
{
	{
		std::lock_guard<std::mutex> lock(fLock);
		fQuit = true;
	}
	fWake.notify_all();
	if (fThread.joinable())
		fThread.join();
	std::lock_guard<std::mutex> lock(fLock);
	fState.state = kLiveOff;
	fState.message = "Not connected";
	fState.hasJob = false;
}


void
LivePool::SetLimits(const Limits& limits)
{
	{
		std::lock_guard<std::mutex> lock(fLock);
		fLimits = limits;
	}
	fWake.notify_all();
}


void
LivePool::SetIdle(bool idle)
{
	std::lock_guard<std::mutex> lock(fLock);
	fIdle = idle;
}


void
LivePool::SetTotals(int builds, int succeeded, int failed, double buildSeconds)
{
	std::lock_guard<std::mutex> lock(fLock);
	fState.builds = builds;
	fState.succeeded = succeeded;
	fState.failed = failed;
	fState.buildSeconds = buildSeconds;
}


LiveSnapshot
LivePool::Snapshot()
{
	std::lock_guard<std::mutex> lock(fLock);
	LiveSnapshot copy = fState;
	copy.now = _Now() - fStart;
	return copy;
}


bool
LivePool::_Wait(double seconds)
{
	std::unique_lock<std::mutex> lock(fLock);
	fWake.wait_for(lock, std::chrono::duration<double>(seconds),
		[this] { return fQuit; });
	return !fQuit;
}


void
LivePool::_Event(LiveEventKind kind, const std::string& text)
{
	std::lock_guard<std::mutex> lock(fLock);
	LiveEvent event;
	event.time = _Now() - fStart;
	event.kind = kind;
	event.text = text;
	fState.events.push_back(event);
	if (fState.events.size() > kMaxEvents)
		fState.events.erase(fState.events.begin());
}


void
LivePool::_SetState(LiveState state, const std::string& message)
{
	std::lock_guard<std::mutex> lock(fLock);
	fState.state = state;
	fState.message = message;
}


bool
LivePool::_Allowed()
{
	std::lock_guard<std::mutex> lock(fLock);
	return !fLimits.paused && (!fLimits.idleOnly || fIdle);
}


void
LivePool::_AddBuildTime(double seconds)
{
	std::lock_guard<std::mutex> lock(fLock);
	double now = _Now();
	if (fBucketStart == 0)
		fBucketStart = now;
	while (now - fBucketStart >= kBucketSeconds) {
		fBucketStart += kBucketSeconds;
		fState.history.push_back(0);
		if (fState.history.size() > kHistoryBuckets)
			fState.history.erase(fState.history.begin());
	}
	fState.history.back() += seconds;
	fState.buildSeconds += seconds;
}


void
LivePool::_Run()
{
	double backoff = 10;
	double nextPoll = 0;
	double nextPackages = 0;
	bool connected = false;

	while (true) {
		{
			std::lock_guard<std::mutex> lock(fLock);
			if (fQuit)
				return;
		}

		if (!connected) {
			_SetState(kLiveConnecting, "Connecting to " + fConfig.server);
			fPortal.Configure(fConfig.server, fConfig.clientId, fConfig.secret);
			PortalStatus status = fPortal.Authenticate();
			if (status == kPortalOk) {
				connected = true;
				backoff = 10;
				{
					std::lock_guard<std::mutex> lock(fLock);
					fState.platform = fPortal.Platform();
				}
				_Event(kLiveInfo, "Connected to " + fConfig.server
					+ " as a " + fPortal.Platform() + " build client");
			} else {
				std::string why = status == kPortalUnauthorized
					? "The coordinator didn't accept this client's id and "
						"secret"
					: "Can't reach the coordinator: " + fPortal.LastError();
				_SetState(kLiveError, why);
				_Event(kLiveProblem, why);
				if (!_Wait(status == kPortalUnauthorized ? 300 : backoff))
					return;
				backoff = std::min(backoff * 2, 300.0);
				continue;
			}
		}

		double now = _Now();
		if (now >= nextPackages) {
			std::vector<PortalPackage> packages;
			if (fPortal.RecentPackages(fPortal.Platform(), packages)
					== kPortalOk) {
				std::reverse(packages.begin(), packages.end());
				std::lock_guard<std::mutex> lock(fLock);
				fState.recent = packages;
			}
			nextPackages = now + kPackagesEvery;
		}

		if (!_Allowed()) {
			bool paused;
			{
				std::lock_guard<std::mutex> lock(fLock);
				paused = fLimits.paused;
			}
			_SetState(paused ? kLivePaused : kLiveWaitingIdle,
				paused ? "Paused" : "Waiting for the computer to be idle");
			if (!_Wait(1))
				return;
			continue;
		}

		if (now < nextPoll) {
			{
				std::lock_guard<std::mutex> lock(fLock);
				fState.state = kLiveIdle;
				fState.nextPoll = nextPoll - now;
			}
			if (!_Wait(std::min(1.0, nextPoll - now)))
				return;
			continue;
		}

		PortalJob job;
		PortalStatus status = fPortal.Next(job);
		if (status == kPortalNoWork) {
			_SetState(kLiveIdle, "No queued jobs for " + fPortal.Platform());
			nextPoll = _Now() + fConfig.pollSeconds;
			continue;
		}
		if (status != kPortalOk) {
			std::string why = "Asking for work failed: " + fPortal.LastError();
			_SetState(kLiveError, why);
			_Event(kLiveProblem, why);
			if (status == kPortalUnauthorized || status == kPortalNetworkError)
				connected = false;
			if (!_Wait(backoff))
				return;
			backoff = std::min(backoff * 2, 300.0);
			continue;
		}
		backoff = 10;
		_Build(job);
		nextPoll = 0;		// ask for the next job at once
		nextPackages = 0;	// and show what was just published
	}
}


void
LivePool::_Build(const PortalJob& job)
{
	char name[48];
	snprintf(name, sizeof(name), "job-%lld", (long long)job.id);
	std::string jobDir = fConfig.workDir + "/" + name;
	std::string outDir = jobDir + "/out";
	EmptyDirectory(outDir);
	if (!MakeDirectories(outDir)) {
		fPortal.Fail(job.id, "can't create " + outDir, "");
		_Event(kLiveFailed, job.port + ": can't create the work folder");
		return;
	}

	int cores;
	{
		std::lock_guard<std::mutex> lock(fLock);
		cores = fLimits.cores;
		fState.hasJob = true;
		fState.job = job;
		fState.phase = kFetching;
		fState.elapsed = 0;
		fState.lastLine.clear();
		fState.uploaded = 0;
		fState.toUpload = 0;
	}
	char coresText[16], idText[32];
	snprintf(coresText, sizeof(coresText), "%d", cores);
	snprintf(idText, sizeof(idText), "%lld", (long long)job.id);

	std::vector<std::string> args;
	args.push_back(job.port);
	args.push_back(job.version);
	args.push_back(outDir);
	std::vector<std::pair<std::string, std::string> > environment;
	environment.push_back(std::make_pair("RENKUPORTS_PLATFORM", job.platform));
	environment.push_back(std::make_pair("RENKUPORTS_JOB_ID",
		std::string(idText)));
	environment.push_back(std::make_pair("RENKUPORTS_RECIPE_PATH",
		job.recipePath));
	environment.push_back(std::make_pair("AWASE_CORES",
		std::string(coresText)));

	Builder builder;
	if (!builder.Start(fConfig.buildCommand, args, environment,
			jobDir + "/build.log")) {
		fPortal.Fail(job.id, "can't start the build command "
			+ fConfig.buildCommand, "");
		_Event(kLiveFailed, job.port + ": can't start the build command");
		std::lock_guard<std::mutex> lock(fLock);
		fState.hasJob = false;
		return;
	}
	_SetState(kLiveBuilding, "Building " + job.port + " " + job.version);
	_Event(kLiveStarted, "Started " + job.port + " " + job.version);

	double last = _Now();
	while (builder.Poll()) {
		bool quit;
		{
			std::lock_guard<std::mutex> lock(fLock);
			quit = fQuit;
		}
		if (quit) {
			builder.Stop();
			fPortal.Fail(job.id, "client stopped", builder.Log());
			_Event(kLiveFailed, job.port + ": stopped with the client");
			EmptyDirectory(outDir);
			std::lock_guard<std::mutex> lock(fLock);
			fState.hasJob = false;
			return;
		}

		bool allowed = _Allowed();
		if (!allowed && !builder.Paused()) {
			builder.Pause();
			_SetState(kLivePaused, "Build paused");
		} else if (allowed && builder.Paused()) {
			builder.Resume();
			_SetState(kLiveBuilding, "Building " + job.port + " "
				+ job.version);
		}

		double now = _Now();
		if (!builder.Paused())
			_AddBuildTime(now - last);
		{
			std::lock_guard<std::mutex> lock(fLock);
			if (!builder.Paused())
				fState.elapsed += now - last;
			fState.phase = builder.Phase();
			fState.lastLine = builder.LastLine();
		}
		last = now;
		_Wait(0.25);
	}

	_Finish(job, builder, outDir);
	EmptyDirectory(outDir);
	std::lock_guard<std::mutex> lock(fLock);
	fState.hasJob = false;
}


void
LivePool::_Finish(const PortalJob& job, Builder& builder,
	const std::string& outDir)
{
	std::string log = builder.Log();
	std::vector<std::string> packages = PackagesIn(outDir);
	std::string what = job.port + " " + job.version;

	std::string failure;
	if (builder.Killed())
		failure = "the build command was killed";
	else if (builder.ExitCode() != 0) {
		char buffer[64];
		snprintf(buffer, sizeof(buffer), "the build command exited with %d",
			builder.ExitCode());
		failure = buffer;
	} else if (packages.empty())
		failure = "the build command made no packages";

	if (failure.empty()) {
		{
			std::lock_guard<std::mutex> lock(fLock);
			fState.state = kLiveUploading;
			fState.message = "Uploading " + what;
			fState.phase = kSigning;
			fState.toUpload = (int)packages.size();
		}
		for (size_t i = 0; i < packages.size(); i++) {
			PortalStatus status = fPortal.Upload(job.id, packages[i]);
			if (status != kPortalOk) {
				failure = "uploading " + packages[i].substr(
					packages[i].rfind('/') + 1) + " failed: "
					+ fPortal.LastError();
				break;
			}
			std::lock_guard<std::mutex> lock(fLock);
			fState.uploaded = (int)i + 1;
		}
	}

	_SetState(kLiveReporting, "Reporting " + what);
	{
		std::lock_guard<std::mutex> lock(fLock);
		fState.builds++;
	}

	if (failure.empty()) {
		PortalStatus status = fPortal.Complete(job.id, log);
		if (status == kPortalOk) {
			char buffer[32];
			snprintf(buffer, sizeof(buffer), "%d package%s",
				(int)packages.size(), packages.size() == 1 ? "" : "s");
			_Event(kLiveSucceeded, what + " built and published: "
				+ buffer);
			std::lock_guard<std::mutex> lock(fLock);
			fState.succeeded++;
			fState.published.push_back(job.port);
			if (fState.published.size() > 200)
				fState.published.erase(fState.published.begin());
			return;
		}
		if (status == kPortalConflict || status == kPortalForbidden) {
			_Event(kLiveProblem, what + ": the coordinator had given the job "
				"to another client (" + fPortal.LastError() + ")");
			std::lock_guard<std::mutex> lock(fLock);
			fState.failed++;
			return;
		}
		failure = "completing the job failed: " + fPortal.LastError();
	}

	PortalStatus status = fPortal.Fail(job.id, failure, log);
	_Event(kLiveFailed, what + ": " + failure
		+ (status == kPortalOk ? "" : " (and reporting it failed: "
			+ fPortal.LastError() + ")"));
	std::lock_guard<std::mutex> lock(fLock);
	fState.failed++;
}


}	// namespace awase
