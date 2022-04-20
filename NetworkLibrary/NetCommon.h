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
	//��Ʈ��ũ �޼����� ���۵�
	CRingBuffer recvQ;
	CLockFreeQueue<CPacket*> sendQ;
	DWORD64 sessionID;

	//send �� ������
	CPacket* sendBuf[200];
	//monitor
	DWORD sendCnt; // << ���� �޼����� Ȯ��

	//readonly
	SOCKET sock;
	WCHAR IP[16];

	SESSION() {
		ioCnt = 0;
	}
};

#pragma pack(push, 1)
struct NET_HEADER {
	BYTE staticKey;
	WORD len;
	BYTE randomKey;
	BYTE CHECKSUM;
};
#pragma pack(pop)
