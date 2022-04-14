#pragma once
#include "LockFreeMemoryPool.h"

#define NODEMASK 0x000007ffffffffff

template <class DATA>
struct QUEUE_NODE {
	QUEUE_NODE* volatile next;
	DATA val;
};

template <class DATA>
class CLockFreeQueue
{
public:
	CLockFreeQueue();
	~CLockFreeQueue();

	void Enqueue(DATA val);
	bool Dequeue(DATA* val);

	int GetSize();

private:
	CLockFreeMemoryPool<QUEUE_NODE<DATA>> memPool;

	QUEUE_NODE<DATA>* headNode;
	QUEUE_NODE<DATA>* tailNode;

	int size;
	unsigned int pushCnt;
};

template<class DATA>
inline CLockFreeQueue<DATA>::CLockFreeQueue()
{
	size = 0;
	headNode = memPool.Alloc();
	headNode->next = NULL;
	tailNode = headNode;
	pushCnt = 1;
}

template<class DATA>
inline CLockFreeQueue<DATA>::~CLockFreeQueue()
{
	QUEUE_NODE<DATA>* head = headNode;
	QUEUE_NODE<DATA>* next;
	while (head != NULL) {
		head = (QUEUE_NODE<DATA>*)((__int64)head & NODEMASK);
		next = head->next;
		memPool.Free(head);
		head = next;
	}
}

template<class DATA>
inline void CLockFreeQueue<DATA>::Enqueue(DATA val)
{
	QUEUE_NODE<DATA>* volatile node = NULL;
	node = memPool.Alloc();

	DWORD thread = GetCurrentThreadId();

	node->val = val;
	node->next = NULL;

	node = (QUEUE_NODE<DATA>*)((__int64)node | ((__int64)InterlockedIncrement(&pushCnt) << 44));


	for (;;) {
		QUEUE_NODE<DATA>* volatile tail = tailNode;
		QUEUE_NODE<DATA>* volatile tailVal = (QUEUE_NODE<DATA>*)((__int64)tail & NODEMASK);
		QUEUE_NODE<DATA>* volatile next = tailVal->next;

		if (tail == tailNode) {
			if (next == NULL) {
				if (InterlockedCompareExchange64((long long*)&tailVal->next, (long long)node, NULL) == NULL) {
					InterlockedCompareExchange64((long long*)&tailNode, (long long)node, (long long)tail);
					break;
				}
			}
			else {
				//ENQ의 2번째 CAS가 실패할 경우 밀어줘야 한다
				InterlockedCompareExchange64((long long*)&tailNode, (long long)next, (long long)tail);
			}
		}

	}

	InterlockedIncrement((long*)&size);
}

template<class DATA>
inline bool CLockFreeQueue<DATA>::Dequeue(DATA* val)
{
	if (InterlockedDecrement((long*)&size) < 0) {
		InterlockedIncrement((long*)&size);
		return false;
	}

	for (;;) {
		QUEUE_NODE<DATA>* volatile head = headNode;
		QUEUE_NODE<DATA>* volatile tail = tailNode;
		QUEUE_NODE<DATA>* volatile headVal = (QUEUE_NODE<DATA>*)((__int64)head & NODEMASK);
		QUEUE_NODE<DATA>* volatile next = headVal->next;
		QUEUE_NODE<DATA>* volatile nextVal = (QUEUE_NODE<DATA>*)((__int64)next & NODEMASK);
		
		//만약 노드가 존재하나 반영이 안된 경우 대비 || head가 tail보다 앞서는 것 역시 방지
		if (head == tail) {
			if (next != NULL) {
				InterlockedCompareExchange64((long long*)&tailNode, (long long)next, (long long)tail);
			}
			continue;
		}

		if (InterlockedCompareExchange64((long long*)&headNode, (long long)next, (long long)head) == (long long)head) {
			*val = nextVal->val;
			memPool.Free(headVal);
			return true;
		}
	}

	return false;
}

template<class DATA>
inline int CLockFreeQueue<DATA>::GetSize()
{
	return size;
}

#undef NODEMASK