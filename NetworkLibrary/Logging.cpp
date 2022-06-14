#include "pch.h"
#include "Logging.h"
#include <direct.h>

#define MAX_LOG_LEN 1024

int g_logLevel = LOG_LEVEL_DEBUG;
WCHAR g_dir[32] = L"LOG";
WCHAR g_logBuf[MAX_LOG_LEN];
WCHAR fileTime[16];

void LogInit()
{
	SYSTEMTIME sysTime;

	GetLocalTime(&sysTime);
	swprintf_s(fileTime, L"_%d%02d", sysTime.wMonth, sysTime.wDay);
}

void FileLog(const WCHAR* fileName, int loglevel, const WCHAR* fmt, ...) {
	if (fileTime[0] == 0) {
		LogInit();
	}

	WCHAR name[256];
	DWORD threadId = GetCurrentThreadId();
	FILE* fp;
	SYSTEMTIME sysTime;
	int wroteLen;

	WCHAR logBuf[1024];

	_wmkdir(g_dir);

	//시간 박제
	GetLocalTime(&sysTime);
	wroteLen = swprintf_s(logBuf, L"[%02d:%02d:%02d] ", sysTime.wHour, sysTime.wMinute, sysTime.wSecond);

	va_list ap;
	va_start(ap, fmt);

	vswprintf_s(&logBuf[wroteLen], MAX_LOG_LEN - wroteLen, fmt, ap);

	va_end(ap);

	swprintf_s(name, L"%s\\%s%s_%u.txt", g_dir, fileName, fileTime, threadId);

	_wfopen_s(&fp, name, L"at");
	if (fp == NULL) return;

	fwprintf_s(fp, logBuf);
	fwprintf_s(fp, L"\n");

	fclose(fp);
}