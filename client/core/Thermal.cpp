/*
 * Copyright 2026, Ian Dumych, idumych@gmail.com.
 * SPDX-License-Identifier: MIT
 */


#include "Thermal.h"

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <string>


namespace awase {


// The whole of a small file or device, or "" if it can't be read.
static std::string
ReadSmall(const std::string& path)
{
	int fd = open(path.c_str(), O_RDONLY);
	if (fd < 0)
		return "";
	char buffer[512];
	ssize_t length = read(fd, buffer, sizeof(buffer) - 1);
	close(fd);
	if (length <= 0)
		return "";
	return std::string(buffer, length);
}


#ifdef __HAIKU__


// Each driver answers a read with lines such as
// "  Current Temperature: 47.0 C".
int
CpuTemperature()
{
	static const char* kDrivers[] = { "amd_thermal", "pch_thermal",
		"acpi_thermal" };
	int hottest = -1;
	for (size_t d = 0; d < sizeof(kDrivers) / sizeof(kDrivers[0]); d++) {
		std::string directory = std::string("/dev/power/") + kDrivers[d];
		DIR* dir = opendir(directory.c_str());
		if (dir == NULL)
			continue;
		struct dirent* entry;
		while ((entry = readdir(dir)) != NULL) {
			if (entry->d_name[0] == '.')
				continue;
			std::string text = ReadSmall(directory + "/" + entry->d_name);
			size_t at = text.find("Current Temperature:");
			if (at == std::string::npos)
				continue;
			int celsius = (int)strtod(text.c_str() + at + 20, NULL);
			// A zone that isn't wired up reads 0; nothing real is that cold.
			if (celsius > 0 && celsius > hottest)
				hottest = celsius;
		}
		closedir(dir);
	}
	return hottest;
}


#else	// Linux


static int
MilliToCelsius(const std::string& text)
{
	if (text.empty() || !isdigit((unsigned char)text[0]))
		return -1;
	return (int)(strtol(text.c_str(), NULL, 10) / 1000);
}


int
CpuTemperature()
{
	int fallback = -1;
	char path[96];
	for (int i = 0; i < 32; i++) {
		snprintf(path, sizeof(path), "/sys/class/thermal/thermal_zone%d/type",
			i);
		std::string type = ReadSmall(path);
		if (type.empty())
			continue;
		snprintf(path, sizeof(path), "/sys/class/thermal/thermal_zone%d/temp",
			i);
		int celsius = MilliToCelsius(ReadSmall(path));
		if (celsius < 0)
			continue;
		for (size_t c = 0; c < type.size(); c++)
			type[c] = tolower((unsigned char)type[c]);
		if (type.compare(0, 12, "x86_pkg_temp") == 0
			|| type.find("cpu") != std::string::npos
			|| type.find("soc") != std::string::npos)
			return celsius;
		if (fallback < 0)
			fallback = celsius;
	}
	// k10temp (AMD) reports through hwmon, not a thermal zone.
	for (int i = 0; i < 16; i++) {
		snprintf(path, sizeof(path), "/sys/class/hwmon/hwmon%d/name", i);
		std::string name = ReadSmall(path);
		while (!name.empty() && isspace((unsigned char)name[name.size() - 1]))
			name.erase(name.size() - 1);
		if (name != "k10temp" && name != "coretemp" && name != "zenpower")
			continue;
		snprintf(path, sizeof(path), "/sys/class/hwmon/hwmon%d/temp1_input", i);
		int celsius = MilliToCelsius(ReadSmall(path));
		if (celsius >= 0)
			return celsius;
	}
	return fallback;
}


#endif


}	// namespace awase
