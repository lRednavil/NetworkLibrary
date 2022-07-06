#pragma once

enum OVERLAP_ENUM {
	OV_RECV,
	OV_SEND_FIN,
	OV_DISCONNECT,
	OV_SEND_REQ,

	OV_SERVER_END = 0xff
};

struct OVERLAPPEDEX {
	OVERLAPPED overlap;
	WORD type;
};

extern OVERLAPPEDEX g_disconnect_overlap;
extern OVERLAPPEDEX g_serverEnd_overlap;
extern OVERLAPPEDEX g_sendReq_overlap;

extern CTLSMemoryPool<CPacket> g_PacketPool;
extern char ZeroField[2048];

void MY_MEMORY_CLEAR(void* ptr, int size);

#define MEMORY_CLEAR(ptr, size) MY_MEMORY_CLEAR((void*)ptr, (size))
