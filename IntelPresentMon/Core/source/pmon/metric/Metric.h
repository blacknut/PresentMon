// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: MIT
#pragma once
#include <string>
#include <optional>

namespace p2c::gfx::lay
{
    class GraphData;
}

namespace p2c::pmon
{
    class Metric
    {
    public:
        // types
        struct Info
        {
            size_t index;
            // name of the metric category (corresponds to each API metric endpoint function)
            std::wstring category;
            // name of the metric (CPU FPS, GPU Power, etc.)
            std::wstring name;
            // statistic type (avg, min, 99%, etc.)
            std::wstring statType;
            // W(atts), V(olts), FPS, etc.
            std::wstring units;
            // metric interface class name (Text / Numeric)
            std::wstring className;
        };
        // functions
        Metric(std::wstring name, std::wstring units);
        virtual ~Metric();
        virtual std::wstring ReadStringValue(double timestamp);
        virtual const std::wstring& GetCategory() const = 0;
        virtual std::wstring GetStatName() const;
        const std::wstring& GetName() const;
        const std::wstring& GetUnits() const;
        virtual const std::wstring& GetMetricClassName() const = 0;
        virtual std::optional<float> ReadValue(double timestamp) = 0;
        virtual void PopulateData(gfx::lay::GraphData& data, double timestamp) = 0;
        Info GetInfo(size_t index) const;
    private:
        std::wstring name;
        std::wstring units;
    };
}