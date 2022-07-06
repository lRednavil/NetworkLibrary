#pragma once

#define ACCEPT_THREAD 1
#define TIMER_THREAD 1
#define SEND_THREAD 1

#define SESSION_MASK 0x00000fffffffffff
#define MASK_SHIFT 45
#define RELEASE_FLAG 0x7000000000000000

#define STATIC_CODE 0x77
#define STATIC_KEY 0x32
#define TCB_MAX 500

#define SEND_PACKET_MAX 128
#define TIMER_PRECISION 5

#define PACKETPOOL_MAX 100

#pragma pack(push, 1)
struct GAME_PACKET_HEADER {
	BYTE staticCode;
	WORD len;
	BYTE randomKey;
	BYTE checkSum;
};
#pragma pack(pop)
