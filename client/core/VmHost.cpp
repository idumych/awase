/*
 * Copyright 2026, Ian Dumych.
 * SPDX-License-Identifier: MIT
 */


#include "VmHost.h"

#include "Builder.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#if defined(__x86_64__) || defined(__i386__)
#	include <cpuid.h>
#endif


namespace awase {


static void
Brand(HostCheck& host)
{
#if defined(__x86_64__) || defined(__i386__)
	unsigned int a, b, c, d;
	char vendor[13] = {};
	if (__get_cpuid(0, &a, &b, &c, &d)) {
		memcpy(vendor, &b, 4);
		memcpy(vendor + 4, &d, 4);
		memcpy(vendor + 8, &c, 4);
	}
	char brand[49] = {};
	if (__get_cpuid(0x80000000, &a, &b, &c, &d) && a >= 0x80000004) {
		for (unsigned int leaf = 0; leaf < 3; leaf++) {
			__get_cpuid(0x80000002 + leaf, &a, &b, &c, &d);
			memcpy(brand + leaf * 16, &a, 4);
			memcpy(brand + leaf * 16 + 4, &b, 4);
			memcpy(brand + leaf * 16 + 8, &c, 4);
			memcpy(brand + leaf * 16 + 12, &d, 4);
		}
	}
	host.cpu = brand;
	while (!host.cpu.empty() && host.cpu[0] == ' ')
		host.cpu.erase(0, 1);
	// What the processor reports; firmware may still have it switched off,
	// which NVMM finds out when it starts.
	if (strcmp(vendor, "GenuineIntel") == 0) {
		if (__get_cpuid(1, &a, &b, &c, &d) && (c & (1u << 5)))
			host.virt = "VT-x";
	} else if (strcmp(vendor, "AuthenticAMD") == 0) {
		if (__get_cpuid(0x80000001, &a, &b, &c, &d) && (c & (1u << 2)))
			host.virt = "AMD-V";
	}
#endif
}


std::string
HostCheck::Refusal() const
{
	if (!x86_64)
		return "This isn't a 64-bit x86 machine, and NVMM runs only on those.";
	if (!nvmm) {
		std::string why = "The NVMM driver isn't running";
		if (virt.empty())
			why += ": the processor reports no Intel VT-x or AMD-V.";
		else
			why += ". " + virt + " may be switched off in the firmware settings.";
		return why;
	}
	return "";
}


HostCheck
CheckHost(const std::string& vmDir, const std::vector<std::string>& devices)
{
	HostCheck host;
#if defined(__x86_64__)
	host.x86_64 = true;
#else
	host.x86_64 = false;
#endif
	Brand(host);

	std::vector<std::string> paths = devices;
	if (paths.empty()) {
		paths.push_back("/dev/misc/nvmm");	// Haiku's libnvmm looks here first
		paths.push_back("/dev/nvmm");
	}
	host.nvmm = false;
	for (size_t i = 0; i < paths.size() && !host.nvmm; i++) {
		struct stat st;
		if (stat(paths[i].c_str(), &st) == 0) {
			host.nvmm = true;
			host.nvmmDevice = paths[i];
		}
	}

	long pages = sysconf(_SC_PHYS_PAGES);
	long free = sysconf(_SC_AVPHYS_PAGES);
	long size = sysconf(_SC_PAGESIZE);
	host.memoryTotal = pages > 0 && size > 0 ? (int64_t)pages * size : 0;
	host.memoryFree = free > 0 && size > 0 ? (int64_t)free * size : 0;

	// The VM folder may not exist yet: measure the nearest folder that does
	std::string dir = vmDir.empty() ? "/" : vmDir;
	struct statvfs vfs;
	host.diskFree = 0;
	for (;;) {
		if (statvfs(dir.c_str(), &vfs) == 0) {
			host.diskFree = (int64_t)vfs.f_bavail * vfs.f_frsize;
			break;
		}
		size_t slash = dir.rfind('/');
		if (dir == "/" || slash == std::string::npos)
			break;
		dir = slash == 0 ? "/" : dir.substr(0, slash);
	}

	host.qemu = FindProgram("qemu-system-i386");
	return host;
}


std::string
FindProgram(const std::string& name)
{
	std::string path = getenv("PATH") != NULL ? getenv("PATH") : "";
	path += ":/bin:/boot/system/bin:/boot/system/non-packaged/bin"
		":/boot/home/config/non-packaged/bin:/usr/bin";
	size_t start = 0;
	while (start <= path.size()) {
		size_t end = path.find(':', start);
		if (end == std::string::npos)
			end = path.size();
		std::string dir = path.substr(start, end - start);
		if (!dir.empty()) {
			std::string candidate = dir + "/" + name;
			if (access(candidate.c_str(), X_OK) == 0)
				return candidate;
		}
		start = end + 1;
	}
	return "";
}


bool
TestNvmm(const std::string& qemu, const std::string& logPath, std::string& why)
{
	// A machine that never runs (-S): QEMU sets up the accelerator first,
	// and exits with an error if NVMM can't make a VM.
	std::vector<std::string> args;
	const char* list[] = { "-accel", "nvmm", "-machine", "pc", "-m", "32",
		"-S", "-display", "none", "-nodefaults", "-monitor", "none", NULL };
	for (int i = 0; list[i] != NULL; i++)
		args.push_back(list[i]);
	Builder run;
	if (!run.Start(qemu, args, std::vector<std::pair<std::string,
			std::string> >(), logPath)) {
		why = "QEMU didn't start";
		return false;
	}
	for (int i = 0; i < 30; i++) {
		if (!run.Poll())
			break;
		struct timespec tick = { 0, 100000000 };
		nanosleep(&tick, NULL);
	}
	if (run.Poll()) {
		run.Stop();
		return true;
	}
	std::string log = run.Log();
	while (!log.empty() && (log[log.size() - 1] == '\n' || log[log.size() - 1] == ' '))
		log.erase(log.size() - 1);
	size_t line = log.rfind('\n');
	why = log.empty() ? "QEMU stopped with NVMM" : log.substr(
		line == std::string::npos ? 0 : line + 1);
	return false;
}


std::string
FormatBytes(int64_t bytes)
{
	char buffer[32];
	double gb = bytes / (1024.0 * 1024 * 1024);
	if (gb >= 10)
		snprintf(buffer, sizeof(buffer), "%.0f GB", gb);
	else if (gb >= 1)
		snprintf(buffer, sizeof(buffer), "%.1f GB", gb);
	else
		snprintf(buffer, sizeof(buffer), "%.0f MB", bytes / (1024.0 * 1024));
	return buffer;
}


}	// namespace awase
