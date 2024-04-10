#pragma once
#include "Middleware.h"
#include "../../Interprocess/source/Interprocess.h"
#include <any>

namespace pmon::mid
{
	class MockMiddleware : public Middleware
	{
	public:
		// functions
		MockMiddleware(bool mockServer);
		void AdvanceTime(uint32_t milliseconds);
		void Speak(char* buffer) const override;
		const PM_INTROSPECTION_ROOT* GetIntrospectionData() override;
		void FreeIntrospectionData(const PM_INTROSPECTION_ROOT* pRoot) override;
		PM_STATUS StartStreaming(uint32_t processId) override { return PM_STATUS_SUCCESS; }
		PM_STATUS StopStreaming(uint32_t processId) override { return PM_STATUS_SUCCESS; }
		PM_STATUS SetTelemetryPollingPeriod(uint32_t deviceId, uint32_t timeMs) override { return PM_STATUS_SUCCESS; }
		PM_DYNAMIC_QUERY* RegisterDynamicQuery(std::span<PM_QUERY_ELEMENT> queryElements, double windowSizeMs, double metricOffsetMs) override;
		void FreeDynamicQuery(const PM_DYNAMIC_QUERY* pQuery) override;
		void PollDynamicQuery(const PM_DYNAMIC_QUERY* pQuery, uint32_t processId, uint8_t* pBlob, uint32_t* numSwapChains) override;
		void PollStaticQuery(const PM_QUERY_ELEMENT& element, uint32_t processId, uint8_t* pBlob) override;
		PM_FRAME_QUERY* RegisterFrameEventQuery(std::span<PM_QUERY_ELEMENT> queryElements, uint32_t& blobSize) override;
		void FreeFrameEventQuery(const PM_FRAME_QUERY* pQuery) override;
		void ConsumeFrameEvents(const PM_FRAME_QUERY* pQuery, uint32_t processId, uint8_t* pBlob, uint32_t& numFrames) override;
		// data
		static constexpr const char* mockIntrospectionNsmName = "pm_api2_intro_nsm_mock";
	private:
		uint32_t t = 0;
		std::any pendingFrameEvents;
		bool holdoffReleased = false;
		std::unique_ptr<ipc::ServiceComms> pServiceComms;
		std::unique_ptr<ipc::MiddlewareComms> pMiddlewareComms;
	};
}