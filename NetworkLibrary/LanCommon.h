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
	SRWLOCK sessionLock;
	//네트워크 메세지용 버퍼들
	CRingBuffer recvQ;
	CLockFreeQueue<CPacket*> sendQ;
	DWORD64 sessionID;

	//send 후 해제용
	CPacket* sendBuf[100];
	//monitor
	DWORD sendCnt; // << 보낸 메세지수 확보

	//readonly
	SOCKET sock;
	WCHAR IP[16];

	SESSION() {
		ioCnt = 0;
	}
};

struct NET_HEADER {
	WORD len;
};

template <class DATA>
class CStack {
public:
	CStack<DATA>(int size) : size(size) {
		arr = new DATA[size];
	}
	~CStack<DATA>() {
		delete[] arr;
	}

public:
	bool Push(DATA val);
	bool Pop(DATA val);

private:
	DATA* arr;
	int size;

	int top = 0;
};

