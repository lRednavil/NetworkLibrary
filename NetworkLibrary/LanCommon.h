#pragma once

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
	bool isDisconnect;
	//네트워크 메세지용 버퍼들
	CRingBuffer recvQ;
	CLockFreeQueue<CPacket*> sendQ;
	DWORD64 sessionID;

	//send 후 해제용
	CPacket* sendBuf[200];
	//monitor
	DWORD sendCnt; // << 보낸 메세지수 확보

	//readonly
	SOCKET sock;
	WCHAR IP[16];

	SESSION() {
		ioCnt = 0;
	}
};

struct LAN_HEADER {
	WORD len;
};