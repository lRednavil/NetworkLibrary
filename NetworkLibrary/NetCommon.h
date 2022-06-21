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

enum OVERLAP_ENUM {
	OV_RECV = 0,
	OV_SEND = 1,
	OV_DISCONNECT = 2,
	OV_SERVEREND = 0xff,
};
struct OVERLAPPEDEX {
	OVERLAPPED overlap;
	WORD type;
};

#pragma pack(push, 1)
struct NET_HEADER {
	BYTE staticCode;
	WORD len;
	BYTE randomKey;
	BYTE checkSum;
};
#pragma pack(pop)
