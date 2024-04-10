// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: MIT
#include <windows.h>
#include <string>
#include "Console.h"
#include <stdint.h>
#include <iostream>
#include <fstream>
#include <thread>
#include <TlHelp32.h>
#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <format>
#include <chrono>
#include <conio.h>
#include "../PresentMonAPI2/PresentMonAPI.h"
#include "../PresentMonAPI2/Internal.h"
#include "CliOptions.h"

#include "../PresentMonAPIWrapper/PresentMonAPIWrapper.h"
#include "../PresentMonAPIWrapper/FixedQuery.h"
#include "Utils.h"
#include "DynamicQuerySample.h"
#include "FrameQuerySample.h"
#include "IntrospectionSample.h"
#include "CheckMetricSample.h"
#include "WrapperStaticQuery.h"
#include "MetricListSample.h"


int main(int argc, char* argv[])
{
    try {
        if (auto e = clio::Options::Init(argc, argv)) {
            return *e;
        }
        auto& opt = clio::Options::Get();

        // validate options, better to do this with CLI11 validation but framework needs upgrade...
        if (bool(opt.controlPipe) != bool(opt.introNsm)) {
            std::cout << "Must set both control pipe and intro NSM, or neither.\n";
            return -1;
        }

        // determine requested activity
        if (opt.introspectionSample ^ opt.dynamicQuerySample ^ opt.frameQuerySample ^ opt.checkMetricSample ^ opt.wrapperStaticQuerySample ^ opt.metricListSample) {
            std::unique_ptr<pmapi::Session> pSession;
            if (opt.controlPipe) {
                pSession = std::make_unique<pmapi::Session>(*opt.controlPipe, *opt.introNsm);
            }
            else {
                pSession = std::make_unique<pmapi::Session>();
            }

            if (opt.introspectionSample) {
                return IntrospectionSample(std::move(pSession));
            }
            else if (opt.checkMetricSample) {
                return CheckMetricSample(std::move(pSession));
            }
            else if (opt.dynamicQuerySample) {
                return DynamicQuerySample(std::move(pSession), *opt.windowSize, *opt.metricOffset);
            }
            else if (opt.wrapperStaticQuerySample) {
                return WrapperStaticQuerySample(std::move(pSession));
            }
            else if (opt.metricListSample) {
                return MetricListSample(*pSession);
            }
            else {
                return FrameQuerySample(std::move(pSession));
            }
        }
        else {
            std::cout << "SampleClient supports one action at a time. Select one of:\n";
            std::cout << "--introspection-sample\n";
            std::cout << "--wrapper-static-query-sample\n";
            std::cout << "--dynamic-query-sample [--process-id id | --process-name name.exe] [--add-gpu-metric]\n";
            std::cout << "--frame-query-sample [--process-id id | --process-name name.exe]  [--gen-csv]\n";
            std::cout << "--check-metric-sample --metric PM_METRIC_*\n";
            return -1;
        }
    }
    catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << std::endl;
        return -1;
    }
    catch (...) {
        std::cout << "Unknown Error" << std::endl;
        return -1;
    }

}