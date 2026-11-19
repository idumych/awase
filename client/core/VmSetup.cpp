/*
 * Copyright 2026, Ian Dumych.
 * SPDX-License-Identifier: MIT
 */


#include "VmSetup.h"

#include "Builder.h"
#include "Download.h"
#include "Json.h"
#include "Sha256.h"
#include "VmHost.h"

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>


namespace awase {


static const char* kStepTitles[kStepCount] = {
	"Install QEMU",
	NULL,	// "Check NVMM" or "Check KVM": the constructor names it
	"Download Renku x86_gcc2h",
	"Check the image",
	"Install Renku on a 20 GB disk",
	"Test build",
};

// What each step weighs in the overall bar, roughly by how long it takes
static const double kStepWeight[kStepCount] = { 0.08, 0.02, 0.30, 0.05,
	0.35, 0.20 };


static void
Nap(double seconds)
{
	struct timespec t;
	t.tv_sec = (time_t)seconds;
	t.tv_nsec = (long)((seconds - t.tv_sec) * 1e9);
	nanosleep(&t, NULL);
}


static double
Clock()
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return now.tv_sec + now.tv_nsec / 1e9;
}


static std::vector<std::pair<std::string, std::string> >
NoEnvironment()
{
	return std::vector<std::pair<std::string, std::string> >();
}


static bool
Exists(const std::string& path)
{
	struct stat st;
	return stat(path.c_str(), &st) == 0;
}


static bool
ReadFile(const std::string& path, std::string& text)
{
	FILE* file = fopen(path.c_str(), "rb");
	if (file == NULL)
		return false;
	text.clear();
	char buffer[4096];
	size_t n;
	while ((n = fread(buffer, 1, sizeof(buffer), file)) > 0)
		text.append(buffer, n);
	fclose(file);
	return true;
}


static bool
WriteFile(const std::string& path, const std::string& text)
{
	FILE* file = fopen(path.c_str(), "wb");
	if (file == NULL)
		return false;
	bool ok = fwrite(text.data(), 1, text.size(), file) == text.size();
	return fclose(file) == 0 && ok;
}


static std::string
MakeDirs(const std::string& path)
{
	std::string done;
	size_t start = 0;
	while (start < path.size()) {
		size_t slash = path.find('/', start + 1);
		if (slash == std::string::npos)
			slash = path.size();
		done = path.substr(0, slash);
		mkdir(done.c_str(), 0755);
		start = slash;
	}
	return path;
}


// "286 of 702 MB · 7.4 MB/s"
static std::string
TransferText(int64_t done, int64_t total, double rate)
{
	char buffer[96];
	if (total > 0)
		snprintf(buffer, sizeof(buffer), "%lld of %lld MB \xc2\xb7 %.1f MB/s",
			(long long)(done >> 20), (long long)(total >> 20), rate / 1048576);
	else
		snprintf(buffer, sizeof(buffer), "%lld MB \xc2\xb7 %.1f MB/s",
			(long long)(done >> 20), rate / 1048576);
	return buffer;
}


// The last "NN%" in a line, or -1
static double
PercentIn(const std::string& line)
{
	double found = -1;
	for (size_t i = 0; i < line.size(); i++) {
		if (line[i] != '%' || i == 0 || !isdigit((unsigned char)line[i - 1]))
			continue;
		size_t start = i;
		while (start > 0 && (isdigit((unsigned char)line[start - 1])
				|| line[start - 1] == '.'))
			start--;
		found = atof(line.substr(start, i - start).c_str()) / 100.0;
	}
	return found > 1 ? 1 : found;
}


VmSetup::VmSetup()
	:
	fCancel(false),
	fStepStart(0)
{
	fState.running = false;
	fState.finished = false;
	fState.succeeded = false;
	fState.current = -1;
	fState.fraction = 0;
	fState.secondsLeft = -1;
	for (int i = 0; i < kStepCount; i++) {
		VmStep step;
		step.title = kStepTitles[i] != NULL ? kStepTitles[i]
			: std::string("Check ") + HostAccelerator().name;
		step.state = kStepWaiting;
		step.fraction = 0;
		step.seconds = 0;
		step.secondsLeft = -1;
		fState.steps.push_back(step);
	}
}


VmSetup::~VmSetup()
{
	Cancel();
}


VmSetupConfig
VmSetup::DefaultConfig(const std::string& dir)
{
	VmSetupConfig config;
	config.vm.dir = dir;
	config.vm.qemu = FindProgram("qemu-system-i386");
	config.vm.cores = 4;
	config.vm.memoryMB = 3072;
	config.vm.sshPort = 2290;
	config.diskGB = 20;
	config.release
		= "https://api.github.com/repos/RenkuOS/Source/releases/tags/nightly";
	config.imagePattern = "RenkuOS-x86_gcc2h-";
	config.repoName = "renkuos-x86_gcc2-repo.tar";
#ifdef __HAIKU__
	config.seed = WriteSeedHaiku;
#else
	config.seed = WriteSeedBfsShell;
#endif
	config.testPort = "zlib";
	return config;
}


void
VmSetup::Start(const VmSetupConfig& config)
{
	if (fThread.joinable()) {
		if (Running())
			return;
		fThread.join();
	}
	fConfig = config;
	{
		std::lock_guard<std::mutex> lock(fLock);
		fCancel = false;
		fState.running = true;
		fState.finished = false;
		fState.succeeded = false;
		fState.error.clear();
		for (int i = 0; i < kStepCount; i++) {
			fState.steps[i].state = kStepWaiting;
			fState.steps[i].detail.clear();
			fState.steps[i].fraction = 0;
			fState.steps[i].seconds = 0;
			fState.steps[i].secondsLeft = -1;
		}
	}
	fThread = std::thread(&VmSetup::_Run, this);
}


void
VmSetup::RequestCancel()
{
	std::lock_guard<std::mutex> lock(fLock);
	fCancel = true;
}


void
VmSetup::Cancel()
{
	{
		std::lock_guard<std::mutex> lock(fLock);
		fCancel = true;
	}
	if (fThread.joinable())
		fThread.join();
}


bool
VmSetup::Running()
{
	std::lock_guard<std::mutex> lock(fLock);
	return fState.running;
}


VmSetupSnapshot
VmSetup::Snapshot()
{
	std::lock_guard<std::mutex> lock(fLock);
	VmSetupSnapshot copy = fState;
	if (copy.current >= 0 && copy.steps[copy.current].state == kStepRunning)
		copy.steps[copy.current].seconds = Clock() - fStepStart;
	// The whole bar: finished steps by weight, plus the running one's part
	double done = 0, total = 0;
	for (int i = 0; i < kStepCount; i++) {
		total += kStepWeight[i];
		if (copy.steps[i].state == kStepDone)
			done += kStepWeight[i];
		else if (copy.steps[i].state == kStepRunning
				&& copy.steps[i].fraction > 0)
			done += kStepWeight[i] * copy.steps[i].fraction;
	}
	copy.fraction = done / total;
	return copy;
}


bool
VmSetup::_Cancelled()
{
	std::lock_guard<std::mutex> lock(fLock);
	return fCancel;
}


void
VmSetup::_Begin(int step)
{
	std::lock_guard<std::mutex> lock(fLock);
	fState.current = step;
	fState.steps[step].state = kStepRunning;
	fState.steps[step].fraction = -1;
	fStepStart = Clock();
}


void
VmSetup::_Progress(int step, double fraction, const std::string& detail,
	double secondsLeft)
{
	std::lock_guard<std::mutex> lock(fLock);
	fState.steps[step].fraction = fraction;
	if (!detail.empty())
		fState.steps[step].detail = detail;
	fState.steps[step].secondsLeft = secondsLeft;
	fState.secondsLeft = secondsLeft;
}


void
VmSetup::_Finish(int step, const std::string& detail)
{
	std::lock_guard<std::mutex> lock(fLock);
	fState.steps[step].state = kStepDone;
	fState.steps[step].fraction = 1;
	fState.steps[step].seconds = Clock() - fStepStart;
	fState.steps[step].secondsLeft = -1;
	if (!detail.empty())
		fState.steps[step].detail = detail;
}


bool
VmSetup::_Fail(int step, const std::string& error)
{
	std::lock_guard<std::mutex> lock(fLock);
	fState.steps[step].state = kStepFailed;
	fState.steps[step].seconds = Clock() - fStepStart;
	fState.steps[step].detail = error;
	fState.error = error;
	return false;
}


void
VmSetup::_Run()
{
	MakeDirs(fConfig.vm.dir);
	bool ok = true;
	for (int step = 0; step < kStepCount && ok; step++) {
		if (_Cancelled()) {
			ok = false;
			std::lock_guard<std::mutex> lock(fLock);
			fState.error = "cancelled";
			break;
		}
		_Begin(step);
		ok = _Step(step);
	}
	std::lock_guard<std::mutex> lock(fLock);
	fState.running = false;
	fState.finished = true;
	fState.succeeded = ok;
	fState.current = -1;
	fState.secondsLeft = -1;
	if (ok)
		WriteFile(fConfig.vm.dir + "/ready", "1\n");
}


bool
VmSetup::_Step(int step)
{
	switch (step) {
		case kStepInstallQemu: return _InstallQemu();
		case kStepCheckNvmm: return _CheckNvmm();
		case kStepDownload: return _Download();
		case kStepCheckImage: return _CheckImage();
		case kStepInstall: return _Install();
		case kStepTestBuild: return _TestBuild();
	}
	return false;
}


bool
VmSetup::_InstallQemu()
{
	if (fConfig.vm.qemu.empty())
		fConfig.vm.qemu = FindProgram("qemu-system-i386");
	if (!fConfig.vm.qemu.empty()) {
		_Finish(kStepInstallQemu, "already installed");
		return true;
	}
	std::vector<std::string> args;
	std::string describe;
	std::string program = QemuInstallCommand(args, describe);
	if (program.empty())
		return _Fail(kStepInstallQemu, describe);
	_Progress(kStepInstallQemu, -1, describe);
	Builder run;
	if (!run.Start(program, args, NoEnvironment(),
			fConfig.vm.dir + "/install-qemu-packages.log"))
		return _Fail(kStepInstallQemu, "the package manager didn't start");
	while (run.Poll()) {
		if (_Cancelled()) {
			run.Stop();
			return _Fail(kStepInstallQemu, "cancelled");
		}
		std::string line = run.LastLine();
		_Progress(kStepInstallQemu, PercentIn(line), line);
		Nap(0.2);
	}
	fConfig.vm.qemu = FindProgram("qemu-system-i386");
	if (run.ExitCode() != 0 || fConfig.vm.qemu.empty()) {
		std::string line = run.LastLine();
		return _Fail(kStepInstallQemu, "couldn't install QEMU"
			+ (line.empty() ? std::string() : ": " + line));
	}
	_Finish(kStepInstallQemu, describe);
	return true;
}


bool
VmSetup::_CheckNvmm()
{
	const Accelerator& accel = HostAccelerator();
	std::string what = std::string("a test VM started with the ") + accel.name
		+ " accelerator";
	_Progress(kStepCheckNvmm, -1, what);
	std::string why;
	if (!TestAccel(fConfig.vm.qemu, fConfig.vm.dir + "/accel-test.log", why))
		return _Fail(kStepCheckNvmm, std::string(accel.name)
			+ " can't run a VM: " + why);
	_Finish(kStepCheckNvmm, what);
	return true;
}


bool
VmSetup::_Download()
{
	_Progress(kStepDownload, -1, "finding tonight's image");
	std::string text, error;
	if (!FetchText(fConfig.release, text, error))
		return _Fail(kStepDownload, "can't reach Renku's releases: " + error);
	Json release;
	if (!Json::Parse(text, release))
		return _Fail(kStepDownload, "Renku's release list didn't make sense");
	std::string name;
	const Json& assets = release["assets"];
	for (size_t i = 0; i < assets.Size(); i++) {
		std::string asset = assets[i]["name"].String();
		std::string url = assets[i]["browser_download_url"].String();
		if (asset.compare(0, fConfig.imagePattern.size(),
				fConfig.imagePattern) != 0)
			continue;
		if (asset.size() > 4 && asset.compare(asset.size() - 4, 4, ".iso") == 0) {
			name = asset;
			fImageUrl = url;
		} else if (asset.size() > 11
				&& asset.compare(asset.size() - 11, 11, ".iso.sha256") == 0)
			fSumUrl = url;
	}
	for (size_t i = 0; i < assets.Size(); i++) {
		std::string asset = assets[i]["name"].String();
		if (asset == fConfig.repoName)
			fRepoUrl = assets[i]["browser_download_url"].String();
		else if (asset == fConfig.repoName + ".sha256")
			fRepoSumUrl = assets[i]["browser_download_url"].String();
	}
	if (name.empty() || fSumUrl.empty() || fRepoUrl.empty()
			|| fRepoSumUrl.empty())
		return _Fail(kStepDownload, "tonight's nightly has no x86_gcc2h image;"
			" try again tomorrow");
	{
		std::lock_guard<std::mutex> lock(fLock);
		fState.image = name;
		fState.steps[kStepDownload].title = "Download " + name;
	}
	fImagePath = fConfig.vm.dir + "/" + name;
	// The repository's name has no build number: keep it beside its image
	fRepoPath = fImagePath.substr(0, fImagePath.size() - 4) + "-repo.tar";
	bool had = Exists(fImagePath) && Exists(fRepoPath);
	if (!Exists(fImagePath) && !_Fetch(fImageUrl, fImagePath, name))
		return false;
	if (!Exists(fRepoPath) && !_Fetch(fRepoUrl, fRepoPath,
			"its repository, for haiku_devel"))
		return false;
	_Finish(kStepDownload, had ? "already downloaded" : name);
	return true;
}


bool
VmSetup::_Fetch(const std::string& url, const std::string& path,
	const std::string& what)
{
	double start = Clock(), last = 0;
	DownloadProgress progress = [&](int64_t done, int64_t total) {
		double now = Clock();
		if (now - last >= 0.25) {
			last = now;
			double rate = done / std::max(0.001, now - start);
			double left = total > 0 && rate > 0 ? (total - done) / rate : -1;
			_Progress(kStepDownload, total > 0 ? (double)done / total : -1,
				what + " \xc2\xb7 " + TransferText(done, total, rate), left);
		}
		return !_Cancelled();
	};
	std::string error;
	if (!DownloadFile(url, path, progress, error))
		return _Fail(kStepDownload, "the download failed: " + error);
	return true;
}


bool
VmSetup::_CheckImage()
{
	_Progress(kStepCheckImage, 0, "its SHA-256 against Renku's");
	if (!_Verify(fImagePath, fSumUrl, 0, 0.92)
			|| !_Verify(fRepoPath, fRepoSumUrl, 0.92, 1))
		return false;
	_Finish(kStepCheckImage, "its SHA-256 matches Renku's");
	return true;
}


bool
VmSetup::_Verify(const std::string& path, const std::string& sumUrl,
	double from, double to)
{
	std::string sums, error;
	if (!FetchText(sumUrl, sums, error))
		return _Fail(kStepCheckImage, "can't fetch the checksum: " + error);
	std::string expected;
	for (size_t i = 0; i < sums.size() && isxdigit((unsigned char)sums[i]); i++)
		expected += tolower((unsigned char)sums[i]);
	if (expected.size() != 64)
		return _Fail(kStepCheckImage, "Renku's checksum file is unreadable");

	FILE* file = fopen(path.c_str(), "rb");
	if (file == NULL)
		return _Fail(kStepCheckImage, "can't read " + path);
	fseek(file, 0, SEEK_END);
	int64_t total = ftell(file);
	fseek(file, 0, SEEK_SET);
	Sha256 hash;
	std::vector<char> buffer(1 << 20);
	int64_t done = 0;
	size_t n;
	while ((n = fread(&buffer[0], 1, buffer.size(), file)) > 0) {
		hash.Update(&buffer[0], n);
		done += n;
		_Progress(kStepCheckImage, from + (to - from)
			* (total > 0 ? (double)done / total : 0), "");
		if (_Cancelled()) {
			fclose(file);
			return _Fail(kStepCheckImage, "cancelled");
		}
	}
	fclose(file);
	if (hash.HexDigest() != expected) {
		remove(path.c_str());
		return _Fail(kStepCheckImage, "a download is damaged (its SHA-256"
			" doesn't match); it was deleted, so setting up again downloads"
			" it anew");
	}
	return true;
}


bool
VmSetup::_Install()
{
	const std::string dir = fConfig.vm.dir;
	std::string key = VmRunner::KeyPath(dir);
	// A setup that stopped later (the test build) carries on from there
	if (Exists(dir + "/installed") && Exists(dir + "/disk.img")
			&& Exists(key)) {
		_Finish(kStepInstall, "already installed");
		return true;
	}
	remove((dir + "/installed").c_str());

	// 1. Awase's own SSH key for the VM
	_Progress(kStepInstall, 0.01, "a key for the VM");
	if (!Exists(key)) {
		std::string keygen = FindProgram("ssh-keygen");
		std::vector<std::string> args;
		const char* list[] = { "-q", "-t", "ed25519", "-N", "", "-C",
			"awase-vm", "-f", NULL };
		for (int i = 0; list[i] != NULL; i++)
			args.push_back(list[i]);
		args.push_back(key);
		Builder run;
		if (keygen.empty() || !run.Start(keygen, args, NoEnvironment(),
				dir + "/keygen.log"))
			return _Fail(kStepInstall, "ssh-keygen isn't there");
		while (run.Poll())
			Nap(0.1);
		if (run.ExitCode() != 0 || !Exists(key + ".pub"))
			return _Fail(kStepInstall, "ssh-keygen failed");
	}
	std::string publicKey;
	ReadFile(key + ".pub", publicKey);

	// 2. A copy of the image to boot once, with the install job in it
	_Progress(kStepInstall, 0.02, "copying the image");
	std::string seed = dir + "/seed.img";
	{
		FILE* from = fopen(fImagePath.c_str(), "rb");
		FILE* to = fopen(seed.c_str(), "wb");
		if (from == NULL || to == NULL) {
			if (from != NULL)
				fclose(from);
			if (to != NULL)
				fclose(to);
			return _Fail(kStepInstall, "can't copy the image");
		}
		std::vector<char> buffer(1 << 20);
		size_t n;
		bool ok = true;
		while (ok && (n = fread(&buffer[0], 1, buffer.size(), from)) > 0)
			ok = fwrite(&buffer[0], 1, n, to) == n;
		fclose(from);
		if (fclose(to) != 0 || !ok)
			return _Fail(kStepInstall, "can't copy the image (disk full?)");
	}
	std::vector<std::pair<std::string, std::string> > files;
	files.push_back(std::make_pair(std::string(
		"system/settings/launch/awase-vm-setup"), std::string(
			GuestInstallLaunchJob())));
	files.push_back(std::make_pair(std::string(
		"system/settings/awase/setup.sh"), std::string(GuestInstallScript())));
	files.push_back(std::make_pair(std::string(
		"system/settings/awase/authorized_keys"), publicKey));
	// haiku_devel and haiku_x86_devel at the image's own version, from the
	// nightly's repository, for HaikuPorter
	std::string devel = dir + "/devel";
	MakeDirs(devel);
	std::string tar = FindProgram("tar");
	if (tar.empty())
		return _Fail(kStepInstall, "tar isn't there");
	{
		std::vector<std::string> args;
		args.push_back("xf");
		args.push_back(fRepoPath);
		args.push_back("-C");
		args.push_back(devel);
		args.push_back("--wildcards");
		args.push_back("packages/haiku_devel-*");
		args.push_back("packages/haiku_x86_devel-*");
		Builder run;
		if (!run.Start(tar, args, NoEnvironment(), dir + "/tar.log"))
			return _Fail(kStepInstall, "tar didn't start");
		while (run.Poll())
			Nap(0.1);
		if (run.ExitCode() != 0)
			return _Fail(kStepInstall, "the repository has no haiku_devel");
	}
	DIR* packages = opendir((devel + "/packages").c_str());
	int found = 0;
	if (packages != NULL) {
		struct dirent* entry;
		while ((entry = readdir(packages)) != NULL) {
			std::string leaf = entry->d_name;
			std::string content;
			if (leaf.size() < 6 || leaf.compare(leaf.size() - 5, 5, ".hpkg") != 0
					|| !ReadFile(devel + "/packages/" + leaf, content))
				continue;
			files.push_back(std::make_pair("system/settings/awase/packages/"
				+ leaf, content));
			found++;
		}
		closedir(packages);
	}
	if (found < 2)
		return _Fail(kStepInstall, "the repository lacks haiku_devel");
	std::string error;
	if (!fConfig.seed(seed, files, dir + "/seed.log", error))
		return _Fail(kStepInstall, "can't prepare the image: " + error);

	// 3. The VM's own disk, sparse
	std::string disk = dir + "/disk.img";
	remove(disk.c_str());
	int fd = open(disk.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0 || ftruncate(fd, (off_t)fConfig.diskGB << 30) != 0) {
		if (fd >= 0)
			close(fd);
		return _Fail(kStepInstall, "can't make the VM's disk");
	}
	close(fd);

	// 4. Boot the copy: it installs Renku on the disk and powers off
	_Progress(kStepInstall, 0.05, "booting the installer");
	std::string serial = dir + "/install-serial.log";
	remove(serial.c_str());
	char cpus[16], memory[16];
	snprintf(cpus, sizeof(cpus), "%d", fConfig.vm.cores);
	snprintf(memory, sizeof(memory), "%d", fConfig.vm.memoryMB);
	std::vector<std::string> args;
	const char* fixed[] = { "-accel", HostAccelerator().qemu, "-machine", "pc",
		"-cpu", "max",
		NULL };
	for (int i = 0; fixed[i] != NULL; i++)
		args.push_back(fixed[i]);
	args.push_back("-smp"); args.push_back(cpus);
	args.push_back("-m"); args.push_back(memory);
	args.push_back("-drive"); args.push_back("file=" + seed
		+ ",format=raw,if=none,id=seed");
	args.push_back("-device"); args.push_back("ide-hd,drive=seed,bus=ide.0");
	args.push_back("-drive"); args.push_back("file=" + disk
		+ ",format=raw,if=none,id=disk");
	args.push_back("-device"); args.push_back("ide-hd,drive=disk,bus=ide.1");
	args.push_back("-netdev"); args.push_back("user,id=n0");
	args.push_back("-device"); args.push_back("e1000,netdev=n0");
	args.push_back("-display"); args.push_back("none");
	args.push_back("-serial"); args.push_back("file:" + serial);
	Builder qemu;
	if (!qemu.Start(fConfig.vm.qemu, args, NoEnvironment(),
			dir + "/install-qemu.log"))
		return _Fail(kStepInstall, "QEMU didn't start");
	time_t until = time(NULL) + 45 * 60;
	std::string said;
	while (qemu.Poll()) {
		if (_Cancelled() || time(NULL) > until) {
			qemu.Stop();
			return _Fail(kStepInstall, _Cancelled() ? "cancelled"
				: "the installer took over 45 minutes");
		}
		// The guest reports "awase-setup: <percent> <what>" on its serial port
		std::string log;
		ReadFile(serial, log);
		size_t at = log.rfind("awase-setup: ");
		if (at != std::string::npos) {
			size_t end = log.find('\n', at);
			said = log.substr(at + 13, end == std::string::npos
				? std::string::npos : end - at - 13);
			int percent = atoi(said.c_str());
			size_t space = said.find(' ');
			std::string what = space == std::string::npos ? said
				: said.substr(space + 1);
			_Progress(kStepInstall, 0.05 + 0.6 * percent / 100.0, what);
		}
		Nap(0.5);
	}
	remove(seed.c_str());
	if (said.find("done") == std::string::npos)
		return _Fail(kStepInstall, "the installer stopped"
			+ (said.empty() ? std::string(" without a word")
				: ": " + said));

	// 5. Boot the installed system and add the build tools
	_Progress(kStepInstall, 0.7, "starting the VM");
	VmRunner vm;
	if (!vm.Start(fConfig.vm, error))
		return _Fail(kStepInstall, error);
	std::function<bool()> cancelled = [this]() { return _Cancelled(); };
	if (!vm.WaitForSsh(10 * 60, cancelled)) {
		vm.Shutdown(20);
		return _Fail(kStepInstall, _Cancelled() ? "cancelled"
			: "the VM didn't answer over SSH");
	}
	_Progress(kStepInstall, 0.8, "installing HaikuPorter and git");
	std::string line;
	// On the first boot the package daemon is still activating packages, and
	// pkgman's transaction can find itself out of date: try again.
	int code = -1;
	for (int attempt = 0; attempt < 4 && code != 0 && !_Cancelled(); attempt++) {
		if (attempt > 0) {
			_Progress(kStepInstall, 0.8, "installing HaikuPorter, again");
			Nap(20);
		}
		code = vm.Ssh("pkgman install -y haikuporter && which haikuporter",
			dir + "/tools.log", 30 * 60, cancelled, &line);
	}
	if (code != 0) {
		vm.Shutdown(20);
		return _Fail(kStepInstall, "installing HaikuPorter failed"
			+ (line.empty() ? std::string() : ": " + line));
	}
	vm.Shutdown(60);
	WriteFile(dir + "/installed", "1\n");
	_Finish(kStepInstall, "Renku, HaikuPorter and the build tools");
	return true;
}


bool
VmSetup::_TestBuild()
{
	const std::string dir = fConfig.vm.dir;
	_Progress(kStepTestBuild, -1, "starting the VM");
	VmRunner vm;
	std::string error;
	if (!vm.Start(fConfig.vm, error))
		return _Fail(kStepTestBuild, error);
	std::function<bool()> cancelled = [this]() { return _Cancelled(); };
	if (!vm.WaitForSsh(10 * 60, cancelled)) {
		vm.Shutdown(20);
		return _Fail(kStepTestBuild, "the VM didn't answer over SSH");
	}
	// The build script, fresh each time, so an existing VM gets this
	// version's. Ssh() passes no stdin: the script goes in the command.
	{
		std::string body = GuestBuildScript();
		std::string quoted = "'";
		for (size_t i = 0; i < body.size(); i++)
			quoted += body[i] == '\'' ? std::string("'\\''")
				: std::string(1, body[i]);
		quoted += "'";
		if (vm.Ssh("mkdir -p /boot/home/awase && printf '%s' " + quoted
				+ " > /boot/home/awase/guest-build.sh", dir + "/put.log", 60,
				cancelled) != 0) {
			vm.Shutdown(20);
			return _Fail(kStepTestBuild, "can't copy the build script into"
				" the VM");
		}
	}
	_Progress(kStepTestBuild, -1, fConfig.testPort + ", built with HaikuPorter");
	std::string line;
	std::string log = dir + "/test-build.log";
	int code = -2;
	{
		// Run it in the background so the window can show its last line
		std::thread run([&]() {
			code = vm.Ssh("sh /boot/home/awase/guest-build.sh " + fConfig.testPort
				+ " '' /boot/home/awase/out/test", log, 90 * 60, cancelled, &line);
		});
		while (code == -2) {
			std::string text;
			ReadFile(log, text);
			while (!text.empty() && (text[text.size() - 1] == '\n'))
				text.erase(text.size() - 1);
			size_t nl = text.rfind('\n');
			std::string last = text.substr(nl == std::string::npos ? 0 : nl + 1);
			if (!last.empty())
				_Progress(kStepTestBuild, -1, last.substr(0, 120));
			Nap(1);
		}
		run.join();
	}
	vm.Ssh("rm -rf /boot/home/awase/out/test", dir + "/cleanup.log", 60,
		cancelled);
	vm.Shutdown(60);
	if (code != 0)
		return _Fail(kStepTestBuild, "the test build of " + fConfig.testPort
			+ " failed; its log is " + log);
	_Finish(kStepTestBuild, fConfig.testPort + " built");
	return true;
}


double
VmSetup::_Now() const
{
	return Clock();
}


const char*
GuestInstallLaunchJob()
{
	return
		"# Written by Awase: installs Renku on the build VM's disk, once.\n"
		"job awase-vm-setup {\n"
		"\tlaunch /bin/sh /boot/system/settings/awase/setup.sh\n"
		"\ton initial_volumes_mounted\n"
		"}\n";
}


const char*
GuestInstallScript()
{
	return
		"#!/bin/sh\n"
		"# Written by Awase. Runs once, in the Renku live system Awase prepared:\n"
		"# installs Renku on the VM's second disk as Installer does, with\n"
		"# Awase's SSH key, and powers off. Progress goes to the serial port.\n"
		"exec > /boot/home/awase-setup.log 2>&1\n"
		"# /dev/dprintf goes to the kernel's debug output, which is the serial\n"
		"# port: Renku keeps that port for itself, so pc_serial0 isn't there.\n"
		"say() { echo \"awase-setup: $*\" > /dev/dprintf; echo \"$*\"; }\n"
		"fail() { say \"0 failed: $*\"; sync; shutdown; exit 1; }\n"
		"sleep 10\n"
		"D=/dev/disk/ata/1/master/raw\n"
		"[ -e $D ] || fail \"no second disk at $D\"\n"
		"say \"5 formatting the 20 GB disk\"\n"
		"mkfs -t bfs -q $D Renku || fail mkfs\n"
		"mkdir -p /Renku && mount $D /Renku || fail mount\n"
		"# packagefs shows these writable folders only if they exist on the\n"
		"# volume when it mounts: the package cache (pkgman), var, non-packaged\n"
		"mkdir -p /Renku/system/packages /Renku/system/settings/ssh"
		" /Renku/home/config/settings/ssh"
		" /Renku/system/cache/package-repositories /Renku/system/cache/tmp"
		" /Renku/system/var/log /Renku/system/var/shared_memory"
		" /Renku/system/var/empty /Renku/system/non-packaged\n"
		"n=$(ls /boot/system/packages/*.hpkg | wc -l); i=0\n"
		"for p in /boot/system/packages/*.hpkg; do\n"
		"\tcp \"$p\" /Renku/system/packages/ || fail \"copying $p\"\n"
		"\ti=$((i + 1)); say \"$((10 + 75 * i / n)) copying the system\"\n"
		"done\n"
		"say \"86 copying the settings\"\n"
		"# As Installer does: the system's settings too, /etc among them (the\n"
		"# registrar writes /etc/group); not the job that runs this script\n"
		"cp -a /boot/system/settings/. /Renku/system/settings/ || fail settings\n"
		"rm -rf /Renku/system/settings/awase"
		" /Renku/system/settings/launch/awase-vm-setup\n"
		"cp /boot/system/settings/awase/packages/*.hpkg /Renku/system/packages/"
		" || fail \"copying haiku_devel\"\n"
		"say \"88 setting up SSH\"\n"
		"cp /boot/system/settings/awase/authorized_keys"
		" /Renku/home/config/settings/ssh/authorized_keys\n"
		"chmod 600 /Renku/home/config/settings/ssh/authorized_keys\n"
		"printf 'PermitRootLogin prohibit-password\\nPubkeyAuthentication yes\\n"
		"PasswordAuthentication no\\n' > /Renku/system/settings/ssh/sshd_config\n"
		"ssh-keygen -A\n"
		"cp /boot/system/settings/ssh/ssh_host_* /Renku/system/settings/ssh/\n"
		"say \"95 making the disk bootable\"\n"
		"makebootable /Renku || fail makebootable\n"
		"sync; unmount /Renku\n"
		"say \"100 done\"\n"
		"sync\n"
		"shutdown\n";
}


bool
WriteSeedHaiku(const std::string& image,
	const std::vector<std::pair<std::string, std::string> >& files,
	const std::string& logPath, std::string& error)
{
#ifdef __HAIKU__
	// diskimage register prints the device it made: .../virtual/files/N/raw
	std::string diskimage = FindProgram("diskimage");
	if (diskimage.empty()) {
		error = "diskimage isn't there";
		return false;
	}
	std::vector<std::string> args;
	args.push_back("register");
	args.push_back(image);
	Builder run;
	if (!run.Start(diskimage, args, NoEnvironment(), logPath)) {
		error = "diskimage didn't start";
		return false;
	}
	while (run.Poll())
		Nap(0.1);
	std::string log = run.Log();
	size_t at = log.find("/dev/");
	if (run.ExitCode() != 0 || at == std::string::npos) {
		error = "diskimage register failed: " + log;
		return false;
	}
	size_t end = at;
	while (end < log.size() && !isspace((unsigned char)log[end])
			&& log[end] != '"' && log[end] != '\'')
		end++;
	std::string raw = log.substr(at, end - at);
	std::string base = raw.substr(0, raw.rfind('/'));
	std::string mount = image + ".mnt";
	mkdir(mount.c_str(), 0755);

	// The anyboot image's BFS partition, partition 0
	bool mounted = false;
	Nap(1);
	const char* parts[] = { "/0", "/0_0", NULL };
	for (int i = 0; parts[i] != NULL && !mounted; i++) {
		std::vector<std::string> margs;
		margs.push_back(base + parts[i]);
		margs.push_back(mount);
		Builder m;
		if (m.Start("/bin/mount", margs, NoEnvironment(), logPath + ".mount")) {
			while (m.Poll())
				Nap(0.1);
			mounted = m.ExitCode() == 0;
		}
	}
	bool ok = mounted;
	if (!mounted)
		error = "can't mount the image's partition";
	for (size_t i = 0; ok && i < files.size(); i++) {
		std::string path = mount + "/" + files[i].first;
		MakeDirs(path.substr(0, path.rfind('/')));
		ok = WriteFile(path, files[i].second);
		if (!ok)
			error = "can't write " + files[i].first;
		else if (path.size() > 3 && path.compare(path.size() - 3, 3, ".sh") == 0)
			chmod(path.c_str(), 0755);
	}
	if (mounted) {
		sync();
		std::vector<std::string> uargs(1, mount);
		Builder u;
		if (u.Start("/bin/unmount", uargs, NoEnvironment(), logPath + ".unmount"))
			while (u.Poll())
				Nap(0.1);
	}
	std::vector<std::string> rargs;
	rargs.push_back("unregister");
	rargs.push_back(image);
	Builder r;
	if (r.Start(diskimage, rargs, NoEnvironment(), logPath + ".unregister"))
		while (r.Poll())
			Nap(0.1);
	rmdir(mount.c_str());
	return ok;
#else
	(void)image; (void)files; (void)logPath;
	error = "preparing the image needs Haiku";
	return false;
#endif
}


std::string
QemuInstallCommand(std::vector<std::string>& argv, std::string& describe)
{
	argv.clear();
#ifdef __HAIKU__
	std::string pkgman = FindProgram("pkgman");
	describe = "qemu from HaikuPorts";
	if (pkgman.empty()) {
		describe = "pkgman isn't there to install QEMU";
		return "";
	}
	argv.push_back("install");
	argv.push_back("-y");
	argv.push_back("qemu");
	return pkgman;
#else
	// The package that has qemu-system-i386, per package manager
	struct Manager { const char* program; const char* package; const char* script; };
	static const Manager kManagers[] = {
		{ "apt-get", "qemu-system-x86", "apt-get update -q || true; "
			"DEBIAN_FRONTEND=noninteractive apt-get install -y -q qemu-system-x86" },
		{ "dnf", "qemu-system-x86-core", "dnf install -y qemu-system-x86-core" },
		{ "pacman", "qemu-system-x86", "pacman -S --noconfirm --needed qemu-system-x86" },
		{ "zypper", "qemu-x86", "zypper --non-interactive install qemu-x86" },
	};
	const Manager* manager = NULL;
	for (size_t i = 0; i < sizeof(kManagers) / sizeof(kManagers[0]); i++) {
		if (!FindProgram(kManagers[i].program).empty()) {
			manager = &kManagers[i];
			break;
		}
	}
	if (manager == NULL) {
		describe = "install QEMU (qemu-system-i386) with your package manager,"
			" then set up again";
		return "";
	}
	describe = std::string(manager->package) + " with " + manager->program;
	// Root: already, or sudo without a password (VitruvianOS's live user),
	// or pkexec, which asks for the password in a window
	std::string sh = FindProgram("sh");
	if (getuid() == 0) {
		argv.push_back("-c");
		argv.push_back(manager->script);
		return sh;
	}
	std::string sudo = FindProgram("sudo");
	if (!sudo.empty() && system("sudo -n true >/dev/null 2>&1") == 0) {
		argv.push_back("-n");
		argv.push_back(sh);
		argv.push_back("-c");
		argv.push_back(manager->script);
		return sudo;
	}
	std::string pkexec = FindProgram("pkexec");
	if (!pkexec.empty()) {
		describe += ", which asks for your password";
		argv.push_back(sh);
		argv.push_back("-c");
		argv.push_back(manager->script);
		return pkexec;
	}
	describe = std::string("install QEMU as root: ") + manager->script;
	return "";
#endif
}


std::string
FindBfsShell()
{
	const char* env = getenv("AWASE_BFS_SHELL");
	if (env != NULL && access(env, X_OK) == 0)
		return env;
	std::vector<std::string> candidates;
	char self[4096];
	ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
	if (n > 0) {
		self[n] = '\0';
		std::string dir(self);
		dir = dir.substr(0, dir.rfind('/'));
		candidates.push_back(dir + "/bfs_shell");
		candidates.push_back(dir + "/../lib/awase/bfs_shell");	// the AppImage
	}
	candidates.push_back("/usr/lib/awase/bfs_shell");			// the .deb
	for (size_t i = 0; i < candidates.size(); i++) {
		if (access(candidates[i].c_str(), X_OK) == 0)
			return candidates[i];
	}
	return FindProgram("bfs_shell");
}


// Where the image's BFS partition starts: the first MBR entry of type 0xeb,
// or the start of the image if it has no partition table.
static int64_t
BfsOffset(const std::string& image)
{
	unsigned char mbr[512];
	FILE* file = fopen(image.c_str(), "rb");
	if (file == NULL)
		return -1;
	size_t n = fread(mbr, 1, sizeof(mbr), file);
	fclose(file);
	if (n != sizeof(mbr) || mbr[510] != 0x55 || mbr[511] != 0xaa)
		return 0;
	for (int i = 0; i < 4; i++) {
		const unsigned char* entry = mbr + 446 + 16 * i;
		if (entry[4] != 0xeb)
			continue;
		uint32_t lba = entry[8] | (entry[9] << 8) | (entry[10] << 16)
			| ((uint32_t)entry[11] << 24);
		return (int64_t)lba * 512;
	}
	return 0;
}


bool
WriteSeedBfsShell(const std::string& image,
	const std::vector<std::pair<std::string, std::string> >& files,
	const std::string& logPath, std::string& error)
{
	std::string bfs = FindBfsShell();
	if (bfs.empty()) {
		error = "bfs_shell isn't there (it comes with Awase)";
		return false;
	}
	int64_t offset = BfsOffset(image);
	if (offset < 0) {
		error = "can't read the image";
		return false;
	}
	// The files to copy, and bfs_shell's commands to copy them
	std::string stage = image + ".files";
	MakeDirs(stage);
	std::string commands, listing;
	for (size_t i = 0; i < files.size(); i++) {
		char name[32];
		snprintf(name, sizeof(name), "/%zu", i);
		if (!WriteFile(stage + name, files[i].second)) {
			error = "can't stage " + files[i].first;
			return false;
		}
		std::string dest = "/myfs/" + files[i].first;
		commands += "mkdir -p " + dest.substr(0, dest.rfind('/')) + "\n";
		commands += "cp :" + stage + name + " " + dest + "\n";
		listing += "ls " + dest + "\n";
	}
	WriteFile(stage + "/commands", commands + "sync\nexit\n");
	WriteFile(stage + "/check", listing + "exit\n");

	std::string dir = bfs.substr(0, bfs.rfind('/'));
	char start[32];
	snprintf(start, sizeof(start), "%lld", (long long)offset);
	std::vector<std::pair<std::string, std::string> > environment;
	environment.push_back(std::make_pair(std::string("LD_LIBRARY_PATH"), dir));
	for (int pass = 0; pass < 2; pass++) {
		std::vector<std::string> args;
		args.push_back("-c");
		args.push_back("exec \"$1\" --start-offset \"$2\" \"$3\" < \"$4\"");
		args.push_back("awase-bfs");
		args.push_back(bfs);
		args.push_back(start);
		args.push_back(image);
		args.push_back(stage + (pass == 0 ? "/commands" : "/check"));
		std::string log = logPath + (pass == 0 ? "" : ".check");
		Builder run;
		if (!run.Start(FindProgram("sh"), args, environment, log)) {
			error = "bfs_shell didn't start";
			return false;
		}
		while (run.Poll())
			Nap(0.1);
		std::string text;
		ReadFile(log, text);
		if (run.ExitCode() != 0 || text.find("Error") != std::string::npos
				|| text.find("rror:") != std::string::npos) {
			error = "bfs_shell couldn't write the image";
			return false;
		}
		if (pass == 1) {
			for (size_t i = 0; i < files.size(); i++) {
				std::string leaf = files[i].first.substr(
					files[i].first.rfind('/') + 1);
				if (text.find(leaf) == std::string::npos) {
					error = files[i].first + " isn't in the image";
					return false;
				}
			}
		}
	}
	for (size_t i = 0; i < files.size(); i++) {
		char name[32];
		snprintf(name, sizeof(name), "/%zu", i);
		remove((stage + name).c_str());
	}
	remove((stage + "/commands").c_str());
	remove((stage + "/check").c_str());
	rmdir(stage.c_str());
	return true;
}


}	// namespace awase
