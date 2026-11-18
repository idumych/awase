/*
 * Copyright 2026, Ian Dumych.
 * SPDX-License-Identifier: MIT
 */

// awase-tui: the Awase build client for terminals, for Linux servers and
// VPSs. The same core as the Renku app (the coordinator protocol, the build
// runner, the live loop), in a tiled ncurses layout:
//
//   awase-tui [options]          the full-screen client
//   awase-tui --plain [options]  one line per event, for systemd and logs
//   awase-tui --demo             the simulated demo pool
//
// Options (each also a key in ~/.config/awase/awase.conf, as key = value):
//   --server URL          the coordinator
//   --client-id ID        this machine's build client
//   --secret-file PATH    a file holding the client's secret (or $AWASE_SECRET)
//   --build-command PATH  run as <command> <port> <version> <outdir>
//   --work-dir PATH       per-job folders (default ~/.cache/awase)
//   --cores N             cores to lend (default: half of them)
//   --poll SECONDS        how often to ask for work when idle (default 60)
//   --theme NAME          kiln (default), ember or tide
//   --config PATH         another config file

#include "DemoPool.h"
#include "LivePool.h"

#include <ctype.h>
#include <locale.h>
#include <ncurses.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace awase;


// #pragma mark - Configuration


struct Options {
	LiveConfig	live;
	std::string	theme;
	int			cores;
	bool		plain;
	bool		demo;
	std::string	error;
};


static std::string
Trim(const std::string& text)
{
	size_t a = text.find_first_not_of(" \t\r\n");
	size_t b = text.find_last_not_of(" \t\r\n");
	return a == std::string::npos ? "" : text.substr(a, b - a + 1);
}


static std::string
Home()
{
	const char* home = getenv("HOME");
	return home != NULL ? home : ".";
}


static std::string
Expand(const std::string& path)
{
	return path.compare(0, 2, "~/") == 0 ? Home() + path.substr(1) : path;
}


static bool
ReadSecretFile(const std::string& path, std::string& secret,
	std::string& error)
{
	struct stat st;
	std::string file = Expand(path);
	if (stat(file.c_str(), &st) != 0) {
		error = "can't read the secret file " + file;
		return false;
	}
	if ((st.st_mode & 077) != 0) {
		error = "the secret file " + file + " is readable by others; "
			"chmod 600 it";
		return false;
	}
	std::ifstream in(file.c_str());
	std::getline(in, secret);
	secret = Trim(secret);
	return true;
}


static void
Apply(Options& options, const std::string& key, const std::string& value,
	std::string& secretFile)
{
	if (key == "server")
		options.live.server = value;
	else if (key == "client-id" || key == "client_id")
		options.live.clientId = value;
	else if (key == "secret-file" || key == "secret_file")
		secretFile = value;
	else if (key == "build-command" || key == "build_command")
		options.live.buildCommand = Expand(value);
	else if (key == "work-dir" || key == "work_dir")
		options.live.workDir = Expand(value);
	else if (key == "cores")
		options.cores = atoi(value.c_str());
	else if (key == "poll" || key == "poll_seconds")
		options.live.pollSeconds = atoi(value.c_str());
	else if (key == "theme")
		options.theme = value;
	else
		options.error = "unknown setting '" + key + "'";
}


static Options
ParseOptions(int argc, char** argv)
{
	Options options;
	options.cores = 0;
	options.plain = false;
	options.demo = false;
	options.live.pollSeconds = 60;
	options.live.workDir = Home() + "/.cache/awase";
	std::string secretFile;

	std::string config = Home() + "/.config/awase/awase.conf";
	for (int i = 1; i + 1 < argc; i++) {
		if (strcmp(argv[i], "--config") == 0)
			config = argv[i + 1];
	}
	std::ifstream in(config.c_str());
	std::string line;
	while (std::getline(in, line)) {
		line = Trim(line);
		if (line.empty() || line[0] == '#')
			continue;
		size_t equals = line.find('=');
		if (equals == std::string::npos)
			continue;
		Apply(options, Trim(line.substr(0, equals)),
			Trim(line.substr(equals + 1)), secretFile);
	}

	for (int i = 1; i < argc; i++) {
		std::string arg = argv[i];
		if (arg == "--plain")
			options.plain = true;
		else if (arg == "--demo")
			options.demo = true;
		else if (arg == "--help" || arg == "-h") {
			options.error = "help";
		} else if (arg.compare(0, 2, "--") == 0 && i + 1 < argc) {
			if (arg != "--config")
				Apply(options, arg.substr(2), argv[i + 1], secretFile);
			i++;
		} else
			options.error = "don't understand '" + arg + "'";
	}

	const char* secret = getenv("AWASE_SECRET");
	if (secret != NULL)
		options.live.secret = secret;
	else if (!secretFile.empty() && options.error.empty())
		ReadSecretFile(secretFile, options.live.secret, options.error);

	if (options.error.empty() && !options.demo) {
		if (options.live.server.empty())
			options.error = "no coordinator: give --server";
		else if (options.live.clientId.empty())
			options.error = "no build client: give --client-id";
		else if (options.live.secret.empty())
			options.error = "no secret: give --secret-file or set AWASE_SECRET";
		else if (options.live.buildCommand.empty())
			options.error = "no build command: give --build-command";
	}
	return options;
}


// #pragma mark - What the screen shows, from either pool


struct RepoItem {
	std::string	name;
	int			state;		// 2 sealed, 1 one build in, 0 waiting, 3 building
	bool		mine;
};


struct EventLine {
	double		age;
	int			kind;		// 0 info, 1 good, 2 problem, 3 started
	std::string	text;
};


struct Screen {
	std::string	status;
	int			statusKind;		// 0 idle, 1 building, 2 paused, 3 trouble
	std::string	message;
	std::string	server;
	std::string	platform;
	bool		hasJob;
	std::string	port;
	std::string	version;
	std::string	jobId;
	double		progress;		// < 0 when nobody knows
	int			phase;			// 0..4
	double		elapsed;
	std::string	logPath;
	std::string	lastLine;
	std::vector<RepoItem> repo;
	std::string	repoTitle;
	std::vector<EventLine> events;
	int			builds;
	int			good;
	int			bad;
	double		hours;
	std::vector<double> history;
	bool		demo;
};


static Screen
FromLive(const LiveSnapshot& live, const LiveConfig& config)
{
	Screen s;
	s.demo = false;
	s.server = config.server;
	size_t scheme = s.server.find("://");
	if (scheme != std::string::npos)
		s.server.erase(0, scheme + 3);
	s.platform = live.platform;
	s.message = live.message;
	switch (live.state) {
		case kLiveBuilding: s.status = "building"; s.statusKind = 1; break;
		case kLiveUploading: s.status = "uploading"; s.statusKind = 1; break;
		case kLiveReporting: s.status = "reporting"; s.statusKind = 1; break;
		case kLivePaused: s.status = "paused"; s.statusKind = 2; break;
		case kLiveWaitingIdle: s.status = "waiting for idle"; s.statusKind = 2; break;
		case kLiveIdle: s.status = "waiting for work"; s.statusKind = 0; break;
		case kLiveConnecting: s.status = "connecting"; s.statusKind = 0; break;
		case kLiveError: s.status = "trouble"; s.statusKind = 3; break;
		default: s.status = "stopped"; s.statusKind = 3; break;
	}
	s.hasJob = live.hasJob;
	if (live.hasJob) {
		s.port = live.job.port;
		s.version = live.job.version;
		char id[32];
		snprintf(id, sizeof(id), "%lld", (long long)live.job.id);
		s.jobId = id;
		s.logPath = config.workDir + "/job-" + id + "/build.log";
	}
	bool uploading = live.state == kLiveUploading
		|| live.state == kLiveReporting;
	s.progress = uploading && live.toUpload > 0
		? (double)live.uploaded / live.toUpload : -1;
	s.phase = uploading ? 4 : std::min((int)live.phase, 3);
	s.elapsed = live.elapsed;
	s.lastLine = live.lastLine;
	for (size_t i = 0; i < live.recent.size(); i++) {
		RepoItem item;
		item.name = live.recent[i].portName.empty() ? live.recent[i].fileName
			: live.recent[i].portName;
		item.state = 2;
		item.mine = std::find(live.published.begin(), live.published.end(),
			item.name) != live.published.end();
		s.repo.push_back(item);
	}
	char title[64];
	snprintf(title, sizeof(title), "%d published", (int)live.recent.size());
	s.repoTitle = title;
	for (int i = (int)live.events.size() - 1; i >= 0; i--) {
		EventLine line;
		line.age = live.now - live.events[i].time;
		line.text = live.events[i].text;
		switch (live.events[i].kind) {
			case kLiveSucceeded: line.kind = 1; break;
			case kLiveFailed: case kLiveProblem: line.kind = 2; break;
			case kLiveStarted: line.kind = 3; break;
			default: line.kind = 0; break;
		}
		s.events.push_back(line);
	}
	s.builds = live.builds;
	s.good = live.succeeded;
	s.bad = live.failed;
	s.hours = live.buildSeconds / 3600.0;
	s.history = live.history;
	return s;
}


static Screen
FromDemo(const DemoPool& demo)
{
	const Pool& pool = demo.GetPool();
	Screen s;
	s.demo = true;
	s.server = "demo pool";
	s.platform = "x86_64";
	s.hasJob = pool.YourPackage() >= 0;
	if (demo.GetLimits().paused) {
		s.status = "paused";
		s.statusKind = 2;
	} else if (s.hasJob) {
		s.status = "building";
		s.statusKind = 1;
	} else {
		s.status = "waiting for a match";
		s.statusKind = 0;
	}
	if (s.hasJob) {
		const Package& package = pool.Packages()[pool.YourPackage()];
		s.port = package.name;
		s.version = package.version;
		s.jobId = "demo";
		s.progress = pool.YourProgress();
		s.phase = std::min((int)pool.YourPhase(), 4);
		s.elapsed = pool.YourProgress() * package.cost * DemoPool::kTimeScale;
		s.message = "Building " + package.name;
	}
	const std::vector<Package>& packages = pool.Packages();
	int sealed = 0;
	for (size_t i = 0; i < packages.size(); i++) {
		RepoItem item;
		item.name = packages[i].name;
		item.mine = false;
		for (size_t c = 0; c < packages[i].copies.size(); c++) {
			if (packages[i].copies[c].volunteer == pool.You())
				item.mine = true;
		}
		for (size_t r = 0; r < packages[i].results.size(); r++) {
			if (packages[i].results[r].volunteer == pool.You())
				item.mine = true;
		}
		item.state = packages[i].state == kSealed ? 2
			: !packages[i].copies.empty() ? 3
			: !packages[i].results.empty() ? 1 : 0;
		if (item.state == 2)
			sealed++;
		s.repo.push_back(item);
	}
	char title[64];
	snprintf(title, sizeof(title), "round %d · %d of %d sealed", demo.Round(),
		sealed, (int)packages.size());
	s.repoTitle = title;
	const std::vector<Event>& events = pool.Events();
	for (int i = (int)events.size() - 1; i >= 0; i--) {
		EventLine line;
		line.age = (pool.Now() - events[i].time) * DemoPool::kTimeScale;
		line.text = events[i].text;
		line.kind = events[i].kind == kEventSealed ? 1
			: events[i].kind == kEventMismatch ? 2
			: events[i].kind == kEventYouStarted ? 3 : 0;
		s.events.push_back(line);
	}
	const Stats& stats = pool.YourStats();
	s.builds = stats.builds;
	s.good = stats.accepted;
	s.bad = stats.mismatched;
	s.hours = stats.buildSeconds / 3600.0;
	s.history = stats.history;
	return s;
}


// #pragma mark - The machine: CPU and temperature from /proc and /sys


struct CpuSample {
	std::vector<unsigned long long> busy, total;
};


static CpuSample
ReadCpu()
{
	CpuSample sample;
	std::ifstream in("/proc/stat");
	std::string line;
	while (std::getline(in, line)) {
		if (line.compare(0, 3, "cpu") != 0 || !isdigit((unsigned char)line[3]))
			continue;
		std::istringstream fields(line.substr(line.find(' ')));
		unsigned long long v[8] = { 0 }, total = 0;
		for (int i = 0; i < 8 && fields >> v[i]; i++)
			total += v[i];
		sample.busy.push_back(total - v[3] - v[4]);	// minus idle and iowait
		sample.total.push_back(total);
	}
	return sample;
}


// The CPU's temperature if the machine says which sensor that is (many
// VPSs have none at all; then nothing is shown).
static int
ReadTemperature()
{
	int fallback = -1;
	for (int i = 0; i < 32; i++) {
		char path[96];
		snprintf(path, sizeof(path), "/sys/class/thermal/thermal_zone%d/type", i);
		std::ifstream typeFile(path);
		std::string type;
		if (!(typeFile >> type))
			continue;
		snprintf(path, sizeof(path), "/sys/class/thermal/thermal_zone%d/temp", i);
		std::ifstream tempFile(path);
		long milli;
		if (!(tempFile >> milli))
			continue;
		std::string lower = type;
		for (size_t c = 0; c < lower.size(); c++)
			lower[c] = tolower((unsigned char)lower[c]);
		if (lower == "x86_pkg_temp" || lower.find("cpu") != std::string::npos
			|| lower.find("soc") != std::string::npos)
			return (int)(milli / 1000);
		if (fallback < 0)
			fallback = (int)(milli / 1000);
	}
	// k10temp (AMD) reports through hwmon, not a thermal zone.
	for (int i = 0; i < 16; i++) {
		char path[96];
		snprintf(path, sizeof(path), "/sys/class/hwmon/hwmon%d/name", i);
		std::ifstream nameFile(path);
		std::string name;
		if (!(nameFile >> name))
			continue;
		if (name != "k10temp" && name != "coretemp" && name != "zenpower")
			continue;
		snprintf(path, sizeof(path), "/sys/class/hwmon/hwmon%d/temp1_input", i);
		std::ifstream tempFile(path);
		long milli;
		if (tempFile >> milli)
			return (int)(milli / 1000);
	}
	return fallback;
}


static std::vector<std::string>
TailOf(const std::string& path, size_t lines)
{
	std::vector<std::string> out;
	FILE* file = fopen(path.c_str(), "rb");
	if (file == NULL)
		return out;
	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	long start = std::max(0L, size - 16384L);
	fseek(file, start, SEEK_SET);
	std::string data(size - start, '\0');
	size_t got = fread(&data[0], 1, data.size(), file);
	fclose(file);
	data.resize(got);
	std::string line;
	for (size_t i = 0; i < data.size(); i++) {
		char c = data[i];
		if (c == '\x1b') {		// drop terminal colours
			while (i < data.size() && !isalpha((unsigned char)data[i]))
				i++;
			continue;
		}
		if (c == '\n') {
			out.push_back(line);
			line.clear();
		} else if (c != '\r')
			line += c;
	}
	if (!line.empty())
		out.push_back(line);
	if (start > 0 && !out.empty())
		out.erase(out.begin());	// probably cut in half
	if (out.size() > lines)
		out.erase(out.begin(), out.end() - lines);
	return out;
}


// #pragma mark - Drawing


enum {
	kFrame = 1, kText, kMuted, kAccent, kSeal, kGold, kStrong, kBar, kDim,
	kKey, kFill
};

struct Theme {
	const char*	name;
	short		background, frame, text, muted, accent, seal, gold, strong,
				panel, dim;
};

// 256-colour palettes: kiln (ink and ember), ember (warmer), tide (blue).
static const Theme kThemes[] = {
	{ "kiln", 233, 238, 223, 244, 208, 160, 179, 230, 234, 237 },
	{ "ember", 52, 94, 223, 138, 214, 160, 221, 230, 52, 88 },
	{ "tide", 17, 24, 153, 67, 81, 160, 179, 195, 17, 24 },
};
static int sTheme = 0;


static void
UseTheme(int index)
{
	sTheme = index;
	const Theme& t = kThemes[index];
	bool rich = COLORS >= 256;
	short bg = rich ? t.panel : COLOR_BLACK;
	init_pair(kFrame, rich ? t.frame : COLOR_WHITE, bg);
	init_pair(kText, rich ? t.text : COLOR_WHITE, bg);
	init_pair(kMuted, rich ? t.muted : COLOR_WHITE, bg);
	init_pair(kAccent, rich ? t.accent : COLOR_YELLOW, bg);
	init_pair(kSeal, rich ? 230 : COLOR_WHITE, rich ? t.seal : COLOR_RED);
	init_pair(kGold, rich ? t.gold : COLOR_YELLOW, bg);
	init_pair(kStrong, rich ? t.strong : COLOR_WHITE, bg);
	init_pair(kBar, rich ? t.accent : COLOR_YELLOW, bg);
	init_pair(kDim, rich ? t.dim : COLOR_BLUE, bg);
	init_pair(kKey, rich ? t.strong : COLOR_WHITE, rich ? t.dim : COLOR_BLUE);
	init_pair(kFill, rich ? t.accent : COLOR_YELLOW, rich ? t.dim : COLOR_BLACK);
	bkgd(COLOR_PAIR(kText));
}


static void
Put(int y, int x, int width, int pair, const std::string& text,
	bool bold = false)
{
	if (width <= 0 || y < 0 || y >= LINES)
		return;
	attron(COLOR_PAIR(pair) | (bold ? A_BOLD : 0));
	// Count columns, not bytes: the text may hold UTF-8.
	std::string clipped;
	int columns = 0;
	for (size_t i = 0; i < text.size() && columns < width;) {
		unsigned char c = text[i];
		size_t length = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
		int cells = (length == 3 && (unsigned char)text[i] >= 0xe3) ? 2 : 1;
		if (columns + cells > width)
			break;
		clipped += text.substr(i, length);
		columns += cells;
		i += length;
	}
	mvaddstr(y, x, clipped.c_str());
	attroff(COLOR_PAIR(pair) | A_BOLD);
}


static void
Box(int y, int x, int h, int w, const std::string& title,
	const std::string& subtitle)
{
	attron(COLOR_PAIR(kFrame));
	mvaddstr(y, x, "┌");
	mvaddstr(y, x + w - 1, "┐");
	mvaddstr(y + h - 1, x, "└");
	mvaddstr(y + h - 1, x + w - 1, "┘");
	for (int i = 1; i < w - 1; i++) {
		mvaddstr(y, x + i, "─");
		mvaddstr(y + h - 1, x + i, "─");
	}
	for (int i = 1; i < h - 1; i++) {
		mvaddstr(y + i, x, "│");
		mvaddstr(y + i, x + w - 1, "│");
	}
	attroff(COLOR_PAIR(kFrame));
	Put(y, x + 2, 1, kFrame, " ");
	Put(y, x + 3, w - 8, kAccent, title, true);
	int after = x + 3 + (int)title.size();
	Put(y, after, 1, kFrame, " ");
	if (!subtitle.empty()) {
		std::string text = subtitle + " ";
		Put(y, after + 1, x + w - after - 3, kMuted, text);
	}
	for (int row = 1; row < h - 1; row++)
		Put(y + row, x + 1, w - 2, kText, std::string(w - 2, ' '));
}


static std::string
Clock(double seconds)
{
	char buffer[32];
	int s = (int)seconds;
	if (s < 3600)
		snprintf(buffer, sizeof(buffer), "%d:%02d", s / 60, s % 60);
	else
		snprintf(buffer, sizeof(buffer), "%dh%02d", s / 3600, s / 60 % 60);
	return buffer;
}


static std::string
Ago(double seconds)
{
	char buffer[24];
	if (seconds < 60)
		snprintf(buffer, sizeof(buffer), "now");
	else if (seconds < 3600)
		snprintf(buffer, sizeof(buffer), "%dm", (int)(seconds / 60));
	else
		snprintf(buffer, sizeof(buffer), "%dh", (int)(seconds / 3600));
	return buffer;
}


static void
DrawRepo(int y, int x, int h, int w, const Screen& s)
{
	Box(y, x, h, w, s.demo ? "pool" : "repo", s.repoTitle);
	int row = y + 1;
	for (size_t i = 0; i < s.repo.size() && row < y + h - 3; i++, row++) {
		const RepoItem& item = s.repo[i];
		if (item.state == 2) {
			Put(row, x + 2, 4, kSeal, " 合 ", true);
		} else if (item.state == 3)
			Put(row, x + 2, 4, kAccent, "[▸ ]");
		else if (item.state == 1)
			Put(row, x + 2, 4, kGold, "[·1]");
		else
			Put(row, x + 2, 4, kDim, "[  ]");
		Put(row, x + 7, w - 9, item.mine ? kAccent : (item.state == 0
			? kMuted : kText), item.name, item.mine);
	}
	if (s.repo.empty())
		Put(y + 2, x + 2, w - 4, kMuted, "nothing published yet");
	Put(y + h - 2, x + 2, w - 4, kMuted,
		s.demo ? "合 sealed · ·1 one build in" : "orange: built here");
}


static void
DrawNow(int y, int x, int h, int w, const Screen& s, double clock)
{
	Box(y, x, h, w, "now", s.hasJob ? "job " + s.jobId : s.status);
	if (!s.hasJob) {
		Put(y + 2, x + 2, w - 4, kStrong, s.statusKind == 2 ? "Taking a break"
			: s.statusKind == 3 ? "Can't build right now" : "Nothing to build",
			true);
		Put(y + 3, x + 2, w - 4, kMuted, s.message);
		return;
	}
	Put(y + 1, x + 2, w - 4, kStrong, s.port, true);
	int after = x + 3 + (int)s.port.size();
	Put(y + 1, after, w - (after - x) - 14, kMuted, s.version + " · "
		+ s.platform);
	std::string elapsed = Clock(s.elapsed) + " elapsed";
	Put(y + 1, x + w - 2 - (int)elapsed.size(), (int)elapsed.size(), kText,
		elapsed);

	// The bar: real progress when known, otherwise a light that sweeps.
	int width = w - 18;
	std::string bar;
	if (s.progress >= 0) {
		int filled = (int)(width * s.progress + 0.5);
		for (int i = 0; i < width; i++)
			bar += i < filled ? "█" : " ";
	} else {
		int head = (int)(clock * 18) % (width + 8) - 4;
		for (int i = 0; i < width; i++) {
			int d = head - i;
			bar += d >= 0 && d < 1 ? "█" : d >= 1 && d < 3 ? "▓"
				: d >= 3 && d < 6 ? "▒" : "░";
		}
	}
	Put(y + 3, x + 2, width, s.progress >= 0 ? kFill : kAccent, bar);
	static const char* kSteps[] = { "fetch", "configure", "compile", "package",
		"upload" };
	char label[32];
	if (s.progress >= 0)
		snprintf(label, sizeof(label), "%s %d%%", kSteps[s.phase],
			(int)(s.progress * 100));
	else
		snprintf(label, sizeof(label), "%s", kSteps[s.phase]);
	Put(y + 3, x + 3 + width, 14, kText, label);
	int cx = x + 2;
	for (int i = 0; i < 5; i++) {
		std::string step = kSteps[i];
		Put(y + 5, cx, (int)step.size(), i == s.phase ? kAccent
			: i < s.phase ? kText : kMuted, step, i == s.phase);
		cx += (int)step.size();
		if (i < 4) {
			Put(y + 5, cx, 3, kDim, " ▸ ");
			cx += 3;
		}
	}
}


static void
DrawLog(int y, int x, int h, int w, const Screen& s)
{
	Box(y, x, h, w, "log", s.hasJob && !s.demo ? "build.log · follow" : "");
	std::vector<std::string> lines;
	if (!s.logPath.empty())
		lines = TailOf(s.logPath, h - 2);
	if (lines.empty()) {
		// No build log: show what happened lately instead.
		for (size_t i = 0; i < s.events.size() && (int)i < h - 2; i++)
			lines.insert(lines.begin(), s.events[i].text);
	}
	int row = y + 1;
	for (size_t i = 0; i < lines.size() && row < y + h - 1; i++, row++) {
		std::string lower = lines[i];
		for (size_t c = 0; c < lower.size(); c++)
			lower[c] = tolower((unsigned char)lower[c]);
		int pair = lower.find("error") != std::string::npos ? kSeal
			: lower.find("warning") != std::string::npos ? kGold
			: i + 1 == lines.size() ? kText : kMuted;
		Put(row, x + 2, w - 4, pair, lines[i]);
	}
}


static void
DrawCores(int y, int x, int h, int w, const std::vector<double>& usage,
	int lent)
{
	char subtitle[48];
	snprintf(subtitle, sizeof(subtitle), "%d of %d lent", lent,
		(int)usage.size());
	Box(y, x, h, w, "cores", subtitle);
	static const char* kLevels[] = { " ", "▁", "▂", "▃", "▄", "▅", "▆", "▇",
		"█" };
	int rows = h - 3;
	int count = (int)usage.size();
	int step = count * 2 <= w - 4 ? 2 : 1;	// a gap between bars if they fit
	if (count * step > w - 4)
		count = (w - 4) / step;
	double total = 0;
	for (size_t c = 0; c < usage.size(); c++)
		total += usage[c];
	for (int c = 0; c < count; c++) {
		double level = usage[c] * rows * 8;
		int pair = usage[c] > 0.75 ? kAccent : usage[c] > 0.3 ? kGold : kDim;
		for (int r = 0; r < rows; r++) {
			int part = std::max(0, std::min(8, (int)(level - (rows - 1 - r) * 8)));
			if (part > 0)
				Put(y + 1 + r, x + 2 + c * step, 1, pair, kLevels[part]);
			else if (r == rows - 1)
				Put(y + 1 + r, x + 2 + c * step, 1, kDim, "\xe2\x96\x81");
		}
	}
	char line[64];
	snprintf(line, sizeof(line), "busy %d%%", usage.empty() ? 0
		: (int)(100 * total / usage.size()));
	Put(y + h - 2, x + 2, w - 4, kMuted, line);
	if ((int)usage.size() > count) {
		snprintf(line, sizeof(line), "%d shown", count);
		Put(y + h - 2, x + w - 2 - (int)strlen(line), (int)strlen(line),
			kMuted, line);
	}
}


static void
DrawSeals(int y, int x, int h, int w, const Screen& s)
{
	Box(y, x, h, w, "seals", s.demo ? "demo" : "this machine");
	char line[96];
	snprintf(line, sizeof(line), "%d build%s · %d good · %d bad", s.builds,
		s.builds == 1 ? "" : "s", s.good, s.bad);
	Put(y + 1, x + 2, w - 4, kText, line);
	snprintf(line, sizeof(line), "%.1f build-hours", s.hours);
	Put(y + 2, x + 2, w - 4, kMuted, line);
	// Build time as a row of shades, oldest first.
	static const int kShade[] = { kDim, kDim, kGold, kAccent, kAccent };
	static const char* kGlyph[] = { "·", "▪", "▪", "■", "█" };
	double peak = 1;
	for (size_t i = 0; i < s.history.size(); i++)
		peak = std::max(peak, s.history[i]);
	int slots = std::min(24, (w - 4) / 1);
	int offset = slots - std::min(slots, (int)s.history.size());
	for (int i = 0; i < slots; i++) {
		int index = i - offset;
		int level = index < 0 ? 0 : (int)(s.history[index] / peak * 4 + 0.5);
		Put(y + 4, x + 2 + i, 1, kShade[level], kGlyph[level]);
	}
	Put(y + 5, x + 2, w - 4, kMuted, "build time, 5 min each");
}


static void
DrawEvents(int y, int x, int h, int w, const Screen& s)
{
	Box(y, x, h, w, "events", "");
	int row = y + 1;
	for (size_t i = 0; i < s.events.size() && row < y + h - 1; i++, row++) {
		const EventLine& e = s.events[i];
		const char* mark = e.kind == 1 ? "合" : e.kind == 2 ? "!" : e.kind == 3
			? "▸" : "·";
		if (e.kind == 1)
			Put(row, x + 2, 2, kSeal, mark, true);
		else
			Put(row, x + 2, 2, e.kind == 2 ? kGold : kAccent, mark);
		std::string ago = Ago(e.age);
		Put(row, x + 5, w - 8 - (int)ago.size(), e.kind == 2 ? kGold : kText,
			e.text);
		Put(row, x + w - 2 - (int)ago.size(), (int)ago.size(), kMuted, ago);
	}
}


static void
DrawStatus(const Screen& s, const Limits& limits, int temperature)
{
	Put(0, 0, COLS, kText, std::string(COLS, ' '));
	Put(0, 1, 10, kSeal, " 合 awase ", true);
	int x = 12;
	std::string state = "● " + s.status;
	if (s.hasJob && s.statusKind == 1)
		state += " " + s.port;
	int pair = s.statusKind == 1 ? kAccent : s.statusKind == 2 ? kGold
		: s.statusKind == 3 ? kSeal : kMuted;
	Put(0, x, (int)state.size(), pair, state);
	x += (int)state.size() + 2;
	Put(0, x, 10, kMuted, s.platform);
	x += (int)s.platform.size() + 2;
	Put(0, x, 1, kDim, "│");
	x += 2;
	Put(0, x, 6, kText, "cores ");
	x += 6;
	for (int i = 0; COLS >= 100 && i < limits.maxCores && i < 16; i++, x++)
		Put(0, x, 1, i < limits.cores ? kAccent : kDim, "▮");
	char text[64];
	snprintf(text, sizeof(text), " %d/%d", limits.cores, limits.maxCores);
	Put(0, x, 8, kText, text);
	x += (int)strlen(text) + 2;
	if (temperature > 0 && x + 10 < COLS) {
		Put(0, x, 1, kDim, "│");
		snprintf(text, sizeof(text), "%d°C", temperature);
		Put(0, x + 2, 8, temperature >= limits.maxTemperature ? kGold : kText,
			text);
		x += 10;
	}
	time_t now = time(NULL);
	char clock[16];
	strftime(clock, sizeof(clock), "%H:%M", localtime(&now));
	std::string right = s.server + "  " + clock;
	if ((int)right.size() + x < COLS)
		Put(0, COLS - 1 - (int)right.size(), (int)right.size(), kMuted, right);
}


static void
DrawKeys(int y)
{
	Put(y, 0, COLS, kText, std::string(COLS, ' '));
	const char* keys[][2] = { { "p", "pause" }, { "+ -", "cores" },
		{ "t", "theme" }, { "q", "quit" } };
	int x = 1;
	for (int i = 0; i < 4; i++) {
		std::string key = std::string(" ") + keys[i][0] + " ";
		Put(y, x, (int)key.size(), kKey, key, true);
		x += (int)key.size();
		std::string what = std::string(" ") + keys[i][1] + "  ";
		Put(y, x, (int)what.size(), kMuted, what);
		x += (int)what.size();
	}
	int tx = COLS - 1;
	for (int i = 2; i >= 0; i--) {
		std::string name = kThemes[i].name;
		tx -= (int)name.size();
		Put(y, tx, (int)name.size(), i == sTheme ? kAccent : kMuted, name,
			i == sTheme);
		if (i > 0) {
			tx -= 3;
			Put(y, tx, 3, kDim, " · ");
		}
	}
	Put(y, tx - 7, 6, kMuted, "theme ");
}


static void
Draw(const Screen& s, const Limits& limits, const std::vector<double>& cpu,
	int temperature, double clock)
{
	erase();
	DrawStatus(s, limits, temperature);
	int top = 1, bottom = LINES - 2;
	int height = bottom - top;
	if (COLS >= 100 && LINES >= 24) {
		int left = std::max(26, COLS * 27 / 100);
		int right = std::max(28, COLS * 25 / 100);
		int middle = COLS - left - right;
		DrawRepo(top, 0, height, left, s);
		DrawNow(top, left, 8, middle, s, clock);
		DrawLog(top + 8, left, height - 8, middle, s);
		int coresHeight = 9;
		DrawCores(top, left + middle, coresHeight, right, cpu, limits.cores);
		DrawSeals(top + coresHeight, left + middle, 7, right, s);
		DrawEvents(top + coresHeight + 7, left + middle,
			height - coresHeight - 7, right, s);
	} else {
		// Narrow terminals: one column.
		DrawNow(top, 0, 8, COLS, s, clock);
		int rest = height - 8;
		DrawLog(top + 8, 0, rest / 2, COLS, s);
		DrawEvents(top + 8 + rest / 2, 0, rest - rest / 2, COLS, s);
	}
	DrawKeys(LINES - 1);
	refresh();
}


// #pragma mark - Running


static volatile sig_atomic_t sQuit = 0;


static void
OnSignal(int)
{
	sQuit = 1;
}


static double
Seconds()
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return now.tv_sec + now.tv_nsec / 1e9;
}


static void
Usage(FILE* out)
{
	fprintf(out,
		"usage: awase-tui [--plain] [--server URL --client-id ID "
		"--secret-file PATH\n"
		"                 --build-command PATH] [--work-dir PATH] [--cores N] "
		"[--poll S]\n"
		"                 [--theme kiln|ember|tide]\n"
		"       awase-tui --demo\n"
		"Settings can also go in ~/.config/awase/awase.conf as key = value.\n"
		"The secret can come from $AWASE_SECRET instead of a file.\n");
}


static int
RunPlain(LivePool& live, const LiveConfig& config)
{
	size_t printed = 0;
	std::string lastState;
	while (!sQuit) {
		LiveSnapshot snapshot = live.Snapshot();
		for (; printed < snapshot.events.size(); printed++) {
			time_t now = time(NULL);
			char stamp[32];
			strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
			printf("%s %s\n", stamp, snapshot.events[printed].text.c_str());
		}
		if (snapshot.message != lastState && snapshot.state == kLiveError) {
			printf("%s\n", snapshot.message.c_str());
			lastState = snapshot.message;
		}
		fflush(stdout);
		usleep(250000);
	}
	printf("stopping%s\n", live.Snapshot().hasJob
		? " (the running build is reported as stopped)" : "");
	live.Stop();
	(void)config;
	return 0;
}


int
main(int argc, char** argv)
{
	Options options = ParseOptions(argc, argv);
	if (options.error == "help") {
		Usage(stdout);
		return 0;
	}
	if (!options.error.empty()) {
		fprintf(stderr, "awase-tui: %s\n", options.error.c_str());
		Usage(stderr);
		return 2;
	}

	long cpus = sysconf(_SC_NPROCESSORS_ONLN);
	Limits limits;
	limits.maxCores = cpus > 0 ? (int)cpus : 1;
	limits.cores = options.cores > 0 ? std::min(options.cores, limits.maxCores)
		: std::max(1, limits.maxCores / 2);
	limits.idleOnly = false;		// a server is never at its keyboard
	limits.maxTemperature = 85;
	limits.paused = false;

	signal(SIGINT, OnSignal);
	signal(SIGTERM, OnSignal);
	signal(SIGHUP, OnSignal);

	DemoPool demo;
	LivePool live;
	if (options.demo) {
		demo.SetLimits(limits);
		limits = demo.GetLimits();
	} else {
		live.SetLimits(limits);
		live.Start(options.live);
		if (options.plain)
			return RunPlain(live, options.live);
	}

	setlocale(LC_ALL, "");
	initscr();
	cbreak();
	noecho();
	curs_set(0);
	keypad(stdscr, TRUE);
	nodelay(stdscr, TRUE);
	if (has_colors()) {
		start_color();
		int theme = 0;
		for (int i = 0; i < 3; i++) {
			if (options.theme == kThemes[i].name)
				theme = i;
		}
		UseTheme(theme);
	}

	CpuSample previous = ReadCpu();
	std::vector<double> usage(previous.busy.size(), 0);
	double lastCpu = Seconds(), lastStep = Seconds(), start = Seconds();
	int temperature = ReadTemperature();
	bool confirmQuit = false;

	while (!sQuit) {
		int key = getch();
		if (key == 'q' || key == 'Q') {
			bool building = !options.demo && live.Snapshot().hasJob;
			if (!building || confirmQuit)
				break;
			confirmQuit = true;
		} else if (key != ERR)
			confirmQuit = false;
		if (key == 'p' || key == ' ')
			limits.paused = !limits.paused;
		else if ((key == '+' || key == '=') && limits.cores < limits.maxCores)
			limits.cores++;
		else if (key == '-' && limits.cores > 1)
			limits.cores--;
		else if (key == 't' && has_colors())
			UseTheme((sTheme + 1) % 3);
		if (key != ERR) {
			if (options.demo)
				demo.SetLimits(limits);
			else
				live.SetLimits(limits);
		}

		double now = Seconds();
		if (now - lastCpu >= 1.0) {
			CpuSample sample = ReadCpu();
			for (size_t i = 0; i < sample.busy.size() && i < previous.busy.size();
				i++) {
				double total = (double)(sample.total[i] - previous.total[i]);
				usage[i] = total > 0
					? (sample.busy[i] - previous.busy[i]) / total : 0;
			}
			previous = sample;
			lastCpu = now;
			temperature = ReadTemperature();
		}

		Screen screen;
		if (options.demo) {
			demo.Step(now - lastStep);
			screen = FromDemo(demo);
		} else
			screen = FromLive(live.Snapshot(), options.live);
		lastStep = now;
		if (confirmQuit)
			screen.message = "a build is running: press q again to stop it "
				"and report it to the coordinator";
		Draw(screen, limits, usage, temperature, now - start);
		if (confirmQuit) {
			std::string text = " Press q again to stop the build and quit ";
			Put(LINES / 2, (COLS - (int)text.size()) / 2, (int)text.size(),
				kKey, text, true);
			refresh();
		}
		usleep(100000);
	}

	endwin();
	if (!options.demo) {
		if (live.Snapshot().hasJob)
			fprintf(stderr, "awase-tui: stopping the build and reporting it\n");
		live.Stop();
	}
	return 0;
}
