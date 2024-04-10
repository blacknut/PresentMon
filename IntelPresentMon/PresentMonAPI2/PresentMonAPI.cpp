#include <memory>
#include <crtdbg.h>
#include <unordered_map>
#include "../PresentMonMiddleware/source/MockMiddleware.h"
#include "../PresentMonMiddleware/source/ConcreteMiddleware.h"
#include "../PresentMonMiddleware/source/Exception.h"
#include "Internal.h"
#include "PresentMonAPI.h"


using namespace pmon::mid;

// global state
bool useMockedMiddleware_ = false;
bool useCrtHeapDebug_ = false;
bool useLocalShmServer_ = false;
// map handles (session, query, introspection) to middleware instances
std::unordered_map<const void*, std::shared_ptr<Middleware>> handleMap_;


// private implementation functions
Middleware& LookupMiddleware_(const void* handle)
{
	try {
		return *handleMap_.at(handle);
	}
	catch (...) {
		throw Exception{ PM_STATUS_SESSION_NOT_OPEN };
	}
}

void DestroyMiddleware_(PM_SESSION_HANDLE handle)
{
	try {
		if (!handleMap_.erase(handle)) {
			throw Exception{ PM_STATUS_SESSION_NOT_OPEN };
		}
	}
	catch (...) {
		throw Exception{ PM_STATUS_FAILURE };
	}
}

void AddHandleMapping_(PM_SESSION_HANDLE sessionHandle, const void* dependentHandle)
{
	handleMap_[dependentHandle] = handleMap_[sessionHandle];
}

void RemoveHandleMapping_(const void* dependentHandle)
{
	if (!handleMap_.erase(dependentHandle)) {
		// TODO: add error code to indicate a bad / missing handle (other than session handle)
		throw Exception{ PM_STATUS_FAILURE };
	}
}

// private endpoints
PRESENTMON_API2_EXPORT void pmSetMiddlewareAsMock_(bool mocked, bool activateCrtHeapDebug, bool useLocalShmServer)
{
	useMockedMiddleware_ = mocked;
	useCrtHeapDebug_ = activateCrtHeapDebug;
	useLocalShmServer_ = useLocalShmServer;
	if (useCrtHeapDebug_) {
		_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF);
	}
	else {
		_CrtSetDbgFlag(0);
	}
}

PRESENTMON_API2_EXPORT _CrtMemState pmCreateHeapCheckpoint_()
{
	_CrtMemState s;
	_CrtMemCheckpoint(&s);
	return s;
}

PRESENTMON_API2_EXPORT PM_STATUS pmMiddlewareSpeak_(PM_SESSION_HANDLE handle, char* buffer)
{
	try {
		LookupMiddleware_(handle).Speak(buffer);
		return PM_STATUS_SUCCESS;
	}
	catch (const Exception& e) {
		return e.GetErrorCode();
	}
	catch (...) {
		return PM_STATUS_FAILURE;
	}
}

PRESENTMON_API2_EXPORT PM_STATUS pmMiddlewareAdvanceTime_(PM_SESSION_HANDLE handle, uint32_t milliseconds)
{
	try {
		Middleware& mid = LookupMiddleware_(handle);
		dynamic_cast<MockMiddleware&>(mid).AdvanceTime(milliseconds);
		return PM_STATUS_SUCCESS;
	}
	catch (const Exception& e) {
		return e.GetErrorCode();
	}
	catch (...) {
		return PM_STATUS_FAILURE;
	}
}

PRESENTMON_API2_EXPORT PM_STATUS pmOpenSession_(PM_SESSION_HANDLE* pHandle, const char* pipeNameOverride, const char* introNsmOverride)
{
	try {
		if (!pHandle) {
			// TODO: add error code to indicate bad argument
			return PM_STATUS_FAILURE;
		}
		std::shared_ptr<Middleware> pMiddleware;
		if (useMockedMiddleware_) {
			pMiddleware = std::make_shared<MockMiddleware>(useLocalShmServer_);
		}
		else {
			std::optional<std::string> pipeName;
			std::optional<std::string> introNsm;
			if (pipeNameOverride) {
				pipeName = std::string(pipeNameOverride);
			}
			if (introNsmOverride) {
				introNsm = std::string(introNsmOverride);
			}
			pMiddleware = std::make_shared<ConcreteMiddleware>(std::move(pipeName), std::move(introNsm));
		}
		*pHandle = pMiddleware.get();
		handleMap_[*pHandle] = std::move(pMiddleware);
		return PM_STATUS_SUCCESS;
	}
	catch (const Exception& e) {
		return e.GetErrorCode();
	}
	catch (...) {
		return PM_STATUS_FAILURE;
	}
}

// public endpoints
PRESENTMON_API2_EXPORT PM_STATUS pmOpenSession(PM_SESSION_HANDLE* pHandle)
{
	return pmOpenSession_(pHandle, nullptr, nullptr);
}

PRESENTMON_API2_EXPORT PM_STATUS pmCloseSession(PM_SESSION_HANDLE handle)
{
	try {
		DestroyMiddleware_(handle);
		return PM_STATUS_SUCCESS;
	}
	catch (const Exception& e) {
		return e.GetErrorCode();
	}
	catch (...) {
		return PM_STATUS_FAILURE;
	}
}

PRESENTMON_API2_EXPORT PM_STATUS pmStartTrackingProcess(PM_SESSION_HANDLE handle, uint32_t processId)
{
	try {
		// TODO: consider tracking resource usage for process tracking to validate Start/Stop pairing
		// TODO: middleware should not return status codes
		return LookupMiddleware_(handle).StartStreaming(processId);
	}
	catch (const Exception& e) {
		return e.GetErrorCode();
	}
	catch (...) {
		return PM_STATUS_FAILURE;
	}
}

PRESENTMON_API2_EXPORT PM_STATUS pmStopTrackingProcess(PM_SESSION_HANDLE handle, uint32_t processId)
{
	try {
		// TODO: consider tracking resource usage for process tracking to validate Start/Stop pairing
		// TODO: middleware should not return status codes
		return LookupMiddleware_(handle).StopStreaming(processId);
	}
	catch (const Exception& e) {
		return e.GetErrorCode();
	}
	catch (...) {
		return PM_STATUS_FAILURE;
	}
}

PRESENTMON_API2_EXPORT PM_STATUS pmGetIntrospectionRoot(PM_SESSION_HANDLE handle, const PM_INTROSPECTION_ROOT** ppInterface)
{
	try {
		if (!ppInterface) {
			// TODO: error code to signal bad argument
			return PM_STATUS_FAILURE;
		}
		const auto pIntro = LookupMiddleware_(handle).GetIntrospectionData();
		AddHandleMapping_(handle, pIntro);
		*ppInterface = pIntro;
		return PM_STATUS_SUCCESS;
	}
	catch (const Exception& e) {
		return e.GetErrorCode();
	}
	catch (...) {
		return PM_STATUS_FAILURE;
	}
}

PRESENTMON_API2_EXPORT PM_STATUS pmFreeIntrospectionRoot(const PM_INTROSPECTION_ROOT* pInterface)
{
	try {
		if (!pInterface) {
			return PM_STATUS_SUCCESS;
		}
		auto& mid = LookupMiddleware_(pInterface);
		RemoveHandleMapping_(pInterface);
		mid.FreeIntrospectionData(pInterface);
		return PM_STATUS_SUCCESS;
	}
	catch (const Exception& e) {
		return e.GetErrorCode();
	}
	catch (...) {
		return PM_STATUS_FAILURE;
	}
}

PRESENTMON_API2_EXPORT PM_STATUS pmSetTelemetryPollingPeriod(PM_SESSION_HANDLE handle, uint32_t deviceId, uint32_t timeMs)
{
	try {
		LookupMiddleware_(handle).SetTelemetryPollingPeriod(deviceId, timeMs);
		return PM_STATUS_SUCCESS;
	}
	catch (const Exception& e) {
		return e.GetErrorCode();
	}
	catch (...) {
		return PM_STATUS_FAILURE;
	}
}

PRESENTMON_API2_EXPORT PM_STATUS pmRegisterDynamicQuery(PM_SESSION_HANDLE sessionHandle, PM_DYNAMIC_QUERY_HANDLE* pQueryHandle,
	PM_QUERY_ELEMENT* pElements, uint64_t numElements, double windowSizeMs, double metricOffsetMs)
{
	try {
		if (!pElements || !numElements) {
			// TODO: error code for bad args
			return PM_STATUS_FAILURE;
		}
		const auto queryHandle = LookupMiddleware_(sessionHandle).RegisterDynamicQuery(
			{pElements, numElements}, windowSizeMs, metricOffsetMs);
		AddHandleMapping_(sessionHandle, queryHandle);
		*pQueryHandle = queryHandle;
		return PM_STATUS_SUCCESS;
	}
	catch (const Exception& e) {
		return e.GetErrorCode();
	}
	catch (...) {
		return PM_STATUS_FAILURE;
	}
}

PRESENTMON_API2_EXPORT PM_STATUS pmFreeDynamicQuery(PM_DYNAMIC_QUERY_HANDLE handle)
{
	try {
		if (!handle) {
			return PM_STATUS_SUCCESS;
		}
		auto& mid = LookupMiddleware_(handle);
		RemoveHandleMapping_(handle);
		mid.FreeDynamicQuery(handle);
		return PM_STATUS_SUCCESS;
	}
	catch (const Exception& e) {
		return e.GetErrorCode();
	}
	catch (...) {
		return PM_STATUS_FAILURE;
	}
}

PRESENTMON_API2_EXPORT PM_STATUS pmPollDynamicQuery(PM_DYNAMIC_QUERY_HANDLE handle, uint32_t processId, uint8_t* pBlob, uint32_t* numSwapChains)
{
	try {
		if (!pBlob || !numSwapChains || !*numSwapChains) {
			// TODO: error code for bad args
			return PM_STATUS_FAILURE;
		}
		LookupMiddleware_(handle).PollDynamicQuery(handle, processId, pBlob, numSwapChains);
		return PM_STATUS_SUCCESS;
	}
	catch (const Exception& e) {
		return e.GetErrorCode();
	}
	catch (...) {
		return PM_STATUS_FAILURE;
	}
}

PRESENTMON_API2_EXPORT PM_STATUS pmPollStaticQuery(PM_SESSION_HANDLE sessionHandle, const PM_QUERY_ELEMENT* pElement, uint32_t processId, uint8_t* pBlob)
{
	try {
		if (!pElement || !pBlob) {
			// TODO: error code for bad args
			return PM_STATUS_FAILURE;
		}
		LookupMiddleware_(sessionHandle).PollStaticQuery(*pElement, processId, pBlob);
		return PM_STATUS_SUCCESS;
	}
	catch (const Exception& e) {
		return e.GetErrorCode();
	}
	catch (...) {
		return PM_STATUS_FAILURE;
	}
}

PRESENTMON_API2_EXPORT PM_STATUS pmRegisterFrameQuery(PM_SESSION_HANDLE sessionHandle, PM_FRAME_QUERY_HANDLE* pQueryHandle, PM_QUERY_ELEMENT* pElements, uint64_t numElements, uint32_t* pBlobSize)
{
	try {
		if (!pElements || !numElements || !pBlobSize) {
			// TODO: error code for bad args
			return PM_STATUS_FAILURE;
		}
		const auto queryHandle = LookupMiddleware_(sessionHandle).RegisterFrameEventQuery({ pElements, numElements }, *pBlobSize);
		AddHandleMapping_(sessionHandle, queryHandle);
		*pQueryHandle = queryHandle;
		return PM_STATUS_SUCCESS;
	}
	catch (const Exception& e) {
		return e.GetErrorCode();
	}
	catch (...) {
		return PM_STATUS_FAILURE;
	}
}

PRESENTMON_API2_EXPORT PM_STATUS pmConsumeFrames(PM_FRAME_QUERY_HANDLE handle, uint32_t processId, uint8_t* pBlob, uint32_t* pNumFramesToRead)
{
	try {
		if (!handle || !pBlob) {
			// TODO: error code for bad args
			return PM_STATUS_FAILURE;
		}
		LookupMiddleware_(handle).ConsumeFrameEvents(handle, processId, pBlob, *pNumFramesToRead);
		return PM_STATUS_SUCCESS;
	}
	catch (const Exception& e) {
		return e.GetErrorCode();
	}
	catch (...) {
		return PM_STATUS_FAILURE;
	}
}

PRESENTMON_API2_EXPORT PM_STATUS pmFreeFrameQuery(PM_FRAME_QUERY_HANDLE handle)
{
	try {
		auto& mid = LookupMiddleware_(handle);
		RemoveHandleMapping_(handle);
		mid.FreeFrameEventQuery(handle);
		return PM_STATUS_SUCCESS;
	}
	catch (const Exception& e) {
		return e.GetErrorCode();
	}
	catch (...) {
		return PM_STATUS_FAILURE;
	}
}
