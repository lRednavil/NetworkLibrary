#include "pch.h"
#include "SerializedBuffer.h"

CPacket::CPacket() : _bufferSize(eBUFFER_DEFAULT)
{
    _buffer = (char*)malloc(eBUFFER_DEFAULT);
}

CPacket::CPacket(int size) : _bufferSize(size)
{
    _buffer = (char*)malloc(size);
}

CPacket::~CPacket()
{
    free(_buffer);
}

void CPacket::Release(void)
{
    _head = _rear = 0;
}

void CPacket::Clear(void)
{
    _head = _rear = 0;
}

int CPacket::MoveWritePos(int size)
{
    if (_rear + size >= _bufferSize) return -1;

    _rear += size;
    return size;
}

int CPacket::MoveReadPos(int size)
{
    if (_head + size > _rear) return -1;

    _head += size;
    return size;
}

CPacket& CPacket::operator=(CPacket& clSrcPacket)
{
    memmove_s(this->_buffer, _bufferSize, clSrcPacket._buffer, _bufferSize);
    return *this;
}

CPacket& CPacket::operator<<(unsigned char byValue)
{
    memmove_s(_buffer + _rear, sizeof(unsigned char), &byValue, sizeof(unsigned char));
    _rear += sizeof(unsigned char);
    return *this;
}

CPacket& CPacket::operator<<(char chValue)
{
    memmove_s(_buffer + _rear, sizeof(char), &chValue, sizeof(char));
    _rear += sizeof(char);
    return *this;
}

CPacket& CPacket::operator<<(short shValue)
{
    memmove_s(_buffer + _rear, sizeof(short), &shValue, sizeof(short));
    _rear += sizeof(short);
    return *this;
}

CPacket& CPacket::operator<<(unsigned short wValue)
{
    memmove_s(_buffer + _rear, sizeof(unsigned short), &wValue, sizeof(unsigned short));
    _rear += sizeof(unsigned short);
    return *this;
}

CPacket& CPacket::operator<<(int iValue)
{
    memmove_s(_buffer + _rear, sizeof(int), &iValue, sizeof(int));
    _rear += sizeof(int);
    return *this;
}

CPacket& CPacket::operator<<(DWORD dwValue)
{
    memmove_s(_buffer + _rear, sizeof(DWORD), &dwValue, sizeof(DWORD));
    _rear += sizeof(DWORD);
    return *this;
}

CPacket& CPacket::operator<<(long lValue)
{
    memmove_s(_buffer + _rear, sizeof(long), &lValue, sizeof(long));
    _rear += sizeof(long);
    return *this;
}

CPacket& CPacket::operator<<(float fValue)
{
    memmove_s(_buffer + _rear, sizeof(float), &fValue, sizeof(float));
    _rear += sizeof(float);
    return *this;
}

CPacket& CPacket::operator<<(__int64 iValue)
{
    memmove_s(_buffer + _rear, sizeof(__int64), &iValue, sizeof(__int64));
    _rear += sizeof(__int64);
    return *this;
}

CPacket& CPacket::operator<<(double dValue)
{
    memmove_s(_buffer + _rear, sizeof(double), &dValue, sizeof(double));
    _rear += sizeof(double);
    return *this;
}

CPacket& CPacket::operator>>(BYTE& byValue)
{
    memmove_s(&byValue, sizeof(unsigned char), _buffer + _head, sizeof(unsigned char));
    _head += sizeof(unsigned char);
    return *this;
}

CPacket& CPacket::operator>>(char& chValue)
{
    memmove_s(&chValue, sizeof(char), _buffer + _head, sizeof(char));
    _head += sizeof(char);
    return *this;
}

CPacket& CPacket::operator>>(short& shValue)
{
    memmove_s(&shValue, sizeof(short), _buffer + _head, sizeof(short));
    _head += sizeof(short);
    return *this;
}

CPacket& CPacket::operator>>(WORD& wValue)
{
    memmove_s(&wValue, sizeof(WORD), _buffer + _head, sizeof(WORD));
    _head += sizeof(WORD);
    return *this;
}

CPacket& CPacket::operator>>(int& iValue)
{
    memmove_s(&iValue, sizeof(int), _buffer + _head, sizeof(int));
    _head += sizeof(int);
    return *this;
}

CPacket& CPacket::operator>>(DWORD& dwValue)
{
    memmove_s(&dwValue, sizeof(DWORD), _buffer + _head, sizeof(DWORD));
    _head += sizeof(DWORD);
    return *this;
}

CPacket& CPacket::operator>>(float& fValue)
{
    memmove_s(&fValue, sizeof(float), _buffer + _head, sizeof(float));
    _head += sizeof(float);
    return *this;
}

CPacket& CPacket::operator>>(__int64& iValue)
{
    memmove_s(&iValue, sizeof(__int64), _buffer + _head, sizeof(__int64));
    _head += sizeof(__int64);
    return *this;
}

CPacket& CPacket::operator>>(double& dValue)
{
    memmove_s(&dValue, sizeof(double), _buffer + _head, sizeof(double));
    _head += sizeof(double);
    return *this;
}

int CPacket::GetData(char* chpDest, int iSize)
{
    if (iSize > _bufferSize - _rear) return -1;

    int ret;
    ret = memmove_s(chpDest, iSize, _buffer + _head, iSize);

    if (ret != 0) return ret;

    _head += iSize;
    return ret;
}

int CPacket::PutData(char* chpSrc, int iSrcSize)
{
    if (iSrcSize > _bufferSize - _rear) return -1;

    int ret;
    ret = memmove_s(_buffer + _rear, iSrcSize, chpSrc, iSrcSize);


    if (ret != 0) return ret;

    _rear += iSrcSize;
    return ret;
}

void CPacket::AddRef(int addVal)
{
    InterlockedAdd(&refCnt, addVal);
}

long CPacket::SubRef()
{
    return InterlockedDecrement(&refCnt);
}