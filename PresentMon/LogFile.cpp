#include <time.h>

#include "PresentMon.hpp"

const char* logLevel[] = { "error", "warning", "info" };
const char* defaultLogFile = "c:\\Users\\blk\\Documents\\logs\\stream.212.presentmon.log";

FILE* pLogFile = nullptr;
char* streamId = nullptr;

void log(int level, const char* filename, const char* func, int line, const char* format, ...)
{
	auto const& args = GetCommandLineArgs();
	const char* logFileName = args.mLogFile;

	// Default log file.
	if (logFileName == nullptr) {
		logFileName = defaultLogFile;
	}

	FILE* pFile = nullptr;
	fopen_s(&pFile, "c:\\Users\\blk\\Documents\\logs\\pouet.txt", "ab");
	if (pFile != nullptr)
	{
		fprintf(pFile, "log file name: %s %I64d\r\n", logFileName, strlen(logFileName));
		fflush(pFile);
	}

	// Initialize log file:
	//	- expected file name : <file_path>\\stream.<stream_id>.presentmon.log
	if ((logFileName != nullptr) && (pLogFile == nullptr) && (streamId == nullptr)) {
		if (fopen_s(&pLogFile, logFileName, "ab") == 0) {
			char* strLogFileName = new char[strlen(logFileName) + 1];
			strcpy_s(strLogFileName, strlen(logFileName) + 1, logFileName);
			char* str = strLogFileName;

			// Search file name
			char* path;
			char* nextPath;
			path = strtok_s(str, "\\.", &nextPath);
			while (path != nullptr) {
				if (pFile != nullptr)
				{
					fprintf(pFile, "path: %s\r\n", path);
					fflush(pFile);
				}
				if (strcmp(path, "stream") == 0) {
					// store the stream id
					path = strtok_s(NULL, ".", &nextPath);
					streamId = new char[strlen(path)+1];
					strcpy_s(streamId, strlen(path)+1, path);
					path = nullptr;
					if (pFile != nullptr)
					{
						fprintf(pFile, "stream: %s\r\n", streamId);
						fflush(pFile);
					}
				}
				else {
					path = strtok_s(NULL, "\\.", &nextPath);
				}
			}
			if (pFile != nullptr)
			{
				fclose(pFile);
			}

			delete[] strLogFileName;
		}
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
		logSize += snprintf(log + logSize, sizeof(log) - logSize, "stream=\"%s\" ", streamId);
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