#pragma once

class CMemoryPool;

struct OVERLAPPEDEX {
	OVERLAPPED overlap;
	WORD type;
};

struct SESSION {
	OVERLAPPEDEX recvOver;
	OVERLAPPEDEX sendOver;
	DWORD ioCnt;
	bool isSending;
	SRWLOCK sessionLock;
	CRingBuffer recvQ;
	CRingBuffer sendQ;
	DWORD64 sessionID;

	//monitor
	DWORD sendCnt; // << 보낸 메세지수 확보

	//readonly
	SOCKET sock;
	WCHAR IP[16];
};

struct NET_HEADER {
	WORD len;
};

extern CMemoryPool g_LanPacketPool;