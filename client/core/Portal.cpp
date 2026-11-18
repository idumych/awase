/*
 * Copyright 2026, Ian Dumych.
 * SPDX-License-Identifier: MIT
 */


#include "Portal.h"

#include "Json.h"
#include "Sha256.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>


namespace awase {


// The server keeps the last 5 MB of a log; send no more than that.
static const size_t kMaxLog = 5 * 1024 * 1024;
static const long kTimeoutSeconds = 120;
static const char* kUserAgent = "Awase/0.2";


static size_t
Collect(char* data, size_t size, size_t count, void* cookie)
{
	std::string* reply = (std::string*)cookie;
	if (reply->size() < 1024 * 1024)
		reply->append(data, size * count);
	return size * count;
}


static std::string
TailOf(const std::string& log)
{
	return log.size() > kMaxLog ? log.substr(log.size() - kMaxLog) : log;
}


static std::string
Base64UrlDecode(const std::string& in)
{
	std::string out;
	unsigned value = 0;
	int bits = 0;
	for (size_t i = 0; i < in.size(); i++) {
		char c = in[i];
		int d;
		if (c >= 'A' && c <= 'Z')
			d = c - 'A';
		else if (c >= 'a' && c <= 'z')
			d = c - 'a' + 26;
		else if (c >= '0' && c <= '9')
			d = c - '0' + 52;
		else if (c == '-' || c == '+')
			d = 62;
		else if (c == '_' || c == '/')
			d = 63;
		else
			continue;
		value = (value << 6) | d;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out += (char)((value >> bits) & 0xff);
		}
	}
	return out;
}


// The token is a JWT; its "platform" claim says what the client builds for.
static std::string
PlatformFromToken(const std::string& token)
{
	size_t first = token.find('.');
	size_t second = first == std::string::npos
		? std::string::npos : token.find('.', first + 1);
	if (second == std::string::npos)
		return "";
	Json claims;
	if (!Json::Parse(Base64UrlDecode(token.substr(first + 1,
			second - first - 1)), claims))
		return "";
	return claims["platform"].String();
}


// libcurl looks for CA certificates where the system it was built on keeps
// them. A static awase-tui runs on other distributions too, so when that
// file is missing, use SSL_CERT_FILE or the first common bundle found.
static const char*
CaBundle()
{
	static const std::string sPath = [] {
		struct stat st;
		const char* env = getenv("SSL_CERT_FILE");
		if (env != NULL && stat(env, &st) == 0)
			return std::string(env);
		curl_version_info_data* info = curl_version_info(CURLVERSION_NOW);
		if (info->cainfo != NULL && stat(info->cainfo, &st) == 0)
			return std::string();
		static const char* const kBundles[] = {
			"/etc/ssl/certs/ca-certificates.crt",
			"/etc/pki/tls/certs/ca-bundle.crt",
			"/etc/ssl/ca-bundle.pem",
			"/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",
			"/etc/ssl/cert.pem",
		};
		for (const char* path : kBundles) {
			if (stat(path, &st) == 0)
				return std::string(path);
		}
		return std::string();
	}();
	return sPath.empty() ? NULL : sPath.c_str();
}


PortalClient::PortalClient()
{
	static bool sInitialized = false;
	if (!sInitialized) {
		curl_global_init(CURL_GLOBAL_DEFAULT);
		sInitialized = true;
	}
}


PortalClient::~PortalClient()
{
}


void
PortalClient::Configure(const std::string& server, const std::string& clientId,
	const std::string& secret)
{
	fServer = server;
	while (!fServer.empty() && fServer[fServer.size() - 1] == '/')
		fServer.erase(fServer.size() - 1);
	fClientId = clientId;
	fSecret = secret;
	fToken.clear();
	fPlatform.clear();
}


PortalStatus
PortalClient::_Status(long httpCode, const std::string& reply)
{
	if (httpCode >= 200 && httpCode < 300)
		return httpCode == 204 ? kPortalNoWork : kPortalOk;

	// ASP.NET problem details: { title, detail }
	Json problem;
	std::string reason;
	if (Json::Parse(reply, problem))
		reason = problem["detail"].String(problem["title"].String());
	char buffer[64];
	snprintf(buffer, sizeof(buffer), "HTTP %ld", httpCode);
	fLastError = reason.empty() ? buffer : reason + " (" + buffer + ")";

	switch (httpCode) {
		case 400: return kPortalBadRequest;
		case 401: return kPortalUnauthorized;
		case 403: return kPortalForbidden;
		case 404: return kPortalBadRequest;
		case 409: return kPortalConflict;
		default: return kPortalServerError;
	}
}


PortalStatus
PortalClient::_Request(const std::string& method, const std::string& path,
	const std::string& jsonBody, bool authorized, std::string& reply,
	long* httpCode)
{
	reply.clear();
	CURL* curl = curl_easy_init();
	if (curl == NULL) {
		fLastError = "can't start libcurl";
		return kPortalNetworkError;
	}

	std::string url = fServer + path;
	struct curl_slist* headers = NULL;
	headers = curl_slist_append(headers, "Accept: application/json");
	if (method == "POST")
		headers = curl_slist_append(headers, "Content-Type: application/json");
	std::string bearer;
	if (authorized) {
		bearer = "Authorization: Bearer " + fToken;
		headers = curl_slist_append(headers, bearer.c_str());
	}

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Collect);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &reply);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, kTimeoutSeconds);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent);
	if (CaBundle() != NULL)
		curl_easy_setopt(curl, CURLOPT_CAINFO, CaBundle());
	if (method == "POST") {
		curl_easy_setopt(curl, CURLOPT_POST, 1L);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonBody.c_str());
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)jsonBody.size());
	}

	CURLcode result = curl_easy_perform(curl);
	long code = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);

	if (httpCode != NULL)
		*httpCode = code;
	if (result != CURLE_OK) {
		fLastError = curl_easy_strerror(result);
		return kPortalNetworkError;
	}
	return _Status(code, reply);
}


PortalStatus
PortalClient::_Authorized(const std::string& method, const std::string& path,
	const std::string& jsonBody, std::string& reply, long* httpCode)
{
	if (fToken.empty()) {
		PortalStatus status = Authenticate();
		if (status != kPortalOk)
			return status;
	}
	PortalStatus status = _Request(method, path, jsonBody, true, reply,
		httpCode);
	if (status == kPortalUnauthorized) {
		// The token may have expired: get a new one and try once more.
		fToken.clear();
		status = Authenticate();
		if (status == kPortalOk)
			status = _Request(method, path, jsonBody, true, reply, httpCode);
	}
	return status;
}


PortalStatus
PortalClient::Authenticate()
{
	std::string body = "{\"clientId\":" + Json::Quote(fClientId)
		+ ",\"secret\":" + Json::Quote(fSecret) + "}";
	std::string reply;
	PortalStatus status = _Request("POST", "/api/client/token", body, false,
		reply);
	if (status != kPortalOk)
		return status;
	Json json;
	if (!Json::Parse(reply, json) || json["accessToken"].String().empty()) {
		fLastError = "the coordinator's token reply wasn't understood";
		return kPortalServerError;
	}
	fToken = json["accessToken"].String();
	fPlatform = PlatformFromToken(fToken);
	return kPortalOk;
}


PortalStatus
PortalClient::PortsRepo(std::string& url, std::string& branch)
{
	url.clear();
	branch.clear();
	std::string reply;
	PortalStatus status = _Authorized("GET", "/api/client/me", "", reply);
	if (status != kPortalOk)
		return status;
	Json json;
	if (!Json::Parse(reply, json)) {
		fLastError = "the coordinator's client details weren't understood";
		return kPortalServerError;
	}
	url = json["portsRepo"]["url"].String();
	branch = json["portsRepo"]["branch"].String();
	return kPortalOk;
}


PortalStatus
PortalClient::Next(PortalJob& job)
{
	std::string reply;
	PortalStatus status = _Authorized("POST", "/api/client/jobs/next", "",
		reply);
	if (status != kPortalOk)
		return status;
	Json json;
	if (!Json::Parse(reply, json) || json["jobId"].IsNull()) {
		fLastError = "the coordinator's job wasn't understood";
		return kPortalServerError;
	}
	job.id = (int64_t)json["jobId"].Number();
	job.port = json["portName"].String();
	job.version = json["version"].String();
	job.category = json["category"].String();
	job.recipePath = json["recipePath"].String();
	job.platform = json["platform"].String();
	job.build = (int)json["build"].Number(1);
	return kPortalOk;
}


PortalStatus
PortalClient::Upload(int64_t job, const std::string& path, bool* extra)
{
	if (extra != NULL)
		*extra = false;
	std::string sha;
	if (!Sha256::HashFile(path, sha)) {
		fLastError = "can't read " + path;
		return kPortalBadRequest;
	}
	struct stat st;
	char buildDate[32] = "";
	if (stat(path.c_str(), &st) == 0) {
		struct tm utc;
		gmtime_r(&st.st_mtime, &utc);
		strftime(buildDate, sizeof(buildDate), "%Y-%m-%dT%H:%M:%SZ", &utc);
	}
	std::string name = path.substr(path.rfind('/') + 1);

	if (fToken.empty()) {
		PortalStatus status = Authenticate();
		if (status != kPortalOk)
			return status;
	}

	char route[96];
	snprintf(route, sizeof(route), "/api/client/jobs/%lld/packages",
		(long long)job);

	PortalStatus status = kPortalNetworkError;
	for (int attempt = 0; attempt < 2; attempt++) {
		CURL* curl = curl_easy_init();
		if (curl == NULL)
			return kPortalNetworkError;
		// The server reads the fields before the file, so they go first.
		curl_mime* form = curl_mime_init(curl);
		curl_mimepart* part = curl_mime_addpart(form);
		curl_mime_name(part, "sha256");
		curl_mime_data(part, sha.c_str(), CURL_ZERO_TERMINATED);
		if (buildDate[0] != '\0') {
			part = curl_mime_addpart(form);
			curl_mime_name(part, "buildDate");
			curl_mime_data(part, buildDate, CURL_ZERO_TERMINATED);
		}
		part = curl_mime_addpart(form);
		curl_mime_name(part, "file");
		curl_mime_filedata(part, path.c_str());
		curl_mime_filename(part, name.c_str());
		curl_mime_type(part, "application/octet-stream");

		std::string bearer = "Authorization: Bearer " + fToken;
		struct curl_slist* headers = curl_slist_append(NULL, bearer.c_str());
		std::string url = fServer + route;
		std::string reply;
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
		curl_easy_setopt(curl, CURLOPT_MIMEPOST, form);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Collect);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &reply);
		curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
		curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent);
		if (CaBundle() != NULL)
			curl_easy_setopt(curl, CURLOPT_CAINFO, CaBundle());
		// Large packages: no overall timeout, but give up on a stalled link.
		curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
		curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 300L);

		CURLcode result = curl_easy_perform(curl);
		long code = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
		curl_slist_free_all(headers);
		curl_mime_free(form);
		curl_easy_cleanup(curl);

		if (result != CURLE_OK) {
			fLastError = curl_easy_strerror(result);
			return kPortalNetworkError;
		}
		status = _Status(code, reply);
		if (status == kPortalOk && extra != NULL) {
			Json json;
			if (Json::Parse(reply, json))
				*extra = json["extra"].Bool();
		}
		if (status != kPortalUnauthorized || attempt > 0)
			break;
		fToken.clear();
		if (Authenticate() != kPortalOk)
			break;
	}
	return status;
}


PortalStatus
PortalClient::Complete(int64_t job, const std::string& log)
{
	char route[96];
	snprintf(route, sizeof(route), "/api/client/jobs/%lld/complete",
		(long long)job);
	std::string reply;
	return _Authorized("POST", route, "{\"log\":" + Json::Quote(TailOf(log)) + "}",
		reply);
}


PortalStatus
PortalClient::Fail(int64_t job, const std::string& summary,
	const std::string& log)
{
	char route[96];
	snprintf(route, sizeof(route), "/api/client/jobs/%lld/fail",
		(long long)job);
	std::string reply;
	return _Authorized("POST", route, "{\"summary\":" + Json::Quote(summary)
		+ ",\"log\":" + Json::Quote(TailOf(log)) + "}", reply);
}


PortalStatus
PortalClient::Release(int64_t job, const std::string& reason,
	const std::string& log)
{
	char route[96];
	snprintf(route, sizeof(route), "/api/client/jobs/%lld/release",
		(long long)job);
	std::string reply;
	long code = 0;
	PortalStatus status = _Authorized("POST", route, "{\"reason\":"
		+ Json::Quote(reason) + ",\"log\":" + Json::Quote(TailOf(log)) + "}",
		reply, &code);
	if (code == 404)
		return Fail(job, "infrastructure: " + reason, log);
	return status;
}


PortalStatus
PortalClient::RecentPackages(const std::string& platform,
	std::vector<PortalPackage>& packages)
{
	std::string reply;
	// The newest page is enough for the window.
	PortalStatus status = _Request("GET", "/api/repo/" + platform
		+ "/packages?pageSize=200", "", false, reply);
	if (status != kPortalOk)
		return status;
	Json json;
	bool parsed = Json::Parse(reply, json);
	// Paged since 56dde86; a bare array before that.
	const Json& items = parsed && json.GetType() != Json::kArray
		? json["items"] : json;
	if (!parsed || items.GetType() != Json::kArray) {
		fLastError = "the package list wasn't understood";
		return kPortalServerError;
	}
	packages.clear();
	for (size_t i = 0; i < items.Size(); i++) {
		const Json& item = items[i];
		PortalPackage package;
		package.fileName = item["fileName"].String();
		package.portName = item["portName"].String();
		package.builtBy = item["builtBy"].String();
		package.uploadedAt = item["uploadedAt"].String();
		package.size = (int64_t)item["size"].Number();
		package.verification = item["verification"].String();
		packages.push_back(package);
	}
	return kPortalOk;
}


/*static*/ const char*
PortalClient::StatusName(PortalStatus status)
{
	switch (status) {
		case kPortalOk: return "ok";
		case kPortalNoWork: return "no work";
		case kPortalUnauthorized: return "unauthorized";
		case kPortalForbidden: return "forbidden";
		case kPortalConflict: return "conflict";
		case kPortalBadRequest: return "bad request";
		case kPortalServerError: return "server error";
		case kPortalNetworkError: return "network error";
	}
	return "?";
}


}	// namespace awase
