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

    if (ThreadInit(createThreads, runningThreads) == false) {
        isServerOn = false;
        sessionStack.~CLockFreeStack();
        return false;
    }

    myMonitor = new CProcessMonitor;
    totalMonitor = new CProcessorMonitor;

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
    wprintf_s(L"=============================\n");
    wprintf_s(L"Accept TPS : %llu \n", totalAccept - lastAccept);
    wprintf_s(L"Send TPS : %llu \n", totalSend - lastSend);
    wprintf_s(L"Recv TPS : %llu \n", totalRecv - lastRecv);
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
}

bool CNetServer::Disconnect(DWORD64 sessionID)
{
    SESSION* session = AcquireSession(sessionID);
    if (session == NULL) {
        return false;
    }

    CancelIoEx((HANDLE)session->sock, NULL);
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

    //같은 세션인지 재확인
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

    session->sock = sock;

    wmemmove_s(session->IP, 16, IP, 16);
    session->sessionID = *ID = sessionID;

    ZeroMemory(&session->recvOver, sizeof(session->recvOver));
    session->recvOver.type = 0;
    ZeroMemory(&session->sendOver, sizeof(session->sendOver));
    session->sendOver.type = 1;

    //iocp match
    h = CreateIoCompletionPort((HANDLE)session->sock, hIOCP, (ULONG_PTR)sessionID, 0);
    if (h != hIOCP) {
        _LOG(LOG_LEVEL_SYSTEM, L"IOCP to SOCKET Failed");
        OnError(-1, L"IOCP to SOCKET Failed");
        return false;
    }

    session->lastTime = currentTime;

    //recv용 ioCount증가
    InterlockedIncrement(&session->ioCnt);
    session->ioCnt &= ~RELEASE_FLAG;

    InterlockedIncrement(&sessionCnt);
    //recv start
    return RecvPost(session);
}

void CNetServer::ReleaseSession(SESSION* session)
{
    //cas로 플래그 전환
    if (InterlockedCompareExchange64((long long*)&session->ioCnt, RELEASE_FLAG, 0) != 0) {
        return;
    }

    SOCKET sock = session->sock;
    int leftCnt;
    CPacket* packet;

    closesocket(sock);

    OnClientLeave(session->sessionID);

    //남은 Q 찌꺼기 제거
    while (session->sendQ.Dequeue(&packet))
    {
        PacketFree(packet);
    }

    //sendBuffer에 남은 찌꺼기 제거
    for (leftCnt = 0; leftCnt < session->sendCnt; ++leftCnt) {
        packet = session->sendBuf[leftCnt];
        PacketFree(packet);
    }
    session->sendCnt = 0;

    InterlockedExchange8((char*)&session->isSending, false);
    InterlockedDecrement(&sessionCnt);

    sessionStack.Push(session->sessionID >> MASK_SHIFT);
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

        session = server->AcquireSession(sessionID);

        if (session == NULL) {
            continue;
        }
		//recvd
		if (overlap->type == 0) {
			if (ret == false || bytes == 0) {
				server->Disconnect(sessionID);
			}
			else
			{
				session->recvQ.MoveRear(bytes);
				//추가 recv에 맞춘 acquire
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
				//추가로 send에 맞춘 acquire
				server->AcquireSession(sessionID);
				server->SendPost(session);
			}
		}
		//작업 완료에 대한 lose
		server->LoseSession(session);

        //gqcs 후 session의 acquire에 대한 해제
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
    DWORD64 sessionID;

    while (server->isServerOn) {
        sock = accept(server->listenSock, (sockaddr*)&addr, NULL);
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

        server->OnClientJoin(sessionID);
    }

    return 0;
}

unsigned int __stdcall CNetServer::TimerProc(void* arg)
{
    CNetServer* server = (CNetServer*)arg;
    SESSION* session;
    int cnt;
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
    //Packet 떼기 (netHeader 제거)
    NET_HEADER netHeader;
    NET_HEADER* header;
    DWORD len;
    CRingBuffer* recvQ = &session->recvQ;
    CPacket* packet;

    session->lastTime = currentTime;

    for (;;) {
        packet = PacketAlloc();

        len = recvQ->GetUsedSize();
        //길이 판별
        if (sizeof(netHeader) > len) {
            packet->SubRef();
            g_PacketPool.Free(packet);
            break;
        }

        //넷헤더 추출
        recvQ->Peek((char*)&netHeader, sizeof(netHeader));

        //길이 판별
        if (sizeof(netHeader) + netHeader.len > len) {
            packet->SubRef();
            g_PacketPool.Free(packet);
            break;
        }

        InterlockedIncrement(&totalRecv);

        //헤더영역 dequeue
        recvQ->Dequeue((char*)packet->GetBufferPtr(), sizeof(netHeader) + netHeader.len);
        packet->MoveWritePos(netHeader.len);

        if (netHeader.staticCode != STATIC_CODE) {
            packet->SubRef();
            g_PacketPool.Free(packet);
            OnError(-1, L"Packet CheckSum Error");
            //헤드코드 변조시 접속 제거
            Disconnect(session->sessionID);
            return;
        }

        Decode(packet);
        //checksum검증
        header = (NET_HEADER*)packet->GetBufferPtr();
        if (header->checkSum != MakeCheckSum(packet)) {
            packet->SubRef();
            g_PacketPool.Free(packet);
            OnError(-1, L"Packet CheckSum Error");
            //체크섬 변조시 접속 제거
            Disconnect(session->sessionID);
            return;
        }
        //사용전 net헤더 스킵
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

    ret = WSARecv(session->sock, pBuf, 2, NULL, &flag, (LPWSAOVERLAPPED)&session->recvOver, NULL);

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

bool CNetServer::SendPost(SESSION* session)
{
    int ret;
    int err;
    WORD cnt;
    int sendCnt;

    CLockFreeQueue<CPacket*>* sendQ;
    CPacket* packet;

    WSABUF pBuf[SEND_PACKET_MAX];

    //다른 SendPost진행중인지 확인용
    if (InterlockedExchange8((char*)&session->isSending, 1) == true) {
        LoseSession(session);
        return false;
    }

    sendQ = &session->sendQ;
    sendCnt = sendQ->GetSize();
    //0byte send시 iocp에 결과만 Enqueue, 실제 0바이트 송신 X
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

    ret = WSASend(session->sock, pBuf, session->sendCnt, NULL, 0, (LPWSAOVERLAPPED)&session->sendOver, NULL);

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
