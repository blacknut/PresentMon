// Copyright (C) 2020-2023 Intel Corporation
// SPDX-License-Identifier: MIT

#include "PresentMonTests.h"

#include <initializer_list>
#include <unordered_set>

void AddTestFailure(char const* file, int line, char const* fmt, ...)
{
    char buffer[512];

    va_list val;
    va_start(val, fmt);
    vsnprintf(buffer, _countof(buffer), fmt, val);
    va_end(val);

    GTEST_MESSAGE_AT_(file, line, buffer, ::testing::TestPartResult::kNonFatalFailure);
}

namespace {

void CheckAll(size_t const* columnIndex, bool* ok, std::initializer_list<PresentMonCsv::Header> const& headers)
{
    for (auto const& h : headers) {
        if (columnIndex[h] == SIZE_MAX) {
            *ok = false;
            return;
        }
    }
}

size_t CheckOne(size_t const* columnIndex, bool* ok, std::initializer_list<PresentMonCsv::Header> const& headers)
{
    size_t i = 0;
    for (auto const& h : headers) {
        if (columnIndex[h] != SIZE_MAX) {
            for (auto const& h2 : headers) {
                if (h2 != h && columnIndex[h2] != SIZE_MAX) {
                    *ok = false;
                    break;
                }
            }
            return i;
        }
        ++i;
    }
    *ok = false;
    return SIZE_MAX;
}

bool CheckAllIfAny(size_t const* columnIndex, bool* ok, std::initializer_list<PresentMonCsv::Header> const& headers)
{
    for (auto const& h : headers) {
        if (columnIndex[h] != SIZE_MAX) {
            CheckAll(columnIndex, ok, headers);
            return true;
        }
    }
    return false;
}

PresentMonCsv::Header FindHeader(char const* header)
{
    for (uint32_t i = 0; i < PresentMonCsv::KnownHeaderCount; ++i) {
        auto h = (PresentMonCsv::Header) i;
        if (strcmp(header, PresentMonCsv::GetHeaderString(h)) == 0) {
            return h;
        }
    }
    return PresentMonCsv::UnknownHeader;
}

}

bool PresentMonCsv::Open(char const* file, int line, std::wstring const& path)
{
    // Load the CSV
    for (uint32_t i = 0; i < _countof(headerColumnIndex_); ++i) {
        headerColumnIndex_[i] = SIZE_MAX;
    }
    cols_.clear();
    path_ = path;
    line_ = 0;

    if (_wfopen_s(&fp_, path.c_str(), L"r")) {
        AddTestFailure(file, line, "Failed to open file: %ls", path.c_str());
        return false;
    }

    // Remove UTF-8 marker if there is one.
    if (fread(row_, 3, 1, fp_) != 1 ||
        row_[0] != -17 || // 0xef
        row_[1] != -69 || // 0xbb
        row_[2] != -65) { // 0xbf
        fseek(fp_, 0, SEEK_SET);
    }

    // Read the header and ensure required columns are present
    ReadRow();

    for (size_t i = 0, n = cols_.size(); i < n; ++i) {
        auto h = FindHeader(cols_[i]);
        switch (h) {
        case KnownHeaderCount:
        case UnknownHeader:
            AddTestFailure(Convert(path_).c_str(), (int) line_, "Unrecognised column: %s", cols_[i]);
            break;
        default:
            if (headerColumnIndex_[(size_t) h] != SIZE_MAX) {
                AddTestFailure(Convert(path_).c_str(), (int) line_, "Duplicate column: %s", cols_[i]);
            } else {
                headerColumnIndex_[(size_t) h] = i;
            }
            break;
        }
    }

    bool columnsOK = true;
    CheckAll(headerColumnIndex_, &columnsOK, { Header_Application,
                                               Header_ProcessID,
                                               Header_SwapChainAddress,
                                               Header_Runtime,
                                               Header_SyncInterval,
                                               Header_PresentFlags });

    auto v1 = CheckAllIfAny(headerColumnIndex_, &columnsOK, { Header_Dropped,
                                                              Header_TimeInSeconds,
                                                              Header_msBetweenPresents,
                                                              Header_msInPresentAPI });
    if (v1) {
        auto qpc_time        = CheckAllIfAny(headerColumnIndex_, &columnsOK, { Header_QPCTime, });
        auto track_display   = CheckAllIfAny(headerColumnIndex_, &columnsOK, { Header_AllowsTearing,
                                                                               Header_PresentMode,
                                                                               Header_msBetweenDisplayChange,
                                                                               Header_msUntilRenderComplete,
                                                                               Header_msUntilDisplayed });
        auto track_gpu       = CheckAllIfAny(headerColumnIndex_, &columnsOK, { Header_msUntilRenderStart,
                                                                               Header_msGPUActive });
        auto track_gpu_video = CheckAllIfAny(headerColumnIndex_, &columnsOK, { Header_msGPUVideoActive });
        auto track_input     = CheckAllIfAny(headerColumnIndex_, &columnsOK, { Header_msSinceInput });

                              params_.emplace_back(L"--v1_metrics");
        if (qpc_time)         params_.emplace_back(L"--qpc_time");
        if (!track_display)   params_.emplace_back(L"--no_track_display");
        if (!track_gpu)       params_.emplace_back(L"--no_track_gpu");
        if (track_gpu_video)  params_.emplace_back(L"--track_gpu_video");
        if (!track_input)     params_.emplace_back(L"--no_track_input");
    } else {
        CheckAll(headerColumnIndex_, &columnsOK, { Header_CPUBusy,
                                                   Header_CPUWait });

        size_t time          = CheckOne(headerColumnIndex_, &columnsOK,      { Header_CPUStartTime,
                                                                               Header_CPUStartQPC,
                                                                               Header_CPUStartQPCTime,
                                                                               Header_CPUStartDateTime });
        auto track_display   = CheckAllIfAny(headerColumnIndex_, &columnsOK, { Header_AllowsTearing,
                                                                               Header_PresentMode,
                                                                               Header_DisplayLatency,
                                                                               Header_DisplayedTime });
        auto track_gpu       = CheckAllIfAny(headerColumnIndex_, &columnsOK, { Header_GPULatency,
                                                                               Header_GPUBusy,
                                                                               Header_GPUWait });
        auto track_gpu_video = CheckAllIfAny(headerColumnIndex_, &columnsOK, { Header_VideoBusy });
        auto track_input     = CheckAllIfAny(headerColumnIndex_, &columnsOK, { Header_ClickToPhotonLatency });

        switch (time) {
        case 1: params_.emplace_back(L"--qpc_time");    break;
        case 2: params_.emplace_back(L"--qpc_time_ms"); break;
        case 3: params_.emplace_back(L"--date_time");   break;
        }
        if (!track_display)  params_.emplace_back(L"--no_track_display");
        if (!track_gpu)      params_.emplace_back(L"--no_track_gpu");
        if (track_gpu_video) params_.emplace_back(L"--track_gpu_video");
        if (!track_input)    params_.emplace_back(L"--no_track_input");
    }

    if (!columnsOK) {
        AddTestFailure(Convert(path_).c_str(), (int) line_, "Missing required columns.");
    }

    return true;
}

void PresentMonCsv::Close()
{
    fclose(fp_);
    fp_ = nullptr;
}

bool PresentMonCsv::ReadRow()
{
    row_[0] = '\0';
    cols_.clear();

    // Read a line
    if (fgets(row_, _countof(row_), fp_) == nullptr) {
        if (ferror(fp_) != 0) {
            AddTestFailure(Convert(path_).c_str(), (int) line_, "File read error");
        }
        return false;
    }

    line_ += 1;

    // Split line into columns, skipping leading/trailing whitespace
    auto p0 = row_;
    for (; *p0 == ' ' || *p0 == '\t'; ++p0) *p0 = '\0';
    for (auto p = p0; ; ++p) {
        auto ch = *p;
        if (ch == ',' || ch == '\0') {
            *p = '\0';
            cols_.push_back(p0);
            for (p0 = p + 1; *p0 == ' ' || *p0 == '\t'; ++p0) *p0 = '\0';
            for (auto q = p - 1; *q == ' ' || *q == '\t' || *q == '\n' || *q == '\r'; --q) *q = '\0';
            if (ch == '\0') break;
        }
    }

    return true;
}

size_t PresentMonCsv::GetColumnIndex(char const* header) const
{
    auto h = FindHeader(header);
    return h < KnownHeaderCount ? headerColumnIndex_[h] : SIZE_MAX;
}

PresentMon::PresentMon()
    : cmdline_()
    , csvArgSet_(false)
{
    cmdline_ += L'\"';
    cmdline_ += exePath_;
    cmdline_ += L"\" --no_console_stats";
}

PresentMon::~PresentMon()
{
    if (::testing::Test::HasFailure()) {
        printf("%ls\n", cmdline_.c_str());
    }
}

void PresentMon::AddEtlPath(std::wstring const& etlPath)
{
    cmdline_ += L" --etl_file \"";
    cmdline_ += etlPath;
    cmdline_ += L'\"';
}

void PresentMon::AddCsvPath(std::wstring const& csvPath)
{
    EXPECT_FALSE(csvArgSet_);
    cmdline_ += L" --output_file \"";
    cmdline_ += csvPath;
    cmdline_ += L'\"';
    csvArgSet_ = true;

    // Delete the file if it exists.  Otherwise, PresentMon may not output
    // anything and any previous content will remain.
    DeleteFile(csvPath.c_str());
}

void PresentMon::Add(wchar_t const* args)
{
    cmdline_ += L' ';
    cmdline_ += args;
}

void PresentMon::Start(char const* file, int line)
{
    if (!csvArgSet_) {
        cmdline_ += L" --no_csv";
        csvArgSet_ = true;
    }

    STARTUPINFO si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    if (CreateProcess(nullptr, &cmdline_[0], nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, (PROCESS_INFORMATION*) this) == 0) {
        AddTestFailure(file, line, "Failed to start PresentMon");
    }
}

bool PresentMon::IsRunning(DWORD timeoutMilliseconds) const
{
    return WaitForSingleObject(hProcess, timeoutMilliseconds) == WAIT_TIMEOUT;
}

void PresentMon::ExpectExited(char const* file, int line, DWORD timeoutMilliseconds, DWORD expectedExitCode)
{
    auto isRunning = IsRunning(timeoutMilliseconds);
    if (isRunning) {
        AddTestFailure(file, line, "PresentMon still running after %ums", timeoutMilliseconds);

        TerminateProcess(hProcess, 0);
        WaitForSingleObject(hProcess, INFINITE);
    } else {
        DWORD exitCode = 0;
        GetExitCodeProcess(hProcess, &exitCode);

        if (exitCode != expectedExitCode) {
            AddTestFailure(file, line, "Unexpected PresentMon exit code: %d (expecting %d)", exitCode, expectedExitCode);
        }
    }

    CloseHandle(hProcess);
    CloseHandle(hThread);
}
