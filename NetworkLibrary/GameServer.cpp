#include "pch.h"
#include "GameServer.h"
#include "GameCommon.h"

#include <timeapi.h>

#pragma comment(lib, "Winmm")

#pragma region UnitClass
void CUnitClass::InitClass(WORD targetFrame, BYTE endOpt)
{
    frameDelay = 1000 / targetFrame;
    endOption = endOpt;
}

inline bool CUnitClass::MoveClass(const WCHAR* className, DWORD64 sessionID)
{
    return server->MoveClass(className, sessionID);
}

inline bool CUnitClass::FollowClass(DWORD64 targetID, DWORD64 followID)
{
    return server->FollowClass(targetID, followID);
}

inline bool CUnitClass::Disconnect(DWORD64 sessionID)
{
    return server->Disconnect(sessionID);
}

inline bool CUnitClass::SendPacket(DWORD64 sessionID, CPacket* packet)
{
    return server->SendPacket(sessionID, packet);
}

inline CPacket* CUnitClass::PacketAlloc()
{
    return g_PacketPool.Alloc();
}

inline void CUnitClass::PacketFree(CPacket* packet)
{
    g_PacketPool.Free(packet);
}

inline void CUnitClass::SetTimeOut(DWORD64 sessionID, DWORD timeVal)
{
    server->SetTimeOut(sessionID, timeVal);
}

#pragma endregion

bool CGameServer::MoveClass(const WCHAR* tagName, DWORD64 sessionID)
{
    return true;
}

bool CGameServer::FollowClass(DWORD64 targetID, DWORD64 followID)
{
    SESSION* target = AcquireSession(targetID);
    SESSION* follower = AcquireSession(followID);
    bool res;

    do {
        if (target == NULL || follower == NULL) {
            res = false;
            break;
        }

        if (target->belongClass->currentUser >= target->belongClass->maxUser) {
            res = false;
            break;
        }

        

        res = true;
    } while (0);

    LoseSession(target);
    LoseSession(follower);
    
    return res;
}

bool CGameServer::JoinThread(const WCHAR* tagName)
{
    int cnt;
    WORD tcbIdx;
    WORD unitIdx;

    for (cnt = 0; cnt < tcbCnt; ++cnt) {
        if (wcscmp(tagName, tcbArray[cnt].tagName) != 0)
            continue;

        if (tcbArray[cnt].currentUnits == tcbArray[cnt].max_class_unit)
            continue;

        return JoinClass(&tcbArray[cnt]);
    }

    return false;
}

bool CGameServer::JoinClass(CUSTOM_TCB* tcb)
{
    int cnt;
    CUnitClass* unit;

    for (cnt = 0; cnt < tcb->max_class_unit; cnt++) {
        unit = tcb->classList[cnt];

        if (unit->isAwake == true) continue;

        
    }
    return false;
}

void CGameServer::AttatchClass(const WCHAR* tagName, CUnitClass* const classPtr, const WORD maxUnitCnt)
{
    int cnt;
    WORD tcbIdx;
    WORD unitIdx;

    if (classPtr->server != nullptr) {
        _FILE_LOG(LOG_LEVEL_ERROR, L"User_Error", L"UnitClass Already Have Another Game Server");
        CRASH();
    }

    classPtr->server = this;
    for (cnt = 0; cnt < tcbCnt; ++cnt) {
        if (wcscmp(tagName, tcbArray[cnt].tagName) != 0) 
            continue;

        unitIdx = InterlockedIncrement16((short*)&tcbArray[cnt].currentUnits);
        if (unitIdx >= tcbArray[cnt].max_class_unit) {
            InterlockedDecrement16((short*)&tcbArray[cnt].currentUnits);
            continue;
        }

        //기존에 있는 스레드 정보에 부착 성공
        tcbArray[cnt].classList[unitIdx] = classPtr;
        return;
    }

    //새로운 tcb의 생성 과정
    tcbIdx = InterlockedIncrement16((short*)&tcbCnt);
    wmemmove_s(tcbArray[tcbIdx].tagName, TAG_NAME_MAX, tagName, TAG_NAME_MAX);
    tcbArray[tcbIdx].max_class_unit = maxUnitCnt;
    tcbArray[tcbIdx].classList = new CUnitClass*[maxUnitCnt];
    tcbArray[tcbIdx].classList[0] = classPtr;

    tcbArray[tcbIdx].currentUnits = 1;
    
    TCB_TO_THREAD* arg = new TCB_TO_THREAD{ this, &tcbArray[tcbIdx] };
    _beginthreadex(NULL, 0, UnitProc, arg, 0, NULL);
}

bool CGameServer::NetInit(WCHAR* IP, DWORD port, bool isNagle)
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

bool CGameServer::ThreadInit(const DWORD createThreads, const DWORD runningThreads)
{
    int cnt;

    hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, runningThreads);

    if (hIOCP == NULL) {
        return false;
    }
    _LOG(LOG_LEVEL_SYSTEM, L"NetServer IOCP Created");

    //add 1 for accept thread
    hThreads = new HANDLE[createThreads + ACCEPT_THREAD + TIMER_THREAD];

    hThreads[0] = (HANDLE)_beginthreadex(NULL, 0, CGameServer::AcceptProc, this, NULL, NULL);
    hThreads[1] = (HANDLE)_beginthreadex(NULL, 0, CGameServer::TimerProc, this, NULL, NULL);

    for (cnt = ACCEPT_THREAD + TIMER_THREAD; cnt < createThreads + ACCEPT_THREAD + TIMER_THREAD; cnt++) {
        hThreads[cnt] = (HANDLE)_beginthreadex(NULL, 0, CGameServer::WorkProc, this, NULL, NULL);
    }

    for (cnt = 0; cnt < createThreads + ACCEPT_THREAD + TIMER_THREAD; cnt++) {
        if (hThreads[cnt] == INVALID_HANDLE_VALUE) {
            _FILE_LOG(LOG_LEVEL_ERROR, L"Server_Error", L"GameServer Thread Create Failed");
            return false;
        }
    }
    _LOG(LOG_LEVEL_SYSTEM, L"NetServer Thread Created");

    return true;
}


void CGameServer::HeaderAlloc(CPacket* packet)
{
    if (packet->isEncoded) return;

    NET_HEADER* header = (NET_HEADER*)packet->GetBufferPtr();
    header->staticCode = STATIC_CODE;
    header->len = packet->GetDataSize() - sizeof(NET_HEADER);
    header->randomKey = rand();
    header->checkSum = MakeCheckSum(packet);
}

BYTE CGameServer::MakeCheckSum(CPacket* packet)
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

void CGameServer::Encode(CPacket* packet)
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

void CGameServer::Decode(CPacket* packet)
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

SESSION* CGameServer::AcquireSession(DWORD64 sessionID)
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

void CGameServer::LoseSession(SESSION* session)
{
    if (InterlockedDecrement(&session->ioCnt) == 0) {
        ReleaseSession(session);
    }
}

SESSION* CGameServer::FindSession(DWORD64 sessionID)
{
    int sessionID_high = sessionID >> MASK_SHIFT;

    return &sessionArr[sessionID_high];
}

bool CGameServer::MakeSession(WCHAR* IP, SOCKET sock, DWORD64* ID)
{
    int sessionID_high;
    DWORD64 sessionID;
    SESSION* session;

    //iocp var
    HANDLE h;

    if (sessionStack.Pop(&sessionID_high) == false) {
        _FILE_LOG(LOG_LEVEL_SYSTEM, L"LibraryLog", L"All Session is in Use");
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

    session->isDisconnectReserved = false;
    session->lastTime = currentTime;

    //recv용 ioCount증가
    InterlockedIncrement(&session->ioCnt);
    InterlockedAnd64((__int64*)&session->ioCnt, ~RELEASE_FLAG);

    //iocp match
    h = CreateIoCompletionPort((HANDLE)session->sock, hIOCP, (ULONG_PTR)sessionID, 0);
    if (h != hIOCP) {
        _FILE_LOG(LOG_LEVEL_ERROR, L"LibraryLog", L"IOCP to SOCKET Failed");
        //crash 용도
        ID = 0;
        *ID = 0;
        return false;
    }


    return true;
}

void CGameServer::ReleaseSession(SESSION* session)
{
    //cas로 플래그 전환
    if (InterlockedCompareExchange64((long long*)&session->ioCnt, RELEASE_FLAG, 0) != 0) {
        return;
    }

    SOCKET sock = session->sock;
    int leftCnt;
    CPacket* packet;

    closesocket(sock & ~RELEASE_FLAG);

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

    session->recvQ.ClearBuffer();

    InterlockedExchange8((char*)&session->isSending, false);
    InterlockedDecrement(&sessionCnt);

    PostQueuedCompletionStatus(hIOCP, 0, session->sessionID, (LPOVERLAPPED)OV_DISCONNECT);
}

unsigned int __stdcall CGameServer::WorkProc(void* arg)
{
    int ret;
    int err;
    DWORD ioCnt;

    DWORD bytes;
    DWORD64 sessionID;
    SESSION* session;
    OVERLAPPEDEX* overlap = NULL;

    CGameServer* server = (CGameServer*)arg;

    while (server->isServerOn) {
        ret = GetQueuedCompletionStatus(server->hIOCP, &bytes, (PULONG_PTR)&sessionID, (LPOVERLAPPED*)&overlap, INFINITE);

        //gqcs is false and overlap is NULL
        //case totally failed
        if (overlap == NULL) {
            err = WSAGetLastError();
            _FILE_LOG(LOG_LEVEL_ERROR, L"LibraryLog", L"GQCS return NULL ovelap");
            CRASH();
            continue;
        }

        session = server->FindSession(sessionID);

        if (session == NULL) {
            continue;
        }
        //disconnect의 경우
        if ((__int64)overlap == OV_DISCONNECT) {
            //session->belongClass->OnClientLeave(sessionID);
            server->sessionStack.Push(session->sessionID >> MASK_SHIFT);
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
            if (session->isDisconnectReserved) {
                server->Disconnect(sessionID);
            }
            else if (ret != false) {
                //추가로 send에 맞춘 acquire
                server->AcquireSession(sessionID);
                server->SendPost(session);
            }
        }
        //작업 완료에 대한 lose
        server->LoseSession(session);

    }



    return 0;
}

unsigned int __stdcall CGameServer::AcceptProc(void* arg)
{
    SOCKADDR_IN addr;
    SOCKET sock;
    WCHAR IP[16];

    CGameServer* server = (CGameServer*)arg;
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

unsigned int __stdcall CGameServer::TimerProc(void* arg)
{
    CGameServer* server = (CGameServer*)arg;
    SESSION* session;
    int cnt;
    //초기오류 방지구간
    server->currentTime = timeGetTime();
    Sleep(250);

    while (server->isServerOn) {
        server->currentTime = timeGetTime();

        for (cnt = 0; cnt < server->maxConnection; ++cnt) {
            session = &server->sessionArr[cnt];

            if (session->ioCnt & RELEASE_FLAG) continue;

            if (server->currentTime - session->lastTime >= session->timeOutVal) {
                server->Disconnect(session->sessionID);
                session->belongClass->OnTimeOut(session->sessionID);
            }
        }

        Sleep(5);
    }

    return 0;
}

unsigned int __stdcall CGameServer::UnitProc(void* arg)
{
    TCB_TO_THREAD* info = (TCB_TO_THREAD*)arg;
    CGameServer* server = info->thisPtr;
    CUSTOM_TCB* tcb = info->tcb;
    CUnitClass* unit;

    int cnt;
    int lim;

    while (server->isServerOn) {
        lim = min(tcb->currentUnits, tcb->max_class_unit);

        for (cnt = 0; cnt < lim; cnt++) {
            unit = tcb->classList[cnt];

            if (unit->isAwake == false) continue;

            //jobQ에 들어온 메세지 처리함수
            unit->MsgUpdate();
            
            if (server->currentTime - unit->lastTime >= unit->frameDelay) {
                unit->lastTime = server->currentTime;
                //frameDelay 이상의 시간이 지난 경우 작동
                //frameDelay 미만의 시간이 지난 경우 바로 return
                unit->FrameUpdate();
            }
        }
    }

    return 0;
}

void CGameServer::RecvProc(SESSION* session)
{
	//Packet 떼기 (netHeader 제거)
	NET_HEADER netHeader;
	NET_HEADER* header;
	DWORD len;
	CRingBuffer* recvQ = &session->recvQ;
	CPacket* packet;

	WCHAR errText[100];

	session->lastTime = currentTime;

	for (;;) {
		len = recvQ->GetUsedSize();
		//길이 판별
		if (sizeof(netHeader) > len) {
			break;
		}

		//넷헤더 추출
		recvQ->Peek((char*)&netHeader, sizeof(netHeader));
		packet = PacketAlloc();

		if (netHeader.len > packet->GetBufferSize()) {
			Disconnect(session->sessionID);
			PacketFree(packet);
			_FILE_LOG(LOG_LEVEL_ERROR, L"LibraryLog", L"Unacceptable Length from %s", session->IP);
			session->belongClass->OnError(-1, L"Unacceptable Length");
			LoseSession(session);
			return;
		}

		//길이 판별
		if (sizeof(netHeader) + netHeader.len > len) {
			PacketFree(packet);
			break;
		}


		InterlockedIncrement(&totalRecv);

		//헤더영역 dequeue
		recvQ->Dequeue((char*)packet->GetBufferPtr(), sizeof(netHeader) + netHeader.len);
		packet->MoveWritePos(netHeader.len);

		if (netHeader.staticCode != STATIC_CODE) {
			PacketFree(packet);
			swprintf_s(errText, L"%s %s", L"Packet Code Error", session->IP);
			_FILE_LOG(LOG_LEVEL_ERROR, L"LibraryLog", L"Packet Code Error from %s", session->IP);
            session->belongClass->OnError(-1, errText);
			//헤드코드 변조시 접속 제거
			Disconnect(session->sessionID);
			LoseSession(session);
			return;
		}

		Decode(packet);
		//checksum검증
		header = (NET_HEADER*)packet->GetBufferPtr();
		if (header->checkSum != MakeCheckSum(packet)) {
			PacketFree(packet);
			swprintf_s(errText, L"%s %s", L"Packet Code Error", session->IP);
			_FILE_LOG(LOG_LEVEL_ERROR, L"LibraryLog", L"Packet Checksum Error from %s", session->IP);
            session->belongClass->OnError(-1, errText);
			//체크섬 변조시 접속 제거
			Disconnect(session->sessionID);
			LoseSession(session);
			return;
		}
		//사용전 net헤더 스킵
		packet->MoveReadPos(sizeof(netHeader));

        session->belongClass->OnRecv(session->sessionID, packet);
        
	}

	RecvPost(session);
}

bool CGameServer::RecvPost(SESSION* session)
{
    return false;
}

bool CGameServer::SendPost(SESSION* session)
{
    return false;
}

bool CGameServer::Start(WCHAR* IP, DWORD port, DWORD createThreads, DWORD runningThreads, bool isNagle, DWORD maxConnect)
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

void CGameServer::Stop()
{
    isServerOn = false;
}

int CGameServer::GetSessionCount()
{
    return sessionCnt;
}

void CGameServer::Monitor()
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

bool CGameServer::Disconnect(DWORD64 sessionID)
{
    SESSION* session = AcquireSession(sessionID);
    if (session == NULL) {
        return false;
    }

    CancelIoEx((HANDLE)InterlockedOr64((__int64*)&session->sock, RELEASE_FLAG), NULL);

    LoseSession(session);
    return true;
}

bool CGameServer::SendPacket(DWORD64 sessionID, CPacket* packet)
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

inline CPacket* CGameServer::PacketAlloc()
{
    return g_PacketPool.Alloc();
}

inline void CGameServer::PacketFree(CPacket* packet)
{
    g_PacketPool.Free(packet);
}

void CGameServer::SetTimeOut(DWORD64 sessionID, DWORD timeVal)
{
    SESSION* session = AcquireSession(sessionID);

    if (session == NULL) {
        return;
    }

    session->timeOutVal = timeVal;
    LoseSession(session);
}