#include "pch.h"
#include "LibraryCommon.h"
#include "TLSMemoryPool.h"

CTLSMemoryPool<CPacket> g_PacketPool;
char ZeroField[4096] = {0,};

void MY_MEMORY_CLEAR(void* ptr, int size)
{
	if (size <= 4096) {
		memmove_s(ptr, size, ZeroField, size);
		return;
	}

	int lim = size / 4096;
	int left = size % 4096;

	for (int cnt = 0; cnt < lim; ++cnt) {
		memmove_s(ptr, 4096, ZeroField, 4096);
		ptr = (char*)ptr + 4096;
	}

	memmove_s(ptr, left, ZeroField, left);
}
