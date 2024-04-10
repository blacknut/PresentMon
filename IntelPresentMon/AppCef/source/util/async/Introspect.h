// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: MIT
#pragma once
#include "../AsyncEndpoint.h"
#include <Core/source/kernel/Kernel.h>
#include "../CefValues.h"
#include <PresentMonAPIWrapper/PresentMonAPIWrapper.h>
#include <ranges>
#include <array>

namespace p2c::client::util::async
{
    class Introspect : public AsyncEndpoint
    {
    public:
        static constexpr std::string GetKey() { return "introspect"; }
        Introspect() : AsyncEndpoint{ AsyncEndpoint::Environment::KernelTask } {}
        // {} => {
        //     metrics: [{id: number, name: string, description: string, availableDeviceIds: number[], preferredUnitId: number, arraySize: number, availableStatIds: number[], numeric: boolean}],
        //     stats: [{id: number, name: string, shortName: string, description: string}],
        //     units: [{id: number, name: string, shortName: string, description: string}],
        // }
        Result ExecuteOnKernelTask(uint64_t uid, CefRefPtr<CefValue> pArgObj, kern::Kernel& kernel) const override
        {
            using namespace std::string_literals;
            namespace vi = std::views;
            namespace rn = std::ranges;

            auto& intro = kernel.GetIntrospectionRoot();

            //  --- metrics ---
            // set of types that are numeric, used to generate numeric flag that the frontend uses
            const std::array numericTypes{ PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_UINT32, PM_DATA_TYPE_INT32, PM_DATA_TYPE_UINT64 };
            // filter predicate to only pick up metrics usable in dynamic queries (plus hardcoded blacklist)
            const auto filterPred = [](const pmapi::intro::MetricView& m) { const auto type = m.GetType();
                return
                    (   m.GetId() != PM_METRIC_GPU_LATENCY &&
                        m.GetId() != PM_METRIC_DISPLAY_LATENCY)
                    &&
                    (   type == PM_METRIC_TYPE_DYNAMIC ||
                        type == PM_METRIC_TYPE_DYNAMIC_FRAME ||
                        type == PM_METRIC_TYPE_STATIC);
            };
            // we must know the size of the array up-front for a cef list, so we traverse filtered range
            auto metricListCef = MakeCefList(rn::distance(intro.GetMetrics() | vi::filter(filterPred)));
            // now process each applicable metric, filtering ones not usable for dynamic queries
            for (auto&&[i, m] : intro.GetMetrics() | vi::filter(filterPred) | vi::enumerate) {
                // array size: stopgap measure to use largest among all available devices
                // will replace this with per-device size when loadout per-line device selection
                // and per-line array index selection is implemented
                int arraySize = 0;
                // generate device list
                size_t availableDeviceCount = 0;
                for (auto&& d : m.GetDeviceMetricInfo()) {
                    if (d.IsAvailable()) availableDeviceCount++;
                }
                auto metricDeviceListCef = MakeCefList(availableDeviceCount);
                for (auto&& [i, d] : m.GetDeviceMetricInfo() | vi::enumerate) {
                    if (!d.IsAvailable()) continue;
                    arraySize = std::max(arraySize, (int)d.GetArraySize());
                    metricDeviceListCef->SetInt(i, (int)d.GetDevice().GetId());
                }
                // generate stat list
                auto metricStatListCef = MakeCefList(m.GetStatInfo().size());
                for (auto&& [i, s] : m.GetStatInfo() | vi::enumerate) {
                    metricStatListCef->SetInt(i, (int)s.GetStat());
                }
                // add metric
                metricListCef->SetValue(i, MakeCefObject(
                    CefProp{ "id", m.GetId() },
                    CefProp{ "name", m.Introspect().GetName() },
                    CefProp{ "description", m.Introspect().GetDescription() },
                    CefProp{ "availableDeviceIds", CefValueDecay(std::move(metricDeviceListCef)) },
                    // TODO: populate this, currently not used so setting to 0
                    CefProp{ "preferredUnitId", 0 },
                    CefProp{ "arraySize", arraySize },
                    CefProp{ "availableStatIds", CefValueDecay(std::move(metricStatListCef)) },
                    CefProp{ "numeric", rn::contains(numericTypes, m.GetDataTypeInfo().GetPolledType()) }
                ));
            }
            // --- stats ---
            auto&& statRange = intro.FindEnum(PM_ENUM_STAT).GetKeys();
            auto statListCef = MakeCefList(statRange.size());
            for (auto&& [i, s] : statRange | vi::enumerate) {
                statListCef->SetValue(i, MakeCefObject(
                    CefProp{ "id", s.GetId() },
                    CefProp{ "name", s.GetName() },
                    CefProp{ "shortName", s.GetShortName() },
                    CefProp{ "description", s.GetDescription() }
                ));
            }
            // --- units --- (empty/dummy because not used yet)
            auto unitListCef = MakeCefList(0);

            return { true, MakeCefObject(
                CefProp{ "metrics", CefValueDecay(std::move(metricListCef)) },
                CefProp{ "stats", CefValueDecay(std::move(statListCef)) },
                CefProp{ "units", CefValueDecay(std::move(unitListCef)) }
            ) };
        }
    };
}