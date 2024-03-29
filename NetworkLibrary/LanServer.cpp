#include "pch.h"
#include "LanServer.h"
#include "LanCommon.h"


struct SESSION {
    OVERLAPPEDEX recvOver;
    OVERLAPPEDEX sendOver;
    //session refCnt의 역할
    alignas(64)
        DWORD64 ioCnt;
    alignas(64)
        bool isSending;
    //send 후 해제용
    CPacket* sendBuf[SEND_PACKET_MAX];
    //monitor
    DWORD sendCnt; // << 보낸 메세지수 확보
    //네트워크 메세지용 버퍼들
    alignas(64)
        CRingBuffer recvQ;
    alignas(64)
        CLockFreeQueue<CPacket*> sendQ;
    alignas(64)
        SOCKET sock;

    //readonly
    alignas(64)
        DWORD64 sessionID;

    WCHAR IP[16];

    SESSION() {
        ioCnt = RELEASE_FLAG;
        isSending = 0;
    }
};


bool CLanServer::Start(const WCHAR * IP, DWORD port, DWORD createThreads, DWORD runningThreads, bool isNagle, DWORD maxConnect, DWORD packetSize)
{
    if (NetInit(IP, port, isNagle) == false) {
        return false;
    }

    isServerOn = true;
    sessionArr = new SESSION[maxConnect];

    totalAccept = 0;
    sessionCnt = 0;
    maxConnection = maxConnect;
    this->packetSize = packetSize;
   
    if (packetSize != CPacket::eBUFFER_DEFAULT) {
        packetPool = &g_PacketPool;
    }
    else {
        packetPool = new CTLSMemoryPool<CPacket>;
    }

    for (DWORD cnt = 0; cnt < maxConnect; cnt++) {
        sessionStack.Push(cnt);
    }

    if (ThreadInit(createThreads, runningThreads) == false) {
        isServerOn = false;
        return false;
    }

    return true;
}

void CLanServer::Stop()
{
    isServerOn = false;

    if (packetPool != &g_PacketPool) {
        delete packetPool;
    }
}

int CLanServer::GetSessionCount()
{
    return sessionCnt;
}

bool CLanServer::Disconnect(DWORD64 sessionID)
{
    SESSION* session = AcquireSession(sessionID);
    if (session == NULL) {
        return false;
    }

    CancelIoEx((HANDLE)session->sock, NULL);

    LoseSession(session);
    return true;
}

bool CLanServer::SendPacket(DWORD64 sessionID, CPacket* packet)
{
    SESSION* session = AcquireSession(sessionID);

    if (session == NULL) {
        PacketFree(packet);
        return false;
    }

    session->sendQ.Enqueue(packet);

    if (InterlockedExchange8((char*)&session->isSending, true) == false) {
        PostQueuedCompletionStatus(hIOCP, 0, (ULONG_PTR)session, (LPOVERLAPPED)&g_sendReq_overlap);
    }
    else {
        LoseSession(session);
    }

    return true;
}

void CLanServer::SendPacketToAll(CPacket* packet)
{
    int idx;
    SESSION* session;

    packet->AddRef(maxConnection);
    for (idx = 0; idx < maxConnection; idx++) {
        SendPacket(sessionArr[idx].sessionID, packet);
    }
    PacketFree(packet);
}

CPacket* CLanServer::PacketAlloc()
{
    CPacket* packet = packetPool->Alloc();
    if (packet->GetBufferSize() != packetSize) {
        packet->~CPacket();
        new (packet)CPacket(packetSize);
    }

    packet->AddRef(1);
    packet->Clear();
    packet->MoveWritePos(sizeof(LAN_HEADER));
    return packet;
}

void CLanServer::HeaderAlloc(CPacket* packet)
{
    LAN_HEADER* header = (LAN_HEADER*)packet->GetBufferPtr();
    header->len = packet->GetDataSize() - sizeof(LAN_HEADER);
}

void CLanServer::PacketFree(CPacket* packet)
{
    if (packet->SubRef() == 0) {
        packetPool->Free(packet);
    }
}

bool CLanServer::NetInit(const WCHAR * IP, DWORD port, bool isNagle)
{
    int ret;
    int err;

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    //socket
    listenSock = socket(AF_INET, SOCK_STREAM, NULL);
    if (listenSock == INVALID_SOCKET) {
        err = WSAGetLastError();
        return false;
    }
    _LOG(LOG_LEVEL_SYSTEM, L"Server Socket Made");

    //setsockopt
    LINGER optval;
    int sndSize = 0;

    optval.l_linger = 0;
    optval.l_onoff = 1;

    ret = setsockopt(listenSock, SOL_SOCKET, SO_LINGER, (char*)&optval, sizeof(optval));
    if (ret == SOCKET_ERROR) {
        err = WSAGetLastError();
        return false;
    }

    ret = setsockopt(listenSock, SOL_SOCKET, SO_SNDBUF, (char*)&sndSize, sizeof(sndSize));
    if (ret == SOCKET_ERROR) {
        err = WSAGetLastError();
        return false;
    }

    if (isNagle == false) {
        ret = setsockopt(listenSock, IPPROTO_TCP, TCP_NODELAY, (char*)&isNagle, sizeof(isNagle));
        if (ret == SOCKET_ERROR) {
            err = WSAGetLastError();
            return false;
        }
    }
    _LOG(LOG_LEVEL_SYSTEM, L"Server SetSockOpt");

    //bind
    SOCKADDR_IN sockAddr;
    sockAddr.sin_family = AF_INET;
    sockAddr.sin_port = htons(port);
    InetPton(AF_INET, IP, &sockAddr.sin_addr);

    ret = bind(listenSock, (sockaddr*)&sockAddr, sizeof(sockAddr));
    if (ret == SOCKET_ERROR) {
        err = WSAGetLastError();
        return false;
    }
    _LOG(LOG_LEVEL_SYSTEM, L"Server Binded");

    //listen
    ret = listen(listenSock, SOMAXCONN);
    if (ret == SOCKET_ERROR) {
        err = WSAGetLastError();
        return false;
    }
    _LOG(LOG_LEVEL_SYSTEM, L"Server Start Listen");

    return true;
}

bool CLanServer::ThreadInit(const DWORD createThreads, const DWORD runningThreads)
{
    DWORD cnt;
    
    hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, runningThreads);

    if (hIOCP == NULL) {
        return false;
    }
    _LOG(LOG_LEVEL_SYSTEM, L"LanServer IOCP Created");

    //add 1 for accept thread
    hAccept = (HANDLE)_beginthreadex(NULL, 0, CLanServer::AcceptProc, this, NULL, NULL);
    hThreads = new HANDLE[createThreads];

    for (cnt = 0; cnt < createThreads ; cnt++) {
        hThreads[cnt] = (HANDLE)_beginthreadex(NULL, 0, CLanServer::WorkProc, this, NULL, NULL);
    }

    for (cnt = 0; cnt < createThreads; cnt++) {
        if (hThreads[cnt] == INVALID_HANDLE_VALUE) {
            OnError(-1, L"Create Thread Failed");
            return false;
        }
    }
    _LOG(LOG_LEVEL_SYSTEM, L"LanServer Thread Created");

    return true;
}

SESSION* CLanServer::AcquireSession(DWORD64 sessionID)
{
    //find
    int sessionID_high = sessionID >> MASK_SHIFT;
    SESSION* session = &sessionArr[sessionID_high];

    //add IO
    InterlockedIncrement(&session->ioCnt);

    if (session->ioCnt & RELEASE_FLAG) {
        LoseSession(session);
        return NULL;
    }

    //같은 세션인지 재확인
    if (session->sessionID != sessionID) {
        LoseSession(session);
        return NULL;
    }

    return session;
}

void CLanServer::LoseSession(SESSION* session)
{
    if (InterlockedDecrement(&session->ioCnt) == 0) {
        ReleaseSession(session);
    }
}

SESSION* CLanServer::FindSession(DWORD64 sessionID)
{
    int sessionID_high = sessionID >> MASK_SHIFT;

    return &sessionArr[sessionID_high];
}

bool CLanServer::MakeSession(WCHAR* IP, SOCKET sock, DWORD64* ID)
{
    int sessionID_high;
    DWORD64 sessionID;
    SESSION* session;

    //iocp var
    HANDLE h;

    if (sessionStack.Pop(&sessionID_high) == false) {
        _FILE_LOG(LOG_LEVEL_SYSTEM, L"LibraryLog", L"All Session is in Use");
        OnError(-1, L"All Session is in Use");
        return false;
    }

    sessionID = (totalAccept & SESSION_MASK);
    sessionID |= ((__int64)sessionID_high << MASK_SHIFT);

    session = &sessionArr[sessionID_high];

    session->sock = sock;    

    wmemmove_s(session->IP, 16, IP, 16);
    session->sessionID = *ID = sessionID;

    MEMORY_CLEAR(&session->recvOver, sizeof(session->recvOver));
    session->recvOver.type = OV_RECV;
    MEMORY_CLEAR(&session->sendOver, sizeof(session->sendOver));
    session->sendOver.type = OV_SEND_FIN;

    //recv용 ioCount증가
    InterlockedIncrement(&session->ioCnt);
    InterlockedAnd64((__int64*)&session->ioCnt, ~RELEASE_FLAG);

    //iocp match
    h = CreateIoCompletionPort((HANDLE)session->sock, hIOCP, (ULONG_PTR)sessionID, 0);
    if (h != hIOCP) {
        _LOG(LOG_LEVEL_SYSTEM, L"IOCP to SOCKET Failed");
        return false;
    }

    InterlockedIncrement(&sessionCnt);
    return true;
}

void CLanServer::ReleaseSession(SESSION* session)
{
    //cas로 플래그 전환
    if (InterlockedCompareExchange64((long long*)&session->ioCnt, RELEASE_FLAG, 0) != 0) {
        return;
    }

    int leftCnt;
    CPacket* packet;
    
    closesocket(session->sock);

    OnClientLeave(session->sessionID);

    //남은 Q 찌꺼기 제거
    while(session->sendQ.Dequeue(&packet))
    {
        PacketFree(packet);    
    }

    //sendBuffer에 남은 찌꺼기 제거
    leftCnt = session->sendCnt;
    session->sendCnt = 0;
    while (leftCnt) {
        --leftCnt;
        packet = session->sendBuf[leftCnt];
        PacketFree(packet);
    }

    session->recvQ.ClearBuffer();

    session->isSending = false;
    InterlockedDecrement(&sessionCnt);

    sessionStack.Push(session->sessionID >> MASK_SHIFT);
}

unsigned int __stdcall CLanServer::WorkProc(void* arg)
{
    CLanServer* server = (CLanServer*)arg;
    server->_WorkProc();

    return 0;
}

unsigned int __stdcall CLanServer::AcceptProc(void* arg)
{
    CLanServer* server = (CLanServer*)arg;
    server->_AcceptProc();

    return 0;
}

void CLanServer::_WorkProc()
{
    int ret;
    int err;
    DWORD ioCnt;

    DWORD bytes;
    DWORD64 sessionID;
    SESSION* session;
    OVERLAPPEDEX* overlap = NULL;

    for (;;) {
        ret = GetQueuedCompletionStatus(hIOCP, &bytes, (PULONG_PTR)&session, (LPOVERLAPPED*)&overlap, INFINITE);

        //gqcs is false and overlap is NULL
        //case totally failed
        if (overlap == NULL) {
            err = WSAGetLastError();
            _FILE_LOG(LOG_LEVEL_ERROR, L"LibraryLog", L"GQCS return NULL ovelap");
            OnError(err, L"GQCS return NULL ovelap");

            continue;
        }

        switch (overlap->type) {
        case OV_RECV:
        {
            if (ret == false || bytes == 0) {
                LoseSession(session);
            }
            else
            {
                session->recvQ.MoveRear(bytes);
                RecvProc(session);
            }
        }
        break;
        case OV_SEND_FIN:
        {
            int sendCnt = session->sendCnt;
            CPacket** sendBuf = session->sendBuf;
            session->sendCnt = 0;

            while (sendCnt) {
                --sendCnt;
                PacketFree(sendBuf[sendCnt]);
            }

            if (session->sendQ.GetSize() > 0) {
                SendPost(session);
            }
            else {
                InterlockedExchange8((char*)&session->isSending, false);
                //작업 완료에 대한 lose
                LoseSession(session);
            }
        }
        break;
        case OV_DISCONNECT:
        {
            OnClientLeave(session->sessionID);
            sessionStack.Push(session->sessionID >> MASK_SHIFT);
        }
        break;
        case OV_SEND_REQ:
        {
            SendPost(session);
        }
        break;
        case OV_SERVER_END:
        {
            PostQueuedCompletionStatus(hIOCP, 0, 0, (LPOVERLAPPED)&g_serverEnd_overlap);
            return;
        }
        break;
        }


    }

}

void CLanServer::_AcceptProc()
{
    SOCKADDR_IN addr;
    SOCKET sock;
    WCHAR IP[16];

    SESSION* session;
    DWORD64 sessionID;

    while(isServerOn) {
        sock = accept(listenSock, (sockaddr*)&addr, NULL);
        ++totalAccept;
        if (sock == INVALID_SOCKET) {
            continue;
        }

        InetNtop(AF_INET, &addr.sin_addr, IP, 16);
        
        if (OnConnectionRequest(IP, ntohs(addr.sin_port)) == false) {
            continue;
        }

        if (MakeSession(IP, sock, &sessionID) == false) {
            continue;
        }

        session = FindSession(sessionID);

        if (OnClientJoin(sessionID) == false) {
            LoseSession(session);
            continue;
        }

        RecvPost(session);
    }

}

void CLanServer::RecvProc(SESSION* session)
{
    //Packet 떼기 (lanHeader 제거)
    LAN_HEADER lanHeader;
    DWORD len;
    CRingBuffer* recvQ = &session->recvQ;
    CPacket* packet;

    for (;;) {
        len = recvQ->GetUsedSize();
        //길이 판별
        if (sizeof(lanHeader) > len) {
            break;
        }

        //넷헤더 추출
        recvQ->Peek((char*)&lanHeader, sizeof(lanHeader));

        //길이 판별
        if (sizeof(lanHeader) + lanHeader.len > len) {
            break;
        }
        packet = PacketAlloc();

        //나중에 메세지 헤더 따라 처리 or MsgUpdate(session, packet)
        recvQ->Dequeue((char*)packet->GetBufferPtr(), sizeof(lanHeader) + lanHeader.len);
        //헤더영역 안읽기
        packet->MoveReadPos(sizeof(lanHeader));
        //패킷 길이만큼 이동
        packet->MoveWritePos(lanHeader.len);

        OnRecv(session->sessionID, packet);
    }

    RecvPost(session);
}

bool CLanServer::RecvPost(SESSION* session)
{
	int ret;
	int err;

	CRingBuffer* recvQ = &session->recvQ;

	DWORD len;
	DWORD flag = 0;

	WSABUF pBuf[2];

	len = recvQ->DirectEnqueueSize();

	pBuf[0] = { len, recvQ->GetRearBufferPtr() };
	pBuf[1] = { recvQ->GetFreeSize() - len, recvQ->GetBufferPtr() };

	ret = WSARecv(InterlockedAdd64((__int64*)&session->sock, 0), pBuf, 2, NULL, &flag, (LPWSAOVERLAPPED)&session->recvOver, NULL);

    if (ret == SOCKET_ERROR) {
        err = WSAGetLastError();

        if (err == WSA_IO_PENDING) {
            //good
        }
        else {
            switch (err) {
            case 10004:
            case 10053:
            case 10054:
            case 10057:
            case 10058:
            case 10060:
            case 10061:
            case 10064:
                break;
            default:
                OnError(err, L"RecvPost Error");
            }
            LoseSession(session);
            return false;
        }
    }
    else {
        //_LOG(LOG_LEVEL_DEBUG, L"Recv In Time");
    }

    return true;
}

bool CLanServer::SendPost(SESSION* session)
{
    int ret;
    int err;
    WORD cnt;
    int sendCnt;

    CLockFreeQueue<CPacket*>* sendQ;
    CPacket* packet;

    WSABUF pBuf[SEND_PACKET_MAX];

    sendQ = &session->sendQ;
    sendCnt = sendQ->GetSize();
    if (sendCnt > SEND_PACKET_MAX) sendCnt = SEND_PACKET_MAX;

    session->sendCnt = sendCnt;
    MEMORY_CLEAR(pBuf, WSABUFSIZE);

    for (cnt = 0; cnt < sendCnt; cnt++) {
        sendQ->Dequeue(&packet);
        session->sendBuf[cnt] = packet;
        pBuf[cnt].buf = packet->GetBufferPtr();
        pBuf[cnt].len = packet->GetDataSize();
    }

    ret = WSASend(InterlockedAdd64((__int64*)&session->sock, 0), pBuf, sendCnt, NULL, 0, (LPWSAOVERLAPPED)&session->sendOver, NULL);

    if (ret == SOCKET_ERROR) {
        err = WSAGetLastError();

        if (err == WSA_IO_PENDING) {
            //good
        }
        else {
            switch (err) {
            case 10004:
            case 10053:
            case 10054:
            case 10057:
            case 10058:
            case 10060:
            case 10061:
            case 10064:
                break;
            default:
                _FILE_LOG(LOG_LEVEL_ERROR, L"LibraryLog", L"SendPost Error %d", err);
                OnError(err, L"SendPost Error");
            }
            InterlockedExchange8((char*)&session->isSending, false);
            LoseSession(session);
            return false;
        }
    }
    else {
        //sent in time
    }

    return true;
}

int CLanServer::GetPacketPoolCapacity()
{
    return packetPool->GetCapacityCount();
}

int CLanServer::GetPacketPoolUse()
{
    return packetPool->GetUseCount();
}
