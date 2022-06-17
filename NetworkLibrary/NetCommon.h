#pragma once

#define ACCEPT_THREAD 1
#define TIMER_THREAD 1
#define SEND_THREAD 1

#define SESSION_MASK 0x00000fffffffffff
#define MASK_SHIFT 45
#define RELEASE_FLAG 0x7000000000000000

#define STATIC_CODE 0x77
#define STATIC_KEY 0x32

#define SEND_PACKET_MAX 200

#pragma comment(lib, "Winmm")

constexpr int WSABUFSIZE = sizeof(WSABUF) * SEND_PACKET_MAX;
constexpr int TRANSBUFSIZE = sizeof(TRANSMIT_PACKETS_ELEMENT) * SEND_PACKET_MAX;

class CMemoryPool;

enum OVERLAP_ENUM {
	OV_RECV = 0,
	OV_SEND = 1,
	OV_DISCONNECT = 2,
	OV_SERVEREND = 0xff,
};
struct OVERLAPPEDEX {
	OVERLAPPED overlap;
	WORD type;
};

struct SESSION {
	OVERLAPPEDEX recvOver;
	OVERLAPPEDEX sendOver;
	LPFN_TRANSMITPACKETS transFn;
	
	//session refCnt�� ����
	alignas(64)
		DWORD64 ioCnt;
	alignas(64)
		short isSending;
	//send �� ������
	CPacket* sendBuf[SEND_PACKET_MAX];
	//monitor
	DWORD sendCnt; // << ���� �޼����� Ȯ��

	//��Ʈ��ũ �޼����� ���۵�
	alignas(64)
		CRingBuffer recvQ;
	alignas(64)
		CLockFreeQueue<CPacket*> sendQ;
	alignas(64)
		SOCKET sock;

	//readonly
	alignas(64)
		DWORD64 sessionID;

	//timeOut�� ������
	DWORD lastTime;
	DWORD timeOutVal;
	bool isTimeOutReserved = false;
	bool isDisconnectReserved = false;

	WCHAR IP[16];

	SESSION() {
		ioCnt = RELEASE_FLAG;
		isSending = 0;
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
