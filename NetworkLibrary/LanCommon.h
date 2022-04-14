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

union SESSIONID {
	struct {
		WORD index;
		struct ID {
			WORD high;
			DWORD low;
		};
	};
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

