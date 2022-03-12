#pragma once

extern short _rearCheckSum;

// 64, 128, 256, 512, 1024, 2048, 4096, 8192
#define DATA_INDEX 10
//데이터 최소단위 64
#define DATA_DEFAULT 64
#define rearCheckSum 0x2835
//default 생성자 옵션 || 최소단위 64byte, placement new 사용 X
//최소단위 변경시 2의 배수로 선언할 것
class CMemoryPool
{
public:

	//////////////////////////////////////////////////////////////////////////
	// 생성자, 파괴자.
	//
	// Parameters:	(int) 초기 블럭 개수.
	//				(bool) Alloc 시 생성자 / Free 시 파괴자 호출 여부
	// Return:
	//////////////////////////////////////////////////////////////////////////
	CMemoryPool(int minSize = DATA_DEFAULT, bool newCall = false);
	virtual	~CMemoryPool();


	//////////////////////////////////////////////////////////////////////////
	// 블럭 하나를 할당받는다.  
	//
	// Parameters: 없음.
	// Return: (DATA *) 데이타 블럭 포인터.
	//////////////////////////////////////////////////////////////////////////
	template <class DATA>
	DATA* Alloc(DATA* ret);

	//////////////////////////////////////////////////////////////////////////
	// 사용중이던 블럭을 해제한다.
	//
	// Parameters: (DATA *) 블럭 포인터.
	// Return: (BOOL) TRUE, FALSE.
	//////////////////////////////////////////////////////////////////////////
	template <class DATA>
	bool	Free(DATA* pData);


	//////////////////////////////////////////////////////////////////////////
	// 현재 확보 된 블럭 개수를 얻는다. (메모리풀 내부의 전체 개수)
	//
	// Parameters: 없음.
	// Return: (int) 메모리 풀 내부 전체 개수
	//////////////////////////////////////////////////////////////////////////
	int		GetCapacityCount(int size);

	//////////////////////////////////////////////////////////////////////////
	// 현재 사용중인 블럭 개수를 얻는다.
	//
	// Parameters: 없음.
	// Return: (int) 사용중인 블럭 개수.
	//////////////////////////////////////////////////////////////////////////
	int		GetUseCount(int size);




private:
	/*
	struct BLOCK_NODE {
		BLOCK_NODE* nextNode = NULL;
		//data 길이 함유
		int len; 
		//poolID겸 언더플로우 체크
		void* poolID;
		//malloc이후 별도로 부여
		BYTE data[len];
		short reserved;
	};*/


	struct HEADER {
		HEADER* nextNode = NULL;
		int len;
		void* poolID;
	};

	bool _newCall;

	// deque 방식으로 반환된 (미사용) 오브젝트 블럭을 관리.
	// garbage는 queing으로 관리 재사용은 stacking으로 관리
	HEADER* _topNode[DATA_INDEX];
	HEADER* _bottomNode[DATA_INDEX];

	int _capacity[DATA_INDEX] = { 0 };
	int _useCount[DATA_INDEX] = { 0 };

	//dataPool Size 종류 저장
	int __DATA_SIZE[DATA_INDEX];
	//dataPool 최소단위
	int _minSize;

	SRWLOCK poolLock;
};

inline CMemoryPool::CMemoryPool(int minSize, bool newCall) : _minSize(minSize), _newCall(newCall)
{
	for (int idx = 0; idx < DATA_INDEX; idx++) {
		__DATA_SIZE[idx] = _minSize * (idx + 1);
	}
	InitializeSRWLock(&poolLock);
}

inline CMemoryPool::~CMemoryPool()
{
	HEADER* temp;
	for (int idx = 0; idx < DATA_INDEX; idx++) {
		while (_topNode[idx] != NULL) {
			temp = _topNode[idx]->nextNode;
			free(_topNode[idx]);
			_topNode[idx] = temp;
		}
	}
}

//parameter는 단순히 data Type 및 생성자 호출용
template<class DATA>
inline DATA* CMemoryPool::Alloc(DATA* val)
{
	CLock LOCK(&poolLock, 1);
	int idx = sizeof(DATA);
	DATA* ret;
	//0할당 예외처리
	if (idx == 0) {
		return NULL;
	}

	idx = (idx - 1) / _minSize;	
	//사이즈 지나치게 클 시 그냥 할당
	if (idx >= DATA_INDEX) {
		ret = new DATA;
		return ret;
	}

	_useCount[idx]++;

	if (_topNode[idx] == NULL) {
		_capacity[idx]++;
		//HEADER + DATA영역 + rearCheckSum
		HEADER* node = (HEADER*)malloc(sizeof(HEADER) + __DATA_SIZE[idx] + sizeof(short));
		node->len = sizeof(DATA);
		//rearCheckSum 삽입
		memmove_s((void*)((__int64)node + sizeof(HEADER) + node->len), sizeof(short), &_rearCheckSum, sizeof(short));
		node->poolID = this;

		ret = (DATA*)((__int64)node + sizeof(HEADER));
		
		//생성자 호출
		ret = new (ret)DATA();
		return ret;
	}
	else {
		ret = (DATA*)((__int64)_topNode[idx] + sizeof(HEADER));
		_topNode[idx]->len = sizeof(DATA);
				
		//rearCheckSum 삽입
		memmove_s((void*)((__int64)_topNode[idx] + sizeof(HEADER) + _topNode[idx]->len), sizeof(short), &_rearCheckSum, sizeof(short));

		_topNode[idx] = _topNode[idx]->nextNode;
		if (_newCall) {
			//생성자 호출
			ret = new (ret)DATA();
		}
		
		return ret;
	}
}

template<class DATA>
inline bool CMemoryPool::Free(DATA* pData)
{
	CLock LOCK(&poolLock, 1);
	HEADER* node = (HEADER*)((__int64)pData - sizeof(HEADER));
	short* rearCheck = (short*)((__int64)pData + node->len);
	int idx = (node->len - 1) / _minSize;

	//잘못된 큰 데이터 삽입
	if (idx >= DATA_INDEX) {
		delete pData;
		return false;
	}

	if (node->poolID != this) {
		abort();
	}

	if (*rearCheck != rearCheckSum) {
		abort();
	}

	if (_newCall) {
		pData->~DATA();
	}
	node->nextNode = _topNode[idx];
	_topNode[idx] = node;

	_useCount[idx]--;

	return true;
}

inline int CMemoryPool::GetCapacityCount(int size)
{
	int idx = (size - 1) / _minSize;
	return _capacity[idx];
}

inline int CMemoryPool::GetUseCount(int size)
{
	int idx = (size - 1) / _minSize;
	return _useCount[idx];
}

#undef DATA_INDEX
#undef DATA_DEFAULT
#undef rearCheckSum
