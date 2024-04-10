// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: MIT
#pragma once
#include "../AsyncEndpoint.h"
#include "../../DataBindAccessor.h"
#include <Core/source/infra/log/Logging.h>
#include "../CefValues.h"
#include <CommonUtilities\str\String.h>

namespace p2c::client::util::async
{
    class LaunchKernel : public AsyncEndpoint
    {
    public:
        static constexpr std::string GetKey() { return "launchKernel"; }
        LaunchKernel() : AsyncEndpoint{ AsyncEndpoint::Environment::RenderImmediate } {}
        // {} => null
        void ExecuteOnRenderAccessor(uint64_t uid, CefRefPtr<CefValue> pArgObj, cef::DataBindAccessor& accessor) const override
        {
            using ::pmon::util::str::ToWide;

            try {
                accessor.LaunchKernel();
            }
            catch (const std::exception& e) {
                const auto pExName = typeid(e).name();
                auto result = AsyncEndpoint::MakeStringErrorResult(ToWide(
                    std::format("Async API endpoint [launchKernel] failed with exception [{}]: {}", pExName, e.what())
                ));
                accessor.ResolveAsyncEndpoint(uid, result.succeeded, std::move(result.pArgs));
            }
            catch (...) {
                auto result = AsyncEndpoint::MakeStringErrorResult(L"Async API endpoint [launchKernel] failed");
                accessor.ResolveAsyncEndpoint(uid, result.succeeded, std::move(result.pArgs));
            }
            accessor.ResolveAsyncEndpoint(uid, true, CefValueNull());
        }
    };
}