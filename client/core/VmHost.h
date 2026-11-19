/*
 * Copyright 2026, Ian Dumych.
 * SPDX-License-Identifier: MIT
 */
#ifndef AWASE_VM_HOST_H
#define AWASE_VM_HOST_H

// What this machine offers for an x86 build VM. Awase sets one up only with
// hardware virtualization, so the VM runs on the CPU itself: NVMM on Haiku
// (from NetBSD and DragonFly BSD), KVM on Linux. On an x86 machine they run
// only x86 code, so x86 (Renku's x86_gcc2h) is the only platform offered.

#include <stdint.h>

#include <string>
#include <vector>


namespace awase {


// The platform's accelerator, as QEMU and the window name it.
struct Accelerator {
	const char*			name;			// "NVMM", "KVM"
	const char*			qemu;			// -accel's argument
	const char*			what;			// "Haiku's hardware virtualization"
};

const Accelerator&	HostAccelerator();


struct HostCheck {
	bool				x86_64;
	std::string			cpu;			// the processor's brand string
	std::string			virt;			// "VT-x", "AMD-V", or ""
	bool				accel;			// the accelerator's device works for us
	bool				accelDenied;	// it's there, but we may not use it
	std::string			accelDevice;
	int64_t				memoryTotal;	// bytes
	int64_t				memoryFree;
	int64_t				diskFree;		// on the volume the VM lives on
	std::string			qemu;			// qemu-system-i386's path, or ""

	// Why a VM can't be set up here ("" when it can).
	std::string			Refusal() const;
};


// devices: where to look for the accelerator (default /dev/misc/nvmm and
// /dev/nvmm on Haiku, /dev/kvm on Linux).
HostCheck		CheckHost(const std::string& vmDir,
					const std::vector<std::string>& devices
						= std::vector<std::string>());

// Starts QEMU with the accelerator for a moment, which fails at once when it
// can't run a VM. True if it ran; why not otherwise.
bool			TestAccel(const std::string& qemu, const std::string& logPath,
					std::string& why);

// A program's full path from PATH (plus the Haiku system bin folders), or "".
std::string		FindProgram(const std::string& name);

std::string		FormatBytes(int64_t bytes);		// "21 GB", "702 MB"


}	// namespace awase


#endif	// AWASE_VM_HOST_H
