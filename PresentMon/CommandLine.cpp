// Copyright (C) 2017,2019-2023 Intel Corporation
// SPDX-License-Identifier: MIT

#include <generated/version.h>

#include "PresentMon.hpp"
#include "../PresentData/Debug.hpp"
#include <algorithm>

namespace {

enum {
    DEFAULT_CONSOLE_WIDTH = 80,
    MAX_ARG_COLUMN_WIDTH = 40,
    MIN_DESC_COLUMN_WIDTH = 20,
    ARG_DESC_COLUMN_PADDING = 4,
};

struct KeyNameCode
{
    wchar_t const* mName;
    UINT mCode;
};

KeyNameCode const HOTKEY_MODS[] = {
    { L"ALT",     MOD_ALT     },
    { L"CONTROL", MOD_CONTROL },
    { L"CTRL",    MOD_CONTROL },
    { L"SHIFT",   MOD_SHIFT   },
    { L"WINDOWS", MOD_WIN     },
    { L"WIN",     MOD_WIN     },
};

KeyNameCode const HOTKEY_KEYS[] = {
    { L"BACKSPACE", VK_BACK },
    { L"TAB", VK_TAB },
    { L"CLEAR", VK_CLEAR },
    { L"ENTER", VK_RETURN },
    { L"PAUSE", VK_PAUSE },
    { L"CAPSLOCK", VK_CAPITAL },
    { L"ESC", VK_ESCAPE },
    { L"SPACE", VK_SPACE },
    { L"PAGEUP", VK_PRIOR },
    { L"PAGEDOWN", VK_NEXT },
    { L"END", VK_END },
    { L"HOME", VK_HOME },
    { L"LEFT", VK_LEFT },
    { L"UP", VK_UP },
    { L"RIGHT", VK_RIGHT },
    { L"DOWN", VK_DOWN },
    { L"PRINTSCREEN", VK_SNAPSHOT },
    { L"INS", VK_INSERT },
    { L"DEL", VK_DELETE },
    { L"HELP", VK_HELP },
    { L"NUMLOCK", VK_NUMLOCK },
    { L"SCROLLLOCK", VK_SCROLL },
    { L"NUM0", VK_NUMPAD0 },
    { L"NUM1", VK_NUMPAD1 },
    { L"NUM2", VK_NUMPAD2 },
    { L"NUM3", VK_NUMPAD3 },
    { L"NUM4", VK_NUMPAD4 },
    { L"NUM5", VK_NUMPAD5 },
    { L"NUM6", VK_NUMPAD6 },
    { L"NUM7", VK_NUMPAD7 },
    { L"NUM8", VK_NUMPAD8 },
    { L"NUM9", VK_NUMPAD9 },
    { L"MULTIPLY", VK_MULTIPLY },
    { L"ADD", VK_ADD },
    { L"SEPARATOR", VK_SEPARATOR },
    { L"SUBTRACT", VK_SUBTRACT },
    { L"DECIMAL", VK_DECIMAL },
    { L"DIVIDE", VK_DIVIDE },
    { L"0", 0x30 },
    { L"1", 0x31 },
    { L"2", 0x32 },
    { L"3", 0x33 },
    { L"4", 0x34 },
    { L"5", 0x35 },
    { L"6", 0x36 },
    { L"7", 0x37 },
    { L"8", 0x38 },
    { L"9", 0x39 },
    { L"A", 0x41 },
    { L"B", 0x42 },
    { L"C", 0x43 },
    { L"D", 0x44 },
    { L"E", 0x45 },
    { L"F", 0x46 },
    { L"G", 0x47 },
    { L"H", 0x48 },
    { L"I", 0x49 },
    { L"J", 0x4A },
    { L"K", 0x4B },
    { L"L", 0x4C },
    { L"M", 0x4D },
    { L"N", 0x4E },
    { L"O", 0x4F },
    { L"P", 0x50 },
    { L"Q", 0x51 },
    { L"R", 0x52 },
    { L"S", 0x53 },
    { L"T", 0x54 },
    { L"U", 0x55 },
    { L"V", 0x56 },
    { L"W", 0x57 },
    { L"X", 0x58 },
    { L"Y", 0x59 },
    { L"Z", 0x5A },
    { L"F1", VK_F1 },
    { L"F2", VK_F2 },
    { L"F3", VK_F3 },
    { L"F4", VK_F4 },
    { L"F5", VK_F5 },
    { L"F6", VK_F6 },
    { L"F7", VK_F7 },
    { L"F8", VK_F8 },
    { L"F9", VK_F9 },
    { L"F10", VK_F10 },
    { L"F11", VK_F11 },
    { L"F12", VK_F12 },
    { L"F13", VK_F13 },
    { L"F14", VK_F14 },
    { L"F15", VK_F15 },
    { L"F16", VK_F16 },
    { L"F17", VK_F17 },
    { L"F18", VK_F18 },
    { L"F19", VK_F19 },
    { L"F20", VK_F20 },
    { L"F21", VK_F21 },
    { L"F22", VK_F22 },
    { L"F23", VK_F23 },
    { L"F24", VK_F24 },
};

CommandLineArgs gCommandLineArgs;

size_t GetConsoleWidth()
{
    CONSOLE_SCREEN_BUFFER_INFO info = {};
    return GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info) == 0
        ? DEFAULT_CONSOLE_WIDTH
        : std::max<size_t>(DEFAULT_CONSOLE_WIDTH, info.srWindow.Right - info.srWindow.Left + 1);
}

bool ParseKeyName(KeyNameCode const* valid, size_t validCount, wchar_t* name, wchar_t const* errorMessage, UINT* outKeyCode)
{
    for (size_t i = 0; i < validCount; ++i) {
        if (_wcsicmp(name, valid[i].mName) == 0) {
            *outKeyCode = valid[i].mCode;
            return true;
        }
    }

    PrintError(L"error: %s: %s\n", errorMessage, name);

    int width = (int) (0.8 * GetConsoleWidth());
    int col = PrintError(L"       valid options:");
    for (size_t i = 0; i < validCount; ++i) {
        col += PrintError(L" %s", valid[i].mName);
        if (col > width) {
            col = PrintError(L"\n                     ") - 1;
        }
    }
    PrintError(L"\n");

    return false;
}

bool AssignHotkey(wchar_t* key, CommandLineArgs* args)
{
    #pragma warning(suppress: 4996)
    auto token = wcstok(key, L"+");
    for (;;) {
        auto prev = token;
        #pragma warning(suppress: 4996)
        token = wcstok(nullptr, L"+");
        if (token == nullptr) {
            if (!ParseKeyName(HOTKEY_KEYS, _countof(HOTKEY_KEYS), prev, L"invalid --hotkey key", &args->mHotkeyVirtualKeyCode)) {
                return false;
            }
            break;
        }

        if (!ParseKeyName(HOTKEY_MODS, _countof(HOTKEY_MODS), prev, L"invalid --hotkey modifier", &args->mHotkeyModifiers)) {
            return false;
        }
    }

    args->mHotkeySupport = true;
    return true;
}

// Allow /ARG, -ARG, or --ARG
bool ParseArgPrefix(wchar_t** arg)
{
    if (**arg == '/') {
        *arg += 1;
        return true;
    }

    if (**arg == '-') {
        *arg += 1;
        if (**arg == '-') {
            *arg += 1;
        }
        return true;
    }

    return false;
}

bool ParseArg(wchar_t* arg, wchar_t const* option)
{
    return
        ParseArgPrefix(&arg) &&
        _wcsicmp(arg, option) == 0;
}

bool ParseValue(wchar_t** argv, int argc, int* i)
{
    if (*i + 1 < argc) {
        *i += 1;
        return true;
    }
    PrintError(L"error: %s expecting argument.\n", argv[*i]);
    return false;
}

bool ParseValue(wchar_t** argv, int argc, int* i, wchar_t const** value)
{
    if (!ParseValue(argv, argc, i)) return false;
    *value = argv[*i];
    return true;
}

bool ParseValue(wchar_t** argv, int argc, int* i, std::vector<std::wstring>* value)
{
    wchar_t const* v = nullptr;
    if (!ParseValue(argv, argc, i, &v)) return false;
    value->emplace_back(v);
    return true;
}

bool ParseValue(wchar_t** argv, int argc, int* i, UINT* value)
{
    wchar_t const* v = nullptr;
    if (!ParseValue(argv, argc, i, &v)) return false;
    *value = wcstoul(v, nullptr, 10);
    return true;
}

// Print command line usage help.  The command line arguments and their description
// is extracted out of the README-ConsoleApplication.md file at compile time (see
// Tools/generate_options_header.cmd) into command_line_options.inl, and this
// function formats and prints them.
void PrintUsage()
{
    fwprintf(stderr, L"PresentMon %hs\n", PRESENT_MON_VERSION);

    // Layout
    wchar_t* s[] = {
        #include <generated/command_line_options.inl>
    };
    size_t argWidth = 0;
    for (size_t i = 0; i < _countof(s); i += 2) {
        auto arg = s[i];
        auto desc = s[i + 1];
        if (desc != nullptr) {
            argWidth = std::max(argWidth, wcslen(arg));
        }
    }

    argWidth = std::min<size_t>(argWidth, MAX_ARG_COLUMN_WIDTH);

    size_t descWidth = std::max<size_t>(MIN_DESC_COLUMN_WIDTH, GetConsoleWidth() - ARG_DESC_COLUMN_PADDING - argWidth);

    // Print
    for (size_t i = 0; i < _countof(s); i += 2) {
        auto arg = s[i];
        auto desc = s[i + 1];
        if (desc == nullptr) {
            fwprintf(stderr, L"\n%s:\n", arg);
        } else {
            fwprintf(stderr, L"  %-*s  ", (int) argWidth, arg);
            for (auto len = wcslen(desc); len > 0; ) {
                if (len <= descWidth) {
                    fwprintf(stderr, L"%s\n", desc);
                    break;
                }

                auto w = descWidth;
                while (desc[w] != ' ') {
                    --w;
                }
                fwprintf(stderr, L"%.*s\n%-*s", (int) w, desc, (int) (argWidth + 4), L"");
                desc += w + 1;
                len -= w + 1;
            }
        }
    }
}

}

void PrintHotkeyError()
{
    auto args = &gCommandLineArgs;

    PrintError(L"error: ");

    for (auto const& mod : HOTKEY_MODS) {
        if (args->mHotkeyModifiers & mod.mCode) {
            PrintError(L"%s+", mod.mName);
        }
    }

    for (auto const& mod : HOTKEY_KEYS) {
        if (args->mHotkeyVirtualKeyCode == mod.mCode) {
            PrintError(L"%s", mod.mName);
            break;
        }
    }

    PrintError(L" is already in use and cannot be used as a --hotkey.\n");
}

CommandLineArgs const& GetCommandLineArgs()
{
    return gCommandLineArgs;
}

bool ParseCommandLine(int argc, wchar_t** argv)
{
    auto args = &gCommandLineArgs;

    args->mTargetProcessNames.clear();
    args->mExcludeProcessNames.clear();
    args->mOutputCsvFileName = nullptr;
    args->mEtlFileName = nullptr;
    args->mSessionName = L"PresentMon";
    args->mTargetPid = 0;
    args->mDelay = 0;
    args->mTimer = 0;
    args->mHotkeyModifiers = MOD_NOREPEAT;
    args->mHotkeyVirtualKeyCode = 0;
    args->mConsoleOutput = ConsoleOutput::Statistics;
    args->mTrackDisplay = true;
    args->mTrackInput = true;
    args->mTrackGPU = true;
    args->mTrackGPUVideo = false;
    args->mScrollLockIndicator = false;
    args->mExcludeDropped = false;
    args->mTerminateExistingSession = false;
    args->mTerminateOnProcExit = false;
    args->mStartTimer = false;
    args->mTerminateAfterTimer = false;
    args->mHotkeySupport = false;
    args->mTryToElevate = false;
    args->mMultiCsv = false;
    args->mUseV1Metrics = false;
    args->mStopExistingSession = false;
    // blk options
    args->mStatsdPort = 0;
    args->mLogFilename = 0;

    bool sessionNameSet  = false;
    bool csvOutputStdout = false;
    bool csvOutputNone   = false;
    bool qpcTime         = false;
    bool qpcmsTime       = false;
    bool dtTime          = false;

    #if PRESENTMON_ENABLE_DEBUG_TRACE
    bool verboseTrace = false;
    #endif

    // Match command line arguments with known options.  These must match
    // options listed in the README.md for documentation and for PrintUsage()
    // to work.
    for (int i = 1; i < argc; ++i) {
        // Capture target options:
             if (ParseArg(argv[i], L"process_name")) { if (ParseValue(argv, argc, &i, &args->mTargetProcessNames))  continue; }
        else if (ParseArg(argv[i], L"exclude"))      { if (ParseValue(argv, argc, &i, &args->mExcludeProcessNames)) continue; }
        else if (ParseArg(argv[i], L"process_id"))   { if (ParseValue(argv, argc, &i, &args->mTargetPid))           continue; }
        else if (ParseArg(argv[i], L"etl_file"))     { if (ParseValue(argv, argc, &i, &args->mEtlFileName))         continue; }

        // Output options:
        else if (ParseArg(argv[i], L"output_file"))      { if (ParseValue(argv, argc, &i, &args->mOutputCsvFileName)) continue; }
        else if (ParseArg(argv[i], L"output_stdout"))    { csvOutputStdout       = true;                              continue; }
        else if (ParseArg(argv[i], L"multi_csv"))        { args->mMultiCsv       = true;                              continue; }
        else if (ParseArg(argv[i], L"no_csv"))           { csvOutputNone         = true;                              continue; }
        else if (ParseArg(argv[i], L"no_console_stats")) { args->mConsoleOutput  = ConsoleOutput::Simple;             continue; }
        else if (ParseArg(argv[i], L"qpc_time"))         { qpcTime               = true;                              continue; }
        else if (ParseArg(argv[i], L"qpc_time_ms"))      { qpcmsTime             = true;                              continue; }
        else if (ParseArg(argv[i], L"date_time"))        { dtTime                = true;                              continue; }
        else if (ParseArg(argv[i], L"exclude_dropped"))  { args->mExcludeDropped = true;                              continue; }
        else if (ParseArg(argv[i], L"v1_metrics"))       { args->mUseV1Metrics   = true;                              continue; }

        // Recording options:
        else if (ParseArg(argv[i], L"hotkey"))           { if (ParseValue(argv, argc, &i) && AssignHotkey(argv[i], args)) continue; }
        else if (ParseArg(argv[i], L"delay"))            { if (ParseValue(argv, argc, &i, &args->mDelay)) continue; }
        else if (ParseArg(argv[i], L"timed"))            { if (ParseValue(argv, argc, &i, &args->mTimer)) { args->mStartTimer = true; continue; } }
        else if (ParseArg(argv[i], L"scroll_indicator")) { args->mScrollLockIndicator = true;  continue; }
        else if (ParseArg(argv[i], L"no_track_gpu"))     { args->mTrackGPU            = false; continue; }
        else if (ParseArg(argv[i], L"track_gpu_video"))  { args->mTrackGPUVideo       = true;  continue; }
        else if (ParseArg(argv[i], L"no_track_input"))   { args->mTrackInput          = false; continue; }
        else if (ParseArg(argv[i], L"no_track_display")) { args->mTrackDisplay        = false; continue; }

        // Execution options:
        else if (ParseArg(argv[i], L"session_name"))               { if (ParseValue(argv, argc, &i, &args->mSessionName)) { sessionNameSet = true; continue; } }
        else if (ParseArg(argv[i], L"stop_existing_session"))      { args->mStopExistingSession      = true; continue; }
        else if (ParseArg(argv[i], L"terminate_existing_session")) { args->mTerminateExistingSession = true; continue; }
        else if (ParseArg(argv[i], L"restart_as_admin"))           { args->mTryToElevate             = true; continue; }
        else if (ParseArg(argv[i], L"terminate_on_proc_exit"))     { args->mTerminateOnProcExit      = true; continue; }
        else if (ParseArg(argv[i], L"terminate_after_timed"))      { args->mTerminateAfterTimer      = true; continue; }

        // blk options: statsd port number
        else if (ParseArg(argv[i], L"output_statsd")) { if (ParseValue(argv, argc, &i, &args->mStatsdPort)) continue; }
        // blk options: logfile
        else if (ParseArg(argv[i], L"log_file")) { if (ParseValue(argv, argc, &i, &args->mLogFilename)) continue; }

        // Hidden options:
        #if PRESENTMON_ENABLE_DEBUG_TRACE
        else if (ParseArg(argv[i], L"debug_verbose_trace")) { verboseTrace = true; continue; }
        #endif

        // Provided argument wasn't recognized
        else if (!(ParseArg(argv[i], L"?") || ParseArg(argv[i], L"h") || ParseArg(argv[i], L"help"))) {
            PrintError(L"error: unrecognized option '%s'.\n", argv[i]);
        }

        PrintUsage();
        return false;
    }

    // Ensure at most one of --qpc_time --qpc_time_ms --date_time.
    if (qpcTime + qpcmsTime + dtTime > 1) {
        PrintError(L"error: only one of the following options may be used:");
        if (qpcTime)   PrintError(L" --qpc_time");
        if (qpcmsTime) PrintError(L" --qpc_time_ms");
        if (dtTime)    PrintError(L" --date_time");
        PrintError(L"\n");
        PrintUsage();
        return false;
    }

    // Disallow --hotkey that are known to be already in use:
    // - CTRL+C, CTRL+PAUSE, and CTRL+SCROLLLOCK already used to exit PresentMon
    // - F12 is reserved for debugger use at all times
    if (args->mHotkeySupport && (
            ((args->mHotkeyModifiers & MOD_CONTROL) && args->mHotkeyVirtualKeyCode == 0x43 /*C*/) ||
            ((args->mHotkeyModifiers & MOD_CONTROL) && args->mHotkeyVirtualKeyCode == VK_SCROLL) ||
            ((args->mHotkeyModifiers & MOD_CONTROL) && args->mHotkeyVirtualKeyCode == VK_PAUSE) ||
            (args->mHotkeyModifiers == MOD_NOREPEAT && args->mHotkeyVirtualKeyCode == VK_F12))) {
        PrintHotkeyError();
        return false;
    }

    // Ensure only one of --output_file --output_stdout --no_csv.
    if (csvOutputNone + csvOutputStdout + (args->mOutputCsvFileName != nullptr) > 1) {
        PrintWarning(L"warning: only one of the following options may be used:");
        if (csvOutputNone)                       PrintWarning(L" --no_csv");
        if (csvOutputStdout)                     PrintWarning(L" --output_stdout");
        if (args->mOutputCsvFileName != nullptr) PrintWarning(L" --output_file");

        PrintWarning(L"\n         ignoring:");
        if (csvOutputNone)                                          { csvOutputNone   = false; PrintWarning(L" --no_csv"); }
        if (csvOutputStdout && args->mOutputCsvFileName != nullptr) { csvOutputStdout = false; PrintWarning(L" --output_stdout"); }
        PrintWarning(L"\n");
    }

    // Ignore CSV-only options when --no_csv is used
    if (csvOutputNone && (qpcTime || qpcmsTime || dtTime || args->mMultiCsv || args->mHotkeySupport)) {
        PrintWarning(L"warning: ignoring CSV-related options due to --no_csv:");
        if (qpcTime)              { qpcTime              = false; PrintWarning(L" --qpc_time"); }
        if (qpcmsTime)            { qpcmsTime            = false; PrintWarning(L" --qpc_time_ms"); }
        if (dtTime)               { dtTime               = false; PrintWarning(L" --date_time"); }
        if (args->mMultiCsv)      { args->mMultiCsv      = false; PrintWarning(L" --multi_csv"); }
        if (args->mHotkeySupport) { args->mHotkeySupport = false; PrintWarning(L" --hotkey"); }
        PrintWarning(L"\n");
    }

    // If we're outputting CSV to stdout, we can't use it for console output.
    //
    // Also ignore --multi_csv since it only applies to file output.
    if (csvOutputStdout) {
        args->mConsoleOutput = ConsoleOutput::None;

        if (args->mMultiCsv) {
            PrintWarning(L"warning: ignoring --multi_csv due to --output_stdout.\n");
            args->mMultiCsv = false;
        }
    }

    // Ignore --track_gpu_video if --no_track_gpu used
    if (args->mTrackGPUVideo && !args->mTrackGPU) {
        PrintWarning(L"warning: ignoring --track_gpu_video due to --no_track_gpu.\n");
        args->mTrackGPUVideo = false;
    }

    // Ignore --no_track_display if required for other requested tracking
    if (!args->mTrackDisplay && args->mTrackGPU) {
        PrintWarning(L"warning: ignoring --no_track_display because display tracking is required when GPU tracking is enabled.\n");
        args->mTrackDisplay = true;
    }

    // If --terminate_existing_session, warn about any normal arguments since we'll just
    // be stopping an existing session and then exiting.
    if (args->mTerminateExistingSession) {
        int expectedArgc = 2;
        if (sessionNameSet) expectedArgc += 1;
        if (argc != expectedArgc) {
            PrintWarning(L"warning: --terminate_existing_session exits without capturing anything; ignoring all other options.\n");
        }
    }

    // Enable verbose trace if requested, and disable Full or Simple console output
    #if PRESENTMON_ENABLE_DEBUG_TRACE
    if (verboseTrace) {
        EnableVerboseTrace(true);
        args->mConsoleOutput = ConsoleOutput::None;
    }
    #endif

    // Try to initialize the console, and warn if we're not going to be able to
    // do the advanced display as requested.
    if (args->mConsoleOutput == ConsoleOutput::Statistics && !StdOutIsConsole()) {
        PrintWarning(L"warning: --no_console_stats added because stdout does not support statistics reporting.\n");
        args->mConsoleOutput = ConsoleOutput::Simple;
    }

    // Convert the provided process names into a canonical form used for comparison.
    // The comparison is not case-sensitive, and does not include any directory nor
    // extension.
    //
    // This is because the different paths for obtaining process information return
    // different image name strings.  e.g., the ProcessStart event typically has a
    // full path including "\\Device\\..." and ProcessStop event sometimes is
    // missing part of the extension.
    for (auto& name : args->mTargetProcessNames) {
        CanonicalizeProcessName(&name);
    }
    for (auto& name : args->mExcludeProcessNames) {
        CanonicalizeProcessName(&name);
    }

    args->mTimeUnit = qpcTime   ? TimeUnit::QPC :
                      qpcmsTime ? TimeUnit::QPCMilliSeconds :
                      dtTime    ? TimeUnit::DateTime
                                : TimeUnit::MilliSeconds;

    args->mCSVOutput = csvOutputNone   ? CSVOutput::None :
                       csvOutputStdout ? CSVOutput::Stdout
                                       : CSVOutput::File;

    return true;
}
