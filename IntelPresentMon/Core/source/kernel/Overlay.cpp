// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: MIT
#include "Overlay.h"
#include <Core/source/gfx/layout/GraphElement.h>
#include <Core/source/gfx/layout/FlexElement.h>
#include <Core/source/gfx/layout/TextElement.h>
#include <Core/source/gfx/layout/ReadoutElement.h>
#include <Core/source/pmon/PresentMon.h>
#include <Core/source/pmon/RawFrameDataWriter.h>
#include <Core/source/pmon/Timekeeper.h>
#include <Core/source/gfx/layout/style/StyleProcessor.h>
#include <Core/source/infra/util/rn/ToVector.h>
#include <Core/source/win/StandardWindow.h>
#include <Core/source/win/OverlayWindow.h>
#include <Core/source/infra/opt/Options.h>
#include <ranges>
#include <set>
#include <chrono>
#include <format>
#include <thread>
#include <array>
#include <cassert>
#include "TargetLostException.h"
#include "MetricPackMapper.h"
#include <PresentMonAPIWrapper/StaticQuery.h>


namespace p2c::kern
{
    using namespace gfx;
    using namespace lay;


    // free functions used by Overlay
    namespace
    {
        auto MakeDocument_(
            gfx::Graphics& gfx,
            const OverlaySpec& spec,
            MetricPackMapper& mapper,
            pmon::MetricFetcherFactory& fetcherFactory,
            std::shared_ptr<TextElement>& captureIndicator)
        {
            auto pRoot = FlexElement::Make({}, { "doc" });
            std::shared_ptr<gfx::lay::Element> pReadoutContainer;

            for (const auto& w : spec.widgets) {
                try {
                    if (auto pGraphSpec = std::get_if<GraphSpec>(&w)) {
                        auto packsData = pGraphSpec->metrics |
                            std::views::transform([&](const GraphMetricSpec& gms) {
                                auto metInfo = fetcherFactory.GetMetricInfo(gms.metric);
                                return std::make_shared<GraphLinePack>(GraphLinePack{
                                    .data = mapper[gms.metric].graphData,
                                    .axisAffinity = gms.axisAffinity,
                                    .label = std::move(metInfo.fullName),
                                    .units = std::move(metInfo.unitLabel),
                                });
                            }) | infra::util::rn::ToVector();
                        pRoot->AddChild(GraphElement::Make(
                            pGraphSpec->type, std::move(packsData), { pGraphSpec->tag }
                        ));
                        pReadoutContainer.reset();
                    }
                    else if (auto pReadoutSpec = std::get_if<ReadoutSpec>(&w)) {
                        if (!pReadoutContainer) {
                            pReadoutContainer = gfx::lay::FlexElement::Make({}, { "readout-container" });
                            pRoot->AddChild(pReadoutContainer);
                        }

                        auto metInfo = fetcherFactory.GetMetricInfo(pReadoutSpec->metric);
                        pReadoutContainer->AddChild(gfx::lay::ReadoutElement::Make(
                            metInfo.isNonNumeric, std::move(metInfo.fullName), std::move(metInfo.unitLabel),
                            mapper[pReadoutSpec->metric].textData.get(), { pReadoutSpec->tag }
                        ));
                    }
                    else {
                        throw std::runtime_error{ "Bad widget variant" };
                    }
                }
                catch (...) {
                    p2clog.warn(L"Failed building a widget into document").commit();
                }
            }

            // capture state indicator
            pRoot->AddChild(FlexElement::Make(
                {
                    TextElement::Make(L"Capture Status:", {"label"}),
                    captureIndicator = TextElement::Make(L"-------------------", {"value"}),
                },
                { "cap" }
            ));

            // trigger layout calculation of entire document
            pRoot->FinalizeAsRoot(DimensionsSpec{ (float)spec.overlayWidth }, spec.sheets, gfx);

            return pRoot;
        }
    }



    // Overlay member functions

    Overlay::Overlay(
        win::Process proc_,
        std::shared_ptr<OverlaySpec> pSpec_,
        pmon::PresentMon* pm_,
        std::unique_ptr<MetricPackMapper> pPackMapper_,
        std::optional<gfx::Vec2I> pos_)
        :
        proc{ std::move(proc_) },
        pm{ pm_ },
        pSpec{ std::move(pSpec_) },
        fetcherFactory{ *pm },
        pPackMapper{ std::move(pPackMapper_) },
        hProcess{ OpenProcess(SYNCHRONIZE, TRUE, proc.pid) },
        moveHandlerToken{ win::EventHookManager::AddHandler(std::make_shared<WindowMoveHandler>(proc, this)) },
        activateHandlerToken{ win::EventHookManager::AddHandler(std::make_shared<WindowActivateHandler>(proc, this)) },
        targetRect{ win::GetWindowClientRect(proc.hWnd) },
        position{ pSpec->overlayPosition },
        upscaleFactor{ pSpec->upscale ? pSpec->upscaleFactor : 1.f },
        graphicsDimensions{ pSpec->overlayWidth, 240 },
        windowDimensions{ Dimensions{ graphicsDimensions } * upscaleFactor },
        pWindow{ MakeWindow_(pos_) },
        gfx{ pWindow->GetHandle(), graphicsDimensions, upscaleFactor, infra::opt::get().allowTearing },
        samplingPeriodMs{ pSpec->samplingPeriodMs },
        samplesPerFrame{ pSpec->samplesPerFrame },
        hideDuringCapture{ pSpec->hideDuringCapture },
        hideAlways{ pSpec->hideAlways },
        samplingWaiter{ float(pSpec->samplingPeriodMs) / 1'000.f }
    {
        UpdateDataSets_();
        pRoot = MakeDocument_(gfx, *pSpec, *pPackMapper, fetcherFactory, pCaptureIndicatorText);
        UpdateCaptureStatusText_();
        AdjustOverlaySituation_(position);
        pm->StartTracking(proc.pid);
    }

    Overlay::~Overlay()
    {
        if (pm) {
            try { pm->StopTracking(); }
            catch (...) {}
        }
    }

    void Overlay::UpdateDataSets_()
    {
        // loop all widgets
        for (auto& w : pSpec->widgets) {
            // if widget is a graph
            if (auto pGraphSpec = std::get_if<GraphSpec>(&w)) {
                // loop all lines in graph
                for (auto& gms : pGraphSpec->metrics) {
                    pPackMapper->AddGraph(gms.metric, pSpec->graphDataWindowSize);
                }
            }
            // if widget is a readout
            else if (auto pReadoutSpec = std::get_if<ReadoutSpec>(&w)) {
                pPackMapper->AddReadout(pReadoutSpec->metric);
            }
        }
        // remove stale data packs, register new query, fill new fetchers
        pPackMapper->CommitChanges(proc.pid, pSpec->averagingWindowSize,
            pSpec->metricsOffset, fetcherFactory);
    }

    std::unique_ptr<win::KernelWindow> Overlay::MakeWindow_(std::optional<Vec2I> pos_)
    {
        UpdateTargetFullscreenStatus();
        std::unique_ptr<win::KernelWindow> pWindow;
        if (pSpec->independentKernelWindow) {
            // try and get the position of the control (CEF) window for calculating independent metrics window pos
            // TODO: connect this more assuredly to the cef control window
            // tried CefBrowserHost::GetWindowHandle, but it causes crashes for some unknown reason
            // should make this at least less brittle with respect to window classname / title
            bool bringToFrontOnCreation = false;
            if (!pos_) {
                bringToFrontOnCreation = true;
                pos_ = Vec2I{ CW_USEDEFAULT, CW_USEDEFAULT };
                if (auto hWndControl = FindWindowA("BrowserWindowClass", "Intel PresentMon")) {
                    RECT controlRect{};
                    if (GetWindowRect(hWndControl, &controlRect)) {
                        pos_ = Vec2I{ controlRect.left + 25, controlRect.top + 25 };
                    }
                    else {
                        p2clog.warn(L"failed to get rect of control window").hr().commit();
                    }
                }
                else {
                    p2clog.warn(L"failed to find control window").commit();
                }
            }
            // make the metrics window
            pWindow = std::make_unique<win::StandardWindow>(
                pos_->x, pos_->y,
                windowDimensions,
                L"PresentMon Data Display",
                bringToFrontOnCreation
            );
        }
        else {
            const auto pos = CalculateOverlayPosition_();
            pWindow = std::make_unique<win::OverlayWindow>(
                targetFullscreen,
                pos.x, pos.y,
                windowDimensions,
                L"P2C#OVERLAY"
            );
            // place overlay just above target in the z-order
            pWindow->Reorder(proc.hWnd);
        }
        return pWindow;
    }

    void Overlay::RebuildDocument(std::shared_ptr<OverlaySpec> pSpec_)
    {
        pSpec = std::move(pSpec_);
        UpdateDataSets_();
        pRoot = MakeDocument_(gfx, *pSpec, *pPackMapper, fetcherFactory, pCaptureIndicatorText);
        UpdateCaptureStatusText_();
        samplingPeriodMs = pSpec->samplingPeriodMs;
        samplingWaiter.SetInterval(pSpec->samplingPeriodMs / 1'000.f);
        samplesPerFrame = pSpec->samplesPerFrame;
        hideDuringCapture = pSpec->hideDuringCapture;
        hideAlways = pSpec->hideAlways;
        AdjustOverlaySituation_(pSpec->overlayPosition);
        if (IsHidden_())
        {
            pWindow->Hide();
        }
        else
        {
            pWindow->Show();
            pWindow->Reorder(proc.hWnd);
        }
    }

    void Overlay::AdjustOverlaySituation_(OverlaySpec::OverlayPosition position_)
    {
        if (const DimensionsI newDims = pRoot->GetElementDims(); newDims != graphicsDimensions)
        {
            graphicsDimensions = newDims;
            windowDimensions = Dimensions{ graphicsDimensions } * upscaleFactor;
            pWindow->Resize(windowDimensions);
            if (!pWindow->Standard()) {
                pWindow->Move(CalculateOverlayPosition_());
            }
            gfx.Resize(graphicsDimensions);
            position = position_;
        }
        else if (position != position_)
        {
            position = position_;
            if (!pWindow->Standard()) {
                pWindow->Move(CalculateOverlayPosition_());
            }
        }
    }

    void Overlay::UpdateGraphData_(double timestamp)
    {
        if (!IsTargetLive()) {
            throw TargetLostException{};
        }
        pPackMapper->Populate(pm->GetTracker(), timestamp);
    }

    void Overlay::UpdateTargetRect(const RectI& newRect)
    {
        if (pWindow->Standard()) {
            // if we are a independent overlay (which is a standard) window, don't move when target moves
            return;
        }

        if (targetRect != newRect) {
            targetRect = newRect;
            pWindow->Move(CalculateOverlayPosition_());
        }
        // hide window during move, record timepoint to determine when to show again
        pWindow->Hide();
        lastMoveTime = std::chrono::high_resolution_clock::now();
    }

    void Overlay::UpdateTargetOrder(bool topmost)
    {
        if (pWindow->Standard()) {
            // if we are a independent overlay (which is a standard) window, don't do reordering
            return;
        }

        if (pWindow->Fullscreen()) {
            // if we are a fullscreen attached overlay, always stay on top
            // TODO: examine how topmost is set on SOTTR with logging to improve this fullscreen kludge
            pWindow->SetTopmost();
        }
        else {
            if (topmost) {
                // this function called with topmost=true when a window activation happens
                // and the activated window is the target, so we need topmost to get on top
                // of it
                pWindow->SetTopmost();
            }
            else {
                // if other window was activated, we need to stop being topmost and insert
                // ourself just above the target in the window order
                pWindow->ClearTopmost();
                pWindow->Reorder(proc.hWnd);
            }
        }
    }

    Vec2I Overlay::CalculateOverlayPosition_() const
    {
        if (position == OverlaySpec::OverlayPosition::Center)
        {
            p2clog.note(L"center overlay position unimplimented").commit();
        }

        int x, y;
        if (position == OverlaySpec::OverlayPosition::TopLeft || position == OverlaySpec::OverlayPosition::BottomLeft)
        {
            x = targetRect.left;
        }
        else
        {
            x = targetRect.right - windowDimensions.width;
        }
        if (position == OverlaySpec::OverlayPosition::TopLeft || position == OverlaySpec::OverlayPosition::TopRight)
        {
            y = targetRect.top;
        }
        else
        {
            y = targetRect.bottom - windowDimensions.height;
        }

        return { x, y };
    }

    bool Overlay::IsHidden_() const
    {
        return (pWriter && hideDuringCapture) || hideAlways;
    }

    void Overlay::Render_()
    {
        // update window contents
        gfx.BeginFrame();
        pRoot->Draw(gfx);
        gfx.EndFrame();
    }

    void Overlay::UpdateCaptureStatusText_()
    {
        if (pWriter) {
            pCaptureIndicatorText->SetText(L"In Progress");
        }
        else {
            pCaptureIndicatorText->SetText(L"Standing By");
        }
    }

    void Overlay::InitiateClose()
    {
        pWindow->Close();
    }

    void Overlay::RunTick()
    {
        using namespace std::chrono_literals;
        // polling data sources multiple times per drawn overlay frame
        if (IsHidden_())
        {
            // hardcode this slow overlay tick for now during capture
            std::this_thread::sleep_for(100ms);
            pmon::Timekeeper::LockNow();
        }
        else
        {
            for (int i = 0; i < samplesPerFrame; i++)
            {
                samplingWaiter.Wait();
                pmon::Timekeeper::LockNow();
                UpdateGraphData_(pmon::Timekeeper::GetLockedNow());
            }
        }

        if (pWriter) {
            pWriter->Process();
        }

        // handle hide during move logic (show if time elapsed and now otherwise hidden)
        if (lastMoveTime) {
            const auto now = std::chrono::high_resolution_clock::now();
            if (std::chrono::duration<float>(now - *lastMoveTime).count() >= 0.1f) {
                lastMoveTime = {};
                if (!IsHidden_()) {
                    pWindow->Show();
                }
            }
        }

        if (!IsHidden_())
        {
            Render_();
        }
    }

    void Overlay::SetCaptureState(bool active, std::wstring path, std::wstring name)
    {
        p2clog.info(std::format(L"Capture set to {}", active)).commit();

        if (active && !pWriter)
        {
            const std::chrono::zoned_time now{ std::chrono::current_zone(), std::chrono::system_clock::now() };
            auto fullPath = std::format(L"{0}{1}-{3}-{2:%y}{2:%m}{2:%d}-{2:%H}{2:%M}{2:%OS}.csv", path, name, now, proc.name);
            // create optional path for stats file
            auto fullStatsPath = [&]() -> std::optional<std::wstring> {
                if (pSpec->generateStats) {
                    return std::format(L"{0}{1}-{3}-{2:%y}{2:%m}{2:%d}-{2:%H}{2:%M}{2:%OS}-stats.csv", path, name, now, proc.name);
                }
                else {
                    return std::nullopt;
                }
            }();
            pWriter = { pm->MakeRawFrameDataWriter(std::move(fullPath), std::move(fullStatsPath), proc.pid, proc.name) };
        }
        else if (!active && pWriter)
        {
            pWriter.reset();
        }

        // handle window visibility
        if (IsHidden_())
        {
            pWindow->Hide();
        }
        else
        {
            pWindow->Show();
            pWindow->Reorder(proc.hWnd);
        }

        // update indicator on overlay
        UpdateCaptureStatusText_();
    }

    bool Overlay::IsTargetLive() const
    {
        const auto ret = WaitForSingleObject(hProcess, 0);
        if (ret == WAIT_FAILED)
        {
            p2clog.hr().commit();
        }
        return ret != WAIT_OBJECT_0;
    }

    bool Overlay::IsStandardWindow() const
    {
        return pWindow->Standard();
    }

    const win::Process& Overlay::GetProcess() const
    {
        return proc;
    }

    void Overlay::UpdateTargetFullscreenStatus()
    {
        if (const auto hMon = MonitorFromWindow(proc.hWnd, MONITOR_DEFAULTTONULL)) {
            MONITORINFOEXW monInfo{};
            monInfo.cbSize = (DWORD)sizeof(MONITORINFOEXW);
            if (GetMonitorInfoW(hMon, &monInfo) != 0) {
                const auto monRect = win::RectToRectI(monInfo.rcMonitor);
                targetFullscreen = targetRect == monRect;
            }
            else {
                p2clog.warn(L"didn't get monitor info").commit();
            }
        }
        else {
            p2clog.warn(L"didn't get monitor from window").commit();
        }
    }

    bool Overlay::NeedsFullscreenReboot() const
    {
        return !pWindow->Standard() && (targetFullscreen != pWindow->Fullscreen());
    }

    const OverlaySpec& Overlay::GetSpec() const
    {
        return *pSpec;
    }

    std::unique_ptr<Overlay> Overlay::SacrificeClone(std::optional<HWND> hWnd_, std::shared_ptr<OverlaySpec> pSpec_)
    {
        p2clog.info(L"doing SacrificeClone").commit();

        std::optional<Vec2I> pos;
        if (pWindow->Standard()) {
            pos = pWindow->GetPosition();
        }

        if (!pSpec_) {
            pSpec_ = std::move(pSpec);
        }

        proc.hWnd = hWnd_.value_or(proc.hWnd);

        auto pNewOverlay = std::make_unique<Overlay>(
            proc,
            std::move(pSpec_),
            pm,
            std::move(pPackMapper),
            pos
        );
        // clear pm so that stream isn't closed when this overlay dies
        pm = nullptr;
        pNewOverlay->pWriter = std::move(pWriter);
        pNewOverlay->UpdateCaptureStatusText_();
        return pNewOverlay;
    }
    std::unique_ptr<Overlay> Overlay::RetargetPidClone(win::Process proc_)
    {
        p2clog.info(L"doing RetargetPidClone").commit();

        std::optional<Vec2I> pos;
        if (pWindow->Standard()) {
            pos = pWindow->GetPosition();
        }

        pm->StopTracking();
        auto pNewOverlay = std::make_unique<Overlay>(
            proc_,
            std::move(pSpec),
            pm,
            std::make_unique<MetricPackMapper>(),
            pos
        );
        // clear pm so that stream isn't closed when this overlay dies
        // (because we manually close it above)
        pm = nullptr;
        return pNewOverlay;
    }

    const gfx::RectI& Overlay::GetTargetRect() const
    {
        return targetRect;
    }
}