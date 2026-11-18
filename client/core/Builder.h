/*
 * Copyright 2026, Ian Dumych, idumych@gmail.com.
 * SPDX-License-Identifier: MIT
 */
#ifndef AWASE_BUILDER_H
#define AWASE_BUILDER_H

// Runs one build the way the RenkuOSPorts portal's own client does:
//
//   <command> <port> <version> <outdir>
//
// with RENKUPORTS_PLATFORM, RENKUPORTS_JOB_ID and RENKUPORTS_RECIPE_PATH
// set, stdout and stderr going to the build log. Awase also sets
// AWASE_CORES. The build runs in its own process group, so pausing stops
// everything it started, compilers included.

#include <sys/types.h>

#include <string>
#include <utility>
#include <vector>

#include "Pool.h"


namespace awase {


class Builder {
public:
								Builder();
								~Builder();

			bool				Start(const std::string& command,
									const std::vector<std::string>& args,
									const std::vector<std::pair<std::string,
										std::string> >& environment,
									const std::string& logPath);

			// Updates the state; true while the build runs (or is paused).
			bool				Poll();
			int					ExitCode() const { return fExitCode; }
			bool				Killed() const { return fKilled; }

			void				Pause();
			void				Resume();
			bool				Paused() const { return fPaused; }
			void				Stop();		// SIGTERM, then SIGKILL

			// What the log says, roughly: HaikuPorter's messages give the step.
			BuildPhase			Phase() const { return fPhase; }
			const std::string&	LastLine() const { return fLastLine; }
			std::string			Log() const;

	static	BuildPhase			PhaseForLine(const std::string& line,
									BuildPhase current);

private:
			void				_ReadLog();

			pid_t				fChild;
			int					fExitCode;
			bool				fKilled;
			bool				fPaused;
			std::string			fLogPath;
			long				fLogOffset;
			std::string			fPartial;
			std::string			fLastLine;
			BuildPhase			fPhase;
};


}	// namespace awase


#endif	// AWASE_BUILDER_H
