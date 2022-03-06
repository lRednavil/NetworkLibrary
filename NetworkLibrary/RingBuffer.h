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

	//����� �뷮 ���
	int GetUsedSize();
	//���� �뷮 ���
	int GetFreeSize();

	//�̾��� ���� �뷮 Ȯ��
	//buffer�� ���������� recv, send�ϱ� ���� �뵵
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


