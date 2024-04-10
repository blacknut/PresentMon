#define NOMINMAX
#include "ConcreteMiddleware.h"
#include <cstring>
#include <string>
#include <vector>
#include <memory>
#include <cassert>
#include <cstdlib>
#include <Shlwapi.h>
#include <numeric>
#include "../../PresentMonUtils/NamedPipeHelper.h"
#include "../../PresentMonUtils/QPCUtils.h"
#include "../../PresentMonAPI2/Internal.h"
#include "../../PresentMonAPIWrapperCommon/Introspection.h"
// TODO: don't need transfer if we can somehow get the PM_ struct generation working without inheritance
// needed right now because even if we forward declare, we don't have the inheritance info
#include "../../Interprocess/source/IntrospectionTransfer.h"
#include "../../Interprocess/source/IntrospectionHelpers.h"
#include "../../Interprocess/source/IntrospectionCloneAllocators.h"
//#include "MockCommon.h"
#include "DynamicQuery.h"
#include "../../ControlLib/PresentMonPowerTelemetry.h"
#include "../../ControlLib/CpuTelemetryInfo.h"
#include "../../PresentMonService/GlobalIdentifiers.h"
#include "FrameEventQuery.h"


#define GLOG_NO_ABBREVIATED_SEVERITIES
#include <glog/logging.h>

namespace pmon::mid
{
    using namespace ipc::intro;

    static const uint32_t kMaxRespBufferSize = 4096;
	static const uint64_t kClientFrameDeltaQPCThreshold = 50000000;
	ConcreteMiddleware::ConcreteMiddleware(std::optional<std::string> pipeNameOverride, std::optional<std::string> introNsmOverride)
	{
        const auto pipeName = pipeNameOverride.transform(&std::string::c_str)
            .value_or(pmon::gid::defaultControlPipeName);

        HANDLE namedPipeHandle;
        // Try to open a named pipe; wait for it, if necessary.
        while (1) {
            namedPipeHandle = CreateFileA(
                pipeName,
                GENERIC_READ | GENERIC_WRITE,
                0,              
                NULL,           
                OPEN_EXISTING,  
                0,              
                NULL);          

            // Break if the pipe handle is valid.
            if (namedPipeHandle != INVALID_HANDLE_VALUE) {
                break;
            }

            // Exit if an error other than ERROR_PIPE_BUSY occurs.
            if (const auto hr = GetLastError(); hr != ERROR_PIPE_BUSY) {
                throw std::runtime_error{ "Service not found" };
            }

            // All pipe instances are busy, so wait for 20 seconds.
            if (!WaitNamedPipeA(pipeName, 20000)) {
                throw std::runtime_error{ "Pipe sessions full" };
            }
        }
        // The pipe connected; change to message-read mode.
        DWORD mode = PIPE_READMODE_MESSAGE;
        BOOL success = SetNamedPipeHandleState(namedPipeHandle,
            &mode,
            NULL,
            NULL);
        if (!success) {
            throw std::runtime_error{ "Pipe error" };
        }
        pNamedPipeHandle.reset(namedPipeHandle);
        clientProcessId = GetCurrentProcessId();
        // connect to the introspection nsm
        pComms = ipc::MakeMiddlewareComms(std::move(introNsmOverride));

        // Get the introspection data
        auto& ispec = GetIntrospectionRoot();
        
        uint32_t gpuAdapterId = 0;
        auto deviceView = ispec.GetDevices();
        for (auto dev : deviceView)
        {
            if (dev.GetType() == PM_DEVICE_TYPE_GRAPHICS_ADAPTER)
            {
                cachedGpuInfo.push_back({ dev.GetVendor(), dev.GetName(), dev.GetId(), gpuAdapterId, 0., 0, 0});
                gpuAdapterId++;
            }
        }
        // Update the static GPU metric data from the service
        GetStaticGpuMetrics();
        GetStaticCpuMetrics();
	}
    
    ConcreteMiddleware::~ConcreteMiddleware() = default;
    
    const PM_INTROSPECTION_ROOT* ConcreteMiddleware::GetIntrospectionData()
    {
        return pComms->GetIntrospectionRoot();
    }

    void ConcreteMiddleware::FreeIntrospectionData(const PM_INTROSPECTION_ROOT* pRoot)
    {
        free(const_cast<PM_INTROSPECTION_ROOT*>(pRoot));
    }

	void ConcreteMiddleware::Speak(char* buffer) const
	{
		strcpy_s(buffer, 256, "concrete-middle");
	}

    PM_STATUS ConcreteMiddleware::SendRequest(MemBuffer* requestBuffer) {
        DWORD bytesWritten;
        BOOL success = WriteFile(
            pNamedPipeHandle.get(),
            requestBuffer->AccessMem(),
            static_cast<DWORD>(requestBuffer->GetCurrentSize()),
            &bytesWritten,
            NULL);

        if (success && requestBuffer->GetCurrentSize() == bytesWritten) {
            return PM_STATUS::PM_STATUS_SUCCESS;
        }
        else {
            return PM_STATUS::PM_STATUS_FAILURE;
        }
    }

    PM_STATUS ConcreteMiddleware::ReadResponse(MemBuffer* responseBuffer) {
        BOOL success;
        DWORD bytesRead;
        BYTE inBuffer[kMaxRespBufferSize];
        ZeroMemory(&inBuffer, sizeof(inBuffer));

        do {
            // Read from the pipe using a nonoverlapped read
            success = ReadFile(pNamedPipeHandle.get(),
                inBuffer,
                sizeof(inBuffer),
                &bytesRead,
                NULL);

            // If the call was not successful AND there was
            // no more data to read bail out
            if (!success && GetLastError() != ERROR_MORE_DATA) {
                break;
            }

            // Either the call was successful or there was more
            // data in the pipe. In both cases add the response data
            // to the memory buffer
            responseBuffer->AddItem(inBuffer, bytesRead);
        } while (!success);  // repeat loop if ERROR_MORE_DATA

        if (success) {
            return PM_STATUS::PM_STATUS_SUCCESS;
        }
        else {
            return PM_STATUS::PM_STATUS_FAILURE;
        }
    }

    PM_STATUS ConcreteMiddleware::CallPmService(MemBuffer* requestBuffer, MemBuffer* responseBuffer)
    {
        PM_STATUS status;

        status = SendRequest(requestBuffer);
        if (status != PM_STATUS::PM_STATUS_SUCCESS) {
            return status;
        }

        status = ReadResponse(responseBuffer);
        if (status != PM_STATUS::PM_STATUS_SUCCESS) {
            return status;
        }

        return status;
    }

    PM_STATUS ConcreteMiddleware::StartStreaming(uint32_t processId)
    {
        MemBuffer requestBuffer;
        MemBuffer responseBuffer;

        NamedPipeHelper::EncodeStartStreamingRequest(&requestBuffer, clientProcessId,
            processId, nullptr);

        PM_STATUS status = CallPmService(&requestBuffer, &responseBuffer);
        if (status != PM_STATUS::PM_STATUS_SUCCESS) {
            return status;
        }

        IPMSMStartStreamResponse startStreamResponse{};

        status = NamedPipeHelper::DecodeStartStreamingResponse(
            &responseBuffer, &startStreamResponse);
        if (status != PM_STATUS::PM_STATUS_SUCCESS) {
            return status;
        }

        // Get the NSM file name from 
        std::string mapFileName(startStreamResponse.fileName);

        // Initialize client with returned mapfile name
        auto iter = presentMonStreamClients.find(processId);
        if (iter == presentMonStreamClients.end()) {
            try {
                std::unique_ptr<StreamClient> client =
                    std::make_unique<StreamClient>(std::move(mapFileName), false);
                presentMonStreamClients.emplace(processId, std::move(client));
            }
            catch (...) {
                return PM_STATUS::PM_STATUS_FAILURE;
            }
        }

        return PM_STATUS_SUCCESS;
    }
    
    PM_STATUS ConcreteMiddleware::StopStreaming(uint32_t processId)
    {
        MemBuffer requestBuffer;
        MemBuffer responseBuffer;

        NamedPipeHelper::EncodeStopStreamingRequest(&requestBuffer,
            clientProcessId,
            processId);

        PM_STATUS status = CallPmService(&requestBuffer, &responseBuffer);
        if (status != PM_STATUS::PM_STATUS_SUCCESS) {
            return status;
        }

        status = NamedPipeHelper::DecodeStopStreamingResponse(&responseBuffer);
        if (status != PM_STATUS::PM_STATUS_SUCCESS) {
            return status;
        }

        // Remove client
        auto iter = presentMonStreamClients.find(processId);
        if (iter != presentMonStreamClients.end()) {
            presentMonStreamClients.erase(std::move(iter));
        }

        return status;
    }

    void ConcreteMiddleware::GetStaticCpuMetrics()
    {
        MemBuffer requestBuffer;
        MemBuffer responseBuffer;

        NamedPipeHelper::EncodeRequestHeader(&requestBuffer, PM_ACTION::GET_STATIC_CPU_METRICS);

        PM_STATUS status = CallPmService(&requestBuffer, &responseBuffer);
        if (status != PM_STATUS::PM_STATUS_SUCCESS) {
            return;
        }

        IPMStaticCpuMetrics staticCpuMetrics{};
        status = NamedPipeHelper::DecodeStaticCpuMetricsResponse(&responseBuffer, &staticCpuMetrics);
        if (status != PM_STATUS::PM_STATUS_SUCCESS ||
            staticCpuMetrics.cpuNameLength > MAX_PM_CPU_NAME) {
            return;
        }

        auto ContainsString = [](std::string str, std::string subStr)
            {
                return std::search(str.begin(), str.end(), subStr.begin(), subStr.end(),
                    [](char c1, char c2) { return std::tolower(c1) == std::tolower(c2); }) != str.end();
            };

        std::string cpuName = staticCpuMetrics.cpuName;
        PM_DEVICE_VENDOR deviceVendor;
        if (ContainsString(cpuName, "intel"))
        {
            deviceVendor = PM_DEVICE_VENDOR_INTEL;
        }
        else if (ContainsString(cpuName, "amd"))
        {
            deviceVendor = PM_DEVICE_VENDOR_AMD;
        }
        else
        {
            deviceVendor = PM_DEVICE_VENDOR_UNKNOWN;
        }

        cachedCpuInfo.push_back(
            { 
                .deviceVendor = deviceVendor,
                .deviceName = cpuName,
                .cpuPowerLimit = staticCpuMetrics.cpuPowerLimit
            }
        );
    }

    std::string ConcreteMiddleware::GetProcessName(uint32_t processId)
    {
        HANDLE handle = NULL;
        std::string processName = "<UNKNOWN>";
        char path[MAX_PATH];
        DWORD numChars = sizeof(path);
        handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
        if (handle) {
            if (QueryFullProcessImageNameA(handle, 0, path, &numChars)) {
                processName = PathFindFileNameA(path);
            }
            CloseHandle(handle);
        }
        return processName;
    }

    const pmapi::intro::Root& mid::ConcreteMiddleware::GetIntrospectionRoot()
    {
        if (!pIntroRoot) {
            pIntroRoot = std::make_unique<pmapi::intro::Root>(GetIntrospectionData(), [this](auto p){FreeIntrospectionData(p);});
        }
        return *pIntroRoot;
    }

    PM_STATUS ConcreteMiddleware::SetTelemetryPollingPeriod(uint32_t deviceId, uint32_t timeMs)
    {
        MemBuffer requestBuffer;
        MemBuffer responseBuffer;

        NamedPipeHelper::EncodeGeneralSetActionRequest(
            PM_ACTION::SET_GPU_TELEMETRY_PERIOD, &requestBuffer, timeMs);

        PM_STATUS status = CallPmService(&requestBuffer, &responseBuffer);
        if (status != PM_STATUS::PM_STATUS_SUCCESS) {
            return status;
        }

        status = NamedPipeHelper::DecodeGeneralSetActionResponse(
            PM_ACTION::SET_GPU_TELEMETRY_PERIOD, &responseBuffer);
        return status;
    }

    PM_DYNAMIC_QUERY* ConcreteMiddleware::RegisterDynamicQuery(std::span<PM_QUERY_ELEMENT> queryElements, double windowSizeMs, double metricOffsetMs)
    { 
        // get introspection data for reference
        // TODO: cache this data so it's not required to be generated every time
        auto& ispec = GetIntrospectionRoot();

        // make the query object that will be managed by the handle
        auto pQuery = std::make_unique<PM_DYNAMIC_QUERY>();
        std::optional<uint32_t> cachedGpuInfoIndex;

        uint64_t offset = 0u;
        for (auto& qe : queryElements)
        {
            // A device of zero is NOT a graphics adapter.
            if (qe.deviceId != 0)
            {
                // If we have already set a device id in this query, check to
                // see if it's the same device id as previously set. Currently
                // we don't support querying multiple gpu devices in the one
                // query
                if (cachedGpuInfoIndex.has_value())
                {
                    if (cachedGpuInfo[cachedGpuInfoIndex.value()].deviceId != qe.deviceId)
                    {
                        throw std::runtime_error{ "Multiple GPU devices not allowed in single query" };
                    }
                }
                else
                {
                    // Go through the cached Gpus and see which device the client
                    // wants
                    for (int i = 0; i < cachedGpuInfo.size(); i++)
                    {
                        if (qe.deviceId == cachedGpuInfo[i].deviceId)
                        {
                            cachedGpuInfoIndex = i;
                            break;
                        }
                    }
                }
            }

            auto metricView = ispec.FindMetric(qe.metric);
            switch (qe.metric) {
            case PM_METRIC_APPLICATION:
            case PM_METRIC_SWAP_CHAIN_ADDRESS:
            case PM_METRIC_PRESENT_MODE:
            case PM_METRIC_PRESENT_RUNTIME:
            case PM_METRIC_PRESENT_FLAGS:
            case PM_METRIC_SYNC_INTERVAL:
            case PM_METRIC_ALLOWS_TEARING:
            case PM_METRIC_CPU_FRAME_QPC:
            case PM_METRIC_GPU_LATENCY:
            case PM_METRIC_GPU_TIME:
            case PM_METRIC_GPU_WAIT:
            case PM_METRIC_GPU_BUSY:
            case PM_METRIC_DISPLAY_LATENCY:
            case PM_METRIC_CLICK_TO_PHOTON_LATENCY:
            case PM_METRIC_PRESENTED_FPS:
            case PM_METRIC_DISPLAYED_FPS:
            case PM_METRIC_DROPPED_FRAMES:
            case PM_METRIC_FRAME_TIME:
            case PM_METRIC_CPU_BUSY:
            case PM_METRIC_CPU_WAIT:
            case PM_METRIC_DISPLAYED_TIME:
                pQuery->accumFpsData = true;
                break;
            case PM_METRIC_GPU_POWER:
                pQuery->accumGpuBits.set(static_cast<size_t>(GpuTelemetryCapBits::gpu_power));
                break;
            case PM_METRIC_GPU_VOLTAGE:
                pQuery->accumGpuBits.set(static_cast<size_t>(GpuTelemetryCapBits::gpu_voltage));
                break;
            case PM_METRIC_GPU_FREQUENCY:
                pQuery->accumGpuBits.set(static_cast<size_t>(GpuTelemetryCapBits::gpu_frequency));
                break;
            case PM_METRIC_GPU_TEMPERATURE:
                pQuery->accumGpuBits.set(static_cast<size_t>(GpuTelemetryCapBits::gpu_temperature));
                break;
            case PM_METRIC_GPU_UTILIZATION:
                pQuery->accumGpuBits.set(static_cast<size_t>(GpuTelemetryCapBits::gpu_utilization));
                break;
            case PM_METRIC_GPU_RENDER_COMPUTE_UTILIZATION:
                pQuery->accumGpuBits.set(static_cast<size_t>(GpuTelemetryCapBits::gpu_render_compute_utilization));
                break;
            case PM_METRIC_GPU_MEDIA_UTILIZATION:
                pQuery->accumGpuBits.set(static_cast<size_t>(GpuTelemetryCapBits::gpu_media_utilization));
                break;
            case PM_METRIC_GPU_MEM_POWER:
                pQuery->accumGpuBits.set(static_cast<size_t>(GpuTelemetryCapBits::vram_power));
                break;
            case PM_METRIC_GPU_MEM_VOLTAGE:
                pQuery->accumGpuBits.set(static_cast<size_t>(GpuTelemetryCapBits::vram_voltage));
                break;
            case PM_METRIC_GPU_MEM_FREQUENCY:
                pQuery->accumGpuBits.set(static_cast<size_t>(GpuTelemetryCapBits::vram_frequency));
                break;
            case PM_METRIC_GPU_MEM_EFFECTIVE_FREQUENCY:
                pQuery->accumGpuBits.set(static_cast<size_t>(GpuTelemetryCapBits::vram_effective_frequency));
                break;
            case PM_METRIC_GPU_MEM_TEMPERATURE:
                pQuery->accumGpuBits.set(static_cast<size_t>(GpuTelemetryCapBits::vram_temperature));
                break;
            case PM_METRIC_GPU_MEM_USED:
                pQuery->accumGpuBits.set(static_cast<size_t>(GpuTelemetryCapBits::gpu_mem_used));
                break;
            case PM_METRIC_GPU_MEM_UTILIZATION:
                // Gpu mem utilization is derived from mem size and mem used.
                pQuery->accumGpuBits.set(static_cast<size_t>(GpuTelemetryCapBits::gpu_mem_used));
                pQuery->accumGpuBits.set(static_cast<size_t>(GpuTelemetryCapBits::gpu_mem_size));
                break;
            case PM_METRIC_GPU_MEM_WRITE_BANDWIDTH:
                pQuery->accumGpuBits.set(static_cast<size_t>(GpuTelemetryCapBits::gpu_mem_write_bandwidth));
                break;
            case PM_METRIC_GPU_MEM_READ_BANDWIDTH:
                pQuery->accumGpuBits.set(static_cast<size_t>(GpuTelemetryCapBits::gpu_mem_read_bandwidth));
                break;
            case PM_METRIC_GPU_POWER_LIMITED:
                pQuery->accumGpuBits.set(static_cast<size_t>(GpuTelemetryCapBits::gpu_power_limited));
                break;
            case PM_METRIC_GPU_TEMPERATURE_LIMITED:
                pQuery->accumGpuBits.set(static_cast<size_t>(GpuTelemetryCapBits::gpu_temperature_limited));
                break;
            case PM_METRIC_GPU_CURRENT_LIMITED:
                pQuery->accumGpuBits.set(static_cast<size_t>(GpuTelemetryCapBits::gpu_current_limited));
                break;
            case PM_METRIC_GPU_VOLTAGE_LIMITED:
                pQuery->accumGpuBits.set(static_cast<size_t>(GpuTelemetryCapBits::gpu_voltage_limited));
                break;
            case PM_METRIC_GPU_UTILIZATION_LIMITED:
                pQuery->accumGpuBits.set(static_cast<size_t>(GpuTelemetryCapBits::gpu_utilization_limited));
                break;
            case PM_METRIC_GPU_MEM_POWER_LIMITED:
                pQuery->accumGpuBits.set(static_cast<size_t>(GpuTelemetryCapBits::vram_power_limited));
                break;
            case PM_METRIC_GPU_MEM_TEMPERATURE_LIMITED:
                pQuery->accumGpuBits.set(static_cast<size_t>(GpuTelemetryCapBits::vram_temperature_limited));
                break;
            case PM_METRIC_GPU_MEM_CURRENT_LIMITED:
                pQuery->accumGpuBits.set(static_cast<size_t>(GpuTelemetryCapBits::vram_current_limited));
                break;
            case PM_METRIC_GPU_MEM_VOLTAGE_LIMITED:
                pQuery->accumGpuBits.set(static_cast<size_t>(GpuTelemetryCapBits::vram_voltage_limited));
                break;
            case PM_METRIC_GPU_MEM_UTILIZATION_LIMITED:
                pQuery->accumGpuBits.set(static_cast<size_t>(GpuTelemetryCapBits::vram_utilization_limited));
                break;
            case PM_METRIC_GPU_FAN_SPEED:
                switch (qe.arrayIndex)
                {
                case 0:
                    pQuery->accumGpuBits.set(static_cast<size_t>(GpuTelemetryCapBits::fan_speed_0));
                    break;
                case 1:
                    pQuery->accumGpuBits.set(static_cast<size_t>(GpuTelemetryCapBits::fan_speed_1));
                    break;
                case 2:
                    pQuery->accumGpuBits.set(static_cast<size_t>(GpuTelemetryCapBits::fan_speed_2));
                    break;
                case 3:
                    pQuery->accumGpuBits.set(static_cast<size_t>(GpuTelemetryCapBits::fan_speed_3));
                    break;
                case 4:
                    pQuery->accumGpuBits.set(static_cast<size_t>(GpuTelemetryCapBits::fan_speed_4));
                    break;
                }
                break;
            case PM_METRIC_CPU_UTILIZATION:
                pQuery->accumCpuBits.set(static_cast<size_t>(CpuTelemetryCapBits::cpu_utilization));
                break;
            case PM_METRIC_CPU_POWER:
                pQuery->accumCpuBits.set(static_cast<size_t>(CpuTelemetryCapBits::cpu_power));
                break;
            case PM_METRIC_CPU_TEMPERATURE:
                pQuery->accumCpuBits.set(static_cast<size_t>(CpuTelemetryCapBits::cpu_temperature));
                break;
            case PM_METRIC_CPU_FREQUENCY:
                pQuery->accumCpuBits.set(static_cast<size_t>(CpuTelemetryCapBits::cpu_frequency));
                break;
            case PM_METRIC_CPU_CORE_UTILITY:
                //pQuery->accumCpuBits.set(static_cast<size_t>(CpuTelemetryCapBits::cpu_power));
                break;
            default:
                break;
            }

            qe.dataOffset = offset;
            qe.dataSize = GetDataTypeSize(metricView.GetDataTypeInfo().GetPolledType());
            offset += qe.dataSize;
        }

        pQuery->metricOffsetMs = metricOffsetMs;
        pQuery->windowSizeMs = windowSizeMs;
        pQuery->elements = std::vector<PM_QUERY_ELEMENT>{ queryElements.begin(), queryElements.end() };
        pQuery->queryCacheSize = pQuery->elements[std::size(pQuery->elements) - 1].dataOffset + pQuery->elements[std::size(pQuery->elements) - 1].dataSize;
        if (cachedGpuInfoIndex.has_value())
        {
            pQuery->cachedGpuInfoIndex = cachedGpuInfoIndex.value();
        }

        return pQuery.release();
    }

namespace {

struct FakePMTraceSession {
    double mMilliSecondsPerQpc = 0.0;

    double QpcDeltaToMilliSeconds(uint64_t qpcDelta) const
    {
        return mMilliSecondsPerQpc * qpcDelta;
    }

    double QpcDeltaToUnsignedMilliSeconds(uint64_t qpcFrom, uint64_t qpcTo) const
    {
        return qpcFrom == 0 || qpcTo <= qpcFrom ? 0.0 : QpcDeltaToMilliSeconds(qpcTo - qpcFrom);
    }

    double QpcDeltaToMilliSeconds(uint64_t qpcFrom, uint64_t qpcTo) const
    {
        return qpcFrom == 0 || qpcTo == 0 || qpcFrom == qpcTo ? 0.0 :
            qpcTo > qpcFrom ? QpcDeltaToMilliSeconds(qpcTo - qpcFrom) :
                             -QpcDeltaToMilliSeconds(qpcFrom - qpcTo);
    }
};

struct FrameMetrics {
    uint64_t mCPUFrameQPC;
    double mCPUDuration;
    double mCPUFramePacingStall;
    double mGPULatency;
    double mGPUDuration;
    double mGPUBusy;
    double mVideoBusy;
    double mDisplayLatency;
    double mDisplayDuration;
    double mInputLatency;
};

void UpdateChain(
    fpsSwapChainData* chain,
    PmNsmPresentEvent const& p)
{
    chain->mCPUFrameQPC         = p.PresentStartTime + p.TimeInPresent;
    chain->mPresentMode         = p.PresentMode;
    chain->mPresentRuntime      = p.Runtime;
    chain->mPresentSyncInterval = p.SyncInterval;
    chain->mPresentFlags        = p.PresentFlags;
    chain->mPresentInfoValid    = true;
    chain->applicationName      = p.application;
    chain->allows_tearing       = p.SupportsTearing;

    if (p.FinalState == PresentResult::Presented) {
        if (chain->display_count == 0) {
            chain->display_0_screen_time = p.ScreenTime;
        }
        chain->display_n_screen_time = p.ScreenTime;
        chain->display_count += 1;
    }
}

void ReportMetrics(
    FakePMTraceSession const& pmSession,
    fpsSwapChainData* chain,
    PmNsmPresentEvent const& p,
    PmNsmPresentEvent const* nextDisplayedPresent)
{
    // PB = PresentStartTime
    // PE = PresentEndTime
    // D  = ScreenTime
    // F  = CPUFrameTime/CPUDuration
    // S  = CPUFramePacingStall
    // D  = DisplayDuration
    //
    // Previous PresentEvent:  PB--PE----D
    // p:                          |        PB--PE----D
    // Next PresentEvent(s):       |        |   |   PB--PE
    //                             |        |   |     |     PB--PE
    // nextDisplayedPresent:       |        |   |     |             PB--PE----D
    //                             |        |   |     |                       |
    // CPUFrameTime/CPUDuration:   |------->|   |     |                       |
    // CPUFramePacingStall:                 |-->|     |                       |
    // DisplayLatency:             |----------------->|                       |
    // DisplayDuration:                               |---------------------->|

    bool displayed = p.FinalState == PresentResult::Presented;

    FrameMetrics metrics;
    metrics.mCPUFrameQPC         = chain->mCPUFrameQPC;
    metrics.mCPUDuration         = pmSession.QpcDeltaToUnsignedMilliSeconds(chain->mCPUFrameQPC, p.PresentStartTime);
    metrics.mCPUFramePacingStall = pmSession.QpcDeltaToMilliSeconds(p.TimeInPresent);
    metrics.mGPULatency          = pmSession.QpcDeltaToUnsignedMilliSeconds(chain->mCPUFrameQPC, p.GPUStartTime);
    metrics.mGPUDuration         = pmSession.QpcDeltaToUnsignedMilliSeconds(p.GPUStartTime, p.ReadyTime);
    metrics.mGPUBusy             = pmSession.QpcDeltaToMilliSeconds(p.GPUDuration);
    metrics.mVideoBusy           = pmSession.QpcDeltaToMilliSeconds(p.GPUVideoDuration);
    metrics.mDisplayLatency      = !displayed       ? 0 : pmSession.QpcDeltaToUnsignedMilliSeconds(chain->mCPUFrameQPC, p.ScreenTime);
    metrics.mDisplayDuration     = !displayed       ? 0 : pmSession.QpcDeltaToUnsignedMilliSeconds(p.ScreenTime, nextDisplayedPresent->ScreenTime);
    metrics.mInputLatency        = p.InputTime == 0 ? 0 : pmSession.QpcDeltaToUnsignedMilliSeconds(p.InputTime, p.ScreenTime);

    UpdateChain(chain, p);

    chain->CPUFrameQPC        .push_back(metrics.mCPUFrameQPC);
    chain->CPUDuration        .push_back(metrics.mCPUDuration);
    chain->CPUFramePacingStall.push_back(metrics.mCPUFramePacingStall);
    chain->GPULatency         .push_back(metrics.mGPULatency);
    chain->GPUWait            .push_back(std::max(0.0, metrics.mGPUDuration - metrics.mGPUBusy));
    chain->GPUBusy            .push_back(metrics.mGPUBusy);
    chain->GPUDuration        .push_back(metrics.mGPUDuration);
    chain->DisplayLatency     .push_back(metrics.mDisplayLatency);
    chain->DisplayDuration    .push_back(metrics.mDisplayDuration);
    chain->InputLatency       .push_back(metrics.mInputLatency);
}

}

    void ConcreteMiddleware::PollDynamicQuery(const PM_DYNAMIC_QUERY* pQuery, uint32_t processId, uint8_t* pBlob, uint32_t* numSwapChains)
    {
        std::unordered_map<uint64_t, fpsSwapChainData> swapChainData;
        std::unordered_map<PM_METRIC, MetricInfo> metricInfo;
        bool allMetricsCalculated = false;
        bool fpsMetricsCalculated = false;

        if (*numSwapChains == 0) {
            return;
        }

        if (pQuery->cachedGpuInfoIndex.has_value())
        {
            if (pQuery->cachedGpuInfoIndex.value() != currentGpuInfoIndex)
            {
                // Set the adapter id 
                SetActiveGraphicsAdapter(cachedGpuInfo[pQuery->cachedGpuInfoIndex.value()].deviceId);
                // Set the current index to the queried one
                currentGpuInfoIndex = pQuery->cachedGpuInfoIndex.value();
            }
        }

        auto iter = presentMonStreamClients.find(processId);
        if (iter == presentMonStreamClients.end()) {
            return;
        }

        // Get the named shared memory associated with the stream client
        StreamClient* client = iter->second.get();
        auto nsm_view = client->GetNamedSharedMemView();
        auto nsm_hdr = nsm_view->GetHeader();
        if (!nsm_hdr->process_active) {
            // TODO: Do we want to inform the client if the server has destroyed the
            // named shared memory?
            // Server destroyed the named shared memory due to process exit. Destroy the
            // mapped view from client side.
            //StopStreamProcess(process_id);
            //return PM_STATUS::PM_STATUS_PROCESS_NOT_EXIST;
            return;
        }

        uint64_t index = 0;
        double adjusted_window_size_in_ms = pQuery->windowSizeMs;
        auto result = queryFrameDataDeltas.emplace(std::pair(std::pair(pQuery, processId), uint64_t()));
        auto queryToFrameDataDelta = &result.first->second;
        
        PmNsmFrameData* frame_data = GetFrameDataStart(client, index, SecondsDeltaToQpc(pQuery->metricOffsetMs/1000., client->GetQpcFrequency()), *queryToFrameDataDelta, adjusted_window_size_in_ms);
        if (frame_data == nullptr) {
            CopyMetricCacheToBlob(pQuery, processId, pBlob);
            return;
        }

        // Calculate the end qpc based on the current frame's qpc and
        // requested window size coverted to a qpc
        // then loop from the most recent frame data until we either run out of data or
        // we meet the window size requirements sent in by the client
        uint64_t end_qpc =
            frame_data->present_event.PresentStartTime -
            SecondsDeltaToQpc(adjusted_window_size_in_ms/1000., client->GetQpcFrequency());

        std::vector<PmNsmFrameData*> frames;
        while (frame_data->present_event.PresentStartTime > end_qpc) {
            frames.push_back(frame_data);

            // Get the index of the next frame
            if (DecrementIndex(nsm_view, index) == false) {
                // We have run out of data to process, time to go
                break;
            }
            frame_data = client->ReadFrameByIdx(index);
            if (frame_data == nullptr) {
                break;
            }
        }

        FakePMTraceSession pmSession;
        pmSession.mMilliSecondsPerQpc = 1000.0 / client->GetQpcFrequency().QuadPart;

        for (const auto& frame_data : frames | std::views::reverse) {
            if (pQuery->accumFpsData)
            {
                auto result = swapChainData.emplace(
                    frame_data->present_event.SwapChainAddress, fpsSwapChainData());
                auto swap_chain = &result.first->second;

                auto presentEvent = &frame_data->present_event;
                auto chain = swap_chain;
                if (!chain->mPresentInfoValid) {
                    UpdateChain(chain, *presentEvent);
                } else
                if (presentEvent->FinalState == PresentResult::Presented) {
                    for (auto const& pp : chain->mPendingPresents) {
                        ReportMetrics(pmSession, chain, pp, presentEvent);
                    }
                    chain->mPendingPresents.clear();
                    chain->mPendingPresents.push_back(*presentEvent);
                } else {
                    if (chain->mPendingPresents.empty()) {
                        ReportMetrics(pmSession, chain, *presentEvent, nullptr);
                    } else {
                        chain->mPendingPresents.push_back(*presentEvent);
                    }
                }
            }

            for (size_t i = 0; i < pQuery->accumGpuBits.size(); ++i) {
                if (pQuery->accumGpuBits[i])
                {
                    GetGpuMetricData(i, frame_data->power_telemetry, metricInfo);
                }
            }

            for (size_t i = 0; i < pQuery->accumCpuBits.size(); ++i) {
                if (pQuery->accumCpuBits[i])
                {
                    GetCpuMetricData(i, frame_data->cpu_telemetry, metricInfo);
                }
            }
        }

        CalculateMetrics(pQuery, processId, pBlob, numSwapChains, client->GetQpcFrequency(), swapChainData, metricInfo);
    }

    std::optional<size_t> ConcreteMiddleware::GetCachedGpuInfoIndex(uint32_t deviceId)
    {
        for (std::size_t i = 0; i < cachedGpuInfo.size(); ++i)
        {
            if (cachedGpuInfo[i].deviceId == deviceId)
            {
                if (cachedGpuInfo[i].adapterId.has_value())
                    return cachedGpuInfo[i].adapterId.value();
                else {
                    return std::nullopt;
                }
            }
        }

        return std::nullopt;
    }

    void ConcreteMiddleware::CopyStaticMetricData(PM_METRIC metric, uint32_t deviceId, uint8_t* pBlob, uint64_t blobOffset, size_t sizeInBytes)
    {
        switch (metric)
        {
        case PM_METRIC_CPU_NAME:
        {
            strcpy_s(reinterpret_cast<char*>(&pBlob[blobOffset]), sizeInBytes, cachedCpuInfo[0].deviceName.c_str());
        }
            break;
        case PM_METRIC_CPU_VENDOR:
        {
            auto& output = reinterpret_cast<PM_DEVICE_VENDOR&>(pBlob[blobOffset]);
            output = cachedCpuInfo[0].deviceVendor;
        }
            break;
        case PM_METRIC_CPU_POWER_LIMIT:
        {
            auto& output = reinterpret_cast<double&>(pBlob[blobOffset]);
            output = cachedCpuInfo[0].cpuPowerLimit.has_value() ? cachedCpuInfo[0].cpuPowerLimit.value() : 0.;
        }
            break;
        case PM_METRIC_GPU_NAME:
        {
            auto index = GetCachedGpuInfoIndex(deviceId);
            if (index.has_value())
            {
                strcpy_s(reinterpret_cast<char*>(&pBlob[blobOffset]), sizeInBytes, cachedGpuInfo[index.value()].deviceName.c_str());
            }
        }
            break;
        case PM_METRIC_GPU_VENDOR:
        {
            auto index = GetCachedGpuInfoIndex(deviceId);
            auto& output = reinterpret_cast<PM_DEVICE_VENDOR&>(pBlob[blobOffset]);
            output = index.has_value() ? cachedGpuInfo[index.value()].deviceVendor : PM_DEVICE_VENDOR_UNKNOWN;
        }
            break;
        case PM_METRIC_GPU_MEM_MAX_BANDWIDTH:
        {
            auto& output = reinterpret_cast<double&>(pBlob[blobOffset]);
            auto index = GetCachedGpuInfoIndex(deviceId);
            if (index.has_value())
            {
                output = cachedGpuInfo[index.value()].gpuMemoryMaxBandwidth.has_value() ? 
                    cachedGpuInfo[index.value()].gpuMemoryMaxBandwidth.value() : 0.;
            }
            else
            {
                output = 0.;
            }
        }
            break;
        case PM_METRIC_GPU_MEM_SIZE:
        {
            auto& output = reinterpret_cast<double&>(pBlob[blobOffset]);
            auto index = GetCachedGpuInfoIndex(deviceId);
            if (index.has_value())
            {
                output = cachedGpuInfo[index.value()].gpuMemorySize.has_value() ?
                    static_cast<double>(cachedGpuInfo[index.value()].gpuMemorySize.value()) : 0.;
            }
            else
            {
                output = 0.;
            }
        }
            break;
        case PM_METRIC_GPU_SUSTAINED_POWER_LIMIT:
        {
            auto& output = reinterpret_cast<double&>(pBlob[blobOffset]);
            auto index = GetCachedGpuInfoIndex(deviceId);
            if (index.has_value())
            {
                output = cachedGpuInfo[index.value()].gpuSustainedPowerLimit.has_value() ?
                    cachedGpuInfo[index.value()].gpuSustainedPowerLimit.value() : 0.f;
            }
            else
            {
                output = 0.f;
            }
        }
            break;
        default:
            break;
        }
        return;
    }

    void ConcreteMiddleware::PollStaticQuery(const PM_QUERY_ELEMENT& element, uint32_t processId, uint8_t* pBlob)
    {
        auto& ispec = GetIntrospectionRoot();
        auto metricView = ispec.FindMetric(element.metric);
        if (metricView.GetType() != int(PM_METRIC_TYPE_STATIC)) {
            throw std::runtime_error{ "dynamic metric in static query poll" };
        }

        auto elementSize = GetDataTypeSize(metricView.GetDataTypeInfo().GetPolledType());

        CopyStaticMetricData(element.metric, element.deviceId, pBlob, 0, elementSize);

        return;
    }

    PM_FRAME_QUERY* mid::ConcreteMiddleware::RegisterFrameEventQuery(std::span<PM_QUERY_ELEMENT> queryElements, uint32_t& blobSize)
    {
        const auto pQuery = new PM_FRAME_QUERY{ queryElements };
        blobSize = (uint32_t)pQuery->GetBlobSize();
        return pQuery;
    }

    void mid::ConcreteMiddleware::FreeFrameEventQuery(const PM_FRAME_QUERY* pQuery)
    {
        delete const_cast<PM_FRAME_QUERY*>(pQuery);
    }

    void mid::ConcreteMiddleware::ConsumeFrameEvents(const PM_FRAME_QUERY* pQuery, uint32_t processId, uint8_t* pBlob, uint32_t& numFrames)
    {
        PM_STATUS status = PM_STATUS::PM_STATUS_SUCCESS;

        const auto frames_to_copy = numFrames;
        // We have saved off the number of frames to copy, now set
        // to zero in case we error out along the way BEFORE we
        // copy frames into the buffer. If a successful copy occurs
        // we'll set to actual number copied.
        uint32_t frames_copied = 0;
        numFrames = 0;

        StreamClient* pShmClient = nullptr;
        try {
            pShmClient = presentMonStreamClients.at(processId).get();
        }
        catch (...) {
            LOG(INFO)
                << "Stream client for process " << processId
                << " doesn't exist. Please call pmStartStream to initialize the "
                "client.";
            throw std::runtime_error{ "Failed to find stream for pid in ConsumeFrameEvents" };
        }

        const auto nsm_view = pShmClient->GetNamedSharedMemView();
        const auto nsm_hdr = nsm_view->GetHeader();
        if (!nsm_hdr->process_active) {
            StopStreaming(processId);
            throw std::runtime_error{ "Process died cannot consume frame events" };
        }

        const auto last_frame_idx = pShmClient->GetLatestFrameIndex();
        if (last_frame_idx == UINT_MAX) {
            // There are no frames available, no error frames copied = 0
            return;
        }

        // make sure active device is the one referenced in this query
        if (auto devId = pQuery->GetReferencedDevice()) {
            SetActiveGraphicsAdapter(*devId);
        }

        // context transmits various data that applies to each gather command in the query
        PM_FRAME_QUERY::Context ctx{ nsm_hdr->start_qpc, pShmClient->GetQpcFrequency().QuadPart };

        for (uint32_t i = 0; i < frames_to_copy; i++) {
            const PmNsmFrameData* pNsmFrameData = nullptr;
            const auto status = pShmClient->ConsumePtrToNextNsmFrameData(&pNsmFrameData);
            if (status != PM_STATUS::PM_STATUS_SUCCESS) {
                throw std::runtime_error{ "Error while trying to get frame data from shared memory" };
            }
            if (!pNsmFrameData) {
                break;
            }

            // if we make it here, we have a ptr to frame data in nsm, time to gather to blob
            ctx.UpdateSourceData(pNsmFrameData,
                pShmClient->PeekNextDisplayedFrame(),
                pShmClient->PeekPreviousFrame());
            pQuery->GatherToBlob(ctx, pBlob);
            pBlob += pQuery->GetBlobSize();
            frames_copied++;
        }
        // Set to the actual number of frames copied
        numFrames = frames_copied;
    }

    void ConcreteMiddleware::CalculateFpsMetric(fpsSwapChainData& swapChain, const PM_QUERY_ELEMENT& element, uint8_t* pBlob, LARGE_INTEGER qpcFrequency)
    {
        auto& output = reinterpret_cast<double&>(pBlob[element.dataOffset]);

        switch (element.metric)
        {
        case PM_METRIC_APPLICATION:
            strcpy_s(reinterpret_cast<char*>(&pBlob[element.dataOffset]), 260, swapChain.applicationName.c_str());
            break;
        case PM_METRIC_PRESENT_MODE:
            reinterpret_cast<PM_PRESENT_MODE&>(pBlob[element.dataOffset]) = (PM_PRESENT_MODE)swapChain.mPresentMode;
            break;
        case PM_METRIC_PRESENT_RUNTIME:
            reinterpret_cast<PM_GRAPHICS_RUNTIME&>(pBlob[element.dataOffset]) = (PM_GRAPHICS_RUNTIME)swapChain.mPresentRuntime;
            break;
        case PM_METRIC_PRESENT_FLAGS:
            reinterpret_cast<uint32_t&>(pBlob[element.dataOffset]) = swapChain.mPresentFlags;
            break;
        case PM_METRIC_SYNC_INTERVAL:
            reinterpret_cast<uint32_t&>(pBlob[element.dataOffset]) = swapChain.mPresentSyncInterval;
            break;
        case PM_METRIC_ALLOWS_TEARING:
            reinterpret_cast<bool&>(pBlob[element.dataOffset]) = swapChain.allows_tearing;
            break;
        // no statistics on PM_METRIC_FRAME_QPC
        case PM_METRIC_GPU_LATENCY:
            CalculateMetric(output, swapChain.GPULatency, element.stat);
            break;
        case PM_METRIC_GPU_WAIT:
            CalculateMetric(output, swapChain.GPUWait, element.stat);
            break;
        case PM_METRIC_GPU_BUSY:
            CalculateMetric(output, swapChain.GPUBusy, element.stat);
            break;
        case PM_METRIC_DISPLAY_LATENCY:
        {
            std::vector<double> display_latency;
            display_latency.reserve(swapChain.DisplayLatency.size());
            for (size_t i = 0; i < swapChain.DisplayLatency.size(); ++i) {
                if (swapChain.DisplayLatency[i] != 0.0) {
                    display_latency.push_back(swapChain.DisplayLatency[i]);
                }
            }
            CalculateMetric(output, display_latency, element.stat);
        }
            break;
        case PM_METRIC_DISPLAYED_TIME:
        {
            std::vector<double> display_duration;
            display_duration.reserve(swapChain.DisplayDuration.size());
            for (size_t i = 0; i < swapChain.DisplayDuration.size(); ++i) {
                if (swapChain.DisplayDuration[i] != 0.0) {
                    display_duration.push_back(swapChain.DisplayDuration[i]);
                }
            }
            CalculateMetric(output, display_duration, element.stat);
        }
            break;
        case PM_METRIC_CLICK_TO_PHOTON_LATENCY:
            CalculateMetric(output, swapChain.InputLatency, element.stat);
            break;

        case PM_METRIC_PRESENTED_FPS:
        {
            std::vector<double> frameTimes(swapChain.CPUDuration.size());
            for (size_t i = 0; i < swapChain.CPUDuration.size(); ++i) {
                frameTimes[i] = swapChain.CPUDuration[i] + swapChain.CPUFramePacingStall[i];
            }
            switch (element.stat)
            {
            case PM_STAT_PERCENTILE_99:
                CalculateMetric(output, frameTimes, PM_STAT_PERCENTILE_01);
                break;
            case PM_STAT_PERCENTILE_95:
                CalculateMetric(output, frameTimes, PM_STAT_PERCENTILE_05);
                break;
            case PM_STAT_PERCENTILE_90:
                CalculateMetric(output, frameTimes, PM_STAT_PERCENTILE_10);
                break;
            case PM_STAT_PERCENTILE_10:
                CalculateMetric(output, frameTimes, PM_STAT_PERCENTILE_90);
                break;
            case PM_STAT_PERCENTILE_05:
                CalculateMetric(output, frameTimes, PM_STAT_PERCENTILE_95);
                break;
            case PM_STAT_PERCENTILE_01:
                CalculateMetric(output, frameTimes, PM_STAT_PERCENTILE_99);
                break;
            case PM_STAT_MAX:
                CalculateMetric(output, frameTimes, PM_STAT_MIN);
                break;
            case PM_STAT_MIN:
                CalculateMetric(output, frameTimes, PM_STAT_MAX);
                break;
            default:
                CalculateMetric(output, frameTimes, element.stat);
            }
            // Convert to FPS
            output = 1000.0 / output;
        }
            break;
        case PM_METRIC_DISPLAYED_FPS:
        {
            std::vector<double> displayed_fps;
            displayed_fps.reserve(swapChain.DisplayDuration.size());
            for (size_t i = 0; i < swapChain.DisplayDuration.size(); ++i) {
                if (swapChain.DisplayDuration[i] != 0.0) {
                    displayed_fps.push_back(1000.0 / swapChain.DisplayDuration[i]);
                }
            }
            CalculateMetric(output, displayed_fps, element.stat);
        }
            break;
        case PM_METRIC_DROPPED_FRAMES:
        {
            std::vector<double> dropped(swapChain.DisplayDuration.size());
            for (size_t i = 0; i < swapChain.DisplayDuration.size(); ++i) {
                dropped[i] = swapChain.DisplayDuration[i] == 0.0 ? 1.0 : 0.0;
            }
            CalculateMetric(output, dropped, element.stat);
        }
            break;
        case PM_METRIC_FRAME_TIME:
        {
            std::vector<double> frame_times(swapChain.CPUDuration.size());
            for (size_t i = 0; i < swapChain.CPUDuration.size(); ++i) {
                frame_times[i] = swapChain.CPUDuration[i] + swapChain.CPUFramePacingStall[i];
            }
            CalculateMetric(output, frame_times, element.stat);
        }
            break;
        case PM_METRIC_CPU_BUSY:
            CalculateMetric(output, swapChain.CPUDuration, element.stat);
            break;
        case PM_METRIC_CPU_WAIT:
            CalculateMetric(output, swapChain.CPUFramePacingStall, element.stat);
            break;
        case PM_METRIC_GPU_TIME:
            CalculateMetric(output, swapChain.GPUDuration, element.stat);
            break;
        default:
            output = 0.;
            break;
        }
    }

    void ConcreteMiddleware::CalculateGpuCpuMetric(std::unordered_map<PM_METRIC, MetricInfo>& metricInfo, const PM_QUERY_ELEMENT& element, uint8_t* pBlob)
    {
        auto& output = reinterpret_cast<double&>(pBlob[element.dataOffset]);
        output = 0.;

        auto it = metricInfo.find(element.metric);
        if (it != metricInfo.end())
        {
            MetricInfo& mi = it->second;
            auto it2 = mi.data.find(element.arrayIndex);
            if (it2 != mi.data.end())
            {
                CalculateMetric(output, it2->second, element.stat);
            }
            
        }
        return;
    }

    void ConcreteMiddleware::CalculateMetric(double& pBlob, std::vector<double>& inData, PM_STAT stat)
    {
        auto& output = reinterpret_cast<double&>(pBlob);
        output = 0.;
        if (inData.size() > 1) {
            if (stat == PM_STAT_AVG)
            {
                for (auto& element : inData) {
                    output += element;
                }
                output /= inData.size();
                return;
            }
            else if (stat == PM_STAT_NON_ZERO_AVG)
            {
                std::vector<double> non_zero_data;
                std::copy_if(inData.begin(), inData.end(), std::back_inserter(non_zero_data),
                    [](double value) { return value != 0.0; });
                output = !non_zero_data.empty() ? std::accumulate(non_zero_data.begin(), non_zero_data.end(), 0.0) / non_zero_data.size() : 0.0;
            }
            else if (stat == PM_STAT_MID_POINT)
            {
                size_t middle_index = inData.size() / 2;
                output = inData[middle_index];
                return;
            }
            else {
                std::sort(inData.begin(), inData.end());
                switch (stat)
                {
                case PM_STAT_MIN:
                    output = inData[0];
                    break;
                case PM_STAT_MAX:
                    output = inData[inData.size() - 1];
                    break;
                case PM_STAT_PERCENTILE_99:
                    output = GetPercentile(inData, 0.01);
                    break;
                case PM_STAT_PERCENTILE_95:
                    output = GetPercentile(inData, 0.05);
                    break;
                case PM_STAT_PERCENTILE_90:
                    output = GetPercentile(inData, 0.10);
                    break;
                case PM_STAT_PERCENTILE_01:
                    output = GetPercentile(inData, 0.99);
                    break;
                case PM_STAT_PERCENTILE_05:
                    output = GetPercentile(inData, 0.95);
                    break;
                case PM_STAT_PERCENTILE_10:
                    output = GetPercentile(inData, 0.90);
                    break;
                default:
                    break;
                }
            }
        }
        else if (inData.size() == 1) {
            output = inData[0];
        }
    }

    // Calculate percentile using linear interpolation between the closet ranks
    // data must be pre-sorted
    double ConcreteMiddleware::GetPercentile(std::vector<double>& data, double percentile)
    {
        percentile = max(percentile, 0.);

        double integral_part_as_double;
        double fractpart =
            modf(percentile * static_cast<double>(data.size()),
                &integral_part_as_double);

        uint32_t idx = static_cast<uint32_t>(integral_part_as_double);
        if (idx >= data.size() - 1) {
            return data[data.size() - 1];
        }

        return data[idx] + (fractpart * (data[idx + 1] - data[idx]));
    }

    PmNsmFrameData* ConcreteMiddleware::GetFrameDataStart(StreamClient* client, uint64_t& index, uint64_t queryMetricsDataOffset, uint64_t& queryFrameDataDelta, double& window_sample_size_in_ms)
    {

        PmNsmFrameData* frame_data = nullptr;
        index = 0;
        if (client == nullptr) {
            return nullptr;
        }

        auto nsm_view = client->GetNamedSharedMemView();
        auto nsm_hdr = nsm_view->GetHeader();
        if (!nsm_hdr->process_active) {
            return nullptr;
        }

        index = client->GetLatestFrameIndex();
        frame_data = client->ReadFrameByIdx(index);
        if (frame_data == nullptr) {
            index = 0;
            return nullptr;
        }

        if (queryMetricsDataOffset == 0) {
            // Client has not specified a metric offset. Return back the most
            // most recent frame data
            return frame_data;
        }

        LARGE_INTEGER client_qpc = {};
        QueryPerformanceCounter(&client_qpc);
        uint64_t adjusted_qpc = GetAdjustedQpc(
            client_qpc.QuadPart, frame_data->present_event.PresentStartTime,
            queryMetricsDataOffset, client->GetQpcFrequency(), queryFrameDataDelta);

        if (adjusted_qpc > frame_data->present_event.PresentStartTime) {
            // Need to adjust the size of the window sample size
            double ms_adjustment =
                QpcDeltaToMs(adjusted_qpc - frame_data->present_event.PresentStartTime,
                    client->GetQpcFrequency());
            window_sample_size_in_ms = window_sample_size_in_ms - ms_adjustment;
            if (window_sample_size_in_ms <= 0.0) {
                return nullptr;
            }
        }
        else {
            // Find the frame with the appropriate time based on the adjusted
            // qpc
            for (;;) {

                if (DecrementIndex(nsm_view, index) == false) {
                    // Increment index to match up with the frame_data read below
                    index++;
                    break;
                }
                frame_data = client->ReadFrameByIdx(index);
                if (frame_data == nullptr) {
                    return nullptr;
                }
                if (adjusted_qpc >= frame_data->present_event.PresentStartTime) {
                    break;
                }
            }
        }

        return frame_data;
    }

    uint64_t ConcreteMiddleware::GetAdjustedQpc(uint64_t current_qpc, uint64_t frame_data_qpc, uint64_t queryMetricsOffset, LARGE_INTEGER frequency, uint64_t& queryFrameDataDelta) {
        // Calculate how far behind the frame data qpc is compared
        // to the client qpc
        uint64_t current_qpc_delta = current_qpc - frame_data_qpc;
        if (queryFrameDataDelta == 0) {
            queryFrameDataDelta = current_qpc_delta;
        }
        else {
            if (_abs64(queryFrameDataDelta - current_qpc_delta) >
                kClientFrameDeltaQPCThreshold) {
                queryFrameDataDelta = current_qpc_delta;
            }
        }

        // Add in the client set metric offset in qpc ticks
        return current_qpc -
            (queryFrameDataDelta + queryMetricsOffset);
    }

    bool ConcreteMiddleware::DecrementIndex(NamedSharedMem* nsm_view, uint64_t& index) {

        if (nsm_view == nullptr) {
            return false;
        }

        auto nsm_hdr = nsm_view->GetHeader();
        if (!nsm_hdr->process_active) {
            return false;
        }

        uint64_t current_max_entries =
            (nsm_view->IsFull()) ? nsm_hdr->max_entries - 1 : nsm_hdr->tail_idx;
        index = (index == 0) ? current_max_entries : index - 1;
        if (index == nsm_hdr->head_idx) {
            return false;
        }

        return true;
    }

    bool ConcreteMiddleware::GetGpuMetricData(size_t telemetry_item_bit, PresentMonPowerTelemetryInfo& power_telemetry_info, std::unordered_map<PM_METRIC, MetricInfo>& metricInfo)
    {
        bool validGpuMetric = true;
        GpuTelemetryCapBits bit =
            static_cast<GpuTelemetryCapBits>(telemetry_item_bit);
        switch (bit) {
        case GpuTelemetryCapBits::time_stamp:
            // This is a valid telemetry cap bit but we do not produce metrics for
            // it.
            validGpuMetric = false;
            break;
        case GpuTelemetryCapBits::gpu_power:
            metricInfo[PM_METRIC_GPU_POWER].data[0].emplace_back(power_telemetry_info.gpu_power_w);
            break;
        case GpuTelemetryCapBits::gpu_voltage:
            metricInfo[PM_METRIC_GPU_VOLTAGE].data[0].emplace_back(power_telemetry_info.gpu_voltage_v);
            break;
        case GpuTelemetryCapBits::gpu_frequency:
            metricInfo[PM_METRIC_GPU_FREQUENCY].data[0].emplace_back(power_telemetry_info.gpu_frequency_mhz);
            break;
        case GpuTelemetryCapBits::gpu_temperature:
            metricInfo[PM_METRIC_GPU_TEMPERATURE].data[0].emplace_back(power_telemetry_info.gpu_temperature_c);
            break;
        case GpuTelemetryCapBits::gpu_utilization:
            metricInfo[PM_METRIC_GPU_UTILIZATION].data[0].emplace_back(power_telemetry_info.gpu_utilization);
            break;
        case GpuTelemetryCapBits::gpu_render_compute_utilization:
            metricInfo[PM_METRIC_GPU_RENDER_COMPUTE_UTILIZATION].data[0].emplace_back(power_telemetry_info.gpu_render_compute_utilization);
            break;
        case GpuTelemetryCapBits::gpu_media_utilization:
            metricInfo[PM_METRIC_GPU_MEDIA_UTILIZATION].data[0].emplace_back(power_telemetry_info.gpu_media_utilization);
            break;
        case GpuTelemetryCapBits::vram_power:
            metricInfo[PM_METRIC_GPU_MEM_POWER].data[0].emplace_back(power_telemetry_info.vram_power_w);
            break;
        case GpuTelemetryCapBits::vram_voltage:
            metricInfo[PM_METRIC_GPU_MEM_VOLTAGE].data[0].emplace_back(power_telemetry_info.vram_voltage_v);
            break;
        case GpuTelemetryCapBits::vram_frequency:
            metricInfo[PM_METRIC_GPU_MEM_FREQUENCY].data[0].emplace_back(power_telemetry_info.vram_frequency_mhz);
            break;
        case GpuTelemetryCapBits::vram_effective_frequency:
            metricInfo[PM_METRIC_GPU_MEM_EFFECTIVE_FREQUENCY].data[0].emplace_back(power_telemetry_info.vram_effective_frequency_gbps);
            break;
        case GpuTelemetryCapBits::vram_temperature:
            metricInfo[PM_METRIC_GPU_MEM_TEMPERATURE].data[0].emplace_back(power_telemetry_info.vram_temperature_c);
            break;
        case GpuTelemetryCapBits::fan_speed_0:
            metricInfo[PM_METRIC_GPU_FAN_SPEED].data[0].emplace_back(power_telemetry_info.fan_speed_rpm[0]);
            break;
        case GpuTelemetryCapBits::fan_speed_1:
            metricInfo[PM_METRIC_GPU_FAN_SPEED].data[1].emplace_back(power_telemetry_info.fan_speed_rpm[1]);
            break;
        case GpuTelemetryCapBits::fan_speed_2:
            metricInfo[PM_METRIC_GPU_FAN_SPEED].data[2].emplace_back(power_telemetry_info.fan_speed_rpm[2]);
            break;
        case GpuTelemetryCapBits::fan_speed_3:
            metricInfo[PM_METRIC_GPU_FAN_SPEED].data[3].emplace_back(power_telemetry_info.fan_speed_rpm[3]);
            break;
        case GpuTelemetryCapBits::fan_speed_4:
            metricInfo[PM_METRIC_GPU_FAN_SPEED].data[4].emplace_back(power_telemetry_info.fan_speed_rpm[4]);
            break;
        case GpuTelemetryCapBits::gpu_mem_used:
            metricInfo[PM_METRIC_GPU_MEM_USED].data[0].emplace_back(static_cast<double>(power_telemetry_info.gpu_mem_used_b));
            break;
        case GpuTelemetryCapBits::gpu_mem_write_bandwidth:
            metricInfo[PM_METRIC_GPU_MEM_WRITE_BANDWIDTH].data[0].emplace_back(power_telemetry_info.gpu_mem_write_bandwidth_bps);
            break;
        case GpuTelemetryCapBits::gpu_mem_read_bandwidth:
            metricInfo[PM_METRIC_GPU_MEM_READ_BANDWIDTH].data[0].emplace_back(power_telemetry_info.gpu_mem_read_bandwidth_bps);
            break;
        case GpuTelemetryCapBits::gpu_power_limited:
            metricInfo[PM_METRIC_GPU_POWER_LIMITED].data[0].emplace_back(power_telemetry_info.gpu_power_limited);
            break;
        case GpuTelemetryCapBits::gpu_temperature_limited:
            metricInfo[PM_METRIC_GPU_TEMPERATURE_LIMITED].data[0].emplace_back(power_telemetry_info.gpu_temperature_limited);
            break;
        case GpuTelemetryCapBits::gpu_current_limited:
            metricInfo[PM_METRIC_GPU_CURRENT_LIMITED].data[0].emplace_back(power_telemetry_info.gpu_current_limited);
            break;
        case GpuTelemetryCapBits::gpu_voltage_limited:
            metricInfo[PM_METRIC_GPU_VOLTAGE_LIMITED].data[0].emplace_back(power_telemetry_info.gpu_voltage_limited);
            break;
        case GpuTelemetryCapBits::gpu_utilization_limited:
            metricInfo[PM_METRIC_GPU_UTILIZATION_LIMITED].data[0].emplace_back(power_telemetry_info.gpu_utilization_limited);
            break;
        case GpuTelemetryCapBits::vram_power_limited:
            metricInfo[PM_METRIC_GPU_MEM_POWER_LIMITED].data[0].emplace_back(power_telemetry_info.vram_power_limited);
            break;
        case GpuTelemetryCapBits::vram_temperature_limited:
            metricInfo[PM_METRIC_GPU_MEM_TEMPERATURE_LIMITED].data[0].emplace_back(power_telemetry_info.vram_temperature_limited);
            break;
        case GpuTelemetryCapBits::vram_current_limited:
            metricInfo[PM_METRIC_GPU_MEM_CURRENT_LIMITED].data[0].emplace_back(power_telemetry_info.vram_current_limited);
            break;
        case GpuTelemetryCapBits::vram_voltage_limited:
            metricInfo[PM_METRIC_GPU_MEM_VOLTAGE_LIMITED].data[0].emplace_back(power_telemetry_info.vram_voltage_limited);
            break;
        case GpuTelemetryCapBits::vram_utilization_limited:
            metricInfo[PM_METRIC_GPU_MEM_UTILIZATION_LIMITED].data[0].emplace_back(power_telemetry_info.vram_utilization_limited);
            break;
        default:
            validGpuMetric = false;
            break;
        }
        return validGpuMetric;
    }

    bool ConcreteMiddleware::GetCpuMetricData(size_t telemetryBit, CpuTelemetryInfo& cpuTelemetry, std::unordered_map<PM_METRIC, MetricInfo>& metricInfo)
    {
        bool validCpuMetric = true;
        CpuTelemetryCapBits bit =
            static_cast<CpuTelemetryCapBits>(telemetryBit);
        switch (bit) {
        case CpuTelemetryCapBits::cpu_utilization:
            metricInfo[PM_METRIC_CPU_UTILIZATION].data[0].emplace_back(cpuTelemetry.cpu_utilization);
            break;
        case CpuTelemetryCapBits::cpu_power:
            metricInfo[PM_METRIC_CPU_POWER].data[0].emplace_back(cpuTelemetry.cpu_power_w);
            break;
        case CpuTelemetryCapBits::cpu_temperature:
            metricInfo[PM_METRIC_CPU_TEMPERATURE].data[0].emplace_back(cpuTelemetry.cpu_temperature);
            break;
        case CpuTelemetryCapBits::cpu_frequency:
            metricInfo[PM_METRIC_CPU_FREQUENCY].data[0].emplace_back(cpuTelemetry.cpu_frequency);
            break;
        default:
            validCpuMetric = false;
            break;
        }

        return validCpuMetric;
    }

    void ConcreteMiddleware::SaveMetricCache(const PM_DYNAMIC_QUERY* pQuery, uint32_t processId, uint8_t* pBlob)
    {
        auto it = cachedMetricDatas.find(std::pair(pQuery, processId));
        if (it != cachedMetricDatas.end())
        {
            auto& uniquePtr = it->second;
            std::copy(pBlob, pBlob + pQuery->queryCacheSize, uniquePtr.get());
        }
        else
        {
            auto dataArray = std::make_unique<uint8_t[]>(pQuery->queryCacheSize);
            std::copy(pBlob, pBlob + pQuery->queryCacheSize, dataArray.get());
            cachedMetricDatas.emplace(std::pair(pQuery, processId), std::move(dataArray));
        }
    }

    void ConcreteMiddleware::CopyMetricCacheToBlob(const PM_DYNAMIC_QUERY* pQuery, uint32_t processId, uint8_t* pBlob)
    {
        auto it = cachedMetricDatas.find(std::pair(pQuery, processId));
        if (it != cachedMetricDatas.end())
        {
            auto& uniquePtr = it->second;
            std::copy(uniquePtr.get(), uniquePtr.get() + pQuery->queryCacheSize, pBlob);
        }
    }

    // This code currently doesn't support the copying of multiple swap chains. If a second swap chain
    // is encountered it will update the numSwapChains to the correct number and then copy the swap
    // chain frame information with the most presents. If the client does happen to specify two swap
    // chains this code will incorrectly copy the data. WIP.
    void ConcreteMiddleware::CalculateMetrics(const PM_DYNAMIC_QUERY* pQuery, uint32_t processId, uint8_t* pBlob, uint32_t* numSwapChains, LARGE_INTEGER qpcFrequency, std::unordered_map<uint64_t, fpsSwapChainData>& swapChainData, std::unordered_map<PM_METRIC, MetricInfo>& metricInfo)
    {
        // Find the swapchain with the most frame metrics
        uint32_t maxSwapChainPresents = 0;
        uint32_t maxSwapChainPresentsIndex = 0;
        uint32_t currentSwapChainIndex = 0;
        auto CalcGpuMemUtilization = [this, metricInfo](PM_STAT stat)
            {
                double output = 0.;
                if (cachedGpuInfo[currentGpuInfoIndex].gpuMemorySize.has_value()) {
                    auto gpuMemSize = static_cast<double>(cachedGpuInfo[currentGpuInfoIndex].gpuMemorySize.value());
                    if (gpuMemSize != 0.)
                    {
                        std::vector<double> memoryUtilization;
                        auto it = metricInfo.find(PM_METRIC_GPU_MEM_USED);
                        if (it != metricInfo.end()) {
                            auto memUsed = it->second;
                            auto memUsedVector = memUsed.data[0];
                            for (auto memUsed : memUsedVector) {
                                memoryUtilization.push_back(100. * (memUsed / gpuMemSize));
                            }
                            CalculateMetric(output, memoryUtilization, stat);
                        }
                    }
                }
                return output;
            };

        for (auto& pair : swapChainData) {
            auto& swapChain = pair.second;
            if (swapChain.CPUFrameQPC.size() > maxSwapChainPresents)
            {
                maxSwapChainPresents = (uint32_t)swapChain.CPUFrameQPC.size();
                maxSwapChainPresentsIndex = currentSwapChainIndex;
            }
            currentSwapChainIndex++;
        }

        currentSwapChainIndex = 0;
        bool copyAllMetrics = true;
        bool useCache = false;
        bool allMetricsCalculated = false;

        // If the number of swap chains found in the frame data is greater than the number passed
        // in update the passed in number to notify the client there is more data present than
        // can be returned
        if (swapChainData.size() > *numSwapChains)
        {
            *numSwapChains = static_cast<uint32_t>(swapChainData.size());
            copyAllMetrics = false;
        }

        // If the client chose to monitor frame information then this loop
        // will calculate and store all metrics.
        for (auto& pair : swapChainData) {
            auto& swapChain = pair.second;

            // There are couple reasons where we will not be able to produce
            // fps metric data. The first is if all of the frames are dropped.
            // The second is if in the requested sample window there are
            // no presents.
            if ((swapChain.display_count <= 1) && (swapChain.CPUFrameQPC.size() == 0)) {
                useCache = true;
                break;
            }

            // If we are unable to copy all of the metrics to the blob and the current swap
            // chain isn't the one with the most presents, skip by it
            if ((copyAllMetrics == false) && (currentSwapChainIndex != maxSwapChainPresentsIndex))
            {
                continue;
            }
            for (auto& qe : pQuery->elements) {
                switch (qe.metric)
                {
                case PM_METRIC_SWAP_CHAIN_ADDRESS:
                {
                    auto& output = reinterpret_cast<uint64_t&>(pBlob[qe.dataOffset]);
                    output = pair.first;
                }
                    break;

                case PM_METRIC_CPU_FRAME_QPC:
                case PM_METRIC_PRESENT_MODE:
                case PM_METRIC_PRESENT_RUNTIME:
                case PM_METRIC_PRESENT_FLAGS:
                case PM_METRIC_SYNC_INTERVAL:
                case PM_METRIC_ALLOWS_TEARING:
                case PM_METRIC_GPU_LATENCY:
                case PM_METRIC_GPU_WAIT:
                case PM_METRIC_GPU_BUSY:
                case PM_METRIC_DISPLAY_LATENCY:
                case PM_METRIC_CLICK_TO_PHOTON_LATENCY:
                case PM_METRIC_PRESENTED_FPS:
                case PM_METRIC_DISPLAYED_FPS:
                case PM_METRIC_DROPPED_FRAMES:
                case PM_METRIC_FRAME_TIME:
                case PM_METRIC_CPU_BUSY:
                case PM_METRIC_CPU_WAIT:
                case PM_METRIC_GPU_TIME:
                case PM_METRIC_DISPLAYED_TIME:
                case PM_METRIC_APPLICATION:
                    CalculateFpsMetric(swapChain, qe, pBlob, qpcFrequency);
                    break;
                case PM_METRIC_CPU_VENDOR:
                case PM_METRIC_CPU_POWER_LIMIT:
                case PM_METRIC_GPU_VENDOR:
                case PM_METRIC_GPU_MEM_MAX_BANDWIDTH:
                case PM_METRIC_GPU_MEM_SIZE:
                case PM_METRIC_GPU_SUSTAINED_POWER_LIMIT:
                    CopyStaticMetricData(qe.metric, qe.deviceId, pBlob, qe.dataOffset);
                    break;
                case PM_METRIC_CPU_NAME:
                case PM_METRIC_GPU_NAME:
                    CopyStaticMetricData(qe.metric, qe.deviceId, pBlob, qe.dataOffset, 260);
                    break;
                case PM_METRIC_GPU_MEM_UTILIZATION:
                {
                    auto& output = reinterpret_cast<double&>(pBlob[qe.dataOffset]);
                    output = CalcGpuMemUtilization(qe.stat);
                }
                    break;
                default:
                    if (qe.dataSize == sizeof(double)) {
                        CalculateGpuCpuMetric(metricInfo, qe, pBlob);
                    }
                    break;
                }
            }

            allMetricsCalculated = true;
            currentSwapChainIndex++;
        }

        if (useCache == true) {
            CopyMetricCacheToBlob(pQuery, processId, pBlob);
            return;
        }

        if (allMetricsCalculated == false)
        {
            for (auto& qe : pQuery->elements)
            {
                switch (qe.metric)
                {
                case PM_METRIC_GPU_POWER:
                case PM_METRIC_GPU_FAN_SPEED:
                case PM_METRIC_GPU_VOLTAGE:
                case PM_METRIC_GPU_FREQUENCY:
                case PM_METRIC_GPU_TEMPERATURE:
                case PM_METRIC_GPU_UTILIZATION:
                case PM_METRIC_GPU_RENDER_COMPUTE_UTILIZATION:
                case PM_METRIC_GPU_MEDIA_UTILIZATION:
                case PM_METRIC_GPU_MEM_POWER:
                case PM_METRIC_GPU_MEM_VOLTAGE:
                case PM_METRIC_GPU_MEM_FREQUENCY:
                case PM_METRIC_GPU_MEM_EFFECTIVE_FREQUENCY:
                case PM_METRIC_GPU_MEM_TEMPERATURE:
                case PM_METRIC_GPU_MEM_USED:
                case PM_METRIC_GPU_MEM_WRITE_BANDWIDTH:
                case PM_METRIC_GPU_MEM_READ_BANDWIDTH:
                case PM_METRIC_GPU_POWER_LIMITED:
                case PM_METRIC_GPU_TEMPERATURE_LIMITED:
                case PM_METRIC_GPU_CURRENT_LIMITED:
                case PM_METRIC_GPU_VOLTAGE_LIMITED:
                case PM_METRIC_GPU_UTILIZATION_LIMITED:
                case PM_METRIC_GPU_MEM_POWER_LIMITED:
                case PM_METRIC_GPU_MEM_TEMPERATURE_LIMITED:
                case PM_METRIC_GPU_MEM_CURRENT_LIMITED:
                case PM_METRIC_GPU_MEM_VOLTAGE_LIMITED:
                case PM_METRIC_GPU_MEM_UTILIZATION_LIMITED:
                case PM_METRIC_CPU_UTILIZATION:
                case PM_METRIC_CPU_POWER:
                case PM_METRIC_CPU_TEMPERATURE:
                case PM_METRIC_CPU_FREQUENCY:
                case PM_METRIC_CPU_CORE_UTILITY:
                    CalculateGpuCpuMetric(metricInfo, qe, pBlob);
                    break;
                case PM_METRIC_CPU_VENDOR:
                case PM_METRIC_CPU_POWER_LIMIT:
                case PM_METRIC_GPU_VENDOR:
                case PM_METRIC_GPU_MEM_MAX_BANDWIDTH:
                case PM_METRIC_GPU_MEM_SIZE:
                case PM_METRIC_GPU_SUSTAINED_POWER_LIMIT:
                    CopyStaticMetricData(qe.metric, qe.deviceId, pBlob, qe.dataOffset);
                    break;
                case PM_METRIC_CPU_NAME:
                case PM_METRIC_GPU_NAME:
                    CopyStaticMetricData(qe.metric, qe.deviceId, pBlob, qe.dataOffset, 260);
                    break;
                case PM_METRIC_GPU_MEM_UTILIZATION:
                {
                    auto& output = reinterpret_cast<double&>(pBlob[qe.dataOffset]);
                    output = CalcGpuMemUtilization(qe.stat);
                }
                break;
                default:
                    break;
                }
            }
        }

        // Save calculated metrics blob to cache
        SaveMetricCache(pQuery, processId, pBlob);
    }

    PM_STATUS ConcreteMiddleware::SetActiveGraphicsAdapter(uint32_t deviceId)
    {
        if (activeDevice && *activeDevice == deviceId) {
            return PM_STATUS_SUCCESS;
        }

        MemBuffer requestBuf;
        MemBuffer responseBuf;

        const auto adapterIndex = GetCachedGpuInfoIndex(deviceId);
        if (!adapterIndex.has_value()) {
            return PM_STATUS_INVALID_ADAPTER_ID;
        }

        NamedPipeHelper::EncodeGeneralSetActionRequest(PM_ACTION::SELECT_ADAPTER,
            &requestBuf, static_cast<uint32_t>(adapterIndex.value()));

        PM_STATUS status = CallPmService(&requestBuf, &responseBuf);
        if (status != PM_STATUS::PM_STATUS_SUCCESS) {
            return status;
        }

        status = NamedPipeHelper::DecodeGeneralSetActionResponse(
            PM_ACTION::SELECT_ADAPTER, &responseBuf);

        if (status == PM_STATUS_SUCCESS) {
            activeDevice = deviceId;
        }

        return status;
    }

    void ConcreteMiddleware::GetStaticGpuMetrics()
    {
        MemBuffer requestBuf;
        MemBuffer responseBuf;

        NamedPipeHelper::EncodeRequestHeader(&requestBuf, PM_ACTION::ENUMERATE_ADAPTERS);

        PM_STATUS status = CallPmService(&requestBuf, &responseBuf);
        if (status != PM_STATUS::PM_STATUS_SUCCESS) {
            return;
        }

        IPMAdapterInfoNext adapterInfo{};
        status =
            NamedPipeHelper::DecodeEnumerateAdaptersResponse(&responseBuf, (IPMAdapterInfo*)&adapterInfo);
        if (status != PM_STATUS::PM_STATUS_SUCCESS) {
            return;
        }

        if (adapterInfo.num_adapters != cachedGpuInfo.size())
        {
            LOG(INFO) << "Number of adapters returned from Control Pipe does not match Introspective data";
            return;
        }

        // For each cached gpu search through the returned adapter information and set the returned
        // static gpu metrics
        for (auto& gpuInfo : cachedGpuInfo)
        {
            for (uint32_t i = 0; i < adapterInfo.num_adapters; i++)
            {
                if (gpuInfo.adapterId == adapterInfo.adapters[i].id)
                {
                    gpuInfo.gpuSustainedPowerLimit = adapterInfo.adapters[i].gpuSustainedPowerLimit;
                    gpuInfo.gpuMemorySize = adapterInfo.adapters[i].gpuMemorySize;
                    gpuInfo.gpuMemoryMaxBandwidth = adapterInfo.adapters[i].gpuMemoryMaxBandwidth;
                    break;
                }
            }
        }
    }
}