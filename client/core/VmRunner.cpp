/*
 * Copyright 2026, Ian Dumych.
 * SPDX-License-Identifier: MIT
 */


#include "VmRunner.h"

#include "VmHost.h"

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <vector>


namespace awase {


static void
Nap(double seconds)
{
	struct timespec t;
	t.tv_sec = (time_t)seconds;
	t.tv_nsec = (long)((seconds - t.tv_sec) * 1e9);
	nanosleep(&t, NULL);
}


static std::vector<std::pair<std::string, std::string> >
NoEnvironment()
{
	return std::vector<std::pair<std::string, std::string> >();
}


VmRunner::VmRunner()
	:
	fStarted(false),
	fFrozen(false)
{
}


VmRunner::~VmRunner()
{
	if (fStarted)
		Shutdown(30);
}


std::string
VmRunner::KeyPath(const std::string& dir)
{
	return dir + "/id_ed25519";
}


std::string
VmRunner::MonitorPath(const std::string& dir)
{
	return dir + "/monitor.sock";
}


bool
VmRunner::Monitor(const std::string& dir, const std::string& command)
{
	std::string path = MonitorPath(dir);
	struct sockaddr_un address;
	if (path.size() >= sizeof(address.sun_path))
		return false;
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		return false;
	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	strcpy(address.sun_path, path.c_str());
	bool ok = connect(fd, (struct sockaddr*)&address, sizeof(address)) == 0;
	if (ok) {
		std::string line = command + "\n";
		ok = write(fd, line.c_str(), line.size()) == (ssize_t)line.size();
		Nap(0.3);
	}
	close(fd);
	return ok;
}


bool
VmRunner::Start(const VmConfig& config, std::string& error)
{
	fConfig = config;
	std::string disk = config.dir + "/disk.img";
	struct stat st;
	if (stat(disk.c_str(), &st) != 0) {
		error = "the VM's disk isn't there; set it up again";
		return false;
	}
	unlink(MonitorPath(config.dir).c_str());
	char cpus[16], memory[16], net[96];
	snprintf(cpus, sizeof(cpus), "%d", config.cores);
	snprintf(memory, sizeof(memory), "%d", config.memoryMB);
	snprintf(net, sizeof(net), "user,id=n0,hostfwd=tcp:127.0.0.1:%d-:22",
		config.sshPort);
	std::vector<std::string> args;
	const char* fixed[] = { "-accel", HostAccelerator().qemu, "-machine", "pc",
		"-cpu", "max",
		NULL };
	for (int i = 0; fixed[i] != NULL; i++)
		args.push_back(fixed[i]);
	args.push_back("-smp"); args.push_back(cpus);
	args.push_back("-m"); args.push_back(memory);
	args.push_back("-drive");
	args.push_back("file=" + disk + ",format=raw,if=none,id=disk");
	args.push_back("-device"); args.push_back("ide-hd,drive=disk,bus=ide.0");
	args.push_back("-netdev"); args.push_back(net);
	args.push_back("-device"); args.push_back("e1000,netdev=n0");
	args.push_back("-display"); args.push_back("none");
	args.push_back("-serial");
	args.push_back("file:" + config.dir + "/serial.log");
	args.push_back("-monitor");
	args.push_back("unix:" + MonitorPath(config.dir) + ",server,nowait");
	if (!fQemu.Start(config.qemu, args, NoEnvironment(),
			config.dir + "/qemu.log")) {
		error = "QEMU didn't start";
		return false;
	}
	Nap(1.5);
	if (!fQemu.Poll()) {
		std::string log = fQemu.Log();
		error = "QEMU stopped: " + (log.empty() ? std::string("no message")
			: log.substr(0, 200));
		return false;
	}
	fStarted = true;
	fFrozen = false;
	return true;
}


bool
VmRunner::Running()
{
	return fStarted && fQemu.Poll();
}


std::string
VmRunner::SshCommand() const
{
	char port[16];
	snprintf(port, sizeof(port), "%d", fConfig.sshPort);
	std::string ssh = FindProgram("ssh");
	return "'" + (ssh.empty() ? std::string("ssh") : ssh) + "' -i '"
		+ KeyPath(fConfig.dir) + "' -o IdentitiesOnly=yes"
		" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null"
		" -o BatchMode=yes -o ConnectTimeout=10 -o LogLevel=ERROR -p "
		+ port + " user@127.0.0.1";
}


int
VmRunner::Ssh(const std::string& command, const std::string& logPath,
	int timeoutSeconds, const std::function<bool()>& cancelled,
	std::string* lastLine)
{
	// Through sh, so SshCommand()'s quoting holds
	std::vector<std::string> args;
	args.push_back("-c");
	args.push_back("exec " + SshCommand() + " \"$1\"");
	args.push_back("awase-ssh");
	args.push_back(command);
	Builder run;
	if (!run.Start("/bin/sh", args, NoEnvironment(), logPath))
		return -1;
	time_t until = time(NULL) + timeoutSeconds;
	while (run.Poll()) {
		if (lastLine != NULL)
			*lastLine = run.LastLine();
		if ((cancelled && cancelled()) || time(NULL) > until) {
			run.Stop();
			return -1;
		}
		Nap(0.25);
	}
	if (lastLine != NULL)
		*lastLine = run.LastLine();
	// ssh exits 255 when it couldn't connect
	return run.ExitCode() == 255 ? -1 : run.ExitCode();
}


bool
VmRunner::WaitForSsh(int seconds, const std::function<bool()>& cancelled)
{
	time_t until = time(NULL) + seconds;
	while (time(NULL) < until) {
		if (!Running())
			return false;
		if (Ssh("true", fConfig.dir + "/ssh-wait.log", 20, cancelled) == 0)
			return true;
		if (cancelled && cancelled())
			return false;
		Nap(3);
	}
	return false;
}


void
VmRunner::Freeze(bool frozen)
{
	if (!fStarted || frozen == fFrozen)
		return;
	if (Monitor(fConfig.dir, frozen ? "stop" : "cont"))
		fFrozen = frozen;
}


void
VmRunner::Shutdown(int graceSeconds)
{
	if (!fStarted)
		return;
	if (fFrozen)
		Monitor(fConfig.dir, "cont");
	Monitor(fConfig.dir, "system_powerdown");
	for (int i = 0; i < graceSeconds * 4 && fQemu.Poll(); i++)
		Nap(0.25);
	if (fQemu.Poll())
		fQemu.Stop();
	fStarted = false;
	fFrozen = false;
}


bool
VmRunner::WriteBuildScript(std::string& path, std::string& error)
{
	path = fConfig.dir + "/build-in-vm.sh";
	FILE* file = fopen(path.c_str(), "w");
	if (file == NULL) {
		error = "can't write " + path;
		return false;
	}
	fprintf(file,
		"#!/bin/sh\n"
		"# Written by Awase: builds <port> <version> <outdir> in the x86 VM.\n"
		"set -e\n"
		"port=$1 version=$2 out=$3\n"
		"job=${RENKUPORTS_JOB_ID:-0}\n"
		"vm() { %s \"$@\"; }\n"
		"vm \"env RENKUPORTS_PORTS_URL='$RENKUPORTS_PORTS_URL'"
		" RENKUPORTS_PORTS_REF='$RENKUPORTS_PORTS_REF' AWASE_CORES='%d'"
		" sh /boot/home/awase/guest-build.sh '$port' '$version'"
		" /boot/home/awase/out/$job\"\n"
		"mkdir -p \"$out\"\n"
		"vm \"cd /boot/home/awase/out/$job && tar cf - .\" | (cd \"$out\" && tar xf -)\n"
		"vm \"rm -rf /boot/home/awase/out/$job\"\n",
		SshCommand().c_str(), fConfig.cores);
	fclose(file);
	chmod(path.c_str(), 0755);
	return true;
}


const char*
GuestBuildScript()
{
	return
		"#!/bin/sh\n"
		"# Written by Awase: builds a RenkuOSPorts port with HaikuPorter in the\n"
		"# x86 build VM. guest-build.sh <port> [<version>] <outdir>\n"
		"set -e\n"
		"port=$1 version=$2 out=$3\n"
		"url=${RENKUPORTS_PORTS_URL:-https://github.com/RenkuOS/RenkuOSPorts.git}\n"
		"ref=${RENKUPORTS_PORTS_REF:-main}\n"
		"A=/boot/home/awase; T=$A/ports\n"
		"mkdir -p \"$A\" \"$out\"\n"
		"if [ ! -d \"$T/.git\" ]; then\n"
		"\tgit clone -q --depth 1 --branch \"$ref\" \"$url\" \"$T\"\n"
		"else\n"
		"\tgit -C \"$T\" fetch -q --depth 1 origin \"$ref\"\n"
		"\tgit -C \"$T\" checkout -q -f FETCH_HEAD\n"
		"fi\n"
		"conf=/boot/home/config/settings/haikuports.conf\n"
		"[ -f \"$conf\" ] || cat > \"$conf\" <<EOF\n"
		"TREE_PATH=\"$T\"\n"
		"PACKAGER=\"Awase build VM <awase@localhost>\"\n"
		"TARGET_ARCHITECTURE=\"x86_gcc2\"\n"
		"SECONDARY_TARGET_ARCHITECTURES=\"x86\"\n"
		"ALLOW_UNTESTED=\"yes\"\n"
		"EOF\n"
		"spec=$port\n"
		"[ -z \"$version\" ] || spec=$port-$version\n"
		"stamp=$A/stamp.$$\n"
		"touch \"$stamp\"\n"
		"cd \"$T\"\n"
		"# Downloads in HaikuPorter fail now and then (\"Interrupted system\n"
		"# call\"): try a build three times before calling it failed\n"
		"n=0\n"
		"until haikuporter -y --no-source-packages --get-dependencies"
		" -j\"${AWASE_CORES:-2}\" \"$spec\"; do\n"
		"\tn=$((n + 1))\n"
		"\t[ $n -lt 3 ] || exit 1\n"
		"\techo \"awase: HaikuPorter failed; trying again ($n)\"\n"
		"\tsleep 10\n"
		"done\n"
		"find \"$T/packages\" -name '*.hpkg' -newer \"$stamp\" -exec cp {} \"$out\" \\;\n"
		"rm -f \"$stamp\"\n"
		"ls \"$out\"/*.hpkg >/dev/null\n";
}


}	// namespace awase
