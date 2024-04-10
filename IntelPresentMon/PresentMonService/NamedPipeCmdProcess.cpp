// Copyright (C) 2022-2023 Intel Corporation
// SPDX-License-Identifier: MIT
#include "NamedPipeCmdProcess.h"
#include "..\PresentMonUtils\NamedPipeHelper.h"

#define GOOGLE_GLOG_DLL_DECL
#define GLOG_NO_ABBREVIATED_SEVERITIES
#include <glog/logging.h>

bool EncodeStartStream(PresentMon* pm, MemBuffer* rqstBuf, MemBuffer* rspBuf) {
  const IPMSMGeneralRequestInfo* genRqstInfo =
      NamedPipeHelper::GetGeneralRequestInfo(rqstBuf, PM_ACTION::START_STREAM);
  if (genRqstInfo == nullptr) {
    return false;
  }

  PM_STATUS rspStatus;
  std::string nsmFileName;
  if (genRqstInfo->etlFileNameLength == 0) {
    rspStatus = pm->StartStreaming(genRqstInfo->clientProcessId,
                                   genRqstInfo->targetProcessId, nsmFileName);
  } else {
    std::wstring wetlFileName(genRqstInfo->etlFileName, genRqstInfo->etlFileName + strlen(genRqstInfo->etlFileName));
    rspStatus = pm->ProcessEtlFile(genRqstInfo->clientProcessId, wetlFileName, nsmFileName);
  }

  IPMSMResponseHeader response{};
  IPMSMStartStreamResponse start_stream_response{};

  if (google::IsGoogleLoggingInitialized()) {
    start_stream_response.enable_file_logging = true;
  } else {
    start_stream_response.enable_file_logging = false;
  }

  if (rspStatus != PM_STATUS::PM_STATUS_SUCCESS) {
    NamedPipeHelper::PopulateResponseHeader(response, PM_ACTION::START_STREAM,
                                            1, sizeof(IPMSMStartStreamResponse),
                                            rspStatus);
    rspBuf->AddItem(&response, sizeof(response));
    rspBuf->AddItem(&start_stream_response, sizeof(IPMSMStartStreamResponse));
  } else {
    NamedPipeHelper::PopulateResponseHeader(response, PM_ACTION::START_STREAM,
                                            1, sizeof(IPMSMStartStreamResponse),
                                            rspStatus);
    rspBuf->AddItem(&response, sizeof(response));
    nsmFileName.copy(start_stream_response.fileName,
                     sizeof(start_stream_response.fileName), 0);
    start_stream_response.fileNameLength = nsmFileName.length();
    rspBuf->AddItem(&start_stream_response, sizeof(IPMSMStartStreamResponse));
  }

  return true;
}

bool EncodeStopStream(PresentMon* pm, MemBuffer* rqstBuf, MemBuffer* rspBuf) {
  const IPMSMGeneralRequestInfo* genRqstInfo =
      NamedPipeHelper::GetGeneralRequestInfo(rqstBuf, PM_ACTION::STOP_STREAM);
  if (genRqstInfo == nullptr) {
    return false;
  }

  pm->StopStreaming(genRqstInfo->clientProcessId, genRqstInfo->targetProcessId);

  IPMSMResponseHeader response = {};

  NamedPipeHelper::PopulateResponseHeader(response, PM_ACTION::STOP_STREAM, 1,
                                          0, PM_STATUS::PM_STATUS_SUCCESS);
  rspBuf->AddItem(&response, sizeof(response));

  return true;
}

bool EncodeEnumerateAdapters(PresentMon* pm, MemBuffer* rspBuf) {
  IPMSMResponseHeader response = {};
  IPMAdapterInfo adapter_info = {};

  // Enumerate adapters on the machine
  const auto adapter_infos = pm->EnumerateAdapters();
  if (adapter_infos.size() < MAX_PM_ADAPTERS) {
    adapter_info.num_adapters = static_cast<uint32_t>(adapter_infos.size());
  } else {
    adapter_info.num_adapters = MAX_PM_ADAPTERS;
  }

  for (uint32_t i = 0; i < adapter_info.num_adapters; i++) {
    adapter_info.adapters[i].id = i;
    adapter_info.adapters[i].vendor = (PM_GPU_VENDOR)adapter_infos.at(i)->GetVendor();
    strcpy_s(adapter_info.adapters[i].name, MAX_PM_ADAPTER_NAME,
             adapter_infos.at(i)->GetName().c_str());
    adapter_info.adapters[i].gpuMemorySize = adapter_infos.at(i)->GetDedicatedVideoMemory();
    adapter_info.adapters[i].gpuMemoryMaxBandwidth = adapter_infos.at(i)->GetVideoMemoryMaxBandwidth();
    adapter_info.adapters[i].gpuSustainedPowerLimit = adapter_infos.at(i)->GetSustainedPowerLimit();
  }

  NamedPipeHelper::PopulateResponseHeader(
      response, PM_ACTION::ENUMERATE_ADAPTERS, 1,
      static_cast<DWORD>(sizeof(IPMAdapterInfo)), PM_STATUS::PM_STATUS_SUCCESS);

  rspBuf->AddItem(&response, sizeof(response));
  rspBuf->AddItem(&adapter_info, sizeof(IPMAdapterInfo));
  return true;
}

bool EncodeGetStaticCpuMetrics(PresentMon* pm, MemBuffer* rspBuf) {

  IPMSMResponseHeader response = {};
  IPMStaticCpuMetrics staticCpuMetrics = {};

  NamedPipeHelper::PopulateResponseHeader(
      response, PM_ACTION::GET_STATIC_CPU_METRICS, 1,
      static_cast<DWORD>(sizeof(IPMStaticCpuMetrics)),
      PM_STATUS::PM_STATUS_SUCCESS);

  auto cpu_name = pm->GetCpuName();
  cpu_name.copy(staticCpuMetrics.cpuName, sizeof(staticCpuMetrics.cpuName), 0);
  staticCpuMetrics.cpuNameLength = (uint32_t)cpu_name.size();
  staticCpuMetrics.cpuPowerLimit = pm->GetCpuPowerLimit();

  rspBuf->AddItem(&response, sizeof(IPMSMResponseHeader));
  rspBuf->AddItem(&staticCpuMetrics, sizeof(IPMStaticCpuMetrics));
  return true;
}

bool EncodeGeneralRequestSetAction(PM_ACTION action, PresentMon* pm,
                                   MemBuffer* rqstBuf, MemBuffer* rspBuf) {
  const IPMSMGeneralRequestInfo* genRqstInfo =
      NamedPipeHelper::GetGeneralRequestInfo(rqstBuf, action);
  if (genRqstInfo == nullptr) {
    return false;
  }

  PM_STATUS rsp_status = PM_STATUS_FAILURE;
  switch (action) {
    case PM_ACTION::SELECT_ADAPTER:
      rsp_status = pm->SelectAdapter(genRqstInfo->adapterId);
      break;
    case PM_ACTION::SET_GPU_TELEMETRY_PERIOD:
      rsp_status =
          pm->SetGpuTelemetryPeriod(genRqstInfo->gpuTelemetrySamplePeriodMs);
      break;
  }

  IPMSMResponseHeader response = {};
  NamedPipeHelper::PopulateResponseHeader(response, action, 1, 0, rsp_status);
  rspBuf->AddItem(&response, sizeof(response));
  return true;
}

void ProcessRequests(PresentMon* pm, MemBuffer* rqstBuf, MemBuffer* rspBuf) {
  // Get the incoming request pointer
  const IPMSMRequestHeader* request =
      NamedPipeHelper::GetRequestHeader(rqstBuf);

  IPMSMResponseHeader response;
  BOOL validRequest = FALSE;

  validRequest = NamedPipeHelper::ValidateRequest(rqstBuf, request->action);

  if (validRequest) {
    switch (request->action) {
      case PM_ACTION::START_STREAM:

        validRequest = EncodeStartStream(pm, rqstBuf, rspBuf);

        break;
      case PM_ACTION::STOP_STREAM:

        validRequest = EncodeStopStream(pm, rqstBuf, rspBuf);

        break;
      case PM_ACTION::ENUMERATE_ADAPTERS:
        validRequest = EncodeEnumerateAdapters(pm, rspBuf);
        break;
      case PM_ACTION::SELECT_ADAPTER:
      case PM_ACTION::SET_GPU_TELEMETRY_PERIOD:
        validRequest =
            EncodeGeneralRequestSetAction(request->action, pm, rqstBuf, rspBuf);
        break;
      case PM_ACTION::GET_STATIC_CPU_METRICS:
        validRequest = EncodeGetStaticCpuMetrics(pm, rspBuf);
        break;
      default:
        validRequest = FALSE;
    }
  }

  if (validRequest == FALSE) {
    _tcscpy_s(response.idString, 6, ipmsmIdString);
    response.action = PM_ACTION::INVALID_REQUEST;
    response.version = 1;
    response.result = PM_STATUS::PM_STATUS_FAILURE;
    response.payloadSize = 0;
    rspBuf->AddItem(&response, sizeof(response));
  }

  return;
}