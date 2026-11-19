/*
 * Copyright 2026, Ian Dumych.
 * SPDX-License-Identifier: MIT
 */

// The x86 build VM's setup against stand-ins: tests/run-vm-test.sh serves a
// fake Renku release and puts fake QEMU and ssh first on PATH.
//
//   vm_test <work dir> <release server URL>

#include "VmHost.h"
#include "VmRunner.h"
#include "VmSetup.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <fstream>
#include <map>
#include <sstream>
#include <string>

using namespace awase;


static int sFailures = 0;

#define CHECK(condition) \
	do { \
		if (!(condition)) { \
			fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
				#condition); \
			sFailures++; \
		} \
	} while (false)


static std::string
Read(const std::string& path)
{
	std::ifstream file(path.c_str());
	std::stringstream text;
	text << file.rdbuf();
	return text.str();
}


static bool
ShellSyntaxOk(const std::string& script, const std::string& path)
{
	std::ofstream(path.c_str()) << script;
	return system(("sh -n '" + path + "'").c_str()) == 0;
}


static VmSetupSnapshot
RunSetup(VmSetupConfig config)
{
	VmSetup setup;
	setup.Start(config);
	VmSetupSnapshot last;
	for (int i = 0; i < 1200; i++) {
		last = setup.Snapshot();
		if (last.finished)
			break;
		struct timespec tick = { 0, 100000000 };
		nanosleep(&tick, NULL);
	}
	return last;
}


static void
HostChecks(const std::string& work)
{
	std::string device = work + "/nvmm-device";
	std::ofstream(device.c_str()) << "";
	std::vector<std::string> present(1, device);
	HostCheck host = CheckHost(work, present);
	CHECK(host.nvmm);
	CHECK(host.nvmmDevice == device);
	CHECK(host.memoryTotal > 0);
	CHECK(host.diskFree > 0);
	CHECK(host.qemu.find("qemu-system-i386") != std::string::npos);
#if defined(__x86_64__)
	CHECK(host.x86_64);
	CHECK(host.Refusal().empty());
#endif

	std::vector<std::string> absent(1, work + "/no-such-device");
	host = CheckHost(work + "/not/made/yet", absent);
	CHECK(!host.nvmm);
	CHECK(host.diskFree > 0);	// measured on the nearest folder that exists
	CHECK(host.Refusal().find("NVMM") != std::string::npos
		|| host.Refusal().find("64-bit") != std::string::npos);
	printf("host checks: %s, %s, NVMM %s\n", host.cpu.c_str(),
		host.virt.empty() ? "no VT-x/AMD-V" : host.virt.c_str(),
		host.nvmm ? "yes" : "no");
}


static void
NvmmTest(const std::string& work)
{
	std::string why;
	std::string qemu = FindProgram("qemu-system-i386");
	CHECK(TestNvmm(qemu, work + "/nvmm-ok.log", why));

	setenv("FAKE_QEMU_NVMM", "fail", 1);
	CHECK(!TestNvmm(qemu, work + "/nvmm-fail.log", why));
	CHECK(why.find("NVMM") != std::string::npos);
	unsetenv("FAKE_QEMU_NVMM");
}


static std::map<std::string, std::string> sSeeded;


static bool
FakeSeed(const std::string& image,
	const std::vector<std::pair<std::string, std::string> >& files,
	const std::string&, std::string& error)
{
	struct stat st;
	if (stat(image.c_str(), &st) != 0 || st.st_size != 3000000) {
		error = "the seed isn't a copy of the image";
		return false;
	}
	for (size_t i = 0; i < files.size(); i++)
		sSeeded[files[i].first] = files[i].second;
	return true;
}


static VmSetupConfig
Config(const std::string& dir, const std::string& server)
{
	VmSetupConfig config = VmSetup::DefaultConfig(dir);
	config.release = server + "/release.json";
	config.seed = FakeSeed;
	config.vm.sshPort = 2299;
	// BFS has no sparse files: a 20 GB disk would take 20 GB on Haiku
	config.diskGB = 1;
	return config;
}


static void
FullSetup(const std::string& work, const std::string& server)
{
	std::string dir = work + "/vm";
	VmSetupSnapshot done = RunSetup(Config(dir, server));
	CHECK(done.finished);
	CHECK(done.succeeded);
	if (!done.succeeded)
		fprintf(stderr, "setup failed: %s\n", done.error.c_str());
	for (int i = 0; i < kStepCount; i++)
		CHECK(done.steps[i].state == kStepDone);
	CHECK(done.fraction > 0.999);
	CHECK(done.image == "RenkuOS-x86_gcc2h-20261118-build00093.iso");
	CHECK(done.steps[kStepInstallQemu].detail == "already installed");
	CHECK(done.steps[kStepCheckImage].detail.find("matches") != std::string::npos);

	// What went into the image to boot once
	CHECK(sSeeded["system/settings/launch/awase-vm-setup"].find(
		"/boot/system/settings/awase/setup.sh") != std::string::npos);
	CHECK(sSeeded["system/settings/awase/setup.sh"].find("makebootable")
		!= std::string::npos);
	CHECK(sSeeded["system/settings/awase/setup.sh"].find(
		"/Renku/system/cache/package-repositories") != std::string::npos);
	std::string pub = Read(VmRunner::KeyPath(dir) + ".pub");
	CHECK(!pub.empty());
	CHECK(sSeeded["system/settings/awase/authorized_keys"] == pub);
	CHECK(sSeeded["system/settings/awase/packages/"
		"haiku_devel-r1~beta6_hrev1-1-x86_gcc2.hpkg"] == "devel\n");
	CHECK(sSeeded["system/settings/awase/packages/"
		"haiku_x86_devel-r1~beta6_hrev1-1-x86_gcc2.hpkg"] == "devel\n");
	CHECK(sSeeded.count("system/settings/awase/packages/zlib-1.3-1-x86_gcc2.hpkg")
		== 0);

	struct stat st;
	CHECK(stat((dir + "/disk.img").c_str(), &st) == 0
		&& st.st_size == (off_t)1 << 30);
	CHECK(stat((dir + "/seed.img").c_str(), &st) != 0);	// removed after use
	CHECK(stat((dir + "/ready").c_str(), &st) == 0);

	// What it asked the VM to do
	std::string ssh = Read(work + "/ssh.log");
	CHECK(ssh.find("pkgman install -y haikuporter")
		!= std::string::npos);
	CHECK(ssh.find("/boot/home/awase/guest-build.sh") != std::string::npos);
	CHECK(ssh.find("guest-build.sh zlib") != std::string::npos);

	// A second setup reuses the download
	sSeeded.clear();
	unlink((dir + "/ready").c_str());
	done = RunSetup(Config(dir, server));
	CHECK(done.succeeded);
	CHECK(done.steps[kStepDownload].detail == "already downloaded");
	CHECK(done.steps[kStepInstall].detail == "already installed");
	CHECK(Read(work + "/ssh.log").find("guest-build.sh zlib") != std::string::npos);
}


static void
DamagedImage(const std::string& work, const std::string& server)
{
	std::string dir = work + "/vm-damaged";
	mkdir(dir.c_str(), 0755);
	std::ofstream((dir + "/RenkuOS-x86_gcc2h-20261118-build00093.iso").c_str())
		<< "not the image";
	VmSetupSnapshot done = RunSetup(Config(dir, server));
	CHECK(done.finished);
	CHECK(!done.succeeded);
	CHECK(done.steps[kStepCheckImage].state == kStepFailed);
	CHECK(done.error.find("damaged") != std::string::npos);
	struct stat st;
	CHECK(stat((dir + "/RenkuOS-x86_gcc2h-20261118-build00093.iso").c_str(), &st)
		!= 0);
	CHECK(stat((dir + "/RenkuOS-x86_gcc2h-20261118-build00093-repo.tar").c_str(),
		&st) == 0);
}


static void
NoImageTonight(const std::string& work, const std::string& server)
{
	VmSetupConfig config = Config(work + "/vm-none", server);
	config.imagePattern = "RenkuOS-riscv64-";
	VmSetupSnapshot done = RunSetup(config);
	CHECK(!done.succeeded);
	CHECK(done.steps[kStepDownload].state == kStepFailed);
	CHECK(done.error.find("no x86_gcc2h image") != std::string::npos);
}


static void
Scripts(const std::string& work)
{
	CHECK(ShellSyntaxOk(GuestInstallScript(), work + "/setup.sh"));
	CHECK(ShellSyntaxOk(GuestBuildScript(), work + "/guest-build.sh"));
	CHECK(std::string(GuestInstallLaunchJob()).find("on initial_volumes_mounted")
		!= std::string::npos);

	VmConfig config;
	config.dir = work;
	config.qemu = "/bin/false";
	config.cores = 4;
	config.memoryMB = 3072;
	config.sshPort = 2290;
	VmRunner vm;
	std::string error;
	CHECK(!vm.Start(config, error));	// no disk.img
	CHECK(error.find("disk") != std::string::npos);
}


int
main(int argc, char** argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: vm_test <work dir> <release server URL>\n");
		return 2;
	}
	std::string work = argv[1], server = argv[2];
	HostChecks(work);
	NvmmTest(work);
	Scripts(work);
	FullSetup(work, server);
	DamagedImage(work, server);
	NoImageTonight(work, server);
	if (sFailures == 0)
		printf("all VM tests passed\n");
	return sFailures == 0 ? 0 : 1;
}
