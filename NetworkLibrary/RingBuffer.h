#pragma once
#include <iostream>

#define RINGBUFFERSIZE 5000

class CRingBuffer
{
private:
	char* _buffer;
	int _head = 0;
	int _rear = 0;
	int _size;

public:
	CRingBuffer();
	CRingBuffer(const int bufferSize);
	~CRingBuffer();

	void Resize(const int size);

	int GetBufferSize();

	//사용중 용량 얻기
	int GetUsedSize();
	//여백 용량 얻기
	int GetFreeSize();

	//이어진 버퍼 용량 확인
	//buffer에 직접적으로 recv, send하기 위한 용도
	int DirectEnqueueSize();
	int DirectDequeueSize();

	int Enqueue(char* const data, const int size);

	int Dequeue(char* const dest, const int size);

	int Peek(char* const dest, const int size);

	int MoveRear(const int size);
	int MoveFront(const int size);

	void ClearBuffer();

	char* GetBufferPtr();
	char* GetHeadBufferPtr();
	char* GetRearBufferPtr();
};


