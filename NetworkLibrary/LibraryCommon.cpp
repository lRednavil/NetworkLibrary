#include "pch.h"
#include "LibraryCommon.h"
#include "TLSMemoryPool.h"

OVERLAPPEDEX g_disconnect_overlap = { {}, OV_DISCONNECT };
OVERLAPPEDEX g_sendReq_overlap = { {}, OV_SEND_REQ };
OVERLAPPEDEX g_serverEnd_overlap = { {}, OV_SERVER_END };

CTLSMemoryPool<CPacket> g_PacketPool;
char ZeroField[2048] = {0,};

void MY_MEMORY_CLEAR(void* ptr, int size)
{
	if (size <= 2048) {
		memmove(ptr, ZeroField, size);
		return;
	}

	int lim = size / 2048;
	int left = size % 2048;

	for (int cnt = 0; cnt < lim; ++cnt) {
		memmove(ptr, ZeroField, 2048);
		ptr = (char*)ptr + 2048;
	}

	memmove(ptr, ZeroField, left);
}
