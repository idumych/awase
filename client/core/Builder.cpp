/*
 * Copyright 2026, Ian Dumych, idumych@gmail.com.
 * SPDX-License-Identifier: MIT
 */


#include "Builder.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>


namespace awase {


Builder::Builder()
	:
	fChild(-1),
	fExitCode(-1),
	fKilled(false),
	fPaused(false),
	fLogOffset(0),
	fPhase(kFetching)
{
}


Builder::~Builder()
{
	if (fChild > 0)
		Stop();
}


bool
Builder::Start(const std::string& command, const std::vector<std::string>& args,
	const std::vector<std::pair<std::string, std::string> >& environment,
	const std::string& logPath)
{
	int log = open(logPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (log < 0)
		return false;

	pid_t child = fork();
	if (child < 0) {
		close(log);
		return false;
	}
	if (child == 0) {
		setpgid(0, 0);
		dup2(log, STDOUT_FILENO);
		dup2(log, STDERR_FILENO);
		close(log);
		int null = open("/dev/null", O_RDONLY);
		if (null >= 0) {
			dup2(null, STDIN_FILENO);
			close(null);
		}
		for (size_t i = 0; i < environment.size(); i++)
			setenv(environment[i].first.c_str(), environment[i].second.c_str(), 1);
		std::vector<char*> argv;
		argv.push_back((char*)command.c_str());
		for (size_t i = 0; i < args.size(); i++)
			argv.push_back((char*)args[i].c_str());
		argv.push_back(NULL);
		execv(command.c_str(), &argv[0]);
		fprintf(stderr, "awase: can't run %s: %s\n", command.c_str(),
			strerror(errno));
		_exit(127);
	}
	close(log);
	// Also from here, so the group exists before any signal is sent.
	setpgid(child, child);

	fChild = child;
	fExitCode = -1;
	fKilled = false;
	fPaused = false;
	fLogPath = logPath;
	fLogOffset = 0;
	fPartial.clear();
	fLastLine.clear();
	fPhase = kFetching;
	return true;
}


bool
Builder::Poll()
{
	_ReadLog();
	if (fChild <= 0)
		return false;
	int status;
	pid_t done = waitpid(fChild, &status, WNOHANG);
	if (done == 0)
		return true;
	if (done == fChild) {
		if (WIFEXITED(status))
			fExitCode = WEXITSTATUS(status);
		else {
			fExitCode = -1;
			fKilled = true;
		}
	}
	// The build's own children may still run; they belong to its group.
	kill(-fChild, SIGTERM);
	fChild = -1;
	fPaused = false;
	_ReadLog();
	return false;
}


void
Builder::Pause()
{
	if (fChild > 0 && !fPaused) {
		kill(-fChild, SIGSTOP);
		fPaused = true;
	}
}


void
Builder::Resume()
{
	if (fChild > 0 && fPaused) {
		kill(-fChild, SIGCONT);
		fPaused = false;
	}
}


void
Builder::Stop()
{
	if (fChild <= 0)
		return;
	if (fPaused)
		kill(-fChild, SIGCONT);
	kill(-fChild, SIGTERM);
	// Give it ten seconds to clean up, as a build script may want to.
	for (int i = 0; i < 100; i++) {
		int status;
		if (waitpid(fChild, &status, WNOHANG) == fChild) {
			fChild = -1;
			break;
		}
		struct timespec tick = { 0, 100 * 1000 * 1000 };
		nanosleep(&tick, NULL);
	}
	if (fChild > 0) {
		kill(-fChild, SIGKILL);
		waitpid(fChild, NULL, 0);
		fChild = -1;
	}
	fKilled = true;
	fExitCode = -1;
	fPaused = false;
}


std::string
Builder::Log() const
{
	std::string log;
	FILE* file = fopen(fLogPath.c_str(), "rb");
	if (file == NULL)
		return log;
	char buffer[65536];
	size_t read;
	while ((read = fread(buffer, 1, sizeof(buffer), file)) > 0)
		log.append(buffer, read);
	fclose(file);
	return log;
}


void
Builder::_ReadLog()
{
	if (fLogPath.empty())
		return;
	FILE* file = fopen(fLogPath.c_str(), "rb");
	if (file == NULL)
		return;
	if (fseek(file, fLogOffset, SEEK_SET) == 0) {
		char buffer[16384];
		size_t read;
		while ((read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
			fLogOffset += read;
			fPartial.append(buffer, read);
			size_t newline;
			while ((newline = fPartial.find('\n')) != std::string::npos) {
				std::string line = fPartial.substr(0, newline);
				fPartial.erase(0, newline + 1);
				// Drop terminal colours and carriage returns.
				std::string clean;
				for (size_t i = 0; i < line.size(); i++) {
					if (line[i] == '\x1b') {
						while (i < line.size() && !isalpha((unsigned char)line[i]))
							i++;
						continue;
					}
					if (line[i] != '\r')
						clean += line[i];
				}
				if (clean.find_first_not_of(" \t") == std::string::npos)
					continue;
				fLastLine = clean;
				fPhase = PhaseForLine(clean, fPhase);
			}
		}
	}
	fclose(file);
}


static bool
Contains(const std::string& haystack, const char* needle)
{
	std::string lower;
	for (size_t i = 0; i < haystack.size(); i++)
		lower += (char)tolower((unsigned char)haystack[i]);
	return lower.find(needle) != std::string::npos;
}


/*static*/ BuildPhase
Builder::PhaseForLine(const std::string& line, BuildPhase current)
{
	// shortcut: keywords from HaikuPorter's usual output, not a parse of it.
	// The phase only moves forward, so a stray word can't send it back.
	BuildPhase found = current;
	if (Contains(line, "download") || Contains(line, "fetch")
		|| Contains(line, "checksum"))
		found = kFetching;
	if (Contains(line, "unpack") || Contains(line, "patch")
		|| Contains(line, "configure") || Contains(line, "running prepare"))
		found = kConfiguring;
	if (Contains(line, "running build") || Contains(line, "compiling")
		|| Contains(line, "make[") || Contains(line, "building"))
		found = kCompiling;
	if (Contains(line, "running install") || Contains(line, "packaging")
		|| Contains(line, "creating package") || Contains(line, "package create"))
		found = kPackaging;
	return found > current ? found : current;
}


}	// namespace awase
