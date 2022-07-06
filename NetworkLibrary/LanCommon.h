#pragma once

class CMemoryPool;

#define ACCEPT_THREAD 1

#define SESSION_MASK 0x00000fffffffffff
#define MASK_SHIFT 45
#define RELEASE_FLAG 0x7000000000000000

#define SEND_PACKET_MAX 200

#pragma comment(lib, "Winmm")

constexpr int WSABUFSIZE = sizeof(WSABUF) * SEND_PACKET_MAX;

#pragma pack(push, 1)
struct LAN_HEADER {
	WORD len;
};
#pragma pack(pop)
