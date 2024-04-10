#pragma once
#include "../CommonUtilities//cli/CliFramework.h"

namespace clio
{
	using namespace pmon::util::cli;
	struct Options : public OptionsBase<Options>
	{
		Option<std::string> controlPipe{ this, "--control-pipe", "", "Name of the named pipe to use for the client-service control channel" };
		Option<std::string> introNsm{ this, "--intro-nsm", "", "Name of the NSM used for introspection data" };
		Flag introspectionSample{ this, "--introspection-sample", "Example of how to view availability of all system metrics via introspection" };
		Flag dynamicQuerySample{ this, "--dynamic-query-sample", "Example of how to use a dynamic query to poll metric data" };
		Flag frameQuerySample{ this, "--frame-query-sample", "Example of how to use a frame query to consume raw frame data" };
		Flag checkMetricSample{ this, "--check-metric-sample", "Example of how to check metric availability for a single metric" };
		Flag metricListSample{ this, "--metric-list-sample", "List of all metrics in markdown format" };
		Flag wrapperStaticQuerySample{ this, "--wrapper-static-query-sample", "Example of using the wrapper to poll static metric data" };
		Flag genCsv{ this, "--gen-csv", "Example of creating a csv file from consumed raw frame data" };
		Flag addGPUMetric{ this, "--add-gpu-metric", "Example of how to search for an available GPU metric and add it to a dynamic query" };
		Option<double> metricOffset{ this, "--metric-offset", 1000., "Offset from top for frame data. Used in --dynamic-query-sample" };
		Option<double> windowSize{ this, "--window-size", 2000., "Window size used for metrics calculation. Used in --dynamic-query-sample" };
		Option<unsigned int> processId{ this, "--process-id", 0, "Process Id to use for polling or frame data capture" };
		Option<std::string> processName{ this, "--process-name", "", "Name of process to use for polling or frame data capture" };
		Option<std::string> metric{ this, "--metric", "", "PM_METRIC, ex. PM_METRIC_PRESENTED_FPS" };
		static constexpr const char* description = "Minimal Sample Client for Intel PresentMon service";
		static constexpr const char* name = "SampleClient.exe";
	};
}