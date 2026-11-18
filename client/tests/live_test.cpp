/*
 * Copyright 2026, Ian Dumych.
 * SPDX-License-Identifier: MIT
 */

// Awase against a coordinator: the build-client protocol and the live loop,
// run on the host against tests/portal_stub.py. tests/run-live-test.sh
// starts the stub and passes its URL.

#include "Json.h"
#include "LivePool.h"
#include "Portal.h"
#include "Sha256.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <string>

using namespace awase;

static int sFailures = 0;
static std::string sServer;
static std::string sScript;

#define CHECK(x) \
	do { \
		if (!(x)) { \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); \
			sFailures++; \
		} \
	} while (0)


static size_t
Collect(char* data, size_t size, size_t count, void* cookie)
{
	((std::string*)cookie)->append(data, size * count);
	return size * count;
}


// Calls a stub-only route.
static Json
Stub(const std::string& path, const std::string& body = "")
{
	CURL* curl = curl_easy_init();
	std::string url = sServer + path, reply;
	struct curl_slist* headers = curl_slist_append(NULL,
		"Content-Type: application/json");
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Collect);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &reply);
	if (!body.empty()) {
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
	}
	curl_easy_perform(curl);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	Json json;
	Json::Parse(reply, json);
	return json;
}


static Json
NewClient(const char* platform)
{
	return Stub("/stub/clients",
		std::string("{\"platform\":\"") + platform + "\"}");
}


static void
QueueJob(const char* port, const char* platform = "x86_64")
{
	Stub("/stub/jobs", std::string("{\"port\":\"") + port
		+ "\",\"version\":\"1.0\",\"platform\":\"" + platform + "\"}");
}


static const Json*
FindJob(const Json& state, const char* port)
{
	const Json& jobs = state["jobs"];
	for (size_t i = 0; i < jobs.Size(); i++) {
		if (jobs[i]["port"].String() == port)
			return &jobs[i];
	}
	return NULL;
}


static std::string
JobStatus(const char* port)
{
	Json state = Stub("/stub/state");
	const Json* job = FindJob(state, port);
	return job != NULL ? (*job)["status"].String() : "";
}


static bool
WaitFor(const char* port, const char* status, double seconds)
{
	for (int i = 0; i < seconds * 10; i++) {
		if (JobStatus(port) == status)
			return true;
		usleep(100000);
	}
	return false;
}


static LiveConfig
Config(const Json& client)
{
	LiveConfig config;
	config.server = sServer;
	config.clientId = client["clientId"].String();
	config.secret = client["secret"].String();
	config.buildCommand = sScript;
	char dir[] = "/tmp/awase-live-XXXXXX";
	config.workDir = mkdtemp(dir);
	config.pollSeconds = 5;
	return config;
}


static Limits
Unlimited()
{
	Limits limits;
	limits.cores = 2;
	limits.maxCores = 4;
	limits.idleOnly = false;
	limits.maxTemperature = 200;	// this machine's own sensor never pauses

	limits.paused = false;
	return limits;
}


static void
Credentials()
{
	Json client = NewClient("arm64");
	PortalClient portal;
	portal.Configure(sServer + "/", client["clientId"].String(), "wrong");
	CHECK(portal.Authenticate() == kPortalUnauthorized);
	CHECK(portal.LastError().find("Invalid client id or secret")
		!= std::string::npos);

	portal.Configure(sServer, client["clientId"].String(),
		client["secret"].String());
	CHECK(portal.Authenticate() == kPortalOk);
	CHECK(portal.Platform() == "arm64");
	std::string url, branch;
	CHECK(portal.PortsRepo(url, branch) == kPortalOk);
	CHECK(url == "https://example.invalid/RenkuOSPorts.git");
	CHECK(branch == "main");
	PortalJob job;
	CHECK(portal.Next(job) == kPortalNoWork);
}


static int
CompletedBuilds(const Json& state, const Json* job)
{
	int count = 0;
	const Json& builds = state["builds"];
	for (size_t i = 0; job != NULL && i < builds.Size(); i++) {
		if (builds[i]["jobId"].Number() == (*job)["id"].Number()
			&& builds[i]["status"].String() == "Completed")
			count++;
	}
	return count;
}


static const Json*
FindPackage(const Json& state, const std::string& fileName)
{
	const Json& packages = state["packages"];
	for (size_t i = 0; i < packages.Size(); i++) {
		if (packages[i]["fileName"].String() == fileName)
			return &packages[i];
	}
	return NULL;
}


static void
BuildsSucceedAndFail()
{
	Json client = NewClient("x86_64");
	QueueJob("zlib");
	QueueJob("failing");
	QueueJob("other-arch", "riscv64");

	LivePool pool;
	pool.SetLimits(Unlimited());
	pool.Start(Config(client));
	CHECK(WaitFor("failing", "Failed", 20));
	// One client builds zlib twice (the second time once its window has
	// passed), and the two builds match.
	CHECK(WaitFor("zlib", "Succeeded", 20));

	Json state = Stub("/stub/state");
	const Json* failing = FindJob(state, "failing");
	CHECK(failing != NULL && (*failing)["failureSummary"].String()
		== "the build command exited with 1");
	CHECK(failing != NULL && (*failing)["log"].String().find(
		"mock failure for failing") != std::string::npos);
	// Another platform's job is left alone.
	CHECK(JobStatus("other-arch") == "Queued");

	state = Stub("/stub/state");
	CHECK(CompletedBuilds(state, FindJob(state, "zlib")) == 2);

	// The published package is the script's file, byte for byte.
	const Json* package = FindPackage(state, "zlib-1.0-1-x86_64.hpkg");
	CHECK(state["packages"].Size() == 1);
	Sha256 hash;
	std::string bytes = "mock hpkg\nport=zlib\nversion=1.0\narch=x86_64\n";
	hash.Update(bytes.data(), bytes.size());
	CHECK(package != NULL && (*package)["sha256"].String() == hash.HexDigest());
	CHECK(package != NULL
		&& (*package)["verification"].String() == "self-verified");

	// The window's view of it
	for (int i = 0; i < 100 && (pool.Snapshot().builds < 3
			|| pool.Snapshot().recent.empty()); i++)
		usleep(100000);
	LiveSnapshot snapshot = pool.Snapshot();
	CHECK(snapshot.platform == "x86_64");
	CHECK(snapshot.builds == 3);
	CHECK(snapshot.succeeded == 2);
	CHECK(snapshot.failed == 1);
	CHECK(snapshot.recent.size() == 1);
	CHECK(!snapshot.recent.empty() && snapshot.recent[0].Matched());
	CHECK(std::find(snapshot.built.begin(), snapshot.built.end(), "zlib")
		!= snapshot.built.end());
	bool sawSuccess = false;
	for (size_t i = 0; i < snapshot.events.size(); i++) {
		if (snapshot.events[i].kind == kLiveSucceeded)
			sawSuccess = true;
	}
	CHECK(sawSuccess);
	pool.Stop();
}


static void
TwoClientsVerify()
{
	Json first = NewClient("x86"), second = NewClient("x86");
	setenv("SLOW_SECONDS", "4", 1);
	QueueJob("slowpair", "x86");
	QueueJob("withdeplib", "x86");

	LivePool a, b;
	a.SetLimits(Unlimited());
	b.SetLimits(Unlimited());
	a.Start(Config(first));
	b.Start(Config(second));
	CHECK(WaitFor("slowpair", "Succeeded", 30));
	CHECK(WaitFor("withdeplib", "Succeeded", 30));
	a.Stop();
	b.Stop();

	Json state = Stub("/stub/state");
	const Json* package = FindPackage(state, "slowpair-1.0-1-x86.hpkg");
	CHECK(package != NULL
		&& (*package)["verification"].String() == "verified");
	// The dependency the build made is recorded, not published.
	CHECK(FindPackage(state, "withdeplib-1.0-1-x86.hpkg") != NULL);
	CHECK(FindPackage(state, "depof_withdeplib-1.0-1-x86.hpkg") == NULL);
	bool sawExtra = false;
	const Json& builds = state["builds"];
	for (size_t i = 0; i < builds.Size(); i++) {
		const Json& staged = builds[i]["staged"];
		for (size_t j = 0; j < staged.Size(); j++) {
			if (staged[j]["extra"].Bool())
				sawExtra = true;
		}
	}
	CHECK(sawExtra);
}


static std::atomic<int> sTemperature(50);


static int
FakeSensor()
{
	return sTemperature;
}


static void
HeatPausesTheBuild()
{
	Json client = NewClient("x86");
	setenv("SLOW_SECONDS", "4", 1);
	QueueJob("slowheat", "x86");

	LivePool pool;
	pool.SetTemperatureSource(FakeSensor, 0.1);
	Limits limits = Unlimited();
	limits.maxTemperature = 85;
	pool.SetLimits(limits);
	pool.Start(Config(client));
	CHECK(WaitFor("slowheat", "InProgress", 10));
	usleep(500000);
	CHECK(pool.Snapshot().temperature == 50);
	CHECK(!pool.Snapshot().tooHot);

	// Over the limit: the build pauses.
	sTemperature = 95;
	usleep(600000);
	LiveSnapshot snapshot = pool.Snapshot();
	CHECK(snapshot.tooHot);
	CHECK(snapshot.state == kLivePaused);
	CHECK(snapshot.message.find("Too hot") != std::string::npos);
	double before = snapshot.elapsed;
	sleep(2);
	CHECK(pool.Snapshot().elapsed - before < 0.5);

	// A little cooler, but not 5 degrees under the limit: still paused.
	sTemperature = 82;
	usleep(600000);
	CHECK(pool.Snapshot().tooHot);

	// Cool enough: it carries on and finishes.
	sTemperature = 79;
	usleep(600000);
	snapshot = pool.Snapshot();
	CHECK(!snapshot.tooHot);
	CHECK(snapshot.state != kLivePaused);
	bool completed = false;
	for (int i = 0; i < 150 && !completed; i++) {
		Json state = Stub("/stub/state");
		completed = CompletedBuilds(state, FindJob(state, "slowheat")) > 0;
		if (!completed)
			usleep(100000);
	}
	CHECK(completed);

	bool sawPause = false, sawCool = false;
	snapshot = pool.Snapshot();
	for (size_t i = 0; i < snapshot.events.size(); i++) {
		const std::string& text = snapshot.events[i].text;
		if (text.find("over the 85") != std::string::npos)
			sawPause = true;
		if (text.find("cooled to 79") != std::string::npos)
			sawCool = true;
	}
	CHECK(sawPause);
	CHECK(sawCool);

	// Raising the limit takes effect at the next check (about a second
	// while idle).
	sTemperature = 95;
	usleep(1500000);
	CHECK(pool.Snapshot().tooHot);
	limits.maxTemperature = 100;
	pool.SetLimits(limits);
	usleep(1500000);
	CHECK(!pool.Snapshot().tooHot);
	pool.Stop();
}


static void
PauseStopsTheBuild()
{
	Json client = NewClient("x86_64");
	setenv("SLOW_SECONDS", "4", 1);
	QueueJob("slowpause");

	LivePool pool;
	Limits limits = Unlimited();
	pool.SetLimits(limits);
	pool.Start(Config(client));
	CHECK(WaitFor("slowpause", "InProgress", 10));
	usleep(500000);

	limits.paused = true;
	pool.SetLimits(limits);
	usleep(600000);
	double before = pool.Snapshot().elapsed;
	CHECK(pool.Snapshot().state == kLivePaused);
	sleep(5);	// longer than the whole build
	CHECK(pool.Snapshot().elapsed - before < 0.5);
	CHECK(JobStatus("slowpause") == "InProgress");

	limits.paused = false;
	pool.SetLimits(limits);
	// The build completes (the job waits for a second build after it).
	bool completed = false;
	for (int i = 0; i < 150 && !completed; i++) {
		Json state = Stub("/stub/state");
		completed = CompletedBuilds(state, FindJob(state, "slowpause")) > 0;
		if (!completed)
			usleep(100000);
	}
	CHECK(completed);
	LiveSnapshot snapshot = pool.Snapshot();
	CHECK(snapshot.phase >= kPackaging || !snapshot.hasJob);
	pool.Stop();
}


static void
StoppingReportsTheJob()
{
	// arm64: slowpause is still queued on x86_64, for its second build.
	Json client = NewClient("arm64");
	setenv("SLOW_SECONDS", "20", 1);
	QueueJob("slowstop", "arm64");

	LivePool pool;
	pool.SetLimits(Unlimited());
	pool.Start(Config(client));
	CHECK(WaitFor("slowstop", "InProgress", 10));
	usleep(500000);
	pool.Stop();

	// The job goes back to the queue, not to Failed.
	Json state = Stub("/stub/state");
	const Json* job = FindJob(state, "slowstop");
	CHECK(job != NULL && (*job)["status"].String() == "Queued");
	CHECK(job != NULL && (*job)["failureSummary"].String()
		== "Released: client stopped");
	CHECK(job != NULL && (*job)["log"].String().find("Downloading")
		!= std::string::npos);
}


int
main(int argc, char** argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: live_test <stub url> <build command>\n");
		return 2;
	}
	sServer = argv[1];
	sScript = argv[2];
	curl_global_init(CURL_GLOBAL_DEFAULT);

	Credentials();
	BuildsSucceedAndFail();
	TwoClientsVerify();
	HeatPausesTheBuild();
	PauseStopsTheBuild();
	StoppingReportsTheJob();

	if (sFailures == 0)
		printf("all live tests passed\n");
	return sFailures == 0 ? 0 : 1;
}
