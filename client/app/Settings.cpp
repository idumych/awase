/*
 * Copyright 2026, Ian Dumych.
 * SPDX-License-Identifier: MIT
 */


#include "Settings.h"

#include <File.h>
#include <FindDirectory.h>
#include <Message.h>
#include <OS.h>
#include <Path.h>

#include <stdlib.h>
#include <sys/stat.h>


static status_t
SettingsPath(BPath& path)
{
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status != B_OK)
		return status;
	return path.Append("Awase");
}


static std::string
FindString(const BMessage& message, const char* name, const std::string& fallback)
{
	const char* value;
	return message.FindString(name, &value) == B_OK ? value : fallback;
}


Settings::Settings()
	:
	frame(60, 50, 60 + 1100, 50 + 690),
	mini(false),
	miniAt(60, 50),
	useCoordinator(false),
	vmEnabled(false),
	vmCores(4),
	vmMemoryMB(3072),
	builds(0),
	succeeded(0),
	failed(0),
	buildSeconds(0)
{
	system_info info;
	int cores = get_system_info(&info) == B_OK ? (int)info.cpu_count : 4;
	limits.maxCores = cores > 0 ? cores : 1;
	limits.cores = limits.maxCores > 1 ? limits.maxCores / 2 : 1;
	limits.idleOnly = false;
	limits.maxTemperature = 85;
	limits.paused = false;

	coordinator.pollSeconds = 60;
	BPath cache;
	if (find_directory(B_USER_CACHE_DIRECTORY, &cache) == B_OK
		&& cache.Append("Awase") == B_OK)
		coordinator.workDir = cache.Path();
	else
		coordinator.workDir = "/boot/home/awase-work";
}


bool
Settings::CoordinatorReady() const
{
	return !coordinator.server.empty() && !coordinator.clientId.empty()
		&& !coordinator.secret.empty() && !coordinator.buildCommand.empty();
}


std::string
Settings::VmDir() const
{
	// The non-packaged data folder on Renku and Haiku; VitruvianOS has none
	// (find_directory fails), so its user data folder, ~/config/data
	BPath path;
	if (find_directory(B_USER_NONPACKAGED_DATA_DIRECTORY, &path) == B_OK
		&& path.Append("Awase/vm") == B_OK)
		return path.Path();
	if (find_directory(B_USER_DATA_DIRECTORY, &path) == B_OK
		&& path.Append("Awase/vm") == B_OK)
		return path.Path();
	const char* home = getenv("HOME");
	return std::string(home != NULL ? home : "/tmp")
		+ "/.local/share/Awase/vm";
}


bool
Settings::VmReady() const
{
	struct stat st;
	return stat((VmDir() + "/ready").c_str(), &st) == 0
		&& stat((VmDir() + "/disk.img").c_str(), &st) == 0;
}


status_t
Settings::Load()
{
	BPath path;
	status_t status = SettingsPath(path);
	if (status != B_OK)
		return status;
	BFile file(path.Path(), B_READ_ONLY);
	BMessage message;
	status = message.Unflatten(&file);
	if (status != B_OK)
		return status;

	BRect savedFrame;
	if (message.FindRect("frame", &savedFrame) == B_OK && savedFrame.IsValid())
		frame = savedFrame;
	BPoint point;
	if (message.FindPoint("mini at", &point) == B_OK)
		miniAt = point;
	int32 value;
	bool flag;
	double number;
	if (message.FindInt32("cores", &value) == B_OK && value >= 1)
		limits.cores = value <= limits.maxCores ? value : limits.maxCores;
	if (message.FindInt32("temperature", &value) == B_OK)
		limits.maxTemperature = value;
	if (message.FindBool("idle", &flag) == B_OK)
		limits.idleOnly = flag;
	if (message.FindBool("paused", &flag) == B_OK)
		limits.paused = flag;
	if (message.FindBool("mini", &flag) == B_OK)
		mini = flag;

	if (message.FindBool("use coordinator", &flag) == B_OK)
		useCoordinator = flag;
	coordinator.server = FindString(message, "server", coordinator.server);
	coordinator.clientId = FindString(message, "client", coordinator.clientId);
	coordinator.secret = FindString(message, "secret", coordinator.secret);
	coordinator.buildCommand = FindString(message, "build command",
		coordinator.buildCommand);
	coordinator.workDir = FindString(message, "work folder",
		coordinator.workDir);
	if (message.FindInt32("poll seconds", &value) == B_OK && value >= 5)
		coordinator.pollSeconds = value;

	if (message.FindBool("vm", &flag) == B_OK)
		vmEnabled = flag;
	vmClientId = FindString(message, "vm client", vmClientId);
	vmSecret = FindString(message, "vm secret", vmSecret);
	if (message.FindInt32("vm cores", &value) == B_OK && value >= 1)
		vmCores = value;
	if (message.FindInt32("vm memory", &value) == B_OK && value >= 512)
		vmMemoryMB = value;

	if (message.FindInt32("builds", &value) == B_OK)
		builds = value;
	if (message.FindInt32("succeeded", &value) == B_OK)
		succeeded = value;
	if (message.FindInt32("failed", &value) == B_OK)
		failed = value;
	if (message.FindDouble("build seconds", &number) == B_OK)
		buildSeconds = number;
	return B_OK;
}


status_t
Settings::Save() const
{
	BPath path;
	status_t status = SettingsPath(path);
	if (status != B_OK)
		return status;
	BMessage message('awst');
	message.AddRect("frame", frame);
	message.AddBool("mini", mini);
	message.AddPoint("mini at", miniAt);
	message.AddInt32("cores", limits.cores);
	message.AddInt32("temperature", limits.maxTemperature);
	message.AddBool("idle", limits.idleOnly);
	message.AddBool("paused", limits.paused);
	message.AddBool("use coordinator", useCoordinator);
	message.AddString("server", coordinator.server.c_str());
	message.AddString("client", coordinator.clientId.c_str());
	message.AddString("secret", coordinator.secret.c_str());
	message.AddString("build command", coordinator.buildCommand.c_str());
	message.AddString("work folder", coordinator.workDir.c_str());
	message.AddInt32("poll seconds", coordinator.pollSeconds);
	message.AddBool("vm", vmEnabled);
	message.AddString("vm client", vmClientId.c_str());
	message.AddString("vm secret", vmSecret.c_str());
	message.AddInt32("vm cores", vmCores);
	message.AddInt32("vm memory", vmMemoryMB);
	message.AddInt32("builds", builds);
	message.AddInt32("succeeded", succeeded);
	message.AddInt32("failed", failed);
	message.AddDouble("build seconds", buildSeconds);

	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status = file.InitCheck();
	if (status != B_OK)
		return status;
	// The secret is in here: only the owner may read it.
	file.SetPermissions(S_IRUSR | S_IWUSR);
	return message.Flatten(&file);
}
