// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: MIT
#include "Kernel.h"
#include <Core/source/win/WinAPI.h>
#include <Core/source/win/MessageBox.h>
#include <Core/source/infra/util/Util.h>
#include <Core/source/infra/log/Logging.h>
#include <Core/source/infra/svc/Services.h>
#include <Core/source/infra/util/FolderResolver.h>
#include <random>
#include "OverlayContainer.h"
#include "TargetLostException.h"
#include <Core/source/win/ProcessMapBuilder.h>
#include <Core/source/win/com/WbemConnection.h>
#include <Core/source/infra/opt/Options.h>
#include <CommonUtilities\str\String.h>

using namespace std::literals;

namespace p2c::kern
{
    using ::pmon::util::str::ToWide;

    Kernel::Kernel(KernelHandler* pHandler)
        :
        pHandler{ pHandler },
        constructionSemaphore{ 0 },
        thread{ &Kernel::ThreadProcedure_, this }
    {
        constructionSemaphore.acquire();
        HandleMarshalledException_();
    }

    Kernel::~Kernel()
    {
        {
            std::lock_guard lk{ mtx };
            dying = true;
        }
        cv.notify_one();
    }

    void Kernel::PushSpec(std::unique_ptr<OverlaySpec> pSpec)
    {
        HandleMarshalledException_();
        {
            std::lock_guard lk{ mtx };
            pPushedSpec = std::move(pSpec);
        }
        cv.notify_one();
    }

    void Kernel::ClearOverlay()
    {
        HandleMarshalledException_();
        {
            std::lock_guard lk{ mtx };
            if (pOverlayContainer) {
                clearRequested = true;
                pPushedSpec.reset();
            }
        }
        cv.notify_one();
    }

    std::vector<Process> Kernel::ListProcesses()
    {
        HandleMarshalledException_();
        win::ProcessMapBuilder builder;
        builder.FillWindowHandles();
        builder.FilterHavingWindow();
        auto pmap = builder.Extract();

        std::vector<Process> list;
        list.reserve(pmap.size());
        for (auto& entry : pmap)
        {
            Process proc = std::move(entry.second);
            if (proc.hWnd) {
                proc.windowName = win::GetWindowTitle(proc.hWnd);
            }
            list.push_back(std::move(proc));
        }
        return list;
    }

    void Kernel::SetAdapter(uint32_t id)
    {
        HandleMarshalledException_();
        std::lock_guard lk{ mtx };
        if (!pm) {
            p2clog.warn(L"presentmon not initialized").commit();
            return;
        }
        pm->SetAdapter(id);
    }

    const pmapi::intro::Root& Kernel::GetIntrospectionRoot() const
    {
        HandleMarshalledException_();
        std::lock_guard g{ mtx };
        return pm->GetIntrospectionRoot();
    }

    std::vector<pmon::AdapterInfo> Kernel::EnumerateAdapters() const
    {
        HandleMarshalledException_();
        std::lock_guard lk{ mtx };
        if (!pm) {
            p2clog.warn(L"presentmon not initialized").commit();
            return {};
        }
        try { return pm->EnumerateAdapters(); }
        catch (...) { 
            p2clog.warn(L"failed to enumerate adapters, returning empty set").commit();
            return {};
        }
    }

    void Kernel::SetCapture(bool active)
    {
        HandleMarshalledException_();
        std::lock_guard lk{ mtx };
        pushedCaptureActive = active;
    }

    bool Kernel::IsIdle_() const
    {
        return !dying && !pPushedSpec && !pOverlayContainer;
    }

    std::unique_ptr<OverlaySpec> Kernel::PullSpec_()
    {
        std::lock_guard g{ mtx };
        return std::move(pPushedSpec);
    }

    void Kernel::HandleMarshalledException_() const
    {
        if (hasMarshalledException) {
            std::rethrow_exception(marshalledException);
        }
    }

    void Kernel::ThreadProcedure_()
    {
        try {
            // mutex that prevents frontend from accessing before pmon is connected
            std::unique_lock startLck{ mtx };
            p2clog.info(L"== kernel thread starting ==").pid().tid().commit();

            // command line options
            auto& opt = infra::opt::get();

            // connect to wbem
            win::com::WbemConnection wbemConn;

            // create the PresentMon object
            try { pm.emplace(opt.controlPipe.AsOptional(), opt.shmName.AsOptional()); }
            catch (...) {
                pHandler->OnPresentmonInitFailed();
                p2clog.note(L"Failed to init presentmon api").nox().notrace().commit();
                throw;
            }

            startLck.unlock();
            constructionSemaphore.release();

            while (!dying)
            {
                try
                {
                    {
                        std::unique_lock u{ mtx };
                        cv.wait(u, [this] {return !IsIdle_(); });
                    }
                    if (pPushedSpec && !dying)
                    {
                        // spawn overlay (container)
                        ConfigurePresentMon_(*pPushedSpec);
                        pOverlayContainer = std::make_unique<OverlayContainer>(wbemConn, std::move(pPushedSpec), &*pm);
                    }
                    if (pOverlayContainer && !dying)
                    {
                        // blocks while overlay is active
                        RunOverlayLoop_();
                    }
                }
                // this catch section handles target lost and overlay crashes
                // control app will still work, can attempt to instance another overlay
                catch (const TargetLostException&)
                {
                    if (pOverlayContainer) {
                        pHandler->OnTargetLost(pOverlayContainer->GetProcess().pid);
                        pOverlayContainer.reset();
                        pPushedSpec.reset();
                    }
                    else {
                        pHandler->OnStalePidSelected();
                    }
                }
                catch (const std::exception& e) {
                    pHandler->OnOverlayDied();
                    auto note = std::format(L"Overlay died with error: {}", pmon::ToWide(e.what()));
                    p2clog.note(std::move(note)).nox().notrace().commit();
                    pOverlayContainer.reset();
                    pPushedSpec.reset();
                }
                catch (...) {
                    pHandler->OnOverlayDied();
                    p2clog.note(L"Overlay died").nox().notrace().commit();
                    pOverlayContainer.reset();
                    pPushedSpec.reset();
                }
            }

            pm.reset();

            p2clog.info(L"== core thread exiting ==").pid().commit();
        }
        // this catch section handles failures to initialize kernel, or rare error that escape the main loop catch
        // possibility to marshall exceptions to js whenever an interface function is called (async rejection path)
        catch (const std::exception& e) {
            // TODO: instead of duplicating for std::ex+..., just catch ... and use a function that can throw/catch
            // std::current_exception to extract useful info, if any
            auto msg = std::format("Exception in PresentMon Kernel thread [{}]: {}", typeid(e).name(), e.what());
            p2clog.note(ToWide(msg)).nox().notrace().commit();
            marshalledException = std::current_exception();
            hasMarshalledException.store(true);
        }
        catch (...) {
            p2clog.note(L"Unknown exception in PresentMon Kernel thread").nox().notrace().commit();
            marshalledException = std::current_exception();
            hasMarshalledException.store(true);
        }
        constructionSemaphore.release();
    }

    void Kernel::RunOverlayLoop_()
    {
        // this loop runs while the overlay window is active
        while (pOverlayContainer)
        {
            // checking thread sync signals
            {
                std::lock_guard lk{ mtx };
                if (dying || clearRequested)
                {
                    pOverlayContainer->InitiateClose();
                    pPushedSpec.reset();
                    clearRequested = false;
                    inhibitTargetLostSignal = true;
                }
                else if (pPushedSpec)
                {
                    auto& curSpec = pOverlayContainer->GetSpec();
                    if (pPushedSpec->pid != curSpec.pid) {
                        pOverlayContainer->InitiateClose();
                    }
                    else if (pPushedSpec->independentKernelWindow != curSpec.independentKernelWindow ||
                        pPushedSpec->upscale != curSpec.upscale ||
                        (pPushedSpec->upscale && pPushedSpec->upscaleFactor != curSpec.upscaleFactor)) {
                        ConfigurePresentMon_(*pPushedSpec);
                        inhibitTargetLostSignal = true;
                        pOverlayContainer->RebootOverlay(std::move(pPushedSpec));
                    }
                    else {
                        ConfigurePresentMon_(*pPushedSpec);
                        pOverlayContainer->RebuildDocument(std::move(pPushedSpec));
                    }
                }
                else if (pushedCaptureActive)
                {
                    std::wstring path;
                    if (auto pFolder = infra::svc::Services::ResolveOrNull<infra::util::FolderResolver>()) {
                        path = pFolder->Resolve(infra::util::FolderResolver::Folder::Documents) + L"\\Captures\\";
                    }
                    pOverlayContainer->SetCaptureState(*pushedCaptureActive, std::move(path), pOverlayContainer->GetSpec().captureName);
                    pushedCaptureActive.reset();
                }
            }
            pOverlayContainer->CheckAndProcessFullscreenTransition();
            // process windows messages
            {
                MSG msg;
                while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
                {
                    if (msg.message == WM_QUIT)
                    {
                        if (!dying && !inhibitTargetLostSignal) {
                            pHandler->OnTargetLost(pOverlayContainer->GetProcess().pid);
                        }
                        inhibitTargetLostSignal = false;
                        pOverlayContainer.reset();
                        break;
                    }
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
            }
            // handle data and overlay
            if (pOverlayContainer)
            {
                pOverlayContainer->RunTick();
            }
        }
    }

    void Kernel::ConfigurePresentMon_(const OverlaySpec& newSpec)
    {
        if (newSpec.metricsOffset != pm->GetOffset())
        {
            pm->SetOffset(newSpec.metricsOffset);
        }
        if (newSpec.averagingWindowSize != pm->GetWindow())
        {
            pm->SetWindow(newSpec.averagingWindowSize);
        }
        if (newSpec.telemetrySamplingPeriodMs != pm->GetGpuTelemetryPeriod())
        {
            pm->SetGpuTelemetryPeriod(newSpec.telemetrySamplingPeriodMs);
        }
    }
}
