/*
 * Copyright 2026, Ian Dumych.
 * SPDX-License-Identifier: MIT
 */


#include "VmService.h"


namespace awase {


VmService::VmService()
	:
	fStop(false),
	fState(kVmOff)
{
}


VmService::~VmService()
{
	if (fStopper.joinable())
		fStopper.join();
	Stop();
}


void
VmService::StopAsync()
{
	if (fStopper.joinable())
		fStopper.join();
	{
		std::lock_guard<std::mutex> lock(fLock);
		fStop = true;
	}
	fStopper = std::thread([this]() { Stop(); });
}


void
VmService::Start(const VmConfig& vm, const LiveConfig& live)
{
	if (fStopper.joinable())
		fStopper.join();
	if (Active())
		return;
	if (fBooter.joinable())
		fBooter.join();
	{
		std::lock_guard<std::mutex> lock(fLock);
		fStop = false;
	}
	_Set(kVmStarting, "starting the VM");
	fBooter = std::thread(&VmService::_Boot, this, vm, live);
}


void
VmService::_Boot(VmConfig vm, LiveConfig live)
{
	std::string error;
	if (!fRunner.Start(vm, error)) {
		_Set(kVmFailed, error);
		return;
	}
	std::function<bool()> stopping = [this]() {
		std::lock_guard<std::mutex> lock(fLock);
		return fStop;
	};
	if (!fRunner.WaitForSsh(10 * 60, stopping)) {
		bool stopped = stopping();
		fRunner.Shutdown(20);
		_Set(stopped ? kVmOff : kVmFailed, stopped ? ""
			: "the VM didn't answer over SSH");
		return;
	}
	std::string script;
	if (!fRunner.WriteBuildScript(script, error)) {
		fRunner.Shutdown(20);
		_Set(kVmFailed, error);
		return;
	}
	live.buildCommand = script;
	if (stopping()) {
		fRunner.Shutdown(30);
		_Set(kVmOff, "");
		return;
	}
	fLive.Start(live);
	_Set(kVmRunning, "running");
}


void
VmService::Stop()
{
	{
		std::lock_guard<std::mutex> lock(fLock);
		fStop = true;
	}
	if (fBooter.joinable())
		fBooter.join();
	fLive.Stop();
	fRunner.Shutdown(30);
	_Set(kVmOff, "");
}


bool
VmService::Active()
{
	std::lock_guard<std::mutex> lock(fLock);
	return fState == kVmStarting || fState == kVmRunning;
}


void
VmService::_Set(VmServiceState state, const std::string& message)
{
	std::lock_guard<std::mutex> lock(fLock);
	fState = state;
	fMessage = message;
}


VmServiceState
VmService::State()
{
	std::lock_guard<std::mutex> lock(fLock);
	return fState;
}


std::string
VmService::Message()
{
	std::lock_guard<std::mutex> lock(fLock);
	return fMessage;
}


bool
VmService::Frozen()
{
	return fRunner.Frozen();
}


void
VmService::SetLimits(const Limits& limits)
{
	fLive.SetLimits(limits);
}


void
VmService::SetIdle(bool idle)
{
	fLive.SetIdle(idle);
}


void
VmService::SetTotals(int builds, int succeeded, int failed,
	double buildSeconds)
{
	fLive.SetTotals(builds, succeeded, failed, buildSeconds);
}


void
VmService::Update()
{
	if (State() != kVmRunning)
		return;
	if (!fRunner.Running()) {
		fLive.Stop();
		_Set(kVmFailed, "the VM stopped");
		return;
	}
	LiveSnapshot live = fLive.Snapshot();
	bool hold = live.state == kLivePaused || live.state == kLiveWaitingIdle
		|| live.tooHot;
	fRunner.Freeze(hold);
}


LiveSnapshot
VmService::Snapshot()
{
	return fLive.Snapshot();
}


}	// namespace awase
