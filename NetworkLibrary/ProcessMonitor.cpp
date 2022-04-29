#include "pch.h"
#include "ProcessMonitor.h"

#include <Pdh.h>
#include <Psapi.h>

#pragma comment(lib,"Pdh.lib")

CProcessMonitor::CProcessMonitor(HANDLE hProcess)
{
	//------------------------------------------------------------------
// ���μ��� �ڵ� �Է��� ���ٸ� �ڱ� �ڽ��� �������...
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

	WCHAR queryStr[MAX_PATH];
	WCHAR processName[MAX_PATH];
	DWORD bufSize = MAX_PATH;

	QueryFullProcessImageName(_hProcess, NULL, processName, &bufSize);
	//swprintf_s(queryStr, L"\\Process(*)\\Private Bytes", processName);

	//------------------------------------------------------------------
	// ���μ��� ������ Ȯ���Ѵ�.
	//
	// ���μ��� (exe) ����� ���� cpu ������ �����⸦ �Ͽ� ���� ������ ����.
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

	PdhAddCounter(myQuery, L"\\Process(*)\\Working Set", NULL, &privateBytes);

	UpdateProcessTime();
}

void CProcessMonitor::UpdateProcessTime()
{
	//---------------------------------------------------------
// ���μ��� ������ �����Ѵ�.
//
// ������ ��� ����ü�� FILETIME ������, ULARGE_INTEGER �� ������ �����Ƿ� �̸� �����.
// FILETIME ����ü�� 100 ���뼼���� ������ �ð� ������ ǥ���ϴ� ����ü��.
//---------------------------------------------------------
	ULARGE_INTEGER Idle;
	ULARGE_INTEGER Kernel;
	ULARGE_INTEGER User;

	ULONGLONG UserDiff;
	ULONGLONG KernelDiff;
	ULONGLONG TimeDiff;
	ULONGLONG Total;
	
	//---------------------------------------------------------
	// ������ ���μ��� ������ �����Ѵ�.
	//---------------------------------------------------------
	ULARGE_INTEGER None;
	ULARGE_INTEGER NowTime;
	//---------------------------------------------------------
	// ������ 100 ���뼼���� ���� �ð��� ���Ѵ�. UTC �ð�.
	//
	// ���μ��� ���� �Ǵ��� ����
	//
	// a = ���ð����� �ý��� �ð��� ����. (�׳� ������ ������ �ð�)
	// b = ���μ����� CPU ��� �ð��� ����.
	//
	// a : 100 = b : ���� �������� ������ ����.
	//---------------------------------------------------------
	//---------------------------------------------------------
	// ���� �ð��� �������� 100 ���뼼���� �ð��� ����,
	//---------------------------------------------------------
	GetSystemTimeAsFileTime((LPFILETIME)&NowTime);
	//---------------------------------------------------------
	// �ش� ���μ����� ����� �ð��� ����.
	//
	// �ι�°, ����°�� ����,���� �ð����� �̻��.
	//---------------------------------------------------------
	GetProcessTimes(_hProcess, (LPFILETIME)&None, (LPFILETIME)&None, (LPFILETIME)&Kernel, (LPFILETIME)&User);
	//---------------------------------------------------------
	// ������ ����� ���μ��� �ð����� ���� ���ؼ� ������ ���� �ð��� �������� Ȯ��.
	//
	// �׸��� ���� ������ �ð����� ������ ������ ����.
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

	//�޸� ��ġ ����
	PROCESS_MEMORY_COUNTERS_EX pmCounter;
	GetProcessMemoryInfo(_hProcess, (PROCESS_MEMORY_COUNTERS*)&pmCounter, sizeof(pmCounter));

	privateBytesVal = pmCounter.PrivateUsage;
}
