/*
 * Copyright 2026, Ian Dumych.
 * SPDX-License-Identifier: MIT
 */
#ifndef AWASE_DOWNLOAD_H
#define AWASE_DOWNLOAD_H

// HTTPS fetches for setting up the build VM: a small text (a release's JSON,
// a .sha256 file) or a large file with progress.

#include <stdint.h>

#include <functional>
#include <string>


namespace awase {


// done and total in bytes (total 0 while unknown). Return false to cancel.
typedef std::function<bool(int64_t done, int64_t total)> DownloadProgress;

bool			FetchText(const std::string& url, std::string& text,
					std::string& error);
// Downloads to path + ".part" and renames it to path when complete.
bool			DownloadFile(const std::string& url, const std::string& path,
					const DownloadProgress& progress, std::string& error);


}	// namespace awase


#endif	// AWASE_DOWNLOAD_H
