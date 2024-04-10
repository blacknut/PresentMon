// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: MIT
#include "Service.h"
#include "NamedPipeServer.h"
#include "PresentMon.h"
#include "PowerTelemetryContainer.h"
#include "..\ControlLib\WmiCpu.h"
#include "..\PresentMonUtils\StringUtils.h"
#include <filesystem>
#include "../Interprocess/source/Interprocess.h"
#include "CliOptions.h"
#include "GlobalIdentifiers.h"
#include <ranges>

#define GOOGLE_GLOG_DLL_DECL
#define GLOG_NO_ABBREVIATED_SEVERITIES
#include <glog/logging.h>

using namespace pmon;

void InitializeLogging(const char* servicename, const char* location, const char* basename,
                       const char* extension, int level, bool logstderr) {
  // initialize glog
  if (!google::IsGoogleLoggingInitialized()) {
    google::InitGoogleLogging(servicename);
    if (!basename) {
      basename = "pmon-srv";
    }
    if (location) {
      google::SetLogDestination(
          google::GLOG_INFO,
          std::format("{}\\{}-info-", location, basename).c_str());
      google::SetLogDestination(
          google::GLOG_WARNING,
          std::format("{}\\{}-warn-", location, basename).c_str());
      google::SetLogDestination(
          google::GLOG_ERROR,
          std::format("{}\\{}-err-", location, basename).c_str());
      google::SetLogDestination(
          google::GLOG_FATAL,
          std::format("{}\\{}-fatal-", location, basename).c_str());
    }
    if (extension) {
      google::SetLogFilenameExtension(extension);
    }
    FLAGS_minloglevel = std::clamp(level, 0, 3);
    // TODO: allow setting stderr log without setting log dir, also allow setting stderr exclusive logging
    FLAGS_alsologtostderr = logstderr;
  }
}

bool NanoSleep(int32_t ms, bool alertable) {
  HANDLE timer;
  LARGE_INTEGER li;
  // Convert from ms to 100ns units and negate
  int64_t ns = -10000 * (int64_t)ms;
  // Create a high resolution table
  if (!(timer = CreateWaitableTimerEx(NULL, NULL,
                                      CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                      TIMER_ALL_ACCESS))) {
    return false;
  }
  li.QuadPart = ns;
  if (!SetWaitableTimer(timer, &li, 0, NULL, NULL, FALSE)) {
    CloseHandle(timer);
    return false;
  }
  WaitForSingleObjectEx(timer, INFINITE, BOOL(alertable));
  CloseHandle(timer);
  return true;
}

void SetupFileLogging()
{
    auto& opt = clio::Options::Get();
    auto& logDir = *opt.logDir;
    if (std::filesystem::is_directory(logDir)) {
        InitializeLogging(opt.GetName().c_str(), logDir.c_str(), "pm-srv", ".log", 0, opt.logStderr);
    }
}

// Attempt to use a high resolution sleep but if not
// supported use regular Sleep().
void PmSleep(int32_t ms, bool alertable = false) {
  if (!NanoSleep(ms, alertable)) {
    Sleep(ms);
  }
  return;
}

void IPCCommunication(Service* srv, PresentMon* pm)
{
    // alias for options
    auto& opt = clio::Options::Get();

    bool createNamedPipeServer = true;

    if (srv == nullptr) {
        // TODO: log
        return;
    }

    try
    {
        auto nps = std::make_unique<NamedPipeServer>(srv, pm, opt.controlPipe.AsOptional());
        while (createNamedPipeServer) {
            DWORD result = nps->RunServer();
            if (result == ERROR_SUCCESS) {
                createNamedPipeServer = false;
            }
            else {
                // We were unable to start our named pipe server. Sleep for
                // a bit and then try again.
                PmSleep(3000);
            }
        }
    }
    catch (const std::bad_alloc& e)
    {
        LOG(INFO) << "Unable to create Name Pipe Server. Result: " << e.what();
        return;
    }

    return;
}

void PowerTelemetry(Service* const srv, PresentMon* const pm,
	PowerTelemetryContainer* const ptc, ipc::ServiceComms* const pComms)
{
	if (srv == nullptr || pm == nullptr || ptc == nullptr) {
		// TODO: log error here
		return;
	}

    // we first wait for a client control connection before populating telemetry container
    // after populating, we sample each adapter to gather availability information
    // this is deferred until client connection in order to increase the probability that
    // telemetry metric availability is accurately assessed
    {
        const HANDLE events[]{
              pm->GetFirstConnectionHandle(),
              srv->GetServiceStopHandle(),
        };
        const auto waitResult = WaitForMultipleObjects((DWORD)std::size(events), events, FALSE, INFINITE);
        // TODO: check for wait result error
        // if events[1] was signalled, that means service is stopping so exit thread
        if ((waitResult - WAIT_OBJECT_0) == 1) {
            return;
        }
        ptc->Repopulate();
        for (auto& adapter : ptc->GetPowerTelemetryAdapters()) {
            // sample 2x here as workaround/kludge because Intel provider mispreports 1st sample
            adapter->Sample();
            adapter->Sample();
            pComms->RegisterGpuDevice(adapter->GetVendor(), adapter->GetName(), adapter->GetPowerTelemetryCapBits());
        }
        pComms->FinalizeGpuDevices();
    }

	// only start periodic polling when streaming starts
    // exit polling loop and this thread when service is stopping
    {
        const HANDLE events[]{
          pm->GetStreamingStartHandle(),
          srv->GetServiceStopHandle(),
        };
        while (1) {
            auto waitResult = WaitForMultipleObjects((DWORD)std::size(events), events, FALSE, INFINITE);
            // TODO: check for wait result error
            // if events[1] was signalled, that means service is stopping so exit thread
            if ((waitResult - WAIT_OBJECT_0) == 1) {
                return;
            }
            // otherwise we assume streaming has started and we begin the polling loop
            while (WaitForSingleObject(srv->GetServiceStopHandle(), 0) != WAIT_OBJECT_0) {
                // if device was reset (driver installed etc.) we need to repopulate telemetry
                if (WaitForSingleObject(srv->GetResetPowerTelemetryHandle(), 0) == WAIT_OBJECT_0) {
                    // TODO: log error here or inside of repopulate
                    ptc->Repopulate();
                }
                for (auto& adapter : ptc->GetPowerTelemetryAdapters()) {
                    adapter->Sample();
                }
                PmSleep(pm->GetGpuTelemetryPeriod());
                // go dormant if there are no active streams left
                // TODO: consider race condition here if client stops and starts streams rapidly
                if (pm->GetActiveStreams() == 0) {
                    break;
                }
            }
        }
    }
}

void CpuTelemetry(Service* const srv, PresentMon* const pm,
	pwr::cpu::CpuTelemetry* const cpu)
{
	if (srv == nullptr || pm == nullptr) {
		// TODO: log error on this condition
		return;
	}

    const HANDLE events[] {
        pm->GetStreamingStartHandle(),
        srv->GetServiceStopHandle(),
    };

	while (1) {
		auto waitResult = WaitForMultipleObjects((DWORD)std::size(events), events, FALSE, INFINITE);
		auto i = waitResult - WAIT_OBJECT_0;
		if (i == 1) {
			return;
		}
		while (WaitForSingleObject(srv->GetServiceStopHandle(), 0) !=
			WAIT_OBJECT_0) {
			cpu->Sample();
			PmSleep(pm->GetGpuTelemetryPeriod());
			// Get the number of currently active streams
			auto num_active_streams = pm->GetActiveStreams();
			if (num_active_streams == 0) {
				break;
			}
		}
	}
}

void PresentMonMainThread(Service* const pSvc)
{
    namespace rn = std::ranges; namespace vi = rn::views;

    assert(pSvc);

    // these thread containers need to be created outside of the try scope
    // so that if an exception happens, it won't block during unwinding,
    // trying to join threads that are waiting for a stop signal
    std::jthread controlPipeThread;
    std::jthread gpuTelemetryThread;
    std::jthread cpuTelemetryThread;

    try {
        // alias for options
        auto& opt = clio::Options::Get();

        // spin here waiting for debugger to attach, after which debugger should set
        // debug_service to false in order to proceed
        for (auto debug_service = opt.debug; debug_service;) {
            if (WaitForSingleObject(pSvc->GetServiceStopHandle(), 0) != WAIT_OBJECT_0) {
                PmSleep(500);
            }
            else {
                return;
            }
        }

        if (opt.logDir) {
            SetupFileLogging();
        }

        if (opt.timedStop) {
            const auto hTimer = CreateWaitableTimerA(NULL, FALSE, NULL);
            const LARGE_INTEGER liDueTime{
                // timedStop in ms, we need to express in units of 100ns
                // and making it negative makes the timeout relative to now
                // (positive value indicates an absolute timepoint)
                .QuadPart = -10'000LL * *opt.timedStop,
            };
            struct Completion {
                static void CALLBACK Routine(LPVOID pSvc, DWORD dwTimerLowValue, DWORD dwTimerHighValue) {
                    static_cast<Service*>(pSvc)->SignalServiceStop();
                }
            };
            if (hTimer) {
                SetWaitableTimer(hTimer, &liDueTime, 0, &Completion::Routine, pSvc, FALSE);
            }
        }

        PresentMon pm;
        PowerTelemetryContainer ptc;

        // create service-side comms object for transmitting introspection data to clients
        std::unique_ptr<ipc::ServiceComms> pComms;
        try {
            auto introNsmName = opt.introNsm.AsOptional().value_or(gid::defaultIntrospectionNsmName);
            LOG(INFO) << "Creating comms with NSM name: " << introNsmName;
            pComms = ipc::MakeServiceComms(std::move(introNsmName));
        }
        catch (const std::exception& e) {
            LOG(ERROR) << "Failed making service comms> " << e.what() << std::endl;
            google::FlushLogFiles(0);
            pSvc->SignalServiceStop(-1);
            return;
        }

        // Set the created power telemetry container 
        pm.SetPowerTelemetryContainer(&ptc);

        // Start IPC communication thread
        controlPipeThread = std::jthread{ IPCCommunication, pSvc, &pm };

        try {
            gpuTelemetryThread = std::jthread{ PowerTelemetry, pSvc, &pm, &ptc, pComms.get() };
        }
        catch (...) {
            LOG(ERROR) << "failed creating gpu(power) telemetry thread" << std::endl;
        }

        // Create CPU telemetry
        std::shared_ptr<pwr::cpu::CpuTelemetry> cpu;
        try {
            // Try to use WMI for metrics sampling
            cpu = std::make_shared<pwr::cpu::wmi::WmiCpu>();
        }
        catch (const std::runtime_error& e) {
            LOG(ERROR) << "failed creating wmi cpu telemetry thread; Status: " << e.what() << std::endl;
        }
        catch (...) {
            LOG(ERROR) << "failed creating wmi cpu telemetry thread" << std::endl;
        }

        if (cpu) {
            cpuTelemetryThread = std::jthread{ CpuTelemetry, pSvc, &pm, cpu.get() };
            pm.SetCpu(cpu);
            // sample once to populate the cap bits
            cpu->Sample();
            // determine vendor based on device name
            const auto vendor = [&] {
                const auto lowerNameRn = cpu->GetCpuName() | vi::transform(tolower);
                const std::string lowerName{ lowerNameRn.begin(), lowerNameRn.end() };
                if (rn::search(lowerName, "intel")) {
                    return PM_DEVICE_VENDOR_INTEL;
                }
                else if (rn::search(lowerName, "amd")) {
                    return PM_DEVICE_VENDOR_AMD;
                }
                else {
                    return PM_DEVICE_VENDOR_UNKNOWN;
                }
            }();
            // register cpu
            pComms->RegisterCpuDevice(vendor, cpu->GetCpuName(), cpu->GetCpuTelemetryCapBits());
        }

        while (WaitForSingleObjectEx(pSvc->GetServiceStopHandle(), INFINITE, (bool)opt.timedStop) != WAIT_OBJECT_0) {
            pm.CheckTraceSessions();
            PmSleep(500, opt.timedStop);
        }

        // Stop the PresentMon session
        pm.StopTraceSession();
    }
    catch (...) {
        LOG(ERROR) << "Exception in PMMainThread, bailing" << std::endl;
        if (pSvc) {
            pSvc->SignalServiceStop(-1);
        }
    }
}
