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
	//memory pool°ú ºÐ¸®
	alignas(64)
	STACK_NODE<DATA>* volatile topNode;
	
	alignas(64)
	unsigned long long pushCnt = 0;

	alignas(64)
	unsigned int size;
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
	while (topNode) {
		nxt = topNode->next;
		memPool.Free(topNode);
		topNode = nxt;
	}
}

template<class DATA>
inline void CLockFreeStack<DATA>::Push(DATA val)
{
	STACK_NODE<DATA>* volatile node = memPool.Alloc();
	STACK_NODE<DATA>* volatile nodeVal;
	STACK_NODE<DATA>* volatile t;

	node->val = val;

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

	//*val = tVal->val;
	memmove(val, &tVal->val, sizeof(DATA));
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