/*
 * Copyright 2026, Ian Dumych.
 * SPDX-License-Identifier: MIT
 */
#ifndef AWASE_VM_RUNNER_H
#define AWASE_VM_RUNNER_H

// The x86 build VM: Renku x86_gcc2h on its own 20 GB disk (disk.img in the
// VM folder), run by QEMU with the NVMM accelerator. Awase reaches it over
// SSH on a local port, with a key only it has, and pauses it with QEMU's
// monitor when building must pause.

#include <functional>
#include <string>

#include "Builder.h"


namespace awase {


struct VmConfig {
	std::string			dir;			// disk.img, id_ed25519, logs
	std::string			qemu;			// qemu-system-i386
	int					cores;
	int					memoryMB;
	int					sshPort;
};


class VmRunner {
public:
								VmRunner();
								~VmRunner();

			// Boots the installed VM (disk.img).
			bool				Start(const VmConfig& config,
									std::string& error);
			bool				Running();
			// Waits until the VM answers over SSH. cancelled() is asked
			// between tries.
			bool				WaitForSsh(int seconds,
									const std::function<bool()>& cancelled);
			// Runs a shell command in the VM; its output goes to logPath.
			// Returns the command's exit code, or -1 if SSH failed.
			int					Ssh(const std::string& command,
									const std::string& logPath,
									int timeoutSeconds,
									const std::function<bool()>& cancelled,
									std::string* lastLine = NULL);
			// Pauses or resumes the whole VM.
			void				Freeze(bool frozen);
			bool				Frozen() const { return fFrozen; }
			// Asks the VM to power off; stops QEMU after graceSeconds.
			void				Shutdown(int graceSeconds);

			// The host side of a build: <script> <port> <version> <outdir>
			// runs the build in the VM and copies the packages back.
			bool				WriteBuildScript(std::string& path,
									std::string& error);
			std::string			SshCommand() const;

	static	std::string			KeyPath(const std::string& dir);
	static	std::string			MonitorPath(const std::string& dir);
	static	bool				Monitor(const std::string& dir,
									const std::string& command);

private:
			VmConfig			fConfig;
			Builder				fQemu;
			bool				fStarted;
			bool				fFrozen;
};


// The script that builds a port inside the VM with HaikuPorter.
const char*		GuestBuildScript();


}	// namespace awase


#endif	// AWASE_VM_RUNNER_H
