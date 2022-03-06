#include "pch.h"
#include "RingBuffer.h"

CRingBuffer::CRingBuffer()
{
    _buffer = (char*)malloc(RINGBUFFERSIZE);
    _size = RINGBUFFERSIZE;
}

CRingBuffer::CRingBuffer(const int bufferSize)
{
    _buffer = (char*)malloc(bufferSize);
    _size = bufferSize;
}

CRingBuffer::~CRingBuffer()
{
    free(_buffer);
}

void CRingBuffer::Resize(const int size)
{
    char* buf = (char*)malloc(size);
    Peek(buf, GetUsedSize());
    free(_buffer);

    _buffer = buf;
}

int CRingBuffer::GetBufferSize()
{
    return _size;
}

int CRingBuffer::GetUsedSize()
{
    int head = _head;
    int rear = _rear;

    if (rear >= head) {
        return rear - head;
    }
    else {
        return rear - head + _size;
    }
}

int CRingBuffer::GetFreeSize()
{
    int head = _head;
    int rear = _rear;

    if (rear >= head) {
        return _size - rear + head - 1;
    }
    else {
        return head - rear - 1;
    }
}

int CRingBuffer::DirectEnqueueSize()
{
    int head = _head;
    int rear = _rear;

    //버퍼가 1회 순회한 상황
    if (rear < head) {
        return head - rear - 1;
    }
    else {
        return _size - rear - (head == 0);
    }
}

int CRingBuffer::DirectDequeueSize()
{
    int head = _head;
    int rear = _rear;
    //버퍼가 1회 순회한 상황
    if (rear < head) {
        return _size - head;
    }
    else {
        return rear - head;
    }
}

int CRingBuffer::Enqueue(char* const data, const int size)
{
    int ret;
    int left = this->DirectEnqueueSize();
    if (left < size) {
        ret = memmove_s(_buffer + _rear, left, data, left);
        if (ret != 0) return -1;
        ret = memmove_s(_buffer, size - left, data + left, size - left);
    }
    else {
        ret = memmove_s(_buffer + _rear, size, data, size);
    }

    MoveRear(size);

    if (ret != 0) return -1;
    else return size;
}

int CRingBuffer::Dequeue(char* const dest, const int size)
{
    int ret;
    int left = DirectDequeueSize();
    if (left < size) {
        ret = memmove_s(dest, left, _buffer + _head, left);
        if (ret != 0) return false;
        ret = memmove_s(dest + left, size - left, _buffer, size - left);
    }
    else {
        ret = memmove_s(dest, size, _buffer + _head, size);
    }

    MoveFront(size);

    if (ret != 0) return -1;
    else return size;
}

int CRingBuffer::Peek(char* const dest, const int size)
{
    int ret;
    int left = DirectDequeueSize();

    if (left < size) {
        ret = memmove_s(dest, left, _buffer + _head, left);
        if (ret != 0) return -1;
        ret = memmove_s(dest + left, size - left, _buffer, size - left);
    }
    else {
        ret = memmove_s(dest, size, _buffer + _head, size);
    }

    if (ret != 0) return -1;
    else return size;
}

int CRingBuffer::MoveRear(const int size)
{
    int ret = _rear;
    _rear = _rear + size;
    //ringbuffer 순회하는 경우
    if (_rear >= _size) {
        _rear %= _size;
        ret = _rear + _size - ret;
    }
    else {
        ret = size;
    }
    return ret;
}

int CRingBuffer::MoveFront(const int size)
{
    //빈 버퍼일 경우 바로 리턴
    if (_head == _rear) return 0;

    int ret = _head;
    _head = _head + size;
    //ringbuffer 순회하는 경우
    if (_head >= _size) {
        _head %= _size;
        ret = _head + _size - ret;
    }
    else {
        ret = size;
    }
    return ret;
}

void CRingBuffer::ClearBuffer()
{
    _rear = _head;
}

char* CRingBuffer::GetBufferPtr()
{
    return _buffer;
}

char* CRingBuffer::GetHeadBufferPtr()
{
    return _buffer + _head;
}

char* CRingBuffer::GetRearBufferPtr()
{
    return _buffer + _rear;
}
