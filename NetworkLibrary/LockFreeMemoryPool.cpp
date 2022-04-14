#include "pch.h"
#include "LockFreeMemoryPool.h"

struct POOLHEADER {
	POOLHEADER* nextNode = nullptr;
	int len;
	void* poolID;
};

#define HEADERMASK 0x000007ffffffffff
short rearCheckSum = 0x2835;

POOLHEADER* LFMPBase::PoolHeaderAlloc(int dataSize)
{
	POOLHEADER* header = (POOLHEADER*)malloc(sizeof(POOLHEADER) + dataSize + sizeof(short));

	header->nextNode = nullptr;
	header->len = dataSize;
	header->poolID = this;

	//rearCheckSum »ðÀÔ
	memmove_s((void*)((__int64)header + sizeof(POOLHEADER) + header->len), sizeof(short), &rearCheckSum, sizeof(short));

	return header;
}

POOLHEADER* LFMPBase::PoolHeaderAlloc(void** data, int dataSize)
{
	POOLHEADER* header = (POOLHEADER*)malloc(sizeof(POOLHEADER) + dataSize + sizeof(short));

	header->nextNode = nullptr;
	header->len = dataSize;
	header->poolID = this;

	//rearCheckSum »ðÀÔ
	memmove_s((void*)((__int64)header + sizeof(POOLHEADER) + header->len), sizeof(short), &rearCheckSum, sizeof(short));

	*data = (void*)((__int64)header + sizeof(POOLHEADER));

	return header;
}

void LFMPBase::PoolHeaderSetNext(POOLHEADER* header, POOLHEADER* next)
{
	header->nextNode = next;
}

POOLHEADER* LFMPBase::PoolHeaderGetNext(POOLHEADER* header)
{
	header = (POOLHEADER*)((__int64)header & HEADERMASK);
	return header->nextNode;
}

void LFMPBase::PoolHeaderGetData(POOLHEADER* header, void** data)
{
	*data = (void*)((__int64)header + sizeof(POOLHEADER));
}

void LFMPBase::PoolHeaderGetHeader(void* data, POOLHEADER** header)
{
	*header = (POOLHEADER*)((__int64)data - sizeof(POOLHEADER));
}

bool LFMPBase::PoolHeaderCheck(POOLHEADER* header)
{
	short* rearCheck;
	rearCheck = (short*)((__int64)header + sizeof(POOLHEADER) + header->len);

	if (header->poolID != this) {
		return false;
	}

	if (*rearCheck != rearCheckSum) {
		return false;
	}

	return true;
}
