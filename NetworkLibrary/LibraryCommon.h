#pragma once

extern CTLSMemoryPool<CPacket> g_PacketPool;
extern char ZeroField[2048];

void MY_MEMORY_CLEAR(void* ptr, int size);

#define MEMORY_CLEAR(ptr, size) MY_MEMORY_CLEAR((void*)ptr, (size))
