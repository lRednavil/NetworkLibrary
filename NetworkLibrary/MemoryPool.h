#pragma once

extern short _rearCheckSum;

// 64, 128, 256, 512, 1024, 2048, 4096, 8192
#define DATA_INDEX 10
//������ �ּҴ��� 64
#define DATA_DEFAULT 64
//default ������ �ɼ� || �ּҴ��� 64byte, placement new ��� X
//�ּҴ��� ����� 2�� ����� ������ ��
class CMemoryPool
{
public:

	//////////////////////////////////////////////////////////////////////////
	// ������, �ı���.
	//
	// Parameters:	(int) �ʱ� �� ����.
	//				(bool) Alloc �� ������ / Free �� �ı��� ȣ�� ����
	// Return:
	//////////////////////////////////////////////////////////////////////////
	CMemoryPool(int minSize = DATA_DEFAULT, bool newCall = false);
	virtual	~CMemoryPool();


	//////////////////////////////////////////////////////////////////////////
	// �� �ϳ��� �Ҵ�޴´�.  
	//
	// Parameters: ����.
	// Return: (DATA *) ����Ÿ �� ������.
	//////////////////////////////////////////////////////////////////////////
	template <class DATA>
	DATA* Alloc(DATA* ret);

	//////////////////////////////////////////////////////////////////////////
	// ������̴� ���� �����Ѵ�.
	//
	// Parameters: (DATA *) �� ������.
	// Return: (BOOL) TRUE, FALSE.
	//////////////////////////////////////////////////////////////////////////
	template <class DATA>
	bool	Free(DATA* pData);


	//////////////////////////////////////////////////////////////////////////
	// ���� Ȯ�� �� �� ������ ��´�. (�޸�Ǯ ������ ��ü ����)
	//
	// Parameters: ����.
	// Return: (int) �޸� Ǯ ���� ��ü ����
	//////////////////////////////////////////////////////////////////////////
	int		GetCapacityCount(int size);

	//////////////////////////////////////////////////////////////////////////
	// ���� ������� �� ������ ��´�.
	//
	// Parameters: ����.
	// Return: (int) ������� �� ����.
	//////////////////////////////////////////////////////////////////////////
	int		GetUseCount(int size);




private:
	/*
	struct BLOCK_NODE {
		BLOCK_NODE* nextNode = NULL;
		//data ���� ����
		int len; 
		//poolID�� ����÷ο� üũ
		void* poolID;
		//malloc���� ������ �ο�
		BYTE data[len];
		short reserved;
	};*/


	struct HEADER {
		HEADER* nextNode = NULL;
		int len;
		void* poolID;
	};

	bool _newCall;

	// deque ������� ��ȯ�� (�̻��) ������Ʈ ���� ����.
	// garbage�� queing���� ���� ������ stacking���� ����
	HEADER* _topNode[DATA_INDEX];
	HEADER* _bottomNode[DATA_INDEX];

	int _capacity[DATA_INDEX] = { 0 };
	int _useCount[DATA_INDEX] = { 0 };

	//dataPool Size ���� ����
	int __DATA_SIZE[DATA_INDEX];
	//dataPool �ּҴ���
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

//parameter�� �ܼ��� data Type �� ������ ȣ���
template<class DATA>
inline DATA* CMemoryPool::Alloc(DATA* val)
{
	CLock LOCK(&poolLock, 1);
	int idx = sizeof(DATA);
	DATA* ret;
	//0�Ҵ� ����ó��
	if (idx == 0) {
		return NULL;
	}

	idx = (idx - 1) / _minSize;	
	//������ ����ġ�� Ŭ �� �׳� �Ҵ�
	if (idx >= DATA_INDEX) {
		ret = new DATA;
		return ret;
	}

	_useCount[idx]++;

	if (_topNode[idx] == NULL) {
		_capacity[idx]++;
		//HEADER + DATA���� + rearCheckSum
		HEADER* node = (HEADER*)malloc(sizeof(HEADER) + __DATA_SIZE[idx] + sizeof(short));
		node->len = sizeof(DATA);
		//rearCheckSum ����
		memmove_s((void*)((__int64)node + sizeof(HEADER) + node->len), sizeof(short), &_rearCheckSum, sizeof(short));
		node->poolID = this;

		ret = (DATA*)((__int64)node + sizeof(HEADER));
		
		//������ ȣ��
		ret = new (ret)DATA();
		return ret;
	}
	else {
		ret = (DATA*)((__int64)_topNode[idx] + sizeof(HEADER));
		_topNode[idx]->len = sizeof(DATA);
				
		//rearCheckSum ����
		memmove_s((void*)((__int64)_topNode[idx] + sizeof(HEADER) + _topNode[idx]->len), sizeof(short), &_rearCheckSum, sizeof(short));

		_topNode[idx] = _topNode[idx]->nextNode;
		if (_newCall) {
			//������ ȣ��
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

	//�߸��� ū ������ ����
	if (idx >= DATA_INDEX) {
		delete pData;
		return false;
	}

	if (node->poolID != this) {
		abort();
	}

	if (*rearCheck != _rearCheckSum) {
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
