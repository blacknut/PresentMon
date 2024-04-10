// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <vector>
#include <optional>
#include <bitset>
#include "PresentMonPowerTelemetry.h"
#include "../PresentMonAPI2/PresentMonAPI.h"

namespace pwr
{
    class PowerTelemetryAdapter
    {
    public:
        virtual ~PowerTelemetryAdapter() = default;
        virtual bool Sample() noexcept = 0;
        virtual std::optional<PresentMonPowerTelemetryInfo> GetClosest(uint64_t qpc) const noexcept = 0;
        virtual PM_DEVICE_VENDOR GetVendor() const noexcept = 0;
        virtual std::string GetName() const noexcept = 0;
        virtual uint64_t GetDedicatedVideoMemory() const noexcept = 0;
        virtual uint64_t GetVideoMemoryMaxBandwidth() const noexcept = 0;
        virtual double GetSustainedPowerLimit() const noexcept = 0;
        
        void SetTelemetryCapBit(GpuTelemetryCapBits telemetryCapBit) noexcept
        {
            gpuTelemetryCapBits_.set(static_cast<size_t>(telemetryCapBit));
        }
        std::bitset<
            static_cast<size_t>(GpuTelemetryCapBits::gpu_telemetry_count)>
        GetPowerTelemetryCapBits()
        {
            return gpuTelemetryCapBits_;
        }
        // constants
        static constexpr size_t defaultHistorySize = 300;

       private:
        // data
        std::bitset<static_cast<size_t>(GpuTelemetryCapBits::gpu_telemetry_count)>
            gpuTelemetryCapBits_{};
    };
}