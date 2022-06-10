#pragma once
//Ư�� ��ü�� ������Ʈ Ǯ�θ� ����� ��츦 ���� ����
//���� �پ��� ��ü�� �ٷ�Ŷ�� ���� �����ؾߵ�
#include <thread>
#include <Windows.h>

#define HEADERMASK 0x000007ffffffffff

struct POOLHEADER;

class LFMPBase {
public:
	POOLHEADER* PoolHeaderAlloc(int dataSize);
	POOLHEADER* PoolHeaderAlloc(void** data, int dataSize);
	void PoolHeaderSetNext(POOLHEADER* header, POOLHEADER* next);
	POOLHEADER* PoolHeaderGetNext(POOLHEADER* header);
	
	void PoolHeaderGetData(POOLHEADER* header, void** data);
	void PoolHeaderGetHeader(void* data, POOLHEADER** header);

	bool PoolHeaderCheck(POOLHEADER* header);
	
};

template <class DATA>
class CLockFreeMemoryPool : public LFMPBase
{
public:
	//�̸� ������ ��� ����, ������ ��� ����
	CLockFreeMemoryPool(int initNodes = 0, bool newCall = false);
	virtual ~CLockFreeMemoryPool();

	DATA* Alloc();

	bool Free(DATA* data);

	int GetCapacityCount();

	int GetUseCount();

private:
	POOLHEADER* topNode;
	alignas(64)
	int useCount;
	alignas(64)
	long allocTry;

	//read field
	alignas(64)
	bool newCall;
	int capacity;
};

template<class DATA>
inline CLockFreeMemoryPool<DATA>::CLockFreeMemoryPool(int initNodes, bool newCall) : capacity(initNodes), newCall(newCall)
{
	topNode = NULL;

	useCount = 0;
	allocTry = 0;

	//initnodes
	POOLHEADER* nodeVal;
	POOLHEADER* node;
	POOLHEADER* top;

	for (int cnt = 0; cnt < capacity; cnt++) {
		nodeVal = PoolHeaderAlloc(sizeof(DATA));

		//topnode�� ���� node ����
		node = (POOLHEADER*)((__int64)nodeVal | ((long long)_InterlockedIncrement((long*)&allocTry) << 44));
		
		for (;;) {
			top = topNode;

			PoolHeaderSetNext(nodeVal, top);

			if (_InterlockedCompareExchange64((__int64*)&topNode, (__int64)node, (__int64)top) == (__int64)top) {
				break;
			}
		}
	}

}

template<class DATA>
inline CLockFreeMemoryPool<DATA>::~CLockFreeMemoryPool()
{
	POOLHEADER* top = topNode;
	POOLHEADER* node;
	
	while (top) {
		node = PoolHeaderGetNext(top);

		top = (POOLHEADER*)((__int64)top & HEADERMASK);
		free(top);

		top = node;
	}
}

template<class DATA>
inline DATA* CLockFreeMemoryPool<DATA>::Alloc()
{
	POOLHEADER* top;
	POOLHEADER* nxt;
	DATA* ret;

	_InterlockedIncrement((long*)&useCount);

	for (;;) {
		top = topNode;

		if (top == NULL) {
			_InterlockedIncrement((long*)&capacity);

			top = PoolHeaderAlloc((void**)&ret, sizeof(DATA));

			ret = new (ret)DATA();
			return ret;
		}
		else {
			nxt = PoolHeaderGetNext(top);

			if (_InterlockedCompareExchange64((__int64*)&topNode, (__int64)nxt, (__int64)top) != (__int64)top) {
				continue;
			}

			top = (POOLHEADER*)((__int64)top & HEADERMASK);
			PoolHeaderGetData(top, (void**)&ret);

			if (newCall) {
				ret = new (ret)DATA();
			}

			return ret;
		}
	}

}

template<class DATA>
inline bool CLockFreeMemoryPool<DATA>::Free(DATA* data)
{
	_InterlockedDecrement((long*)&useCount);

	POOLHEADER* node;
	POOLHEADER* next;
	POOLHEADER* top;

	PoolHeaderGetHeader((void*)data, &node);

	if (PoolHeaderCheck(node) == false)
		return false;

	if (newCall) {
		data->~DATA();
	}

	//topnode�� ���� node ����
	next = (POOLHEADER*)((__int64)node | ((long long)_InterlockedIncrement((long*)&allocTry) << 44));

	for (;;) {
		top = topNode;

		PoolHeaderSetNext(node, top);

		if (_InterlockedCompareExchange64((__int64*)&topNode, (__int64)next, (__int64)top) == (__int64)top) {
			break;
		}
	}

	return true;
}

template<class DATA>
inline int CLockFreeMemoryPool<DATA>::GetCapacityCount()
{
	return capacity;
}

template<class DATA>
inline int CLockFreeMemoryPool<DATA>::GetUseCount()
{
	return useCount;
}


#undef POOLHEADERMASK
