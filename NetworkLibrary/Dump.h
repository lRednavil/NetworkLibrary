#pragma once

#include <Windows.h>
#include <crtdbg.h>
#include <DbgHelp.h>
#include <stdio.h>
#include <Psapi.h>
#include <minidumpapiset.h>

#pragma comment(lib, "Dbghelp")

#define CRASH() do{ \
int *p = 0; \
*p = 0;	\
}while(0) \


class CDump
{
public:
	CDump() {
		_dumpCnt = 0;

		_invalid_parameter_handler oldHandler, newHandler;
		newHandler = myInvalidParameterHandler;

		oldHandler = _set_invalid_parameter_handler(newHandler);
		_CrtSetReportMode(_CRT_WARN, 0);
		_CrtSetReportMode(_CRT_ASSERT, 0);
		_CrtSetReportMode(_CRT_ERROR, 0);

		_CrtSetReportHook(_custom_Report_hook);

		_set_purecall_handler(myPurecallHandler);

		SetHandlerDump();
	}

	static void Crash() {
		int* p = NULL;
		*p = 0;
	}

	static LONG WINAPI MyExceptionFilter(__in PEXCEPTION_POINTERS pExceptionPointer) 
	{
		int workingMemory = 0;
		SYSTEMTIME nowTime;

		long dumpCnt = InterlockedIncrement(&_dumpCnt);

		HANDLE hProcess = 0;
		PROCESS_MEMORY_COUNTERS pmc;

		hProcess = GetCurrentProcess();

		if (hProcess == NULL) {
			return 0;
		}

		if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc))) {
			workingMemory = (int)(pmc.WorkingSetSize / 1024 / 1024);
		}
		CloseHandle(hProcess);
		

		WCHAR fileName[MAX_PATH];

		GetLocalTime(&nowTime);
		wsprintf(fileName, L"Dump_%d%02d%02d_%02d.%02d.%02d_%d_%dMB.dmp", 
			nowTime.wYear, nowTime.wMonth, nowTime.wDay, nowTime.wHour, nowTime.wMinute, nowTime.wSecond, dumpCnt, workingMemory);

		wprintf_s(L"Save Dump... \n");

		HANDLE hDumpFile = CreateFile(fileName, 
			GENERIC_WRITE, 
			FILE_SHARE_WRITE, 
			NULL, 
			CREATE_ALWAYS, 
			FILE_ATTRIBUTE_NORMAL, NULL);

		if (hDumpFile != INVALID_HANDLE_VALUE) {
			_MINIDUMP_EXCEPTION_INFORMATION MinidumpExceptionInfo;

			MinidumpExceptionInfo.ThreadId = GetCurrentThreadId();
			MinidumpExceptionInfo.ExceptionPointers = pExceptionPointer;
			MinidumpExceptionInfo.ClientPointers = TRUE;

			MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hDumpFile, MiniDumpWithFullMemory, &MinidumpExceptionInfo, NULL, NULL);

			CloseHandle(hDumpFile);

			wprintf_s(L"Dump Saved \n");
		}

		return EXCEPTION_EXECUTE_HANDLER;
	}

	static void SetHandlerDump() {
		SetUnhandledExceptionFilter(MyExceptionFilter);
	}

	static void myInvalidParameterHandler(const wchar_t* expression, const wchar_t* function, const wchar_t* file, unsigned int line, uintptr_t pReserved) {
		Crash();
	}

	static int _custom_Report_hook(int ireposttype, char* message, int* returnvalue) {
		Crash();
		return true;
	}

	static void myPurecallHandler(void) {
		Crash();
	}

	static long _dumpCnt;
};
