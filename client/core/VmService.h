/*
 * Copyright 2026, Ian Dumych.
 * SPDX-License-Identifier: MIT
 */
#ifndef AWASE_VM_SERVICE_H
#define AWASE_VM_SERVICE_H

// Building in the x86 VM: boots it, waits until it answers, then runs a
// second coordinator client (the x86 build client) whose build command
// builds in the VM. Pauses the whole VM whenever building should pause.

#include <mutex>
#include <string>
#include <thread>

#include "LivePool.h"
#include "VmRunner.h"


namespace awase {


enum VmServiceState {
	kVmOff,
	kVmStarting,
	kVmRunning,
	kVmFailed,
};


class VmService {
public:
								VmService();
								~VmService();

			void				Start(const VmConfig& vm,
									const LiveConfig& live);
			// Gives a build in progress back and powers the VM off.
			void				Stop();
			// The same on a thread, so a window needn't wait for it.
			void				StopAsync();
			bool				Active();

			VmServiceState		State();
			std::string			Message();
			bool				Frozen();

			void				SetLimits(const Limits& limits);
			void				SetIdle(bool idle);
			void				SetTotals(int builds, int succeeded,
									int failed, double buildSeconds);
			// Freezes or thaws the VM to match the client; call it often.
			void				Update();
			LiveSnapshot		Snapshot();

private:
			void				_Boot(VmConfig vm, LiveConfig live);
			void				_Set(VmServiceState state,
									const std::string& message);

			VmRunner			fRunner;
			LivePool			fLive;
			std::thread			fBooter;
			std::thread			fStopper;
			std::mutex			fLock;
			bool				fStop;
			VmServiceState		fState;
			std::string			fMessage;
};


}	// namespace awase


#endif	// AWASE_VM_SERVICE_H
