#include "ProcessMonitor.h"

#include <Pdh.h>

#pragma comment(lib,"Pdh.lib")

CProcessMonitor::CProcessMonitor(HANDLE hProcess)
{
	//------------------------------------------------------------------
// 프로세스 핸들 입력이 없다면 자기 자신을 대상으로...
//------------------------------------------------------------------
	if (hProcess == INVALID_HANDLE_VALUE)
	{
		_hProcess = GetCurrentProcess();
	}
	PDH_STATUS status;
	status = PdhOpenQuery(NULL, NULL, &myQuery);
	if (status != ERROR_SUCCESS) {
		abort();
	}
	//------------------------------------------------------------------
	// 프로세서 개수를 확인한다.
	//
	// 프로세스 (exe) 실행률 계산시 cpu 개수로 나누기를 하여 실제 사용률을 구함.
	//------------------------------------------------------------------
	SYSTEM_INFO SystemInfo;
	GetSystemInfo(&SystemInfo);
	_iNumberOfProcessors = SystemInfo.dwNumberOfProcessors;

	_fProcessTotal = 0;
	_fProcessUser = 0;
	_fProcessKernel = 0;

	_ftProcess_LastUser.QuadPart = 0;
	_ftProcess_LastKernel.QuadPart = 0;
	_ftProcess_LastTime.QuadPart = 0;

	PdhAddCounter(myQuery, L"\\Process(*)\\Private Bytes", NULL, &privateBytes);

	UpdateProcessTime();
}

void CProcessMonitor::UpdateProcessTime()
{
	//---------------------------------------------------------
// 프로세서 사용률을 갱신한다.
//
// 본래의 사용 구조체는 FILETIME 이지만, ULARGE_INTEGER 와 구조가 같으므로 이를 사용함.
// FILETIME 구조체는 100 나노세컨드 단위의 시간 단위를 표현하는 구조체임.
//---------------------------------------------------------
	ULARGE_INTEGER Idle;
	ULARGE_INTEGER Kernel;
	ULARGE_INTEGER User;

	ULONGLONG UserDiff;
	ULONGLONG KernelDiff;
	ULONGLONG TimeDiff;
	ULONGLONG Total;
	
	//---------------------------------------------------------
	// 지정된 프로세스 사용률을 갱신한다.
	//---------------------------------------------------------
	ULARGE_INTEGER None;
	ULARGE_INTEGER NowTime;
	//---------------------------------------------------------
	// 현재의 100 나노세컨드 단위 시간을 구한다. UTC 시간.
	//
	// 프로세스 사용률 판단의 공식
	//
	// a = 샘플간격의 시스템 시간을 구함. (그냥 실제로 지나간 시간)
	// b = 프로세스의 CPU 사용 시간을 구함.
	//
	// a : 100 = b : 사용률 공식으로 사용률을 구함.
	//---------------------------------------------------------
	//---------------------------------------------------------
	// 얼마의 시간이 지났는지 100 나노세컨드 시간을 구함,
	//---------------------------------------------------------
	GetSystemTimeAsFileTime((LPFILETIME)&NowTime);
	//---------------------------------------------------------
	// 해당 프로세스가 사용한 시간을 구함.
	//
	// 두번째, 세번째는 실행,종료 시간으로 미사용.
	//---------------------------------------------------------
	GetProcessTimes(_hProcess, (LPFILETIME)&None, (LPFILETIME)&None, (LPFILETIME)&Kernel, (LPFILETIME)&User);
	//---------------------------------------------------------
	// 이전에 저장된 프로세스 시간과의 차를 구해서 실제로 얼마의 시간이 지났는지 확인.
	//
	// 그리고 실제 지나온 시간으로 나누면 사용률이 나옴.
	//---------------------------------------------------------
	TimeDiff = NowTime.QuadPart - _ftProcess_LastTime.QuadPart;
	UserDiff = User.QuadPart - _ftProcess_LastUser.QuadPart;
	KernelDiff = Kernel.QuadPart - _ftProcess_LastKernel.QuadPart;
	Total = KernelDiff + UserDiff;

	_fProcessTotal = (float)(Total / (double)_iNumberOfProcessors / (double)TimeDiff * 100.0f);
	_fProcessKernel = (float)(KernelDiff / (double)_iNumberOfProcessors / (double)TimeDiff * 100.0f);
	_fProcessUser = (float)(UserDiff / (double)_iNumberOfProcessors / (double)TimeDiff * 100.0f);
	_ftProcess_LastTime = NowTime;
	_ftProcess_LastKernel = Kernel;
	_ftProcess_LastUser = User;

	//메모리 수치 전담
	PDH_FMT_COUNTERVALUE counterVal;
	PdhCollectQueryData(myQuery);

	PDH_STATUS status;
	status = PdhGetFormattedCounterValue(privateBytes, PDH_FMT_LARGE, NULL, &counterVal);
	privateBytesVal = counterVal.largeValue;
}
