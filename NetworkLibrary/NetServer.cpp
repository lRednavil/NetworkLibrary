#include "pch.h"
#include "NetServer.h"
#include "NetCommon.h"
#include <timeapi.h>

#pragma comment(lib, "Winmm")

bool CNetServer::Start(WCHAR* IP, DWORD port, DWORD createThreads, DWORD runningThreads, bool isNagle, DWORD maxConnect)
{
    if (NetInit(IP, port, isNagle) == false) {
        return false;
    }

    isServerOn = true;
    sessionArr = new SESSION[maxConnect];

    totalAccept = 0;
    sessionCnt = 0;
    maxConnection = maxConnect;

    for (int cnt = 0; cnt < maxConnect; cnt++) {
        sessionStack.Push(cnt);
    }

    myMonitor = new CProcessMonitor;
    totalMonitor = new CProcessorMonitor;

    if (ThreadInit(createThreads, runningThreads) == false) {
        isServerOn = false;
        sessionStack.~CLockFreeStack();
        return false;
    }

    return true;
}

void CNetServer::Stop()
{
    isServerOn = false;
}

int CNetServer::GetSessionCount()
{
    return sessionCnt;
}

void CNetServer::Monitor()
{
    system("cls");
    wprintf_s(L"Total Accept : %llu \n", totalAccept);
    wprintf_s(L"Total Send : %llu \n", totalSend);
    wprintf_s(L"Total Recv : %llu \n", totalRecv);
    wprintf_s(L"Total Release : %llu \n", totalRelease);
    wprintf_s(L"=============================\n");
    wprintf_s(L"Accept TPS : %llu \n", totalAccept - lastAccept);
    wprintf_s(L"Send TPS : %llu \n", totalSend - lastSend);
    wprintf_s(L"Recv TPS : %llu \n", totalRecv - lastRecv);
    wprintf_s(L"Release TPS : %llu \n", totalRelease - lastRelease);
    wprintf_s(L"=============================\n");
    wprintf_s(L"Current Sessions : %lu \n", sessionCnt);

    myMonitor->UpdateProcessTime();
    totalMonitor->UpdateHardwareTime();

    wprintf_s(L"======== Process Information ========\n");
    wprintf_s(L"CPU Total : %f%% || User Total : %f%% || Kernel Total : %f%% \nPrivate Bytes : %lld Mb \n", myMonitor->ProcessTotal(), myMonitor->ProcessUser(), myMonitor->ProcessKernel(), myMonitor->ProcessPrivateBytes() / 1024 / 1024);


    wprintf_s(L"======== Processor Information ========\n");
    wprintf_s(L"CPU Total : %f%% || User Total : %f%% || Kernel Total : %f%% \nNonPaged Memory : %lld Mb \n", totalMonitor->ProcessorTotal(), totalMonitor->ProcessorUser(), totalMonitor->ProcessorKernel(), totalMonitor->NonPagedMemory() / 1024 / 1024);

    wprintf_s(L"======== Ethernet Information ========\n");
    wprintf_s(L"Total Recv Bytes : %lf Kb || Total Send Bytes : %lf Kb \nRecv Bytes/sec : %lf Kb || Send Bytes/sec : %lf Kb\n", totalMonitor->EthernetRecv() / 1024, totalMonitor->EthernetSend() / 1024, totalMonitor->EthernetRecvTPS() / 1024, totalMonitor->EthernetSendTPS() / 1024);


    lastAccept = totalAccept;
    lastSend = totalSend;
    lastRecv = totalRecv;
    lastRelease = totalRelease;
}

bool CNetServer::Disconnect(DWORD64 sessionID)
{
    SESSION* session = AcquireSession(sessionID);
    if (session == NULL) {
        return false;
    }

    CancelIoEx((HANDLE)InterlockedOr64((__int64*)&session->sock, RELEASE_FLAG), NULL);

    LoseSession(session);
    return true;
}

bool CNetServer::SendPacket(DWORD64 sessionID, CPacket* packet)
{
    SESSION* session = AcquireSession(sessionID);

    if (session == NULL) {
        PacketFree(packet);
        return false;
    }

    HeaderAlloc(packet);
    Encode(packet);
    session->sendQ.Enqueue(packet);
    SendPost(session);
    return true;
}

CPacket* CNetServer::PacketAlloc()
{
    CPacket* packet = g_PacketPool.Alloc();
    packet->AddRef(1);
    packet->Clear();
    packet->MoveWritePos(sizeof(NET_HEADER));
    packet->isEncoded = false;
    return packet;
}

void CNetServer::HeaderAlloc(CPacket* packet)
{
    if (packet->isEncoded) return;

    NET_HEADER* header = (NET_HEADER*)packet->GetBufferPtr();
    header->staticCode = STATIC_CODE;
    header->len = packet->GetDataSize() - sizeof(NET_HEADER);
    header->randomKey = rand();
    header->checkSum = MakeCheckSum(packet);
}

BYTE CNetServer::MakeCheckSum(CPacket* packet)
{
	BYTE ret = 0;
    char* ptr = packet->GetBufferPtr();
    int len = packet->GetDataSize();
    int cnt = sizeof(NET_HEADER);

    for (cnt; cnt < len; ++cnt) {
        ret += ptr[cnt];
    }

    return ret;
}

void CNetServer::Encode(CPacket* packet)
{
    if (packet->isEncoded) return;

    packet->isEncoded = true;

    NET_HEADER* header = (NET_HEADER*)packet->GetBufferPtr();
    BYTE* ptr = (BYTE*)&header->checkSum;
    BYTE key = STATIC_KEY;
    WORD len = header->len;
    BYTE randKey = header->randomKey;

    WORD cnt;

    ptr[0] ^= randKey + 1;
    for (cnt = 1; cnt <= len; ++cnt) {
        ptr[cnt] ^= ptr[cnt - 1] + randKey + cnt + 1;
    }

    ptr[0] ^= key + 1;
    for (cnt = 1; cnt <= len; ++cnt) {
        ptr[cnt] ^= ptr[cnt - 1] + key + cnt + 1;
    }
}

void CNetServer::Decode(CPacket* packet)
{
    NET_HEADER* header = (NET_HEADER*)packet->GetBufferPtr();
    BYTE* ptr = (BYTE*)&header->checkSum;
    BYTE key = STATIC_KEY;
    WORD len = header->len;
    BYTE randKey = header->randomKey;

    WORD cnt;

    for (cnt = len; cnt > 0; --cnt) {
        ptr[cnt] ^= ptr[cnt - 1] + key + cnt + 1;
    }
    ptr[0] ^= key + 1;

    for (cnt = len; cnt > 0; --cnt) {
        ptr[cnt] ^= ptr[cnt - 1] + randKey + cnt + 1;
    }
    ptr[0] ^= randKey + 1;
}

void CNetServer::PacketFree(CPacket* packet)
{
    if (packet->SubRef() == 0) {
        g_PacketPool.Free(packet);
    }
}

void CNetServer::SetTimeOut(DWORD64 sessionID, DWORD timeVal)
{
    SESSION* session = AcquireSession(sessionID);

    if (session == NULL) {
        return;
    }

    session->timeOutVal = timeVal;
    LoseSession(session);
}

bool CNetServer::NetInit(WCHAR* IP, DWORD port, bool isNagle)
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

bool CNetServer::ThreadInit(const DWORD createThreads, const DWORD runningThreads)
{
    int cnt;

    hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, runningThreads);

    if (hIOCP == NULL) {
        return false;
    }
    _LOG(LOG_LEVEL_SYSTEM, L"NetServer IOCP Created");

    //add 1 for accept thread
    hThreads = new HANDLE[createThreads + ACCEPT_THREAD + TIMER_THREAD];

    hThreads[0] = (HANDLE)_beginthreadex(NULL, 0, CNetServer::AcceptProc, this, NULL, NULL);
    hThreads[1] = (HANDLE)_beginthreadex(NULL, 0, CNetServer::TimerProc, this, NULL, NULL);

    for (cnt = ACCEPT_THREAD + TIMER_THREAD; cnt < createThreads + ACCEPT_THREAD + TIMER_THREAD; cnt++) {
        hThreads[cnt] = (HANDLE)_beginthreadex(NULL, 0, CNetServer::WorkProc, this, NULL, NULL);
    }

    for (cnt = 0; cnt < createThreads + ACCEPT_THREAD + TIMER_THREAD; cnt++) {
        if (hThreads[cnt] == INVALID_HANDLE_VALUE) {
            OnError(-1, L"Create Thread Failed");
            return false;
        }
    }
    _LOG(LOG_LEVEL_SYSTEM, L"NetServer Thread Created");

    return true;
}

SESSION* CNetServer::AcquireSession(DWORD64 sessionID)
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

    //���� �������� ��Ȯ��
    if (session->sessionID != sessionID) {
        LoseSession(session);
        return NULL;
    }

    return session;
}

void CNetServer::LoseSession(SESSION* session)
{
    if (InterlockedDecrement(&session->ioCnt) == 0) {
        ReleaseSession(session);
    }
}

SESSION* CNetServer::FindSession(DWORD64 sessionID)
{
    int sessionID_high = sessionID >> MASK_SHIFT;

    return &sessionArr[sessionID_high];
}

bool CNetServer::MakeSession(WCHAR* IP, SOCKET sock, DWORD64* ID)
{
    int sessionID_high;
    DWORD64 sessionID;
    SESSION* session;

    //iocp var
    HANDLE h;

    if (sessionStack.Pop(&sessionID_high) == false) {
        OnError(-1, L"All Session is in Use");
        return false;
    }

    sessionID = (totalAccept & SESSION_MASK);
    sessionID |= ((__int64)sessionID_high << MASK_SHIFT);

    session = &sessionArr[sessionID_high];

    //session->sock = sock;
    InterlockedExchange64((__int64*)&session->sock, sock);

    wmemmove_s(session->IP, 16, IP, 16);
    session->sessionID = *ID = sessionID;

    ZeroMemory(&session->recvOver, sizeof(session->recvOver));
    session->recvOver.type = 0;
    ZeroMemory(&session->sendOver, sizeof(session->sendOver));
    session->sendOver.type = 1;

    session->lastTime = currentTime;

    //recv�� ioCount����
    InterlockedIncrement(&session->ioCnt);
    InterlockedAnd64((__int64*)&session->ioCnt, ~RELEASE_FLAG);

    //iocp match
    h = CreateIoCompletionPort((HANDLE)session->sock, hIOCP, (ULONG_PTR)sessionID, 0);
    if (h != hIOCP) {
        _LOG(LOG_LEVEL_SYSTEM, L"IOCP to SOCKET Failed");
        OnError(-1, L"IOCP to SOCKET Failed");
        //crash �뵵
        ID = 0;
        *ID = 0;
        return false;
    }


    return true;
}

void CNetServer::ReleaseSession(SESSION* session)
{
    //cas�� �÷��� ��ȯ
    if (InterlockedCompareExchange64((long long*)&session->ioCnt, RELEASE_FLAG, 0) != 0) {
        return;
    }

    SOCKET sock = session->sock;
    int leftCnt;
    CPacket* packet;

    closesocket(sock & ~RELEASE_FLAG);

    InterlockedIncrement64((__int64*)&totalRelease);

    //���� Q ��� ����
    while (session->sendQ.Dequeue(&packet))
    {
        PacketFree(packet);
    }

    //sendBuffer�� ���� ��� ����
    for (leftCnt = 0; leftCnt < session->sendCnt; ++leftCnt) {
        packet = session->sendBuf[leftCnt];
        PacketFree(packet);
    }
    session->sendCnt = 0;

    session->recvQ.ClearBuffer();

    InterlockedExchange8((char*)&session->isSending, false);
    InterlockedDecrement(&sessionCnt);

    PostQueuedCompletionStatus(hIOCP, 0, session->sessionID, (LPOVERLAPPED)2);
}

unsigned int __stdcall CNetServer::WorkProc(void* arg)
{
    int ret;
    int err;
    DWORD ioCnt;

    DWORD bytes;
    DWORD64 sessionID;
    SESSION* session;
    OVERLAPPEDEX* overlap = NULL;

    CNetServer* server = (CNetServer*)arg;

    while (server->isServerOn) {
        ret = GetQueuedCompletionStatus(server->hIOCP, &bytes, (PULONG_PTR)&sessionID, (LPOVERLAPPED*)&overlap, INFINITE);

        //gqcs is false and overlap is NULL
        //case totally failed
        if (overlap == NULL) {
            err = WSAGetLastError();
            server->OnError(err, L"GQCS return NULL ovelap");
            continue;
        }

        session = server->FindSession(sessionID);

        if (session == NULL) {
            continue;
        }
        //disconnect�� ���
        if ((__int64)overlap == 2) {
            server->OnClientLeave(sessionID);
            server->sessionStack.Push(session->sessionID >> MASK_SHIFT);
            continue;
        }

        //recvd
        if (overlap->type == 0) {
            if (ret == false || bytes == 0) {
                //server->Disconnect(sessionID);
            }
            else
            {
                session->recvQ.MoveRear(bytes);
                //�߰� recv�� ���� acquire
                server->AcquireSession(sessionID);
                server->RecvProc(session);
            }
        }
        //sent
        if (overlap->type == 1) {
            while (session->sendCnt) {
                --session->sendCnt;
                server->PacketFree(session->sendBuf[session->sendCnt]);
            }
            InterlockedExchange8((char*)&session->isSending, 0);
            if (ret != false) {
                //�߰��� send�� ���� acquire
                server->AcquireSession(sessionID);
                server->SendPost(session);
            }
        }
        //�۾� �Ϸῡ ���� lose
        server->LoseSession(session);

    }




    return 0;
}

unsigned int __stdcall CNetServer::AcceptProc(void* arg)
{
    SOCKADDR_IN addr;
    SOCKET sock;
    WCHAR IP[16];

    CNetServer* server = (CNetServer*)arg;
    SESSION* session;
    DWORD64 sessionID;
    int addrLen = sizeof(addr);

    while (server->isServerOn) {
        sock = accept(server->listenSock, (sockaddr*)&addr, &addrLen);
        ++server->totalAccept;
        if (sock == INVALID_SOCKET) {
            continue;
        }

        InetNtop(AF_INET, &addr.sin_addr, IP, 16);

        if (server->OnConnectionRequest(IP, ntohs(addr.sin_port)) == false) {
            closesocket(sock);
            continue;
        }

        if (server->MakeSession(IP, sock, &sessionID) == false) {
            closesocket(sock);
            continue;
        }

        session = server->FindSession(sessionID);

        if (server->OnClientJoin(sessionID) == false) {
            server->LoseSession(session);
            continue;
        }

        InterlockedIncrement(&server->sessionCnt);

        server->RecvPost(session);
    }

    return 0;
}

unsigned int __stdcall CNetServer::TimerProc(void* arg)
{
    CNetServer* server = (CNetServer*)arg;
    SESSION* session;
    int cnt;
    //�ʱ���� ��������
    server->currentTime = timeGetTime();
    Sleep(10000);

    while (server->isServerOn) {
        server->currentTime = timeGetTime();
        
        for (cnt = 0; cnt < server->maxConnection; ++cnt) {
            session = &server->sessionArr[cnt];

            if (session->ioCnt & RELEASE_FLAG) continue;

            if (server->currentTime - session->lastTime >= session->timeOutVal) {
                server->Disconnect(session->sessionID);
                server->OnTimeOut(session->sessionID);
            }
        }

        Sleep(1000);
    }

    return 0;
}

void CNetServer::RecvProc(SESSION* session)
{
    //Packet ���� (netHeader ����)
    NET_HEADER netHeader;
    NET_HEADER* header;
    DWORD len;
    CRingBuffer* recvQ = &session->recvQ;
    CPacket* packet;

    WCHAR errText[100];

    session->lastTime = currentTime;

    for (;;) {
        len = recvQ->GetUsedSize();
        //���� �Ǻ�
        if (sizeof(netHeader) > len) {
            break;
        }

        //����� ����
        recvQ->Peek((char*)&netHeader, sizeof(netHeader));
        packet = PacketAlloc();

        if (netHeader.len > packet->GetBufferSize()) {
            Disconnect(session->sessionID);
            PacketFree(packet);
            OnError(-1, L"Unacceptable Length");
            LoseSession(session);
            return;
        }

        //���� �Ǻ�
        if (sizeof(netHeader) + netHeader.len > len) {
            PacketFree(packet);
            break;
        }


        InterlockedIncrement(&totalRecv);

        //������� dequeue
        recvQ->Dequeue((char*)packet->GetBufferPtr(), sizeof(netHeader) + netHeader.len);
        packet->MoveWritePos(netHeader.len);

        if (netHeader.staticCode != STATIC_CODE) {
            PacketFree(packet);
            swprintf_s(errText, L"%s %s", L"Packet Code Error", session->IP);
            OnError(-1, errText);
            //����ڵ� ������ ���� ����
            Disconnect(session->sessionID);
            LoseSession(session);
            return;
        }

        Decode(packet);
        //checksum����
        header = (NET_HEADER*)packet->GetBufferPtr();
        if (header->checkSum != MakeCheckSum(packet)) {
            PacketFree(packet);
            swprintf_s(errText, L"%s %s", L"Packet Code Error", session->IP);
            OnError(-1, errText);
            //üũ�� ������ ���� ����
            Disconnect(session->sessionID);
            LoseSession(session);
            return;
        }
        //����� net��� ��ŵ
        packet->MoveReadPos(sizeof(netHeader));

        OnRecv(session->sessionID, packet);
    }

    RecvPost(session);
}

bool CNetServer::RecvPost(SESSION* session)
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

    ret = WSARecv(InterlockedOr64((__int64*)&session->sock, 0), pBuf, 2, NULL, &flag, (LPWSAOVERLAPPED)&session->recvOver, NULL);

    if (ret == SOCKET_ERROR) {
        err = WSAGetLastError();

        if (err == WSA_IO_PENDING) {
            //good
        }
        else {
            switch (err) {
            case 10004:
            case 10038:
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

bool CNetServer::SendPost(SESSION* session)
{
    int ret;
    int err;
    WORD cnt;
    int sendCnt;

    CLockFreeQueue<CPacket*>* sendQ;
    CPacket* packet;

    WSABUF pBuf[SEND_PACKET_MAX];

    //�ٸ� SendPost���������� Ȯ�ο�
    if (InterlockedExchange8((char*)&session->isSending, 1) == true) {
        LoseSession(session);
        return false;
    }

    sendQ = &session->sendQ;
    sendCnt = sendQ->GetSize();
    //0byte send�� iocp�� ����� Enqueue, ���� 0����Ʈ �۽� X
    if (sendCnt == 0) {
        InterlockedExchange8((char*)&session->isSending, 0);
        LoseSession(session);
        return false;
    }

    session->sendCnt = min(SEND_PACKET_MAX, sendCnt);
    ZeroMemory(pBuf, sizeof(WSABUF) * SEND_PACKET_MAX);

    InterlockedAdd64((__int64*)&totalSend, session->sendCnt);

    for (cnt = 0; cnt < session->sendCnt; cnt++) {
        sendQ->Dequeue(&packet);
        session->sendBuf[cnt] = packet;
        pBuf[cnt].buf = packet->GetBufferPtr();
        pBuf[cnt].len = packet->GetDataSize();
    }

    ret = WSASend(InterlockedOr64((__int64*)&session->sock, 0), pBuf, session->sendCnt, NULL, 0, (LPWSAOVERLAPPED)&session->sendOver, NULL);

    if (ret == SOCKET_ERROR) {
        err = WSAGetLastError();

        if (err == WSA_IO_PENDING) {
            //good
        }
        else {
            switch (err) {
            case 10004:
            case 10038:
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
