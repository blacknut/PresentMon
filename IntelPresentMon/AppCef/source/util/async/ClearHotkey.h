// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: MIT
#pragma once
#include "../AsyncEndpoint.h"
#include <Core/source/kernel/Kernel.h>
#include <include/cef_task.h>
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include "../../DataBindAccessor.h"
#include <Core/source/infra/log/Logging.h>
#include "../CefValues.h"

namespace p2c::client::util::async
{
    class ClearHotkey : public AsyncEndpoint
    {
    public:
        static constexpr std::string GetKey() { return "clearHotkey"; }
        ClearHotkey() : AsyncEndpoint{ AsyncEndpoint::Environment::RenderImmediate } {}
        // {action:int} => null
        void ExecuteOnRenderAccessor(uint64_t uid, CefRefPtr<CefValue> pArgObj, cef::DataBindAccessor& accessor) const override
        {
            accessor.ClearHotkey(*pArgObj, [uid = uid, pAccessor = &accessor](bool succeeded) {
                CefPostTask(TID_RENDERER, base::BindOnce(Resolve_, uid, succeeded, CefRefPtr<cef::DataBindAccessor>{ pAccessor }));
            });
        }
    private:
        static void Resolve_(uint64_t uid, bool succeeded, CefRefPtr<cef::DataBindAccessor> pAccessor)
        {
            if (succeeded) {
                pAccessor->ResolveAsyncEndpoint(uid, true, CefValueNull());
            }
            else {
                auto result = AsyncEndpoint::MakeStringErrorResult(L"Async API endpoint [clearHotkey] failed");
                pAccessor->ResolveAsyncEndpoint(uid, result.succeeded, std::move(result.pArgs));
            }
        }
    };
}