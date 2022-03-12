#include "pch.h"
#include "LanServer.h"
#include "LanCommon.h"

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

//���� ó���Ǵ� ��� IOī��Ʈ�� �ȸ��� �� �����Ƿ� ���� ó���� �ٷ� IOī��Ʈ ������ ��
bool CLanServer::Disconnect(DWORD64 sessionID)
{
    SESSION* session = FindSession(sessionID);
    CancelIoEx((HANDLE)session->sock, (LPOVERLAPPED)&session->recvOver);
    CancelIoEx((HANDLE)session->sock, (LPOVERLAPPED)&session->sendOver);
    return false;
}

bool CLanServer::SendPacket(DWORD64 sessionID, CPacket* packet)
{
    SESSION* session = FindSession(sessionID);

    session->sendQ.Enqueue((char*)&packet, sizeof(void*));

    SendPost(session);

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

    hThreads[0] = (HANDLE)_beginthreadex(NULL, 0, CLanServer::AcceptProc, this, NULL, NULL);

    for (cnt = 1; cnt <= createThreads; cnt++) {
        hThreads[cnt] = (HANDLE)_beginthreadex(NULL, 0, CLanServer::WorkProc, this, NULL, NULL);
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

SESSION* CLanServer::FindSession(DWORD64 sessionID)
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
    //Ǯ�� �Ҵ�
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

    sessionCnt++;
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

    //delete���� Ǯ�� ��ȯ����
    closesocket(sock);
}

unsigned int __stdcall CLanServer::WorkProc(void* arg)
{
    int ret;
    DWORD ioCnt;

    DWORD bytes;
    DWORD64 sessionID;
    SESSION* session;
    OVERLAPPEDEX* overlap = NULL;

    CLanServer* THIS = (CLanServer*)arg;

    for (;;) {
        ret = GetQueuedCompletionStatus(THIS->hIOCP, &bytes, (PULONG_PTR)&sessionID, (LPOVERLAPPED*)&overlap, INFINITE);

        if (ret == false) {
            THIS->lastError = WSAGetLastError();
        }

        if (overlap == NULL) {
            continue;
        }

        session = THIS->FindSession(sessionID);
        //recvd
        if (overlap->type == 0) {
            session->recvQ.MoveRear(bytes);
            THIS->RecvProc(session);
        }
        //sent
        if (overlap->type == 1) {
            //session->sendQ.MoveFront(bytes);
            InterlockedExchange8((char*)&session->isSending, 0);
            THIS->SendPost(session);
        }

        ioCnt = InterlockedDecrement(&session->ioCnt);
        ReleaseSRWLockExclusive(&session->sessionLock);

        if (ioCnt == 0) {
            THIS->ReleaseSession(sessionID, session);
        }
    }

    return 0;
}

unsigned int __stdcall CLanServer::AcceptProc(void* arg)
{
    SOCKADDR_IN addr;
    SOCKET sock;
    WCHAR IP[16];

    CLanServer* THIS = (CLanServer*)arg;

    while(THIS->isServerOn) {
        sock = accept(THIS->listenSock, (sockaddr*)&addr, NULL);
        if (sock == INVALID_SOCKET) {
            continue;
        }

        InetNtop(AF_INET, &addr.sin_addr, IP, 16);
        if (THIS->MakeSession(THIS->g_sessionID++, IP, sock) == false) {
            continue;
        }
    }

    return 0;
}

void CLanServer::RecvProc(SESSION* session)
{
    //Packet ���� (netHeader ����)
    
    NET_HEADER netHeader;
    DWORD len;
    CRingBuffer* recvQ = &session->recvQ;
    CPacket* packet = NULL;

    for (;;) {
        packet = g_LanPacketPool.Alloc(packet);
        len = recvQ->GetUsedSize();
        //���� �Ǻ�
        if (sizeof(netHeader) > len) {
            g_LanPacketPool.Free(packet);
            break;
        }

        //����� ����
        recvQ->Peek((char*)&netHeader, sizeof(netHeader));

        //���� �Ǻ�
        if (sizeof(netHeader) + netHeader.len > len) {
            g_LanPacketPool.Free(packet);
            break;
        }

        //����� ���� ��������
        recvQ->MoveFront(sizeof(netHeader));
        //���߿� �޼��� ��� ���� ó�� or MsgProc(session, packet)
        packet->PutData((char*)&netHeader, sizeof(netHeader));
        recvQ->Dequeue((char*)packet->GetWritePtr(), netHeader.len);
        packet->MoveWritePos(netHeader.len);

        OnRecv(session->sessionID, packet);
    }

    RecvPost(session);
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
    //0byte send�� iocp�� ����� Enqueue, ���� 0����Ʈ �۽� X
    // usedLen == 0 �Ǻ� �� Recv�� ���� SendPost���� isPending Interlock �� ���Խ� �����߻� ���Ŀ� send ��û ��� >> �Ϻ� ��Ŷ �ҽ��� ����
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
