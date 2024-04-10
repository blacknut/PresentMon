#pragma once
#include "../../PresentMonAPI2/PresentMonAPI.h"
#include <vector>
#include <span>
#include <memory>

namespace pmapi::intro
{
	class Root;
}

namespace pmon::mid
{
	class GatherCommand_;
}

struct PM_FRAME_QUERY
{
public:
	// types
	struct Context
	{
		// functions
		Context(uint64_t qpcStart, long long perfCounterFrequency) : qpcStart{ qpcStart },
			performanceCounterPeriodMs{ perfCounterFrequency != 0.f ? 1000.0 / perfCounterFrequency : 0.f } {}
		void UpdateSourceData(const PmNsmFrameData* pSourceFrameData_in,
			const PmNsmFrameData* pNextDisplayedFrameData,
			const PmNsmFrameData* pPreviousFrameData);
		// data
		const PmNsmFrameData* pSourceFrameData = nullptr;
		const double performanceCounterPeriodMs{};
		const uint64_t qpcStart{};
		bool dropped{};
		uint64_t cpuFrameQpc = 0;
		uint64_t nextDisplayedQpc = 0;
	};
	// functions
	PM_FRAME_QUERY(std::span<PM_QUERY_ELEMENT> queryElements);
	~PM_FRAME_QUERY();
	void GatherToBlob(const Context& ctx, uint8_t* pDestBlob) const;
	size_t GetBlobSize() const;
	std::optional<uint32_t> GetReferencedDevice() const;

	PM_FRAME_QUERY(const PM_FRAME_QUERY&) = delete;
	PM_FRAME_QUERY& operator=(const PM_FRAME_QUERY&) = delete;
	PM_FRAME_QUERY(PM_FRAME_QUERY&&) = delete;
	PM_FRAME_QUERY& operator=(PM_FRAME_QUERY&&) = delete;

private:
	// functions
	std::unique_ptr<pmon::mid::GatherCommand_> MapQueryElementToGatherCommand_(const PM_QUERY_ELEMENT& q, size_t pos);
	// data
	std::vector<std::unique_ptr<pmon::mid::GatherCommand_>> gatherCommands_;
	size_t blobSize_ = 0;
	std::optional<uint32_t> referencedDevice_;
};