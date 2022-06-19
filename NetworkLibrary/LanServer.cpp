#include "pch.h"
#include "LanServer.h"
#include "LanCommon.h"

bool CLanServer::Start(const WCHAR * IP, DWORD port, DWORD createThreads, DWORD runningThreads, bool isNagle, DWORD maxConnect, DWORD packetSize)
{
    if (NetInit(IP, port, isNagle) == false) {
        return false;
    }

    isServerOn = true;
    sessionArr = new SESSION[maxConnect];

    totalAccept = 0;
    sessionCnt = 0;
   
    if (packetSize == 1460) {
        packetPool = &g_PacketPool;
    }
    else {
        //packetPool = new CTLSMemoryPool<>;
    }

    for (DWORD cnt = 0; cnt < maxConnect; cnt++) {
        sessionStack.Push(cnt);
    }

    if (ThreadInit(createThreads, runningThreads) == false) {
        isServerOn = false;
        sessionStack.~CLockFreeStack();
        return false;
    }

    return true;
}

void CLanServer::Stop()
{
    isServerOn = false;
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

    CancelIoEx((HANDLE)InterlockedExchange64((__int64*)&session->sock, session->sock | RELEASE_FLAG), NULL);

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

    HeaderAlloc(packet);
    session->sendQ.Enqueue(packet);
    if (session->isSending == 0) {
        SendPost(session);
    }
    else {
        LoseSession(session);
    }
    return true;
}

CPacket* CLanServer::PacketAlloc()
{
    CPacket* packet = g_PacketPool.Alloc();
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
        g_PacketPool.Free(packet);
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
    hThreads = new HANDLE[createThreads + ACCEPT_THREAD];

    hThreads[0] = (HANDLE)_beginthreadex(NULL, 0, CLanServer::AcceptProc, this, NULL, NULL);

    for (cnt = ACCEPT_THREAD; cnt < createThreads + ACCEPT_THREAD; cnt++) {
        hThreads[cnt] = (HANDLE)_beginthreadex(NULL, 0, CLanServer::WorkProc, this, NULL, NULL);
    }

    for (cnt = 0; cnt <= createThreads; cnt++) {
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
    session->recvOver.type = 0;
    MEMORY_CLEAR(&session->sendOver, sizeof(session->sendOver));
    session->sendOver.type = 1;

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
    
    closesocket(session->sock & ~RELEASE_FLAG);

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

    session->isSending = 0;
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

    while(isServerOn) {
        ret = GetQueuedCompletionStatus(hIOCP, &bytes, (PULONG_PTR)&sessionID, (LPOVERLAPPED*)&overlap, INFINITE);

        //gqcs is false and overlap is NULL
        //case totally failed
        if (overlap == NULL) {
            err = WSAGetLastError();
            _FILE_LOG(LOG_LEVEL_ERROR, L"LibraryLog", L"GQCS return NULL ovelap");
            OnError(err, L"GQCS return NULL ovelap");
            continue;
        }

        session = FindSession(sessionID);

        if (session == NULL) {
            continue;
        }
        //recvd
        if (overlap->type == 0) {
            if (ret == false || bytes == 0) {
                LoseSession(session);
            }
            else
            {
                session->recvQ.MoveRear(bytes);
                RecvProc(session);
            }

        }
        //sent
        if (overlap->type == 1) {
            DWORD sendCnt = session->sendCnt;
            CPacket** sendBuf = session->sendBuf;
            session->sendCnt = 0;

            while (sendCnt) {
                --sendCnt;
                PacketFree(sendBuf[sendCnt]);
            }

            InterlockedDecrement16(&session->isSending);

            if (ret != false) {
                if (session->sendQ.GetSize() != 0) {
                    //추가로 send에 맞춘 acquire
                    AcquireSession(sessionID);
                    SendPost(session);
                }
            }
            //작업 완료에 대한 lose
            LoseSession(session);
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
    
    //다른 SendPost진행중인지 확인용
    if (InterlockedIncrement16(&session->isSending) != 1) {
        LoseSession(session);
        return false;
    }

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

    ret = WSASend(InterlockedAdd64((__int64*)&session->sock, 0), pBuf, 200, NULL, 0, (LPWSAOVERLAPPED)&session->sendOver, NULL);

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
                OnError(err, L"SendPost Error");
            }
            LoseSession(session);
            return false;
        }
    }
    else {
        //sent in time
    }

    return true;
}
