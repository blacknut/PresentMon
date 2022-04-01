:: Copyright (C) 2020-2022 Intel Corporation
:: SPDX-License-Identifier: MIT
@echo off
setlocal

where msbuild > NUL
if %errorlevel% neq 0 (
    echo error: dependency missing: msbuild
    exit /b 1
)

msbuild /nologo /verbosity:quiet /maxCpuCount /p:Platform=x64,Configuration=release "%~dp0etw_list"
if %errorlevel% neq 0 (
    echo error: failed to build etw_list
    exit /b 1
)

for %%a in ("%~dp0..") do set out_dir=%%~fa\PresentData\ETW
if not exist "%out_dir%\." mkdir "%out_dir%"

set events=
set events=%events% --event=Present::Start
set events=%events% --event=Present::Stop
call :etw_list "Microsoft-Windows-D3D9" "%out_dir%\Microsoft_Windows_D3D9.h"

set events=
set events=%events% --event=MILEVENT_MEDIA_UCE_PROCESSPRESENTHISTORY_GetPresentHistory::Info
set events=%events% --event=SCHEDULE_PRESENT::Start
set events=%events% --event=SCHEDULE_SURFACEUPDATE::Info
call :etw_list "Microsoft-Windows-Dwm-Core" "%out_dir%\Microsoft_Windows_Dwm_Core.h"

set events=
set events=%events% --event=Present::Start
set events=%events% --event=Present::Stop
set events=%events% --event=PresentMultiplaneOverlay::Start
set events=%events% --event=PresentMultiplaneOverlay::Stop
call :etw_list "Microsoft-Windows-DXGI" "%out_dir%\Microsoft_Windows_DXGI.h"

set events=
set events=%events% --event=Blit::Info
set events=%events% --event=Flip::Info
set events=%events% --event=FlipMultiPlaneOverlay::Info
set events=%events% --event=IndependentFlip::Info
set events=%events% --event=HSyncDPCMultiPlane::Info
set events=%events% --event=VSyncDPCMultiPlane::Info
set events=%events% --event=MMIOFlip::Info
set events=%events% --event=MMIOFlipMultiPlaneOverlay::Info
set events=%events% --event=Present::Info
set events=%events% --event=PresentHistory::Start
set events=%events% --event=PresentHistory::Info
set events=%events% --event=PresentHistoryDetailed::Start
set events=%events% --event=QueuePacket::Start
set events=%events% --event=QueuePacket::Stop
set events=%events% --event=VSyncDPC::Info
call :etw_list "Microsoft-Windows-DxgKrnl" "%out_dir%\Microsoft_Windows_DxgKrnl.h"

set events=
set events=%events% --event=TokenCompositionSurfaceObject::Info
set events=%events% --event=TokenStateChanged::Info
call :etw_list "Microsoft-Windows-Win32k" "%out_dir%\Microsoft_Windows_Win32k.h"

echo %out_dir%\NT_Process.h
echo // Copyright ^(C^) 2020-2021 Intel Corporation> "%out_dir%\NT_Process.h"
echo // SPDX-License-Identifier: MIT>> "%out_dir%\NT_Process.h"
echo //>> "%out_dir%\NT_Process.h"
echo // This file originally generated by run_etw_list.cmd>> "%out_dir%\NT_Process.h"
echo //>> "%out_dir%\NT_Process.h"
echo #pragma once>> "%out_dir%\NT_Process.h"
echo.>> "%out_dir%\NT_Process.h"
echo namespace NT_Process {>> "%out_dir%\NT_Process.h"
echo.>> "%out_dir%\NT_Process.h"
echo struct __declspec^(uuid^("{3d6fa8d0-fe05-11d0-9dda-00c04fd7ba7c}"^)^) GUID_STRUCT;>> "%out_dir%\NT_Process.h"
echo static const auto GUID = __uuidof^(GUID_STRUCT^);>> "%out_dir%\NT_Process.h"
echo.>> "%out_dir%\NT_Process.h"
echo }>> "%out_dir%\NT_Process.h"

exit /b 0

:etw_list
    echo %~2
    "%~dp0..\build\Release\etw_list-dev-x64.exe" --no_event_structs %events% --provider=%~1>%2
    exit /b 0

