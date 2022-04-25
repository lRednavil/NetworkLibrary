#pragma once

#define ACCEPT_THREAD 1
#define TIMER_THREAD 1

#define SESSION_MASK 0x00000fffffffffff
#define MASK_SHIFT 45
#define RELEASE_FLAG 0x7000000000000000

#define STATIC_CODE 0x77
#define STATIC_KEY 0x32

class CMemoryPool;

struct OVERLAPPEDEX {
	OVERLAPPED overlap;
	WORD type;
};

struct SESSION {
	OVERLAPPEDEX recvOver;
	OVERLAPPEDEX sendOver;
	//session refCnt�� ����
	DWORD64 ioCnt;
	bool isSending;
	//��Ʈ��ũ �޼����� ���۵�
	CRingBuffer recvQ;
	CLockFreeQueue<CPacket*> sendQ;
	DWORD64 sessionID;

	//timeOut�� ������
	DWORD lastTime;
	DWORD timeOutVal;

	//send �� ������
	CPacket* sendBuf[200];
	//monitor
	DWORD sendCnt; // << ���� �޼����� Ȯ��

	//readonly
	SOCKET sock;
	WCHAR IP[16];

	SESSION() {
		ioCnt = RELEASE_FLAG;
	}
};

#pragma pack(push, 1)
struct NET_HEADER {
	BYTE staticCode;
	WORD len;
	BYTE randomKey;
	BYTE checkSum;
};
#pragma pack(pop)
