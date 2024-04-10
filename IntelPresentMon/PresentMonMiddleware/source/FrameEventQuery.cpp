#define NOMINMAX
#include "../../PresentMonUtils/PresentMonNamedPipe.h"
#include "FrameEventQuery.h"
#include "../../PresentMonAPIWrapperCommon/Introspection.h"
#include "../../CommonUtilities//Memory.h"
#include "../../CommonUtilities//Meta.h"
#include <algorithm>
#include <cstddef>
#include <limits>

using namespace pmon;
using Context = PM_FRAME_QUERY::Context;

namespace pmon::mid
{
	class GatherCommand_
	{
	public:
		virtual ~GatherCommand_() = default;
		virtual void Gather(const Context& ctx, uint8_t* pDestBlob) const = 0;
		virtual uint32_t GetBeginOffset() const = 0;
		virtual uint32_t GetEndOffset() const = 0;
		virtual uint32_t GetOutputOffset() const = 0;
		uint32_t GetDataSize() const { return GetEndOffset() - GetOutputOffset(); }
		uint32_t GetTotalSize() const { return GetEndOffset() - GetBeginOffset(); }
	};
}

namespace
{
	template<auto pMember>
	constexpr auto GetSubstructurePointer()
	{
		using SubstructureType = util::MemberPointerInfo<decltype(pMember)>::StructType;
		if constexpr (std::same_as<SubstructureType, PmNsmPresentEvent>) {
			return &PmNsmFrameData::present_event;
		}
		else if constexpr (std::same_as<SubstructureType, PresentMonPowerTelemetryInfo>) {
			return &PmNsmFrameData::power_telemetry;
		}
		else if constexpr (std::same_as<SubstructureType, CpuTelemetryInfo>) {
			return &PmNsmFrameData::cpu_telemetry;
		}
	}

	template<auto pMember>
	class CopyGatherCommand_ : public mid::GatherCommand_
	{
		using Type = util::MemberPointerInfo<decltype(pMember)>::MemberType;
	public:
		CopyGatherCommand_(size_t nextAvailableByteOffset, uint16_t index = 0)
			:
			inputIndex_{ index }
		{
			outputPaddingSize_ = (uint16_t)util::GetPadding(nextAvailableByteOffset, alignof(Type));
			outputOffset_ = uint32_t(nextAvailableByteOffset) + outputPaddingSize_;
		}
		void Gather(const Context& ctx, uint8_t* pDestBlob) const override
		{
			constexpr auto pSubstruct = GetSubstructurePointer<pMember>();
			if constexpr (std::is_array_v<Type>) {
				const auto val = (ctx.pSourceFrameData->*pSubstruct.*pMember)[inputIndex_];
				reinterpret_cast<std::remove_const_t<decltype(val)>&>(pDestBlob[outputOffset_]) = val;
			}
			else {
				const auto val = ctx.pSourceFrameData->*pSubstruct.*pMember;
				reinterpret_cast<std::remove_const_t<decltype(val)>&>(pDestBlob[outputOffset_]) = val;
			}
		}
		uint32_t GetBeginOffset() const override
		{
			return outputOffset_ - outputPaddingSize_;
		}
		uint32_t GetEndOffset() const override
		{
			return outputOffset_ + alignof(Type);
		}
		uint32_t GetOutputOffset() const override
		{
			return outputOffset_;
		}
	private:
		uint32_t outputOffset_;
		uint16_t outputPaddingSize_;
		uint16_t inputIndex_;
	};
	template<uint64_t PmNsmPresentEvent::* pMember>
	class QpcDurationGatherCommand_ : public pmon::mid::GatherCommand_
	{
	public:
		QpcDurationGatherCommand_(size_t nextAvailableByteOffset)
		{
			outputPaddingSize_ = (uint16_t)util::GetPadding(nextAvailableByteOffset, alignof(double));
			outputOffset_ = uint32_t(nextAvailableByteOffset) + outputPaddingSize_;
		}
		void Gather(const Context& ctx, uint8_t* pDestBlob) const override
		{
			const auto qpcDuration = ctx.pSourceFrameData->present_event.*pMember;
			if (qpcDuration != 0) {
				const auto val = ctx.performanceCounterPeriodMs * double(qpcDuration);
				reinterpret_cast<double&>(pDestBlob[outputOffset_]) = val;
			}
			else {
				reinterpret_cast<double&>(pDestBlob[outputOffset_]) = 0.;
			}
		}
		uint32_t GetBeginOffset() const override
		{
			return outputOffset_ - outputPaddingSize_;
		}
		uint32_t GetEndOffset() const override
		{
			return outputOffset_ + alignof(double);
		}
		uint32_t GetOutputOffset() const override
		{
			return outputOffset_;
		}
	private:
		uint32_t outputOffset_;
		uint16_t outputPaddingSize_;
	};
	template<uint64_t PmNsmPresentEvent::* pStart, uint64_t PmNsmPresentEvent::* pEnd, bool doZeroCheck, bool doDroppedCheck, bool allowNegative, bool clampZero>
	class QpcDifferenceGatherCommand_ : public pmon::mid::GatherCommand_
	{
	public:
		QpcDifferenceGatherCommand_(size_t nextAvailableByteOffset)
		{
			outputPaddingSize_ = (uint16_t)util::GetPadding(nextAvailableByteOffset, alignof(double));
			outputOffset_ = uint32_t(nextAvailableByteOffset) + outputPaddingSize_;
		}
		void Gather(const Context& ctx, uint8_t* pDestBlob) const override
		{
			static_assert(!allowNegative || !clampZero);
			if constexpr (doDroppedCheck) {
				if (ctx.dropped) {
					reinterpret_cast<double&>(pDestBlob[outputOffset_]) =
						std::numeric_limits<double>::quiet_NaN();
					return;
				}
			}
			uint64_t start = ctx.pSourceFrameData->present_event.*pStart;
			if constexpr (doZeroCheck) {
				if (start == 0ull) {
					reinterpret_cast<double&>(pDestBlob[outputOffset_]) =
						std::numeric_limits<double>::quiet_NaN();
					return;
				}
			}
			if constexpr (allowNegative || clampZero) {
				auto qpcDurationDouble = double(ctx.pSourceFrameData->present_event.*pEnd) - double(start);
				if constexpr (clampZero) {
					qpcDurationDouble = std::max(0., qpcDurationDouble);
				}
				const auto val = ctx.performanceCounterPeriodMs * qpcDurationDouble;
				reinterpret_cast<double&>(pDestBlob[outputOffset_]) = val;
			}
			else {
				const auto qpcDuration = ctx.pSourceFrameData->present_event.*pEnd - start;
				const auto val = ctx.performanceCounterPeriodMs * double(qpcDuration);
				reinterpret_cast<double&>(pDestBlob[outputOffset_]) = val;
			}
		}
		uint32_t GetBeginOffset() const override
		{
			return outputOffset_ - outputPaddingSize_;
		}
		uint32_t GetEndOffset() const override
		{
			return outputOffset_ + alignof(double);
		}
		uint32_t GetOutputOffset() const override
		{
			return outputOffset_;
		}
	private:
		uint32_t outputOffset_;
		uint16_t outputPaddingSize_;
	};
	class DroppedGatherCommand_ : public pmon::mid::GatherCommand_
	{
	public:
		DroppedGatherCommand_(size_t nextAvailableByteOffset) : outputOffset_{ (uint32_t)nextAvailableByteOffset } {}
		void Gather(const Context& ctx, uint8_t* pDestBlob) const override
		{
			reinterpret_cast<bool&>(pDestBlob[outputOffset_]) = ctx.dropped;
		}
		uint32_t GetBeginOffset() const override
		{
			return outputOffset_;
		}
		uint32_t GetEndOffset() const override
		{
			return outputOffset_ + alignof(bool);
		}
		uint32_t GetOutputOffset() const override
		{
			return outputOffset_;
		}
	private:
		uint32_t outputOffset_;
	};
	template<uint64_t PmNsmPresentEvent::* pEnd>
	class StartDifferenceGatherCommand_ : public pmon::mid::GatherCommand_
	{
	public:
		StartDifferenceGatherCommand_(size_t nextAvailableByteOffset)
		{
			outputPaddingSize_ = (uint16_t)util::GetPadding(nextAvailableByteOffset, alignof(double));
			outputOffset_ = uint32_t(nextAvailableByteOffset) + outputPaddingSize_;
		}
		void Gather(const Context& ctx, uint8_t* pDestBlob) const override
		{
			const auto qpcDuration = ctx.pSourceFrameData->present_event.*pEnd - ctx.qpcStart;
			const auto val = ctx.performanceCounterPeriodMs * double(qpcDuration);
			reinterpret_cast<double&>(pDestBlob[outputOffset_]) = val;
		}
		uint32_t GetBeginOffset() const override
		{
			return outputOffset_ - outputPaddingSize_;
		}
		uint32_t GetEndOffset() const override
		{
			return outputOffset_ + alignof(double);
		}
		uint32_t GetOutputOffset() const override
		{
			return outputOffset_;
		}
	private:
		uint32_t outputOffset_;
		uint16_t outputPaddingSize_;
	};
	class CpuFrameQpcGatherCommand_ : public pmon::mid::GatherCommand_
	{
	public:
		CpuFrameQpcGatherCommand_(size_t nextAvailableByteOffset) : outputOffset_{ (uint32_t)nextAvailableByteOffset } {}
		void Gather(const Context& ctx, uint8_t* pDestBlob) const override
		{
			reinterpret_cast<uint64_t&>(pDestBlob[outputOffset_]) = ctx.cpuFrameQpc;
		}
		uint32_t GetBeginOffset() const override
		{
			return outputOffset_;
		}
		uint32_t GetEndOffset() const override
		{
			return outputOffset_ + alignof(uint64_t);
		}
		uint32_t GetOutputOffset() const override
		{
			return outputOffset_;
		}
	private:
		uint32_t outputOffset_;
	};
	template<uint64_t PmNsmPresentEvent::* pEnd, bool doDroppedCheck>
	class CpuFrameQpcDifferenceGatherCommand_ : public pmon::mid::GatherCommand_
	{
	public:
		CpuFrameQpcDifferenceGatherCommand_(size_t nextAvailableByteOffset)
		{
			outputPaddingSize_ = (uint16_t)util::GetPadding(nextAvailableByteOffset, alignof(double));
			outputOffset_ = uint32_t(nextAvailableByteOffset) + outputPaddingSize_;
		}
		void Gather(const Context& ctx, uint8_t* pDestBlob) const override
		{
			if constexpr (doDroppedCheck) {
				if (ctx.dropped) {
					reinterpret_cast<double&>(pDestBlob[outputOffset_]) =
						std::numeric_limits<double>::quiet_NaN();
					return;
				}
			}
			const auto qpcDuration = ctx.pSourceFrameData->present_event.*pEnd - ctx.cpuFrameQpc;
			const auto val = ctx.performanceCounterPeriodMs * double(qpcDuration);
			reinterpret_cast<double&>(pDestBlob[outputOffset_]) = val;
		}
		uint32_t GetBeginOffset() const override
		{
			return outputOffset_ - outputPaddingSize_;
		}
		uint32_t GetEndOffset() const override
		{
			return outputOffset_ + alignof(double);
		}
		uint32_t GetOutputOffset() const override
		{
			return outputOffset_;
		}
	private:
		uint32_t outputOffset_;
		uint16_t outputPaddingSize_;
	};
	template<uint64_t PmNsmPresentEvent::* pStart, bool doDroppedCheck>
	class DisplayDifferenceGatherCommand_ : public pmon::mid::GatherCommand_
	{
	public:
		DisplayDifferenceGatherCommand_(size_t nextAvailableByteOffset)
		{
			outputPaddingSize_ = (uint16_t)util::GetPadding(nextAvailableByteOffset, alignof(double));
			outputOffset_ = uint32_t(nextAvailableByteOffset) + outputPaddingSize_;
		}
		void Gather(const Context& ctx, uint8_t* pDestBlob) const override
		{
			if constexpr (doDroppedCheck) {
				if (ctx.dropped) {
					reinterpret_cast<double&>(pDestBlob[outputOffset_]) =
						std::numeric_limits<double>::quiet_NaN();
					return;
				}
			}
			const auto qpcDuration = ctx.nextDisplayedQpc - ctx.pSourceFrameData->present_event.*pStart;
			const auto val = ctx.performanceCounterPeriodMs * double(qpcDuration);
			reinterpret_cast<double&>(pDestBlob[outputOffset_]) = val;
		}
		uint32_t GetBeginOffset() const override
		{
			return outputOffset_ - outputPaddingSize_;
		}
		uint32_t GetEndOffset() const override
		{
			return outputOffset_ + alignof(double);
		}
		uint32_t GetOutputOffset() const override
		{
			return outputOffset_;
		}
	private:
		uint32_t outputOffset_;
		uint16_t outputPaddingSize_;
	};
	class CpuFrameQpcFrameTimeCommand_ : public pmon::mid::GatherCommand_
	{
	public:
		CpuFrameQpcFrameTimeCommand_(size_t nextAvailableByteOffset) : outputOffset_{ (uint32_t)nextAvailableByteOffset } {}
		void Gather(const Context& ctx, uint8_t* pDestBlob) const override
		{
			const auto qpcDuration = (ctx.pSourceFrameData->present_event.PresentStartTime - ctx.cpuFrameQpc) + 
				ctx.pSourceFrameData->present_event.TimeInPresent;
			const auto val = ctx.performanceCounterPeriodMs * double(qpcDuration);
			reinterpret_cast<double&>(pDestBlob[outputOffset_]) = val;
		}
		uint32_t GetBeginOffset() const override
		{
			return outputOffset_;
		}
		uint32_t GetEndOffset() const override
		{
			return outputOffset_ + alignof(uint64_t);
		}
		uint32_t GetOutputOffset() const override
		{
			return outputOffset_;
		}
	private:
		uint32_t outputOffset_;
	};
	class GpuWaitGatherCommand_ : public pmon::mid::GatherCommand_
	{
	public:
		GpuWaitGatherCommand_(size_t nextAvailableByteOffset) : outputOffset_{ (uint32_t)nextAvailableByteOffset } {}
		void Gather(const Context& ctx, uint8_t* pDestBlob) const override
		{
			const auto qpcDuration = (ctx.pSourceFrameData->present_event.ReadyTime - ctx.pSourceFrameData->present_event.GPUStartTime) -
				ctx.pSourceFrameData->present_event.GPUDuration;
			const auto val = std::max(0., ctx.performanceCounterPeriodMs * double(qpcDuration));
			reinterpret_cast<double&>(pDestBlob[outputOffset_]) = val;
		}
		uint32_t GetBeginOffset() const override
		{
			return outputOffset_;
		}
		uint32_t GetEndOffset() const override
		{
			return outputOffset_ + alignof(uint64_t);
		}
		uint32_t GetOutputOffset() const override
		{
			return outputOffset_;
		}
	private:
		uint32_t outputOffset_;
	};

}

PM_FRAME_QUERY::PM_FRAME_QUERY(std::span<PM_QUERY_ELEMENT> queryElements)
{
	// TODO: validation
	//	only allow array index zero if not array type in nsm
	//	fail if array index out of bounds
	//  fail if any metrics aren't event-compatible
	//  fail if any stats other than NONE are specified
	//  fail if intro size doesn't match nsm size
	
	// we need to keep track of how many non-universal devices are specified
	// current release: only 1 gpu device maybe be polled at a time
	for (auto& q : queryElements) {
		// validate that maximum 1 device (gpu) id is specified throughout the query
		if (q.deviceId != 0) {
			if (!referencedDevice_) {
				referencedDevice_ = q.deviceId;
			}
			else if (*referencedDevice_ != q.deviceId) {
				throw std::runtime_error{ "Cannot specify 2 different non-universal devices in the same query" };
			}
		}
		gatherCommands_.push_back(MapQueryElementToGatherCommand_(q, blobSize_));
		const auto& cmd = gatherCommands_.back();
		q.dataSize = cmd->GetDataSize();
		q.dataOffset = cmd->GetOutputOffset();
		blobSize_ += cmd->GetTotalSize();
	}
	// make sure blobs are a multiple of 16 so that blobs in array always start 16-aligned
	blobSize_ += util::GetPadding(blobSize_, 16);
}

PM_FRAME_QUERY::~PM_FRAME_QUERY() = default;

void PM_FRAME_QUERY::GatherToBlob(const Context& ctx, uint8_t* pDestBlob) const
{
	for (auto& cmd : gatherCommands_) {
		cmd->Gather(ctx, pDestBlob);
	}
}

size_t PM_FRAME_QUERY::GetBlobSize() const
{
	return blobSize_;
}

std::optional<uint32_t> PM_FRAME_QUERY::GetReferencedDevice() const
{
	return referencedDevice_;
}

std::unique_ptr<mid::GatherCommand_> PM_FRAME_QUERY::MapQueryElementToGatherCommand_(const PM_QUERY_ELEMENT& q, size_t pos)
{
	using Pre = PmNsmPresentEvent;
	using Gpu = PresentMonPowerTelemetryInfo;
	using Cpu = CpuTelemetryInfo;

	switch (q.metric) {
	// temporary static metric lookup via nsm
	// only implementing the ones used by appcef right now... others available in the future
	// TODO: implement fill for all static OR drop support for filling static
	case PM_METRIC_GPU_MEM_SIZE:
		return std::make_unique<CopyGatherCommand_<&Gpu::gpu_mem_total_size_b>>(pos);
	case PM_METRIC_GPU_MEM_MAX_BANDWIDTH:
		return std::make_unique<CopyGatherCommand_<&Gpu::gpu_mem_max_bandwidth_bps>>(pos);

	case PM_METRIC_SWAP_CHAIN_ADDRESS:
		return std::make_unique<CopyGatherCommand_<&Pre::SwapChainAddress>>(pos);
	case PM_METRIC_GPU_BUSY:
		return std::make_unique<QpcDurationGatherCommand_<&Pre::GPUDuration>>(pos);
	case PM_METRIC_DROPPED_FRAMES:
		return std::make_unique<DroppedGatherCommand_>(pos);
	case PM_METRIC_PRESENT_MODE:
		return std::make_unique<CopyGatherCommand_<&Pre::PresentMode>>(pos);
	case PM_METRIC_PRESENT_RUNTIME:
		return std::make_unique<CopyGatherCommand_<&Pre::Runtime>>(pos);
	case PM_METRIC_CPU_FRAME_QPC:
		return std::make_unique<CpuFrameQpcGatherCommand_>(pos);
	case PM_METRIC_ALLOWS_TEARING:
		return std::make_unique<CopyGatherCommand_<&Pre::SupportsTearing>>(pos);
	case PM_METRIC_SYNC_INTERVAL:
		return std::make_unique<CopyGatherCommand_<&Pre::SyncInterval>>(pos);

	case PM_METRIC_GPU_POWER:
		return std::make_unique<CopyGatherCommand_<&Gpu::gpu_power_w>>(pos);
	case PM_METRIC_GPU_VOLTAGE:
		return std::make_unique<CopyGatherCommand_<&Gpu::gpu_voltage_v>>(pos);
	case PM_METRIC_GPU_FREQUENCY:
		return std::make_unique<CopyGatherCommand_<&Gpu::gpu_frequency_mhz>>(pos);
	case PM_METRIC_GPU_TEMPERATURE:
		return std::make_unique<CopyGatherCommand_<&Gpu::gpu_temperature_c>>(pos);
	case PM_METRIC_GPU_FAN_SPEED:
		return std::make_unique<CopyGatherCommand_<&Gpu::fan_speed_rpm>>(pos, q.arrayIndex);
	case PM_METRIC_GPU_UTILIZATION:
		return std::make_unique<CopyGatherCommand_<&Gpu::gpu_utilization>>(pos);
	case PM_METRIC_GPU_RENDER_COMPUTE_UTILIZATION:
		return std::make_unique<CopyGatherCommand_<&Gpu::gpu_render_compute_utilization>>(pos);
	case PM_METRIC_GPU_MEDIA_UTILIZATION:
		return std::make_unique<CopyGatherCommand_<&Gpu::gpu_media_utilization>>(pos);
	case PM_METRIC_GPU_MEM_POWER:
		return std::make_unique<CopyGatherCommand_<&Gpu::vram_power_w>>(pos);
	case PM_METRIC_GPU_MEM_VOLTAGE:
		return std::make_unique<CopyGatherCommand_<&Gpu::vram_voltage_v>>(pos);
	case PM_METRIC_GPU_MEM_FREQUENCY:
		return std::make_unique<CopyGatherCommand_<&Gpu::vram_frequency_mhz>>(pos);
	case PM_METRIC_GPU_MEM_EFFECTIVE_FREQUENCY:
		return std::make_unique<CopyGatherCommand_<&Gpu::vram_effective_frequency_gbps>>(pos);
	case PM_METRIC_GPU_MEM_TEMPERATURE:
		return std::make_unique<CopyGatherCommand_<&Gpu::vram_temperature_c>>(pos);
	case PM_METRIC_GPU_MEM_USED:
		return std::make_unique<CopyGatherCommand_<&Gpu::gpu_mem_used_b>>(pos);
	case PM_METRIC_GPU_MEM_WRITE_BANDWIDTH:
		return std::make_unique<CopyGatherCommand_<&Gpu::gpu_mem_write_bandwidth_bps>>(pos);
	case PM_METRIC_GPU_MEM_READ_BANDWIDTH:
		return std::make_unique<CopyGatherCommand_<&Gpu::gpu_mem_read_bandwidth_bps>>(pos);
	case PM_METRIC_GPU_POWER_LIMITED:
		return std::make_unique<CopyGatherCommand_<&Gpu::gpu_power_limited>>(pos);
	case PM_METRIC_GPU_TEMPERATURE_LIMITED:
		return std::make_unique<CopyGatherCommand_<&Gpu::gpu_temperature_limited>>(pos);
	case PM_METRIC_GPU_CURRENT_LIMITED:
		return std::make_unique<CopyGatherCommand_<&Gpu::gpu_current_limited>>(pos);
	case PM_METRIC_GPU_VOLTAGE_LIMITED:
		return std::make_unique<CopyGatherCommand_<&Gpu::gpu_voltage_limited>>(pos);
	case PM_METRIC_GPU_UTILIZATION_LIMITED:
		return std::make_unique<CopyGatherCommand_<&Gpu::gpu_utilization_limited>>(pos);
	case PM_METRIC_GPU_MEM_POWER_LIMITED:
		return std::make_unique<CopyGatherCommand_<&Gpu::vram_power_limited>>(pos);
	case PM_METRIC_GPU_MEM_TEMPERATURE_LIMITED:
		return std::make_unique<CopyGatherCommand_<&Gpu::vram_temperature_limited>>(pos);
	case PM_METRIC_GPU_MEM_CURRENT_LIMITED:
		return std::make_unique<CopyGatherCommand_<&Gpu::vram_current_limited>>(pos);
	case PM_METRIC_GPU_MEM_VOLTAGE_LIMITED:
		return std::make_unique<CopyGatherCommand_<&Gpu::vram_voltage_limited>>(pos);
	case PM_METRIC_GPU_MEM_UTILIZATION_LIMITED:
		return std::make_unique<CopyGatherCommand_<&Gpu::vram_utilization_limited>>(pos);

	case PM_METRIC_CPU_UTILIZATION:
		return std::make_unique<CopyGatherCommand_<&Cpu::cpu_utilization>>(pos);
	case PM_METRIC_CPU_POWER:
		return std::make_unique<CopyGatherCommand_<&Cpu::cpu_power_w>>(pos);
	case PM_METRIC_CPU_TEMPERATURE:
		return std::make_unique<CopyGatherCommand_<&Cpu::cpu_temperature>>(pos);
	case PM_METRIC_CPU_FREQUENCY:
		return std::make_unique<CopyGatherCommand_<&Cpu::cpu_frequency>>(pos);

	case PM_METRIC_PRESENT_FLAGS:
		return std::make_unique<CopyGatherCommand_<&Pre::PresentFlags>>(pos);
	case PM_METRIC_TIME:
		return std::make_unique<StartDifferenceGatherCommand_<&Pre::PresentStartTime>>(pos);
	case PM_METRIC_FRAME_TIME:
		return std::make_unique<CpuFrameQpcFrameTimeCommand_>(pos);
	case PM_METRIC_CPU_BUSY:
		return std::make_unique<CpuFrameQpcDifferenceGatherCommand_<&Pre::PresentStartTime, 0>>(pos);
	case PM_METRIC_CPU_WAIT:
		return std::make_unique<QpcDurationGatherCommand_<&Pre::TimeInPresent>>(pos);
	case PM_METRIC_GPU_TIME:
		return std::make_unique<QpcDifferenceGatherCommand_<&Pre::GPUStartTime, &Pre::ReadyTime, 1, 0, 1, 0>>(pos);
	case PM_METRIC_GPU_WAIT:
		return std::make_unique<GpuWaitGatherCommand_>(pos);
	case PM_METRIC_DISPLAYED_TIME:
		return std::make_unique<DisplayDifferenceGatherCommand_<&Pre::ScreenTime, 1>>(pos);
	case PM_METRIC_GPU_LATENCY:
		return std::make_unique<CpuFrameQpcDifferenceGatherCommand_<&Pre::GPUStartTime, 0>>(pos);
	case PM_METRIC_DISPLAY_LATENCY:
		return std::make_unique<CpuFrameQpcDifferenceGatherCommand_<&Pre::ScreenTime, 1>>(pos);
	case PM_METRIC_CLICK_TO_PHOTON_LATENCY:
		return std::make_unique<QpcDifferenceGatherCommand_<&Pre::InputTime, &Pre::ScreenTime, 1, 1, 0, 0>>(pos);

	default: return {};
	}
}

void PM_FRAME_QUERY::Context::UpdateSourceData(const PmNsmFrameData* pSourceFrameData_in, const PmNsmFrameData* pNextDisplayedFrameData, const PmNsmFrameData* pPreviousFrameData)
{
	pSourceFrameData = pSourceFrameData_in;
	dropped = pSourceFrameData->present_event.FinalState != PresentResult::Presented;
	if (pPreviousFrameData) {
		cpuFrameQpc = pPreviousFrameData->present_event.PresentStartTime + pPreviousFrameData->present_event.TimeInPresent;
	}
	else {
		// TODO: log issue or invalidate related columns or drop frame (or some combination)
		cpuFrameQpc = 0;
	}
	if (pNextDisplayedFrameData) {
		nextDisplayedQpc = pNextDisplayedFrameData->present_event.ScreenTime;
	}
	else {
		// TODO: log issue or invalidate related columns or drop frame (or some combination)
		nextDisplayedQpc = 0;
	}
}
