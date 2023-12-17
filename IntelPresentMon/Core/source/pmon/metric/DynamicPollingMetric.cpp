#include "DynamicPollingMetric.h"
#include <PresentMonAPIWrapper/source/PresentMonAPIWrapper.h>
#include <CommonUtilities/source/str/String.h>
#include <ranges>
#include "../EnumMap.h" 

namespace p2c::pmon::met
{
    using ::pmon::util::str::ToWide;
    DynamicPollingMetric::DynamicPollingMetric(PM_METRIC metricId_, uint32_t arrayIndex_, PM_STAT stat_,
        const pmapi::intro::Root& introRoot)
        :
        Metric{
            MakeMetricName_(metricId_, arrayIndex_, introRoot),
            ToWide(introRoot.FindMetric(metricId_).IntrospectUnit().GetShortName())
        },
        metricId{ metricId_ },
        stat{ stat_ },
        arrayIndex{ arrayIndex_ },
        statName{ ToWide(introRoot.FindEnumKey(PM_ENUM_STAT, stat).GetShortName()) }
    {
        const auto type = introRoot.FindMetric(metricId).GetDataTypeInfo().GetPolledType();
        if (type == PM_DATA_TYPE_STRING || type == PM_DATA_TYPE_ENUM) {
            numeric = false;
        }
    }
    std::wstring DynamicPollingMetric::GetStatName() const { return statName; }
    const std::wstring& DynamicPollingMetric::GetCategory() const
    {
        static std::wstring cat = L"";
        return cat;
    }
    const std::wstring& DynamicPollingMetric::GetMetricClassName() const
    {
        if (numeric) {
            static std::wstring cls = L"Numeric";
            return cls;
        }
        else {
            static std::wstring cls = L"Text";
            return cls;
        }
    }
    PM_QUERY_ELEMENT DynamicPollingMetric::MakeQueryElement() const
    {
        return PM_QUERY_ELEMENT{
            .metric = metricId,
            .stat = stat,
            .deviceId = deviceId,
            .arrayIndex = arrayIndex,
        };
    }
    void DynamicPollingMetric::Finalize(uint32_t offset_)
    {
        offset = offset_;
    }
    std::unique_ptr<DynamicPollingMetric> DynamicPollingMetric::RealizeMetric(const pmapi::intro::Root& introRoot,
        CachingQuery* pQuery, uint32_t activeGpuDeviceId)
    {
        namespace rn = std::ranges;
        const auto metricId = this->metricId;
        const auto metricIntro = introRoot.FindMetric(metricId);
        const auto dataTypeInfo = metricIntro.GetDataTypeInfo();
        const auto dataTypeId = dataTypeInfo.GetPolledType();
        // if we determine a metric is targeting a gpu, use the activeGpuId instead of universal device (0)
        const bool isGpuMetric = rn::any_of(metricIntro.GetDeviceMetricInfo(), [](auto&& info) {
            return info.GetDevice().GetType() == PM_DEVICE_TYPE_GRAPHICS_ADAPTER;
        });
        const auto deviceId = isGpuMetric ? activeGpuDeviceId : 0u;
        switch (dataTypeId) {
        case PM_DATA_TYPE_BOOL:
            return std::make_unique<met::TypedDynamicPollingMetric<bool>>(*this, pQuery, deviceId);
        case PM_DATA_TYPE_INT32:
            return std::make_unique<met::TypedDynamicPollingMetric<int32_t>>(*this, pQuery, deviceId);
        case PM_DATA_TYPE_UINT32:
            return std::make_unique<met::TypedDynamicPollingMetric<uint32_t>>(*this, pQuery, deviceId);
        case PM_DATA_TYPE_UINT64:
            return std::make_unique<met::TypedDynamicPollingMetric<uint64_t>>(*this, pQuery, deviceId);
        case PM_DATA_TYPE_DOUBLE:
            return std::make_unique<met::TypedDynamicPollingMetric<double>>(*this, pQuery, deviceId);
        case PM_DATA_TYPE_STRING:
            return std::make_unique<met::TypedDynamicPollingMetric<const char*>>(*this, pQuery, deviceId);
        case PM_DATA_TYPE_ENUM:
            return std::make_unique<met::TypedDynamicPollingMetric<PM_ENUM>>(*this, pQuery, deviceId,
                EnumMap::GetMapPtr(dataTypeInfo.GetEnumId()));
        }
        // TODO: maybe throw exception here?
        return {};
    }
    uint32_t DynamicPollingMetric::CalculateMaxArrayIndex(PM_METRIC metricId, const pmapi::intro::Root& introRoot)
    {
        const auto metric = introRoot.FindMetric(metricId);
        auto name = ToWide(metric.Introspect().GetName());
        // find max array size among all devices with availability
        uint32_t arraySize = 0;
        for (auto di : metric.GetDeviceMetricInfo()) {
            if (di.IsAvailable()) {
                arraySize = std::max(arraySize, di.GetArraySize());
            }
        }
        return arraySize;
    }
    std::wstring DynamicPollingMetric::MakeMetricName_(PM_METRIC metricId,
        uint32_t arrayIndex, const pmapi::intro::Root& introRoot)
    {
        const auto metric = introRoot.FindMetric(metricId);
        auto name = ToWide(metric.Introspect().GetName());
        // find max array size among all devices with availability
        uint32_t arraySize = CalculateMaxArrayIndex(metricId, introRoot);
        // add [i] to end of metric name if it's an array metric
        if (arraySize > 1) {
            name += std::format(L" [{}]", arrayIndex);
        }
        return name;
    }
}