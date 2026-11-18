/*
 * Copyright 2026, Ian Dumych, idumych@gmail.com.
 * SPDX-License-Identifier: MIT
 */
#ifndef AWASE_PORTAL_H
#define AWASE_PORTAL_H

// The build-client API of the RenkuOSPorts portal
// (github.com/Druware/RenkuOSPorts-Portal, src/RenkuPorts.Server/Endpoints/
// BuildClientApi.cs):
//
//   POST /api/client/token              { clientId, secret } -> { accessToken }
//   POST /api/client/jobs/next          the oldest queued job for the
//                                       client's platform, or 204
//   POST /api/client/jobs/{id}/packages multipart: sha256 and buildDate
//                                       fields, then the 'file' part
//   POST /api/client/jobs/{id}/complete { log }
//   POST /api/client/jobs/{id}/fail     { summary, log }
//   GET  /api/repo/{platform}/packages  the published packages (public)

#include <stdint.h>

#include <string>
#include <vector>


namespace awase {


enum PortalStatus {
	kPortalOk,
	kPortalNoWork,			// 204 from next
	kPortalUnauthorized,	// 401: wrong id or secret, or the client was disabled
	kPortalForbidden,		// 403: another client holds the job
	kPortalConflict,		// 409: the job isn't ours any more, or a package differs
	kPortalBadRequest,		// 400
	kPortalServerError,		// 5xx or a reply we couldn't read
	kPortalNetworkError,	// no reply at all
};


struct PortalJob {
	int64_t				id;
	std::string			port;
	std::string			version;
	std::string			category;
	std::string			recipePath;
	std::string			platform;
};


struct PortalPackage {
	std::string			fileName;
	std::string			portName;
	std::string			builtBy;
	std::string			uploadedAt;		// ISO 8601
	int64_t				size;
};


class PortalClient {
public:
								PortalClient();
								~PortalClient();

			void				Configure(const std::string& server,
									const std::string& clientId,
									const std::string& secret);

			PortalStatus		Authenticate();
			PortalStatus		Next(PortalJob& job);
			PortalStatus		Upload(int64_t job, const std::string& path);
			PortalStatus		Complete(int64_t job, const std::string& log);
			PortalStatus		Fail(int64_t job, const std::string& summary,
									const std::string& log);
			PortalStatus		RecentPackages(const std::string& platform,
									std::vector<PortalPackage>& packages);

			// The platform the coordinator gave this client (from its token).
			const std::string&	Platform() const { return fPlatform; }
			// A short reason for the last failure, for the window.
			const std::string&	LastError() const { return fLastError; }

	static	const char*			StatusName(PortalStatus status);

private:
			PortalStatus		_Request(const std::string& method,
									const std::string& path,
									const std::string& jsonBody,
									bool authorized, std::string& reply,
									long* httpCode = NULL);
			PortalStatus		_Authorized(const std::string& path,
									const std::string& jsonBody,
									std::string& reply, long* httpCode = NULL);
			PortalStatus		_Status(long httpCode, const std::string& reply);

			std::string			fServer;
			std::string			fClientId;
			std::string			fSecret;
			std::string			fToken;
			std::string			fPlatform;
			std::string			fLastError;
};


}	// namespace awase


#endif	// AWASE_PORTAL_H
