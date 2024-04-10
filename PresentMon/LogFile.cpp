#include <time.h>

#include "PresentMon.hpp"

const char* logLevel[] = { "error", "warning", "info" };
const wchar_t* defaultLogFile = L"c:\\Users\\blk\\Documents\\logs\\stream.212.presentmon.log";
const wchar_t* defaultLogTerminateFile = L"c:\\Users\\blk\\Documents\\logs\\stream.212.terminate.presentmon.log";
const wchar_t* defaultStreamId = L"deadcafe";

FILE* pLogFile = nullptr;
wchar_t* streamId = nullptr;

void log(int level, const char* filename, const char* func, int line, const char* format, ...)
{
	auto const& args = GetCommandLineArgs();
	const wchar_t* logFileName = args.mLogFilename;

	// Default log file.
	if (logFileName == nullptr) {
		if (args.mTerminateExistingSession) {
			logFileName = defaultLogTerminateFile;
		}
		else {
			logFileName = defaultLogFile;
		}
	}

	// Initialize log file:
	//	- expected file name : <file_path>\\stream.<stream_id>.presentmon.log
	if ((logFileName != nullptr) && (pLogFile == nullptr) && (streamId == nullptr)) {
		if (_wfopen_s(&pLogFile, logFileName, L"ab") == 0) {
			wchar_t* strLogFileName = new wchar_t[wcslen(logFileName) + 1];
			wcscpy_s(strLogFileName, wcslen(logFileName) + 1, logFileName);
			wchar_t* str = strLogFileName;

			// Search file name
			wchar_t* path;
			wchar_t* nextPath;
			path = wcstok_s(str, L"\\.", &nextPath);
			while (path != nullptr) {
				if (wcscmp(path, L"stream") == 0) {
					// store the stream id
					path = wcstok_s(NULL, L".", &nextPath);
					streamId = new wchar_t[wcslen(path)+1];
					wcscpy_s(streamId, wcslen(path)+1, path);
					path = nullptr;
				}
				else {
					path = wcstok_s(NULL, L"\\.", &nextPath);
				}
			}

			delete[] strLogFileName;
		}
	}
	if (streamId == nullptr) {
		streamId = new wchar_t[wcslen(defaultStreamId) + 1];
		wcscpy_s(streamId, wcslen(defaultStreamId) + 1, defaultStreamId);
	}
	if ((pLogFile != nullptr) && (streamId != nullptr)) {
		size_t logSize = 0;
		char log[2048];

		// Log Message
		char logMsg[2048];
		va_list vl;
		va_start(vl, format);
		vsnprintf(logMsg, sizeof(logMsg), format, vl);
		va_end(vl);

		// Date
		char logDate[500];
		time_t currentTime;
		time(&currentTime);
		struct tm tmDest;
		gmtime_s(&tmDest, &currentTime);
		strftime(logDate, sizeof(logDate), "%Y-%m-%dT%H:%M:%S", &tmDest);

		// Write the log
		logSize += snprintf(log + logSize, sizeof(log) - logSize, "stream=\"%S\" ", streamId);
		logSize += snprintf(log + logSize, sizeof(log) - logSize, "time=\"%s\" ", logDate);
		logSize += snprintf(log + logSize, sizeof(log) - logSize, "level=\"%s\" ", logLevel[level]);
		logSize += snprintf(log + logSize, sizeof(log) - logSize, "thread=\"%d\" ", GetCurrentThreadId());
		logSize += snprintf(log + logSize, sizeof(log) - logSize, "filename=\"%s\" ", filename);
		logSize += snprintf(log + logSize, sizeof(log) - logSize, "function=\"%s\" ", func);
		logSize += snprintf(log + logSize, sizeof(log) - logSize, "line=\"%d\" ", line);
		logSize += snprintf(log + logSize, sizeof(log) - logSize, "msg=\"%s\"", logMsg);

		fprintf(pLogFile, "%s\r\n", log);
		fflush(pLogFile);
	}
}

void cleanLog()
{
	if (pLogFile != nullptr) {
		fflush(pLogFile);
		fclose(pLogFile);
		pLogFile = nullptr;
	}

	if (streamId != nullptr) {
		delete[] streamId;
		streamId = nullptr;
	}
}