/*
 * Copyright 2026, Ian Dumych.
 * SPDX-License-Identifier: MIT
 */
#ifndef AWASE_VM_SETUP_H
#define AWASE_VM_SETUP_H

// Sets up the x86 build VM, on a thread, in six steps the window shows with
// progress: install QEMU, check the accelerator (NVMM on Haiku, KVM on
// Linux), download Renku x86_gcc2h, check the
// image, install Renku on a 20 GB disk (with HaikuPorter and Awase's SSH
// key), and a test build.
//
// The install works as Haiku's Installer does, unattended: Awase copies the
// image and adds a one-time launch job to the copy (mounting it on Haiku,
// with Haiku's bfs_shell tool on Linux, which can't write BFS). Booted, the
// job formats the VM's second disk, copies the system packages to it, makes
// it bootable, adds the SSH key and powers off. From then on the VM boots
// from that disk and Awase drives it over SSH.

#include <stdint.h>

#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "VmRunner.h"


namespace awase {


enum VmStepState {
	kStepWaiting,
	kStepRunning,
	kStepDone,
	kStepFailed,
};

enum VmStepId {
	kStepInstallQemu,
	kStepCheckNvmm,
	kStepDownload,
	kStepCheckImage,
	kStepInstall,
	kStepTestBuild,
	kStepCount
};

struct VmStep {
	std::string			title;
	std::string			detail;
	VmStepState			state;
	double				fraction;		// 0..1, or -1 when it can't tell
	double				seconds;		// how long it took, or has taken
	double				secondsLeft;	// -1 when unknown
};

struct VmSetupSnapshot {
	bool				running;
	bool				finished;
	bool				succeeded;
	int					current;		// the running step, or -1
	std::vector<VmStep>	steps;
	double				fraction;		// of the whole setup
	double				secondsLeft;	// -1 when unknown
	std::string			logLine;
	std::string			error;
	std::string			image;			// the Renku image's file name
};

// Writes files into a disk image's first partition. files: (path in the
// volume, content, executable).
typedef std::function<bool(const std::string& image,
	const std::vector<std::pair<std::string, std::string> >& files,
	const std::string& logPath, std::string& error)> SeedWriter;

struct VmSetupConfig {
	VmConfig			vm;
	int					diskGB;
	std::string			release;		// GitHub API URL of Renku's nightly
	std::string			imagePattern;	// prefix of the image's file name
	// The nightly's own repository: HaikuPorter needs haiku_devel at exactly
	// the image's version, which Renku's online repository may not have yet
	std::string			repoName;
	SeedWriter			seed;			// default: mount it (Haiku)
	std::string			testPort;
};


class VmSetup {
public:
								VmSetup();
								~VmSetup();

	static	VmSetupConfig		DefaultConfig(const std::string& dir);

			void				Start(const VmSetupConfig& config);
			void				Cancel();			// and waits for the thread
			void				RequestCancel();	// returns at once
			bool				Running();
			VmSetupSnapshot		Snapshot();

private:
			void				_Run();
			bool				_Step(int step);
			bool				_InstallQemu();
			bool				_CheckNvmm();
			bool				_Download();
			bool				_CheckImage();
			bool				_Install();
			bool				_TestBuild();

			void				_Begin(int step);
			void				_Progress(int step, double fraction,
									const std::string& detail,
									double secondsLeft = -1);
			void				_Finish(int step, const std::string& detail);
			bool				_Fail(int step, const std::string& error);
			bool				_Cancelled();
			double				_Now() const;

			VmSetupConfig		fConfig;
			std::thread			fThread;
			std::mutex			fLock;
			bool				fCancel;
			VmSetupSnapshot		fState;
			double				fStepStart;
			bool				_Fetch(const std::string& url,
									const std::string& path,
									const std::string& what);
			bool				_Verify(const std::string& path,
									const std::string& sumUrl,
									double from, double to);

			std::string			fImagePath;
			std::string			fImageUrl;
			std::string			fSumUrl;
			std::string			fRepoPath;
			std::string			fRepoUrl;
			std::string			fRepoSumUrl;
};


// The guest side of the unattended install, and its launch job.
const char*		GuestInstallScript();
const char*		GuestInstallLaunchJob();

// Mounts a disk image's first partition on Haiku and writes the files.
bool			WriteSeedHaiku(const std::string& image,
					const std::vector<std::pair<std::string, std::string> >&
						files,
					const std::string& logPath, std::string& error);
// The same with bfs_shell, for Linux (found next to Awase, in
// ../lib/awase, /usr/lib/awase, on PATH, or at $AWASE_BFS_SHELL).
bool			WriteSeedBfsShell(const std::string& image,
					const std::vector<std::pair<std::string, std::string> >&
						files,
					const std::string& logPath, std::string& error);
std::string		FindBfsShell();
// How this system installs QEMU ("" when it can't): the command, and what
// to call it in the window ("qemu-system-x86 with apt").
std::string		QemuInstallCommand(std::vector<std::string>& argv,
					std::string& describe);


}	// namespace awase


#endif	// AWASE_VM_SETUP_H
