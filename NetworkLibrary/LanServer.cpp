#include "pch.h"
#include "LanServer.h"

DWORD64 g_sessionID = 0;

bool CLanServer::Start(WCHAR* IP, DWORD port, DWORD createThreads, DWORD runningThreads, bool isNagle, DWORD maxConnect)
{
    if (NetInit(IP, port, isNagle) == false) {
        return false;
    }

    if (ThreadInit(createThreads, runningThreads) == false) {
        return false;
    }

    //sessionArr = new SESSION[maxConnect];
    InitializeSRWLock(&sessionMapLock);
    isServerOn = true;

    return true;
}

void CLanServer::Stop()
{
}

int CLanServer::GetSessionCount()
{
    return sessionCnt;
}

bool CLanServer::Disconnect(DWORD64 SessionID)
{
    return false;
}

bool CLanServer::SendPacket(DWORD64 SessionID, CPacket* packet)
{
    return false;
}

bool CLanServer::NetInit(WCHAR* IP, DWORD port, bool isNagle)
{
    int ret;

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    //socket
    listenSock = socket(AF_INET, SOCK_STREAM, NULL);
    if (listenSock == INVALID_SOCKET) {
        lastError = WSAGetLastError();
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
        lastError = WSAGetLastError();
        return false;
    }

    ret = setsockopt(listenSock, SOL_SOCKET, SO_SNDBUF, (char*)&sndSize, sizeof(sndSize));
    if (ret == SOCKET_ERROR) {
        lastError = WSAGetLastError();
        return false;
    }

    if (isNagle == false) {
        ret = setsockopt(listenSock, IPPROTO_TCP, TCP_NODELAY, (char*)&isNagle, sizeof(isNagle));
        if (ret == SOCKET_ERROR) {
            lastError = WSAGetLastError();
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
        lastError = WSAGetLastError();
        return false;
    }
    _LOG(LOG_LEVEL_SYSTEM, L"Server Binded");

    //listen
    ret = listen(listenSock, SOMAXCONN);
    if (ret == SOCKET_ERROR) {
        lastError = WSAGetLastError();
        return false;
    }
    _LOG(LOG_LEVEL_SYSTEM, L"Server Start Listen");

    return true;
}

bool CLanServer::ThreadInit(const DWORD createThreads, const DWORD runningThreads)
{
    int cnt;
    
    //add 1 for accept thread
    hThreads = new HANDLE[createThreads + 1];

    hThreads[0] = (HANDLE)_beginthreadex(NULL, 0, AcceptStartFunc, NULL, NULL, NULL);

    for (cnt = 1; cnt <= createThreads; cnt++) {
        hThreads[cnt] = (HANDLE)_beginthreadex(NULL, 0, WorkStartFunc, NULL, NULL, NULL);
    }

    hIOCP = CreateIoCompletionPort(NULL, NULL, NULL, runningThreads);

    if (hIOCP == INVALID_HANDLE_VALUE) {
        return false;
    }

    for (cnt = 0; cnt <= createThreads; cnt++) {
        if (hThreads[cnt] == INVALID_HANDLE_VALUE) {
            return false;
        }
    }

    return true;
}

CLanServer::SESSION* CLanServer::FindSession(DWORD64 sessionID)
{
    CLock _lock(&sessionMapLock, 0);

    if (sessionMap.find(sessionID) == sessionMap.end()) {
        return NULL;
    }

    return sessionMap[sessionID];
}

bool CLanServer::MakeSession(DWORD64 sessionID, WCHAR* IP, SOCKET sock)
{
    SESSION* session = new SESSION;
    //풀로 할당
    //iocp var
    HANDLE h;

    //recv part
    int ret;
    int err;
    WSABUF pBuf[2];
    DWORD flag = 0;
    DWORD len;

    session->sock = sock;
    session->isSending = false;
    session->ioCnt = 0;

    ZeroMemory(&session->recvOver, sizeof(session->recvOver));
    session->recvOver.type = 0;
    ZeroMemory(&session->sendOver, sizeof(session->sendOver));
    session->sendOver.type = 1;

    InitializeSRWLock(&session->sessionLock);

    CLock Lock(&sessionMapLock, 1);
    sessionMap.insert({ sessionID, session });

    //iocp match
    h = CreateIoCompletionPort((HANDLE)session->sock, hIOCP, (ULONG_PTR)sessionID, 0);
    if (h != hIOCP) {
        _LOG(LOG_LEVEL_SYSTEM, L"IOCP to SOCKET Failed");
        return false;
    }

    //recv start
    return RecvPost(session);
}

void CLanServer::ReleaseSession(DWORD64 sessionID, SESSION* session)
{
    CLock Lock(&sessionMapLock, 1);
    SOCKET sock = session->sock;

    sessionMap.erase(sessionID);

    AcquireSRWLockExclusive(&session->sessionLock);
    ReleaseSRWLockExclusive(&session->sessionLock);

    //delete에서 풀로 전환가자
    closesocket(sock);
}

unsigned int _stdcall CLanServer::AcceptStartFunc(void* classPtr)
{
    CLanServer* ptr = (CLanServer*)classPtr;
    return ptr->AcceptProc(NULL);
}

unsigned int _stdcall CLanServer::WorkStartFunc(void* classPtr)
{
    CLanServer* ptr = (CLanServer*)classPtr;
    return ptr->WorkProc(NULL);
}

unsigned int __stdcall CLanServer::WorkProc(void* arg)
{
    int ret;
    DWORD ioCnt;

    DWORD bytes;
    DWORD64 sessionID;
    SESSION* session;
    OVERLAPPEDEX* overlap = NULL;

    for (;;) {
        ret = GetQueuedCompletionStatus(hIOCP, &bytes, (PULONG_PTR)&sessionID, (LPOVERLAPPED*)&overlap, INFINITE);

        if (ret == false) {
            lastError = WSAGetLastError();
        }

        if (overlap == NULL) {
            continue;
        }

        session = FindSession(sessionID);
        //recvd
        if (overlap->type == 0) {
            session->recvQ.MoveRear(bytes);
            RecvProc(session);
        }
        //sent
        if (overlap->type == 1) {
            //session->sendQ.MoveFront(bytes);
            InterlockedExchange8((char*)&session->isSending, 0);
            SendPost(session);
        }

        ioCnt = InterlockedDecrement(&session->ioCnt);
        ReleaseSRWLockExclusive(&session->sessionLock);

        if (ioCnt == 0) {
            ReleaseSession(sessionID, session);
        }
    }

    return 0;
}

unsigned int __stdcall CLanServer::AcceptProc(void* arg)
{
    SOCKADDR_IN addr;
    SOCKET sock;
    WCHAR IP[16];

    while(isServerOn) {
        sock = accept(listenSock, (sockaddr*)&addr, NULL);
        if (sock == INVALID_SOCKET) {
            continue;
        }

        InetNtop(AF_INET, &addr.sin_addr, IP, 16);
        if (MakeSession(g_sessionID++, IP, sock) == false) {
            continue;
        }
    }

    return 0;
}

void CLanServer::RecvProc(SESSION* session)
{
//    OnRecv(session->se)
}

bool CLanServer::RecvPost(SESSION* session)
{
    int ret;

    CRingBuffer* recvQ = &session->recvQ;

    DWORD len;
    DWORD flag = 0;

    WSABUF pBuf[2];

    len = recvQ->DirectEnqueueSize();

    pBuf[0] = { len, recvQ->GetRearBufferPtr() };
    pBuf[1] = { recvQ->GetFreeSize() - len, recvQ->GetBufferPtr() };

    InterlockedIncrement(&session->ioCnt);
    ret = WSARecv(session->sock, pBuf, 2, NULL, &flag, (LPWSAOVERLAPPED)&session->recvOver, NULL);

    if (ret == SOCKET_ERROR) {
        lastError = WSAGetLastError();

        if (lastError == WSA_IO_PENDING) {
            //good
        }
        else {
            InterlockedDecrement(&session->ioCnt);
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

    bool isPending;

    CRingBuffer* sendQ;
    CPacket* packet;

    DWORD usedLen;
    WSABUF pBuf[100];
    
    isPending = InterlockedExchange8((char*)&session->isSending, 1);
    if (isPending == true) {
        return false;
    }

    sendQ = &session->sendQ;
    usedLen = sendQ->GetUsedSize();
    //0byte send시 iocp에 결과만 Enqueue, 실제 0바이트 송신 X
    // usedLen == 0 판별 중 Recv를 통한 SendPost에서 isPending Interlock 선 진입시 오류발생 이후에 send 요청 상실 >> 일부 패킷 소실의 버그
    if (usedLen == 0) {
        InterlockedExchange8((char*)&session->isSending, 0);
        return false;
    }

    session->sendCnt = usedLen / sizeof(void*);

    ZeroMemory(pBuf, sizeof(WSABUF) * 100);

    for (WORD cnt = 0; cnt < session->sendCnt; cnt++) {
        sendQ->Dequeue((char*)&packet, sizeof(void*));
        pBuf[cnt].buf = packet->GetBufferPtr();
        pBuf[cnt].len = packet->GetDataSize();
    }

    InterlockedIncrement(&session->ioCnt);
    ret = WSASend(session->sock, pBuf, 100, NULL, 0, (LPWSAOVERLAPPED)&session->sendOver, NULL);

    if (ret == SOCKET_ERROR) {
        err = WSAGetLastError();

        if (err == WSA_IO_PENDING) {
            //good
        }
        else {
            InterlockedDecrement(&session->ioCnt);
            return false;
        }
    }
    else {
        InterlockedExchange8((char*)&session->isSending, 0);
    }

    return true;
}
