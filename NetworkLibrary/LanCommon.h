#pragma once
#include "pch.h"

struct OVERLAPPEDEX {
	OVERLAPPED overlap;
	WORD type;
};

struct SESSION {
	OVERLAPPEDEX recvOver;
	OVERLAPPEDEX sendOver;
	DWORD ioCnt;
	bool isSending;
	SRWLOCK sessionLock;
	CRingBuffer recvQ;
	CRingBuffer sendQ;
	DWORD64 sessionID;

	//monitor
	DWORD sendCnt; // << ���� �޼����� Ȯ��

	//readonly
	SOCKET sock;
	WCHAR IP[16];
};

struct NET_HEADER {
	WORD len;
};