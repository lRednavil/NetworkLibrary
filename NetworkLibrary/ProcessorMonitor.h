#pragma once
#include <Windows.h>
#include <Pdh.h>
#include <PdhMsg.h>

#pragma comment(lib,"Pdh.lib")

#define df_PDH_ETHERNET_MAX 8
//--------------------------------------------------------------
// �̴��� �ϳ��� ���� Send,Recv PDH ���� ����.
//--------------------------------------------------------------
struct st_ETHERNET
{
	bool _bUse;
	WCHAR _szName[128];
	PDH_HCOUNTER _pdh_Counter_Network_RecvBytes;
	PDH_HCOUNTER _pdh_Counter_Network_SendBytes;
};


class CProcessorMonitor
{
public:
	CProcessorMonitor(HANDLE hProcess = INVALID_HANDLE_VALUE);

	void UpdateHardwareTime();

	float ProcessorTotal(void) { return _fProcessorTotal; }
	float ProcessorUser(void) { return _fProcessorUser; }
	float ProcessorKernel(void) { return _fProcessorKernel; }

	double EthernetRecv() { return _pdh_value_Network_RecvBytes; }
	double EthernetSend() { return _pdh_value_Network_SendBytes; }
	double EthernetRecvTPS() { return tps_Network_RecvBytes; }
	double EthernetSendTPS() { return tps_Network_SendBytes; }

	long long AvailableMemory() { return availableMemVal; }
	long long NonPagedMemory() { return nonPageVal; }

private:
	float _fProcessorTotal;
	float _fProcessorUser;
	float _fProcessorKernel;

	ULARGE_INTEGER _ftProcessor_LastKernel;
	ULARGE_INTEGER _ftProcessor_LastUser;
	ULARGE_INTEGER _ftProcessor_LastIdle;

	PDH_HQUERY myQuery = NULL;
	PDH_HCOUNTER avaliableMem;
	PDH_HCOUNTER nonPagedMem;

	long long nonPageVal;
	long long availableMemVal;
	/// <summary>
	/// �̴��� ���� ������
	/// </summary>
	st_ETHERNET _EthernetStruct[df_PDH_ETHERNET_MAX]; // ��ī�� �� PDH ����
	double _pdh_value_Network_RecvBytes; // �� Recv Bytes ��� �̴����� Recv ��ġ �ջ�
	double tps_Network_RecvBytes;
	double _pdh_value_Network_SendBytes; // �� Send Bytes ��� �̴����� Send ��ġ �ջ�
	double tps_Network_SendBytes;
	
};

