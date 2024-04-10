#define NOMINMAX
#include "MockMiddleware.h"
#include <cstring>
#include <string>
#include <vector>
#include <deque>
#include <memory>
#include <cassert>
#include <cstdlib>
#include "../../PresentMonAPI2/Internal.h"
#include "../../PresentMonAPIWrapperCommon/Introspection.h"
// TODO: don't need transfer if we can somehow get the PM_ struct generation working without inheritance
// needed right now because even if we forward declare, we don't have the inheritance info
#include "../../Interprocess/source/IntrospectionTransfer.h"
#include "../../Interprocess/source/IntrospectionHelpers.h"
#include "../../Interprocess/source/IntrospectionCloneAllocators.h"
#include "../../PresentMonUtils/PresentMonNamedPipe.h"
#include "MockCommon.h"
#include "DynamicQuery.h"
#include "FrameEventQuery.h"
#include <algorithm>

namespace pmon::mid
{
	using namespace ipc::intro;
	using namespace std::string_literals;
	using namespace pmapi;

	MockMiddleware::MockMiddleware(bool useLocalShmServer)
	{
		if (useLocalShmServer) {
			pServiceComms = ipc::MakeServiceComms(mockIntrospectionNsmName);
			ipc::intro::RegisterMockIntrospectionDevices(*pServiceComms);
		}
		pMiddlewareComms = ipc::MakeMiddlewareComms(mockIntrospectionNsmName);
	}

	void MockMiddleware::AdvanceTime(uint32_t milliseconds)
	{
		t += milliseconds;
	}

	void MockMiddleware::Speak(char* buffer) const
	{
		strcpy_s(buffer, 256, "mock-middle");
	}

	const PM_INTROSPECTION_ROOT* MockMiddleware::GetIntrospectionData()
	{
		return pMiddlewareComms->GetIntrospectionRoot();
	}

	void MockMiddleware::FreeIntrospectionData(const PM_INTROSPECTION_ROOT* pRoot)
	{
		free(const_cast<PM_INTROSPECTION_ROOT*>(pRoot));
	}

	PM_DYNAMIC_QUERY* MockMiddleware::RegisterDynamicQuery(std::span<PM_QUERY_ELEMENT> queryElements, double windowSizeMs, double metricOffsetMs)
	{
		// get introspection data for reference
		// TODO: cache this data so it's not required to be generated every time
		pmapi::intro::Root ispec{ GetIntrospectionData(), [this](auto p){FreeIntrospectionData(p);} };

		// make the query object that will be managed by the handle
		auto pQuery = std::make_unique<PM_DYNAMIC_QUERY>();

		uint64_t offset = 0u;
		for (auto& qe : queryElements) {
			auto metricView = ispec.FindMetric(qe.metric);
			if (!intro::MetricTypeIsDynamic((PM_METRIC_TYPE)metricView.GetType())) {
				// TODO: specific exception here
				throw std::runtime_error{ "Static metric in dynamic metric query specification" };
			}
			// TODO: validate device id
			// TODO: validate array index
			qe.dataOffset = offset;
			qe.dataSize = GetDataTypeSize(metricView.GetDataTypeInfo().GetPolledType());
			offset += qe.dataSize;
		}

		pQuery->elements = std::vector<PM_QUERY_ELEMENT>{ queryElements.begin(), queryElements.end() };

		return pQuery.release();
	}

	void MockMiddleware::FreeDynamicQuery(const PM_DYNAMIC_QUERY* pQuery)
	{
		delete pQuery;
	}

	void MockMiddleware::PollDynamicQuery(const PM_DYNAMIC_QUERY* pQuery, uint32_t processId, uint8_t* pBlob, uint32_t* numSwapChains)
	{
		for (auto& qe : pQuery->elements) {
			if (qe.metric == PM_METRIC_PRESENT_MODE) {
				auto& output = reinterpret_cast<int&>(pBlob[qe.dataOffset]);
				if (t % 2 == 0) {
					output = (int)PM_PRESENT_MODE_HARDWARE_LEGACY_FLIP;
				}
				else {
					output = (int)PM_PRESENT_MODE_HARDWARE_INDEPENDENT_FLIP;
				}
			}
			else {
				auto& output = reinterpret_cast<double&>(pBlob[qe.dataOffset]);
				if (t % 2 == 0) {
					output = (double)qe.metric;
				}
				else {
					output = 0.;
				}
			}
		}
	}

	void MockMiddleware::PollStaticQuery(const PM_QUERY_ELEMENT& element, uint32_t processId, uint8_t* pBlob)
	{
		// get introspection data for reference
		// TODO: cache this data so it's not required to be generated every time
		intro::Root ispec{ GetIntrospectionData(), [this](auto p) {FreeIntrospectionData(p); } };

		auto metricView = ispec.FindMetric(element.metric);
		if (metricView.GetType() != PM_METRIC_TYPE_STATIC) {
			// TODO: more specific exception
			throw std::runtime_error{ "dynamic metric in static query poll" };
		}
		if (element.metric == PM_METRIC_APPLICATION) {
			strcpy_s(reinterpret_cast<char*>(pBlob), 260, "dota2.exe");
		}
		else {
			throw std::runtime_error{ "unknown metric in static poll" };
		}
	}

	PM_FRAME_QUERY* MockMiddleware::RegisterFrameEventQuery(std::span<PM_QUERY_ELEMENT> queryElements, uint32_t& blobSize)
	{
		if (!pendingFrameEvents.has_value()) {
			pendingFrameEvents = std::make_any<std::deque<PmNsmFrameData>>(std::deque<PmNsmFrameData>{
				PmNsmFrameData{
					.present_event = {
						.PresentStartTime = 69420ull,
						.Runtime = Runtime::DXGI,
						.PresentMode = PresentMode::Composed_Flip,
					},
					.power_telemetry = {
						.gpu_power_w = 420.,
						.fan_speed_rpm = { 1.1, 2.2, 3.3, 4.4, 5.5 },
						.gpu_temperature_limited = true,
					},
					.cpu_telemetry = {
						.cpu_utilization = 30.,
					},
				},
				PmNsmFrameData{
					.present_event = {
						.PresentStartTime = 69920ull,
						.Runtime = Runtime::DXGI,
						.PresentMode = PresentMode::Composed_Flip,
					},
					.power_telemetry = {
						.gpu_power_w = 400.,
						.fan_speed_rpm = { 1.0, 2.0, 3.0, 4.0, 5.0 },
						.gpu_temperature_limited = false,
					},
					.cpu_telemetry = {
						.cpu_utilization = 27.,
					},
				},
			});
		}
		const auto pQuery = new PM_FRAME_QUERY{ queryElements };
		blobSize = (uint32_t)pQuery->GetBlobSize();
		return pQuery;
	}

	void MockMiddleware::FreeFrameEventQuery(const PM_FRAME_QUERY* pQuery)
	{
		delete const_cast<PM_FRAME_QUERY*>(pQuery);
	}

	void MockMiddleware::ConsumeFrameEvents(const PM_FRAME_QUERY* pQuery, uint32_t processId, uint8_t* pBlob, uint32_t& numFrames)
	{
		auto& frames = std::any_cast<std::deque<PmNsmFrameData>&>(pendingFrameEvents);
		if (t > 0) {
			frames.push_back(PmNsmFrameData{
				.present_event = {
					.PresentStartTime = 77000ull,
					.Runtime = Runtime::DXGI,
					.PresentMode = PresentMode::Hardware_Independent_Flip,
				},
				.power_telemetry = {
					.gpu_power_w = 490.,
					.fan_speed_rpm = { 1.8, 2.8, 3.8, 4.8, 5.8 },
					.gpu_temperature_limited = false,
				},
				.cpu_telemetry = {
					.cpu_utilization = 50.,
				},
			});
		}
		const auto numFramesToProcess = std::min(numFrames, (uint32_t)frames.size());
		const auto blobSize = pQuery->GetBlobSize();
		PM_FRAME_QUERY::Context ctx{ 0ull, 0ll };
		for (uint32_t i = 0; i < numFramesToProcess; i++) {
			// TODO: feed actual prev/next frames into this function
			ctx.UpdateSourceData(&frames.front(), nullptr, nullptr);
			pQuery->GatherToBlob(ctx, pBlob);
			frames.pop_front();
			pBlob += blobSize;
		}
		numFrames = numFramesToProcess;
	}

}