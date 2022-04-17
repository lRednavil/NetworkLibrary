#pragma once
#include "LockFreeMemoryPool.h"

template <class DATA>
struct CHUNKNODE {
	void* pChunk;
	DATA val;
};

template <class DATA>
struct CHUNK {
	short useCount;
	short freeCount;

	CHUNKNODE<DATA>* arr;
};

template <class DATA>
class CTLSMemoryPool : public LFMPBase
{
public:
	//미리 생성할 노드 개수, 생성자 사용 여부
	CTLSMemoryPool(int chunkSize = 1000, bool newCall = false);
	virtual ~CTLSMemoryPool();

	DATA* Alloc();

	bool Free(DATA* data);

	int GetCapacityCount();

	int GetUseCount();

private:
	CHUNK<DATA>* ChunkAlloc();

private:
	bool newCall;

	CLockFreeMemoryPool<CHUNK<DATA>>* chunkPool;

	DWORD tlsID;
	int chunkSize;
	int capacity;
	int useCount;

	long allocTry;
};

template<class DATA>
inline CTLSMemoryPool<DATA>::CTLSMemoryPool(int chunkSize, bool newCall) : chunkSize(chunkSize), newCall(newCall)
{
	tlsID = TlsAlloc();

	capacity = 0;
	useCount = 0;
	allocTry = 0;

	chunkPool = new CLockFreeMemoryPool<CHUNK<DATA>>;
}

template<class DATA>
inline CTLSMemoryPool<DATA>::~CTLSMemoryPool()
{
	TlsFree(tlsID);
}

template<class DATA>
inline DATA* CTLSMemoryPool<DATA>::Alloc()
{
	CHUNK<DATA>* chunk = (CHUNK<DATA>*)TlsGetValue(tlsID);
	DATA* ret;

	if (chunk == NULL) {
		_InterlockedIncrement((long*)&useCount);
		chunk = ChunkAlloc();
		TlsSetValue(tlsID, (void*)chunk);
	}

	--chunk->useCount;

	if (chunk->useCount < 0) {
		abort();
	}

	ret = &(chunk->arr[chunk->useCount].val);

	if (newCall) {
		new (ret)DATA;
	}

	if (chunk->useCount == 0) {
		_InterlockedIncrement((long*)&useCount);
		chunk = ChunkAlloc();
		TlsSetValue(tlsID, (void*)chunk);
	}

	return ret;
}

template<class DATA>
inline CHUNK<DATA>* CTLSMemoryPool<DATA>::ChunkAlloc()
{
	CHUNK<DATA>* chunk = chunkPool->Alloc();

	chunk->freeCount = chunk->useCount = chunkSize;
	if (chunk->arr == NULL) {
		InterlockedIncrement((long*)&capacity);
		chunk->arr = new CHUNKNODE<DATA>[chunkSize];

		for (int arrCnt = 0; arrCnt < chunkSize; arrCnt++) {
			chunk->arr[arrCnt].pChunk = chunk;
		}
	}

	return chunk;
}

template<class DATA>
inline bool CTLSMemoryPool<DATA>::Free(DATA* data)
{
	CHUNK<DATA>* chunk = (CHUNK<DATA>*)((CHUNKNODE<DATA>*)((__int64)data - sizeof(void*)))->pChunk;

	if (newCall) {
		data->~DATA();
	}

	if (_InterlockedDecrement16(&(chunk->freeCount)) > 0) {
		return true;
	}

	_InterlockedDecrement((long*)&useCount);

	return chunkPool->Free(chunk);
}

template<class DATA>
inline int CTLSMemoryPool<DATA>::GetCapacityCount()
{
	return capacity;
}

template<class DATA>
inline int CTLSMemoryPool<DATA>::GetUseCount()
{
	return useCount;
}
