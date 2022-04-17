#pragma once

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
	SRWLOCK sessionLock;
	//��Ʈ��ũ �޼����� ���۵�
	CRingBuffer recvQ;
	CLockFreeQueue<CPacket*> sendQ;
	DWORD64 sessionID;

	//send �� ������
	CPacket* sendBuf[100];
	//monitor
	DWORD sendCnt; // << ���� �޼����� Ȯ��

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

