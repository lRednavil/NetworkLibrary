#include "pch.h"
#include "GameServer.h"
#include "GameCommon.h"

#include <timeapi.h>

#pragma comment(lib, "Winmm")

class CDefautClass : public CUnitClass {
    //virtual함수 영역
    virtual void OnClientJoin(DWORD64 sessionID) {};
    virtual void OnClientLeave(DWORD64 sessionID) {};

    //message 분석 역할
    //메세지 헤더는 알아서 검증할 것
    //업데이트 스레드 처리 필요시 jobQ에 enQ할것
    virtual void OnRecv(DWORD64 sessionID, CPacket* packet) {};

    virtual void OnTimeOut(DWORD64 sessionID) {};

    virtual void OnError(int error, const WCHAR* msg) {};

    //gameserver용
    //jobQ에 EnQ된 메세지들 처리
    virtual void MsgUpdate() {};
    //frame단위의 업데이트 처리
    virtual void FrameUpdate() {};
};

#pragma region UnitClass
CUnitClass::CUnitClass()
{
    joinQ = new CLockFreeQueue<DWORD64>;
    leaveQ = new CLockFreeQueue<DWORD64>;
}
CUnitClass::~CUnitClass()
{
    delete joinQ;
    delete leaveQ;
}
void CUnitClass::InitClass(WORD targetFrame, BYTE endOpt)
{
    frameDelay = 1000 / targetFrame;
    endOption = endOpt;
}

bool CUnitClass::MoveClass(const WCHAR* className, DWORD64 sessionID, WORD classIdx)
{
    return server->MoveClass(className, sessionID, classIdx);
}

bool CUnitClass::MoveClass(const WCHAR* className, DWORD64* sessionIDs, WORD sessionCnt, WORD classIdx)
{
    if (sessionCnt == 0) return false;

    return server->MoveClass(className, sessionIDs, sessionCnt, classIdx);
}

bool CUnitClass::FollowClass(DWORD64 targetID, DWORD64 followID)
{
    return server->FollowClass(targetID, followID);
}

bool CUnitClass::Disconnect(DWORD64 sessionID)
{
    return server->Disconnect(sessionID);
}

bool CUnitClass::SendPacket(DWORD64 sessionID, CPacket* packet)
{
    return server->SendPacket(sessionID, packet);
}

CPacket* CUnitClass::PacketAlloc()
{
    return g_PacketPool.Alloc();
}

void CUnitClass::PacketFree(CPacket* packet)
{
    g_PacketPool.Free(packet);
}

void CUnitClass::SetTimeOut(DWORD64 sessionID, DWORD timeVal)
{
    server->SetTimeOut(sessionID, timeVal);
}

#pragma endregion

CDefautClass g_defaultClass;
CUSTOM_TCB g_defaultTCB;

CGameServer::CGameServer()
{
    //default tcb의 생성 과정
    g_defaultTCB.max_class_unit = 1;
    g_defaultTCB.classList = new CUnitClass*;
    g_defaultTCB.classList[0] = &g_defaultClass;
    g_defaultTCB.hEvent = (HANDLE)(HANDLE)CreateEvent(NULL, TRUE, FALSE, NULL);

    TCB_TO_THREAD* arg = new TCB_TO_THREAD{ this, &g_defaultTCB};
    _beginthreadex(NULL, 0, UnitProc, arg, 0, NULL);
}

CGameServer::~CGameServer()
{
}

bool CGameServer::MoveClass(const WCHAR* tagName, DWORD64 sessionID, WORD classIdx)
{
    SESSION* session = AcquireSession(sessionID);
    int tcbIdx;
    WORD unitIdx;
    CUSTOM_TCB* tcb = NULL;
    CUnitClass* destUnit = NULL;

    if (session == NULL) {
        return false;
    }

    //thread 탐색
    for (tcbIdx = 0; tcbIdx < tcbCnt; ++tcbIdx) {
        if (wcscmp(tagName, tcbArray[tcbIdx].tagName) != 0)
            continue;

        if (tcbArray[tcbIdx].currentUnits == tcbArray[tcbIdx].max_class_unit)
            continue;

        tcb = &tcbArray[tcbIdx];

        //class index가 지정된 경우
        if (classIdx != -1) {
            if (InterlockedIncrement16((short*)&tcb->classList[classIdx]->currentUser) >= tcb->classList[classIdx]->maxUser) {
                InterlockedDecrement16((short*)&tcb->classList[classIdx]->currentUser);
                break;
            }

            destUnit = tcb->classList[classIdx];
            goto END;
        }
        else {
            //class 순회
            for (unitIdx = 0; unitIdx < tcb->max_class_unit; unitIdx++) {
                //같은 곳으로 이동 방지
                if (tcb->classList[unitIdx] == session->belongClass) continue;

                if (InterlockedIncrement16((short*)&tcb->classList[unitIdx]->currentUser) >= tcb->classList[unitIdx]->maxUser) {
                    InterlockedDecrement16((short*)&tcb->classList[unitIdx]->currentUser);
                    continue;
                }

                destUnit = tcb->classList[unitIdx];
                goto END;
            }
        }
    }

END:
    if (destUnit != NULL) {
        //기존 클래스에 퇴장 신호
        session->isMoving = true;
        session->belongClass->leaveQ->Enqueue(sessionID);
        InterlockedDecrement16((short*)&session->belongClass->currentUser);

        session->belongThread = tcb;
        session->belongClass = destUnit;

        destUnit->isAwake = true;

        //이동 클래스에 입장 입력
        session->belongClass->joinQ->Enqueue(sessionID);
        SetEvent(session->belongThread->hEvent);
    }

    LoseSession(session);

    return destUnit == NULL;
}

bool CGameServer::MoveClass(const WCHAR* tagName, DWORD64* sessionIDs, WORD sessionCnt, WORD classIdx)
{
    SESSION** sessionArr = new SESSION * [sessionCnt];
    WORD sessionIdx;
    int tcbIdx;
    WORD unitIdx;
    CUSTOM_TCB* tcb = NULL;
    CUnitClass* destUnit = NULL;

    for (sessionIdx = 0; sessionIdx < sessionCnt; sessionIdx++) {
        sessionArr[sessionIdx] = AcquireSession(sessionIDs[sessionIdx]);
        if (sessionArr[sessionIdx] == NULL) {
            delete[] sessionArr;
            break;
        }
    }

    //thread 탐색
    for (tcbIdx = 0; tcbIdx < tcbCnt; ++tcbIdx) {
        if (wcscmp(tagName, tcbArray[tcbIdx].tagName) != 0)
            continue;

        if (tcbArray[tcbIdx].currentUnits == tcbArray[tcbIdx].max_class_unit)
            continue;

        tcb = &tcbArray[tcbIdx];

        //class index가 지정된 경우
        if (classIdx != -1) {
            if (InterlockedAdd((LONG*)&tcb->classList[classIdx]->currentUser, sessionCnt) >= tcb->classList[classIdx]->maxUser) {
                InterlockedAdd((LONG*)&tcb->classList[classIdx]->currentUser, -sessionCnt);
                break;
            }

            destUnit = tcb->classList[classIdx];
            destUnit->isAwake = true;
            goto END;
        }
        else {
            //class 순회
            for (unitIdx = 0; unitIdx < tcb->max_class_unit; unitIdx++) {
                if (InterlockedAdd((LONG*)&tcb->classList[classIdx]->currentUser, sessionCnt) >= tcb->classList[classIdx]->maxUser) {
                    InterlockedAdd((LONG*)&tcb->classList[classIdx]->currentUser, -sessionCnt);
                    continue;
                }

                destUnit = tcb->classList[unitIdx];
                destUnit->isAwake = true;
                goto END;
            }
        }
    }

END:

    if (destUnit != NULL) {
		for (sessionIdx = 0; sessionIdx < sessionCnt; sessionIdx++) {
            sessionArr[sessionIdx]->isMoving = true;
            //기존 클래스에 퇴장 신호
			sessionArr[sessionIdx]->belongClass->leaveQ->Enqueue(sessionIDs[sessionIdx]);
			InterlockedDecrement16((short*)&sessionArr[sessionIdx]->belongClass->currentUser);

			sessionArr[sessionIdx]->belongThread = tcb;
			sessionArr[sessionIdx]->belongClass = destUnit;

			//이동 클래스에 입장 입력
			sessionArr[sessionIdx]->belongClass->joinQ->Enqueue(sessionIDs[sessionIdx]);
            
            LoseSession(sessionArr[sessionIdx]);
        }

        SetEvent(tcb->hEvent);
    }

    return destUnit == NULL;
}

bool CGameServer::FollowClass(DWORD64 targetID, DWORD64 followID)
{
    SESSION* target = AcquireSession(targetID);
    SESSION* follower = AcquireSession(followID);
    bool res;

    follower->isMoving = true;

    do {
        if (target == NULL || follower == NULL) {
            res = false;
            break;
        }

        if (InterlockedIncrement16((short*)&target->belongClass->currentUser) >= target->belongClass->maxUser) {
            InterlockedDecrement16((short*)&target->belongClass->currentUser);
            res = false;
            break;
        }

        follower->belongClass->OnClientLeave(followID);
        InterlockedDecrement16((short*)&follower->belongClass->currentUser);

        follower->belongThread = target->belongThread;
        follower->belongClass = target->belongClass;

        //이동 클래스에 입장 신호
        follower->belongClass->joinQ->Enqueue(followID);
        SetEvent(follower->belongThread->hEvent);
        res = true;
    } while (0);

    LoseSession(target);
    LoseSession(follower);
    
    return res;
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
    tcbArray[tcbIdx].hEvent = (HANDLE)CreateEvent(NULL, TRUE, FALSE, NULL);
    
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

    session->isMoving = true;
    session->lastTime = currentTime;

    session->belongClass = &g_defaultClass;
    session->belongThread = &g_defaultTCB;

    //recv용 ioCount증가
    InterlockedIncrement(&session->ioCnt);
    InterlockedAnd64((__int64*)&session->ioCnt, ~RELEASE_FLAG);

    //iocp match
    h = CreateIoCompletionPort((HANDLE)session->sock, hIOCP, (ULONG_PTR)sessionID, 0);
    if (h != hIOCP) {
        _FILE_LOG(LOG_LEVEL_ERROR, L"LibraryLog", L"IOCP to SOCKET Failed");
        //crash 용도
        CRASH();
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
			session->belongClass->OnClientLeave(sessionID);
			InterlockedDecrement16((short*)&session->belongClass->currentUser);
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
                //스레드 깨우기용 이벤트 설정
                SetEvent(session->belongThread->hEvent);
            }
        }
        //sent
        if (overlap->type == 1) {
            while (session->sendCnt) {
                --session->sendCnt;
                server->PacketFree(session->sendBuf[session->sendCnt]);
            }
            InterlockedExchange8((char*)&session->isSending, 0);
            //if (session->isDisconnectReserved) {
            //    server->Disconnect(sessionID);
            //}
            //else if (ret != false) {
            //    //추가로 send에 맞춘 acquire
            //    server->AcquireSession(sessionID);
            //    server->SendPost(session);
            //}
			if (ret != false) {
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

        Sleep(TIMER_PRECISION);
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
        WaitForSingleObject(tcb->hEvent, TIMER_PRECISION);
        ResetEvent(tcb->hEvent);

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

            server->UnitJoinLeaveProc(unit);
        }

    }

    //동적할당되므로 삭제처리
    delete arg;

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

    if(session->isMoving == false)
	    RecvPost(session);
}

bool CGameServer::RecvPost(SESSION* session)
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
                _FILE_LOG(LOG_LEVEL_ERROR, L"LibraryLog", L"RecvPost Error %d", err);
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

bool CGameServer::SendPost(SESSION* session)
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
                _FILE_LOG(LOG_LEVEL_ERROR, L"LibraryLog", L"RecvPost Error %d", err);
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

void CGameServer::UnitJoinLeaveProc(CUnitClass* unit)
{
    DWORD64 sessionID;
    SESSION* session;
    while (unit->joinQ->Dequeue(&sessionID)) {
        unit->OnClientJoin(sessionID);
        session = FindSession(sessionID);
        session->isMoving = false;
        RecvPost(session);
    }

    while (unit->leaveQ->Dequeue(&sessionID)) {
        unit->OnClientLeave(sessionID);
        session = FindSession(sessionID);
    }
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

    tcbArray = new CUSTOM_TCB[500];

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

    AttatchClass(L"", &g_defaultClass);

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

CPacket* CGameServer::PacketAlloc()
{
    return g_PacketPool.Alloc();
}

void CGameServer::PacketFree(CPacket* packet)
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

