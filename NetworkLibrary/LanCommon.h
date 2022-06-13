#pragma once

class CMemoryPool;

#define ACCEPT_THREAD 1

#define SESSION_MASK 0x00000fffffffffff
#define MASK_SHIFT 45
#define RELEASE_FLAG 0x7000000000000000

#define SEND_PACKET_MAX 200

#pragma comment(lib, "Winmm")

constexpr int WSABUFSIZE = sizeof(WSABUF) * SEND_PACKET_MAX;

struct OVERLAPPEDEX {
	OVERLAPPED overlap;
	WORD type;
};

struct SESSION {
	OVERLAPPEDEX recvOver;
	OVERLAPPEDEX sendOver;
	//session refCnt�� ����
	alignas(64)
		DWORD64 ioCnt;
	alignas(64)
		short isSending;

	//��Ʈ��ũ �޼����� ���۵�
	alignas(64)
		CRingBuffer recvQ;
	alignas(64)
		CLockFreeQueue<CPacket*> sendQ;
	alignas(64)
		SOCKET sock;
	
	alignas(64)
		DWORD64 sessionID;

	//send �� ������
	CPacket* sendBuf[SEND_PACKET_MAX];
	//monitor
	DWORD sendCnt; // << ���� �޼����� Ȯ��

	//readonly
	WCHAR IP[16];

	SESSION() {
		ioCnt = RELEASE_FLAG;
	}
};

#pragma pack(push, 1)
struct LAN_HEADER {
	WORD len;
};
#pragma pack(pop)
