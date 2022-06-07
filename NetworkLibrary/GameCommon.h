#pragma once

#define ACCEPT_THREAD 1
#define TIMER_THREAD 1

#define SESSION_MASK 0x00000fffffffffff
#define MASK_SHIFT 45
#define RELEASE_FLAG 0x7000000000000000

#define STATIC_CODE 0x77
#define STATIC_KEY 0x32

#define SEND_PACKET_MAX 200
#define TIMER_PRECISION 5

enum OVERLAP_ENUM {
	OV_RECV = 0,
	OV_SEND = 1,
	OV_DISCONNECT = 2,
};

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
	volatile bool isMoving;

	//��Ʈ��ũ �޼����� ���۵�
	CRingBuffer recvQ;
	CLockFreeQueue<CPacket*> sendQ;
	DWORD64 sessionID;

	//timeOut�� ������
	DWORD lastTime;
	DWORD timeOutVal;

	//send �� ������
	CPacket* sendBuf[SEND_PACKET_MAX];

	//monitor
	DWORD sendCnt; // << ���� �޼����� Ȯ��

	//readonly
	SOCKET sock;
	WCHAR IP[16];

	//gameServer��
	CUSTOM_TCB* belongThread;
	CUnitClass* belongClass;

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
