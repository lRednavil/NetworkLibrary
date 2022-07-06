#pragma once

#define ACCEPT_THREAD 1
#define TIMER_THREAD 1
#define SEND_THREAD 1

#define SESSION_MASK 0x00000fffffffffff
#define MASK_SHIFT 45
#define RELEASE_FLAG 0x7000000000000000

#define STATIC_CODE 0x77
#define STATIC_KEY 0x32

#define SEND_PACKET_MAX 200

#pragma comment(lib, "Winmm")

constexpr int WSABUFSIZE = sizeof(WSABUF) * SEND_PACKET_MAX;
constexpr int TRANSBUFSIZE = sizeof(TRANSMIT_PACKETS_ELEMENT) * SEND_PACKET_MAX;

class CMemoryPool;

#pragma pack(push, 1)
struct NET_HEADER {
	BYTE staticCode;
	WORD len;
	BYTE randomKey;
	BYTE checkSum;
};
#pragma pack(pop)
