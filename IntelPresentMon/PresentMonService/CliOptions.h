#pragma once
#include "../CommonUtilities//cli/CliFramework.h"

namespace clio
{
	using namespace pmon::util::cli;
	struct Options : public OptionsBase<Options>
	{
		Flag debug{ this, "--debug,-d", "Stall service by running in a loop after startup waiting for debugger to connect" };
		Option<std::string> logDir{ this, "--log-dir", "", "Enable logging to specified directory" };
		Flag logStderr{ this, "--log-stderr", "When logging to files in specified directory, also log to stderr" };
		Option<std::string> controlPipe{ this, "--control-pipe", "", "Name of the named pipe to use for the client-service control channel" };
		Option<std::string> nsmPrefix{ this, "--nsm-prefix", "", "Prefix to use when naming named shared memory segments created for frame data circular buffers" };
		Option<std::string> introNsm{ this, "--intro-nsm", "", "Name of the NSM used for introspection data" };
		Option<long long> timedStop{ this, "--timed-stop", -1, "Signal stop event after specified number of milliseconds" };
		static constexpr const char* description = "Intel PresentMon service for frame and system performance measurement";
		static constexpr const char* name = "PresentMonService.exe";
	};
}