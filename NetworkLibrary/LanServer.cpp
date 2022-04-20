#include "pch.h"
#include "LanServer.h"
#include "LanCommon.h"

#define ACCEPT_THREAD 1
#define TIMER_THREAD 1

#define SESSION_MASK 0x00000fffffffffff
#define MASK_SHIFT 45
#define RELEASE_FLAG 0x7000000000000000

bool CLanServer::Start(WCHAR* IP, DWORD port, DWORD createThreads, DWORD runningThreads, bool isNagle, DWORD maxConnect)
{
    if (NetInit(IP, port, isNagle) == false) {
        return false;
    }

    isServerOn = true;
    sessionArr = new SESSION[maxConnect];

    totalAccept = 0;
    sessionCnt = 0;
   
    for (int cnt = 0; cnt < maxConnect; cnt++) {
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

    HeaderAlloc(packet);
    session->sendQ.Enqueue(packet);
    SendPost(session);
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
    else {
        abort();
    }
}

bool CLanServer::NetInit(WCHAR* IP, DWORD port, bool isNagle)
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
    int cnt;
    
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

    //���� �������� ��Ȯ��
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
        OnError(-1, L"All Session is in Use");
        return false;
    }

    sessionID = (totalAccept & SESSION_MASK);
    sessionID |= ((__int64)sessionID_high << MASK_SHIFT);

    session = &sessionArr[sessionID_high];

    session->sock = sock;
    session->ioCnt &= ~RELEASE_FLAG;
    

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
        return false;
    }

    InterlockedIncrement(&sessionCnt);
    //recv start
    InterlockedIncrement(&session->ioCnt);
    return RecvPost(session);
}

void CLanServer::ReleaseSession(SESSION* session)
{
    //cas�� �÷��� ��ȯ
    if (InterlockedCompareExchange64((long long*)&session->ioCnt, RELEASE_FLAG, 0) != 0) {
        return;
    }

    SOCKET sock = session->sock;
    int leftCnt;
    CPacket* packet;
    
    closesocket(sock);

    //���� Q ��� ����
    while(session->sendQ.Dequeue(&packet))
    {
        PacketFree(packet);    
    }

    //sendBuffer�� ���� ��� ����
    for (leftCnt = 0; leftCnt < session->sendCnt; ++leftCnt) {
        packet = session->sendBuf[leftCnt];
        PacketFree(packet);    
    }
    session->sendCnt = 0;

    InterlockedExchange8((char*)&session->isSending, false);
    InterlockedDecrement(&sessionCnt);

    sessionStack.Push(session->sessionID >> MASK_SHIFT);
}

unsigned int __stdcall CLanServer::WorkProc(void* arg)
{
    int ret;
    int err;
    DWORD ioCnt;

    DWORD bytes;
    DWORD64 sessionID;
    SESSION* session;
    OVERLAPPEDEX* overlap = NULL;

    CLanServer* server = (CLanServer*)arg;

    while(server->isServerOn) {
        ret = GetQueuedCompletionStatus(server->hIOCP, &bytes, (PULONG_PTR)&sessionID, (LPOVERLAPPED*)&overlap, INFINITE);

        //gqcs is false and overlap is NULL
        //case totally failed
        if (overlap == NULL) {
            err = WSAGetLastError();
            server->OnError(err, L"GQCS return NULL ovelap");
            continue;
        }

        session = server->AcquireSession(sessionID);
        
        if (session != NULL) {
			//recvd
			if (overlap->type == 0) {
				if (ret == false || bytes == 0) {
					server->Disconnect(sessionID);
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

        //gqcs �� session�� acquire�� ���� ����
        server->LoseSession(session);
    }

    return 0;
}

unsigned int __stdcall CLanServer::AcceptProc(void* arg)
{
    SOCKADDR_IN addr;
    SOCKET sock;
    WCHAR IP[16];

    CLanServer* server = (CLanServer*)arg;
    DWORD64 sessionID;

    while(server->isServerOn) {
        sock = accept(server->listenSock, (sockaddr*)&addr, NULL);
        ++server->totalAccept;
        if (sock == INVALID_SOCKET) {
            continue;
        }

        InetNtop(AF_INET, &addr.sin_addr, IP, 16);
        
        if (server->OnConnectionRequest(IP, ntohs(addr.sin_port)) == false) {
            continue;
        }

        if (server->MakeSession(IP, sock, &sessionID) == false) {
            continue;
        }

        server->OnClientJoin(sessionID);
    }

    return 0;
}

void CLanServer::RecvProc(SESSION* session)
{
    //Packet ���� (lanHeader ����)
    LAN_HEADER lanHeader;
    DWORD len;
    CRingBuffer* recvQ = &session->recvQ;
    CPacket* packet;

    for (;;) {
        packet = PacketAlloc();
        
        len = recvQ->GetUsedSize();
        //���� �Ǻ�
        if (sizeof(lanHeader) > len) {
            packet->SubRef(); 
			g_PacketPool.Free(packet);
            break;
        }

        //����� ����
        recvQ->Peek((char*)&lanHeader, sizeof(lanHeader));

        //���� �Ǻ�
        if (sizeof(lanHeader) + lanHeader.len > len) {
            packet->SubRef(); 
			g_PacketPool.Free(packet);
            break;
        }

        //����� ���� ��������
        recvQ->MoveFront(sizeof(lanHeader));
        //���߿� �޼��� ��� ���� ó�� or MsgProc(session, packet)
        recvQ->Dequeue((char*)packet->GetWritePtr(), lanHeader.len);
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

bool CLanServer::SendPost(SESSION* session)
{
    int ret;
    int err;
    WORD cnt;
    int sendCnt;

    CLockFreeQueue<CPacket*>* sendQ;
    CPacket* packet;

    WSABUF pBuf[200];
    
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
    
    session->sendCnt = min(200,sendCnt);
    ZeroMemory(pBuf, sizeof(WSABUF) * 200);

    for (cnt = 0; cnt < sendCnt; cnt++) {
        sendQ->Dequeue(&packet);
        session->sendBuf[cnt] = packet;
        pBuf[cnt].buf = packet->GetBufferPtr();
        pBuf[cnt].len = packet->GetDataSize();
    }

    ret = WSASend(session->sock, pBuf, 200, NULL, 0, (LPWSAOVERLAPPED)&session->sendOver, NULL);

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
