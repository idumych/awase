/*
 * Copyright 2026, Ian Dumych.
 * SPDX-License-Identifier: MIT
 */


#include "Download.h"

#include "Portal.h"

#include <curl/curl.h>
#include <stdio.h>


namespace awase {


static const char* kAgent = "Awase/0.3";


static size_t
AppendText(char* data, size_t size, size_t count, void* cookie)
{
	std::string* text = (std::string*)cookie;
	if (text->size() > 4 * 1024 * 1024)
		return 0;
	text->append(data, size * count);
	return size * count;
}


static void
Common(CURL* curl, const std::string& url)
{
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, kAgent);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
	if (CaBundle() != NULL)
		curl_easy_setopt(curl, CURLOPT_CAINFO, CaBundle());
}


bool
FetchText(const std::string& url, std::string& text, std::string& error)
{
	curl_global_init(CURL_GLOBAL_DEFAULT);
	CURL* curl = curl_easy_init();
	if (curl == NULL) {
		error = "libcurl didn't start";
		return false;
	}
	text.clear();
	Common(curl, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, AppendText);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &text);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
	CURLcode result = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	if (result != CURLE_OK) {
		error = curl_easy_strerror(result);
		return false;
	}
	return true;
}


struct FileCookie {
	FILE*					file;
	const DownloadProgress*	progress;
};


static size_t
WriteFile(char* data, size_t size, size_t count, void* cookie)
{
	return fwrite(data, size, count, ((FileCookie*)cookie)->file) * size;
}


static int
Progress(void* cookie, curl_off_t total, curl_off_t done, curl_off_t, curl_off_t)
{
	const DownloadProgress* progress = ((FileCookie*)cookie)->progress;
	if (progress != NULL && *progress && !(*progress)(done, total))
		return 1;	// cancels the transfer
	return 0;
}


bool
DownloadFile(const std::string& url, const std::string& path,
	const DownloadProgress& progress, std::string& error)
{
	curl_global_init(CURL_GLOBAL_DEFAULT);
	std::string part = path + ".part";
	FileCookie cookie;
	cookie.file = fopen(part.c_str(), "wb");
	cookie.progress = &progress;
	if (cookie.file == NULL) {
		error = "can't write " + part;
		return false;
	}
	CURL* curl = curl_easy_init();
	if (curl == NULL) {
		fclose(cookie.file);
		error = "libcurl didn't start";
		return false;
	}
	Common(curl, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteFile);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &cookie);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, Progress);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &cookie);
	// Give up on a stalled transfer, not a slow one
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 120L);
	CURLcode result = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	bool closed = fclose(cookie.file) == 0;
	if (result != CURLE_OK || !closed) {
		error = result == CURLE_ABORTED_BY_CALLBACK ? "cancelled"
			: result != CURLE_OK ? curl_easy_strerror(result) : "can't write "
				+ part;
		remove(part.c_str());
		return false;
	}
	if (rename(part.c_str(), path.c_str()) != 0) {
		error = "can't rename " + part;
		return false;
	}
	return true;
}


}	// namespace awase
