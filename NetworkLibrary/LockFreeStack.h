#pragma once

#include <thread>
#include <Windows.h>
#include "LockFreeMemoryPool.h"

#define NODEMASK 0x000007ffffffffff


template <class DATA>
struct STACK_NODE {
	STACK_NODE* next;
	DATA val;
};
//
//struct LOGDATA {
//	void* thread = 0;
//	short type = 0;
//	void* val = 0;
//	void* nxt = 0;
//	int popTry;
//};
//
//extern LOGDATA logArr[1000000];
//extern DWORD logidx;

template <class DATA>
class CLockFreeStack
{
public:
	CLockFreeStack();
	~CLockFreeStack();

	void Push(DATA val);
	bool Pop(DATA* val);

	unsigned int GetSize();

private:
	CLockFreeMemoryPool<STACK_NODE<DATA>> memPool;
	STACK_NODE<DATA>* volatile topNode;

	unsigned int size;
	unsigned long long pushCnt = 0;
	unsigned long long realPop = 0;
};

template<class DATA>
inline CLockFreeStack<DATA>::CLockFreeStack()
{
	topNode = NULL;
	size = 0;
}

template<class DATA>
inline CLockFreeStack<DATA>::~CLockFreeStack()
{
	STACK_NODE<DATA>* nxt;
	while (top) {
		nxt = top->next;
		memPool.Free(top);
		top = nxt;
	}
}

template<class DATA>
inline void CLockFreeStack<DATA>::Push(DATA val)
{
	STACK_NODE<DATA>* volatile node = NULL;
	STACK_NODE<DATA>* volatile nodeVal;
	node = memPool.Alloc();
	node->val = val;

	STACK_NODE<DATA>* volatile t;

	nodeVal = node;
	node = (STACK_NODE<DATA>*)((__int64)node | ((__int64)InterlockedIncrement(&pushCnt) << 44));

	for (;;) {
		t = topNode;
		nodeVal->next = t;

		if (_InterlockedCompareExchangePointer((void**)&topNode, node, (PVOID)t) == t) {
			break;
		}
	}

	_InterlockedIncrement(&size);
}

template<class DATA>
inline bool CLockFreeStack<DATA>::Pop(DATA* val)
{
	STACK_NODE<DATA>* volatile t;
	STACK_NODE<DATA>* volatile tVal;
	STACK_NODE<DATA>* volatile nxt;
	for (;;) {
		t = topNode;
		if (t == NULL) {
			return false;
		}

		tVal = (STACK_NODE<DATA>*)((__int64)t & NODEMASK);
		nxt = tVal->next;
		if (_InterlockedCompareExchangePointer((void**)&topNode, nxt, (PVOID)t) == t) {
			break;
		}
	}

	*val = tVal->val;
	memPool.Free(tVal);

	_InterlockedDecrement(&size);

	return true;
}

template<class DATA>
inline unsigned int CLockFreeStack<DATA>::GetSize()
{
	return size;
}

#undef NODEMASK