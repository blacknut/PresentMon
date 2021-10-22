:: Copyright (C) 2020-2021 Intel Corporation
:: SPDX-License-Identifier: MIT

@echo off
setlocal enabledelayedexpansion
set presentmon=%1
set rootdir=%~2
set force=0
if "%~1" equ "" goto usage
if "%~2" equ "" goto usage
if not exist %presentmon% goto usage
if not exist "%rootdir%\." goto usage
if "%~3"=="force" (
    set force=1
) else (
    if not "%~3"=="" goto usage
)
goto args_ok
:usage
    echo usage: create_gold_csvs.cmd PresentMonPath GoldEtlCsvRootDir [force]
    exit /b 1
:args_ok

set pmargs=-no_top -stop_existing_session -qpc_time -track_debug
for /f "tokens=*" %%a in ('dir /s /b /a-d "%rootdir%\*.etl"') do call :create_csv "%%a"
exit /b 0

:create_csv
    if exist "%~dpn1.csv" if %force% neq 1 (
        echo Already exists: %~1
        exit /b 0
    )
    echo %presentmon% %pmargs% -etl_file %1 -output_file "%~dpn1.csv"
    %presentmon% %pmargs% -etl_file %1 -output_file "%~dpn1.csv" >NUL
    echo.
    where /q unix2dos
    if %errorlevel% equ 0 unix2dos -q "%~dpn1.csv"
    exit /b 0
