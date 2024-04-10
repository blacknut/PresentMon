#pragma once
#include "PresentMonAPI.h"


PRESENTMON_API2_EXPORT void pmSetMiddlewareAsMock_(bool mocked, bool useCrtHeapDebug = false, bool useLocalShmServer = true);
PRESENTMON_API2_EXPORT _CrtMemState pmCreateHeapCheckpoint_();
PRESENTMON_API2_EXPORT PM_STATUS pmMiddlewareSpeak_(PM_SESSION_HANDLE handle, char* buffer);
PRESENTMON_API2_EXPORT PM_STATUS pmMiddlewareAdvanceTime_(PM_SESSION_HANDLE handle, uint32_t milliseconds);
PRESENTMON_API2_EXPORT PM_STATUS pmOpenSession_(PM_SESSION_HANDLE* pHandle, const char* pipeNameOverride, const char* introNsmOverride);
