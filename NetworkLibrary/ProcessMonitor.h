#pragma once
#include <Windows.h>
#include <Pdh.h>
#include <PdhMsg.h>

#pragma comment(lib,"Pdh.lib")

class CProcessMonitor
{
public:
	CProcessMonitor(HANDLE hProcess = INVALID_HANDLE_VALUE);

	void UpdateProcessTime();

	float ProcessTotal(void) { return _fProcessTotal; }
	float ProcessUser(void) { return _fProcessUser; }
	float ProcessKernel(void) { return _fProcessKernel; }

	long long ProcessPrivateBytes() { return privateBytesVal; }

private:
	HANDLE _hProcess;
	int _iNumberOfProcessors;

	float _fProcessTotal;
	float _fProcessUser;
	float _fProcessKernel;
	
	ULARGE_INTEGER _ftProcess_LastKernel;
	ULARGE_INTEGER _ftProcess_LastUser;
	ULARGE_INTEGER _ftProcess_LastTime;

	PDH_HQUERY myQuery = NULL;
	PDH_HCOUNTER privateBytes;

	long long privateBytesVal;


};

