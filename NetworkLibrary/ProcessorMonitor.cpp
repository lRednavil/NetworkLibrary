#include "ProcessorMonitor.h"
#include "ProcessMonitor.h"
#include <iostream>
#include <strsafe.h>


CProcessorMonitor::CProcessorMonitor(HANDLE hProcess)
{
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

	_fProcessorTotal = 0;
	_fProcessorUser = 0;
	_fProcessorKernel = 0;
	
	_ftProcessor_LastKernel.QuadPart = 0;
	_ftProcessor_LastUser.QuadPart = 0;
	_ftProcessor_LastIdle.QuadPart = 0;

	//이더넷 영역
	int iCnt = 0;
	bool bErr = false;
	WCHAR* szCur = NULL;
	WCHAR* szCounters = NULL;
	WCHAR* szInterfaces = NULL;
	DWORD dwCounterSize = 0, dwInterfaceSize = 0;
	WCHAR szQuery[1024] = { 0, };

	// PDH enum Object 를 사용하는 방법.
	// 모든 이더넷 이름이 나오지만 실제 사용중인 이더넷, 가상이더넷 등등을 확인불가 함.

	//---------------------------------------------------------------------------------------
// PdhEnumObjectItems 을 통해서 "NetworkInterface" 항목에서 얻을 수 있는
// 측성항목(Counters) / 인터페이스 항목(Interfaces) 를 얻음. 그런데 그 개수나 길이를 모르기 때문에
// 먼저 버퍼의 길이를 알기 위해서 Out Buffer 인자들을 NULL 포인터로 넣어서 사이즈만 확인.
//---------------------------------------------------------------------------------------
	PdhEnumObjectItems(NULL, NULL, L"Network Interface", szCounters, &dwCounterSize, szInterfaces, &dwInterfaceSize, PERF_DETAIL_WIZARD, 0);
	szCounters = new WCHAR[dwCounterSize];
	szInterfaces = new WCHAR[dwInterfaceSize];
	//---------------------------------------------------------------------------------------
	// 버퍼의 동적할당 후 다시 호출!
	//
	// szCounters 와 szInterfaces 버퍼에는 여러개의 문자열이 쭉쭉쭉 들어온다. 2차원 배열도 아니고,
	// 그냥 NULL 포인터로 끝나는 문자열들이 dwCounterSize, dwInterfaceSize 길이만큼 줄줄이 들어있음.
	// 이를 문자열 단위로 끊어서 개수를 확인 해야 함. aaa\0bbb\0ccc\0ddd 이딴 식
	//---------------------------------------------------------------------------------------

	if (PdhEnumObjectItems(NULL, NULL, L"Network Interface", szCounters, &dwCounterSize, szInterfaces, &dwInterfaceSize, PERF_DETAIL_WIZARD,
		0) != ERROR_SUCCESS)
	{
		delete[] szCounters;
		delete[] szInterfaces;
		return;
	}
	iCnt = 0;
	szCur = szInterfaces;
	//---------------------------------------------------------
	// szInterfaces 에서 문자열 단위로 끊으면서 , 이름을 복사받는다.
	//---------------------------------------------------------
	for (; *szCur != L'\0' && iCnt < df_PDH_ETHERNET_MAX; szCur += wcslen(szCur) + 1, iCnt++)
	{
		_EthernetStruct[iCnt]._bUse = true;
		_EthernetStruct[iCnt]._szName[0] = L'\0';
		wcscpy_s(_EthernetStruct[iCnt]._szName, szCur);
		szQuery[0] = L'\0';
		StringCbPrintf(szQuery, sizeof(WCHAR) * 1024, L"\\Network Interface(%s)\\Bytes Received/sec", szCur);
		PdhAddCounter(myQuery, szQuery, NULL, &_EthernetStruct[iCnt]._pdh_Counter_Network_RecvBytes);
		szQuery[0] = L'\0';
		StringCbPrintf(szQuery, sizeof(WCHAR) * 1024, L"\\Network Interface(%s)\\Bytes Sent/sec", szCur);
		PdhAddCounter(myQuery, szQuery, NULL, &_EthernetStruct[iCnt]._pdh_Counter_Network_SendBytes);
	}

	//메모리 수치 처리용
	status = PdhAddCounter(myQuery, L"\\Memory\\Available MBytes", NULL, &avaliableMem);
	status = PdhAddCounter(myQuery, L"\\Memory\\Pool Nonpaged Bytes", NULL, &nonPagedMem);

	UpdateHardwareTime();
}

void CProcessorMonitor::UpdateHardwareTime()
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
	//---------------------------------------------------------
	// 시스템 사용 시간을 구한다.
	//
	// 아이들 타임 / 커널 사용 타임 (아이들포함) / 유저 사용 타임
	//---------------------------------------------------------
	if (GetSystemTimes((PFILETIME)&Idle, (PFILETIME)&Kernel, (PFILETIME)&User) == false)
	{
		return;
	}
	// 커널 타임에는 아이들 타임이 포함됨.
	ULONGLONG KernelDiff = Kernel.QuadPart - _ftProcessor_LastKernel.QuadPart;
	ULONGLONG UserDiff = User.QuadPart - _ftProcessor_LastUser.QuadPart;
	ULONGLONG IdleDiff = Idle.QuadPart - _ftProcessor_LastIdle.QuadPart;
	ULONGLONG Total = KernelDiff + UserDiff;

	if (Total == 0)
	{
		_fProcessorUser = 0.0f;
		_fProcessorKernel = 0.0f;
		_fProcessorTotal = 0.0f;
	}
	else
	{
		// 커널 타임에 아이들 타임이 있으므로 빼서 계산.
		_fProcessorTotal = (float)((double)(Total - IdleDiff) / Total * 100.0f);
		_fProcessorUser = (float)((double)UserDiff / Total * 100.0f);
		_fProcessorKernel = (float)((double)(KernelDiff - IdleDiff) / Total * 100.0f);
	}
	_ftProcessor_LastKernel = Kernel;
	_ftProcessor_LastUser = User;
	_ftProcessor_LastIdle = Idle;

	//이더넷 전담
	PDH_FMT_COUNTERVALUE counterVal;
	PdhCollectQueryData(myQuery);
	PDH_STATUS status;

	for (int iCnt = 0; iCnt < df_PDH_ETHERNET_MAX; iCnt++)
	{
		if (_EthernetStruct[iCnt]._bUse)
		{
			status = PdhGetFormattedCounterValue(_EthernetStruct[iCnt]._pdh_Counter_Network_RecvBytes,
				PDH_FMT_DOUBLE, NULL, &counterVal);
			if (status == 0) _pdh_value_Network_RecvBytes += counterVal.doubleValue;
			status = PdhGetFormattedCounterValue(_EthernetStruct[iCnt]._pdh_Counter_Network_SendBytes,
				PDH_FMT_DOUBLE, NULL, &counterVal);
			if (status == 0) _pdh_value_Network_SendBytes += counterVal.doubleValue;
		}
	}

	//메모리 수치 전담
	status = PdhGetFormattedCounterValue(avaliableMem, PDH_FMT_LARGE, NULL, &counterVal);
	availableMemVal = counterVal.largeValue;
	status = PdhGetFormattedCounterValue(nonPagedMem, PDH_FMT_LARGE, NULL, &counterVal);
	nonPageVal = counterVal.largeValue;

}
