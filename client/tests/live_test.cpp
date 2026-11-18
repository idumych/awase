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
	limits.maxTemperature = 85;
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
	PortalJob job;
	CHECK(portal.Next(job) == kPortalNoWork);
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
	CHECK(WaitFor("zlib", "Succeeded", 20));
	CHECK(WaitFor("failing", "Failed", 20));

	Json state = Stub("/stub/state");
	const Json* failing = FindJob(state, "failing");
	CHECK(failing != NULL && (*failing)["failureSummary"].String()
		== "the build command exited with 1");
	CHECK(failing != NULL && (*failing)["log"].String().find(
		"mock failure for failing") != std::string::npos);
	// Another platform's job is left alone.
	CHECK(JobStatus("other-arch") == "Queued");

	// The uploaded package is the script's file, byte for byte.
	const Json& packages = state["packages"];
	CHECK(packages.Size() == 1);
	Sha256 hash;
	std::string bytes = "mock hpkg\nport=zlib\nversion=1.0\narch=x86_64\n";
	hash.Update(bytes.data(), bytes.size());
	CHECK(packages[0]["fileName"].String() == "zlib-1.0-1-x86_64.hpkg");
	CHECK(packages[0]["sha256"].String() == hash.HexDigest());

	// The window's view of it
	for (int i = 0; i < 50 && pool.Snapshot().builds < 2; i++)
		usleep(100000);
	LiveSnapshot snapshot = pool.Snapshot();
	CHECK(snapshot.platform == "x86_64");
	CHECK(snapshot.builds == 2);
	CHECK(snapshot.succeeded == 1);
	CHECK(snapshot.failed == 1);
	bool sawSuccess = false;
	for (size_t i = 0; i < snapshot.events.size(); i++) {
		if (snapshot.events[i].kind == kLiveSucceeded)
			sawSuccess = true;
	}
	CHECK(sawSuccess);
	pool.Stop();
}


static void
PauseStopsTheBuild()
{
	Json client = NewClient("x86_64");
	setenv("SLOW_SECONDS", "4", 1);
	QueueJob("slow-pause");

	LivePool pool;
	Limits limits = Unlimited();
	pool.SetLimits(limits);
	pool.Start(Config(client));
	CHECK(WaitFor("slow-pause", "InProgress", 10));
	usleep(500000);

	limits.paused = true;
	pool.SetLimits(limits);
	usleep(600000);
	double before = pool.Snapshot().elapsed;
	CHECK(pool.Snapshot().state == kLivePaused);
	sleep(5);	// longer than the whole build
	CHECK(pool.Snapshot().elapsed - before < 0.5);
	CHECK(JobStatus("slow-pause") == "InProgress");

	limits.paused = false;
	pool.SetLimits(limits);
	CHECK(WaitFor("slow-pause", "Succeeded", 15));
	LiveSnapshot snapshot = pool.Snapshot();
	CHECK(snapshot.phase >= kPackaging || !snapshot.hasJob);
	pool.Stop();
}


static void
StoppingReportsTheJob()
{
	Json client = NewClient("x86_64");
	setenv("SLOW_SECONDS", "20", 1);
	QueueJob("slow-stop");

	LivePool pool;
	pool.SetLimits(Unlimited());
	pool.Start(Config(client));
	CHECK(WaitFor("slow-stop", "InProgress", 10));
	usleep(500000);
	pool.Stop();

	Json state = Stub("/stub/state");
	const Json* job = FindJob(state, "slow-stop");
	CHECK(job != NULL && (*job)["status"].String() == "Failed");
	CHECK(job != NULL && (*job)["failureSummary"].String() == "client stopped");
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
	PauseStopsTheBuild();
	StoppingReportsTheJob();

	if (sFailures == 0)
		printf("all live tests passed\n");
	return sFailures == 0 ? 0 : 1;
}
