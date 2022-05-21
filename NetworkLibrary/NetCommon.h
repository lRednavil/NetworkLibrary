#pragma once

#define ACCEPT_THREAD 1
#define TIMER_THREAD 1

#define SESSION_MASK 0x00000fffffffffff
#define MASK_SHIFT 45
#define RELEASE_FLAG 0x7000000000000000

#define STATIC_CODE 0x77
#define STATIC_KEY 0x32

#define SEND_PACKET_MAX 200

class CMemoryPool;

struct OVERLAPPEDEX {
	OVERLAPPED overlap;
	WORD type;
};

struct SESSION {
	OVERLAPPEDEX recvOver;
	OVERLAPPEDEX sendOver;
	//session refCnt의 역할
	DWORD64 ioCnt;
	bool isSending;
	//네트워크 메세지용 버퍼들
	CRingBuffer recvQ;
	CLockFreeQueue<CPacket*> sendQ;
	DWORD64 sessionID;

	//timeOut용 변수들
	DWORD lastTime;
	DWORD timeOutVal;
	bool isTimeOutReserved = false;

	//send 후 해제용
	CPacket* sendBuf[SEND_PACKET_MAX];
	//monitor
	DWORD sendCnt; // << 보낸 메세지수 확보

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
