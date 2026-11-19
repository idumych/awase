/*
 * Copyright 2026, Ian Dumych.
 * SPDX-License-Identifier: MIT
 */
#ifndef AWASE_VM_HOST_H
#define AWASE_VM_HOST_H

// What this machine offers for an x86 build VM. Awase sets one up only with
// NVMM, Haiku's hardware virtualization (from NetBSD and DragonFly BSD), so
// the VM runs on the CPU itself; NVMM runs only x86 code, so x86 (Renku's
// x86_gcc2h) is the only platform a VM is offered for.

#include <stdint.h>

#include <string>
#include <vector>


namespace awase {


struct HostCheck {
	bool				x86_64;
	std::string			cpu;			// the processor's brand string
	std::string			virt;			// "VT-x", "AMD-V", or ""
	bool				nvmm;			// the NVMM device is there
	std::string			nvmmDevice;
	int64_t				memoryTotal;	// bytes
	int64_t				memoryFree;
	int64_t				diskFree;		// on the volume the VM lives on
	std::string			qemu;			// qemu-system-i386's path, or ""

	// Why a VM can't be set up here ("" when it can).
	std::string			Refusal() const;
};


// devices: where to look for NVMM (default /dev/misc/nvmm, /dev/nvmm).
HostCheck		CheckHost(const std::string& vmDir,
					const std::vector<std::string>& devices
						= std::vector<std::string>());

// Starts QEMU with the NVMM accelerator for a moment, which fails at once
// when NVMM can't run a VM. True if it ran; why not otherwise.
bool			TestNvmm(const std::string& qemu, const std::string& logPath,
					std::string& why);

// A program's full path from PATH (plus the Haiku system bin folders), or "".
std::string		FindProgram(const std::string& name);

std::string		FormatBytes(int64_t bytes);		// "21 GB", "702 MB"


}	// namespace awase


#endif	// AWASE_VM_HOST_H
