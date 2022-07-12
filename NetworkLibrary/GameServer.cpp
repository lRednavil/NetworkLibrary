#include "pch.h"
#include "GameServer.h"
#include "GameCommon.h"

#include <timeapi.h>

#pragma comment(lib, "Winmm")

struct MOVE_INFO {
    DWORD64 sessionID;
    CPacket* packet;
};

struct SESSION {
    OVERLAPPEDEX recvOver;
    OVERLAPPEDEX sendOver;
    //session refCnt의 역할
    alignas(64)
        DWORD64 ioCnt;
    alignas(64)
        short isRecving;    
    alignas(64)
        bool isSending;
    alignas(64)
        char isMoving;

    //네트워크 메세지용 버퍼들
    alignas(64)
        CRingBuffer recvQ;
    alignas(64)
        CLockFreeQueue<CPacket*> sendQ;
    alignas(64)
        SOCKET sock; 

    //timeOut용 변수들
    DWORD lastTime;
    DWORD timeOutVal;

    //send 후 해제용
    CPacket* sendBuf[SEND_PACKET_MAX];

    //monitor
    DWORD sendCnt; // << 보낸 메세지수 확보

    //readonly
    alignas(64)
    DWORD64 sessionID;
    WCHAR IP[16];

    //gameServer용
    CUSTOM_TCB* belongThread;
    CUnitClass* belongClass;

    SESSION() {
        ioCnt = RELEASE_FLAG;
        timeOutVal = 1000;
    }
};
//최초 접속시 있을 더미 클래스
class CDefautClass : public CUnitClass {
    //virtual함수 영역
    virtual void OnClientJoin(DWORD64 sessionID, CPacket* packet) {};
    virtual void OnClientLeave(DWORD64 sessionID) {};

    virtual void OnClientDisconnected(DWORD64 sessionID) {};

    //message 분석 역할
    //메세지 헤더는 알아서 검증할 것
    //업데이트 스레드 처리 필요시 jobQ에 enQ할것
    virtual void OnRecv(DWORD64 sessionID, CPacket* packet) {};

    virtual void OnTimeOut(DWORD64 sessionID) {};

    virtual void OnError(int error, const WCHAR* msg) {};
    
    virtual void OnEnd() {};

    //gameserver용
    //jobQ에 EnQ된 메세지들 처리
    virtual void MsgUpdate() {};
    //frame단위의 업데이트 처리
    virtual void FrameUpdate() {};
};

#pragma region UnitClass
CUnitClass::CUnitClass()
{
    joinQ = new CLockFreeQueue<MOVE_INFO>;
    leaveQ = new CLockFreeQueue<MOVE_INFO>;
    disconnectQ = new CLockFreeQueue<MOVE_INFO>;
}
CUnitClass::~CUnitClass()
{
    delete joinQ;
    delete leaveQ;
    delete disconnectQ;
}
void CUnitClass::InitClass(WORD targetFrame, WORD maxUser)
{
    frameDelay = 1000 / targetFrame;
    this->maxUser = maxUser;
}

bool CUnitClass::MoveClass(const WCHAR* className, DWORD64 sessionID, CPacket* packet, WORD classIdx)
{
    return server->MoveClass(className, sessionID, packet, classIdx);
}

bool CUnitClass::FollowClass(DWORD64 targetID, DWORD64 followID, CPacket* packet)
{
    return server->FollowClass(targetID, followID, packet);
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
    return server->PacketAlloc();
}

CPacket* CUnitClass::InfoAlloc()
{
    return server->InfoAlloc();
}

void CUnitClass::PacketFree(CPacket* packet)
{
    server->PacketFree(packet);
}

int CUnitClass::GetPacketPoolCapacity()
{
    return server->GetPacketPoolCapacity();
}

int CUnitClass::GetPacketPoolUse()
{
    return server->GetPacketPoolUse();
}

void CUnitClass::SetTimeOut(DWORD64 sessionID, DWORD timeVal)
{
    SetTimeOut(sessionID, timeVal);
}

#pragma endregion

CDefautClass* g_defaultClass;
CUSTOM_TCB* g_defaultTCB;

CGameServer::CGameServer()
{
    int ret;
    g_defaultClass = new CDefautClass;
    g_defaultTCB = new CUSTOM_TCB;

    //default tcb의 생성 과정
    g_defaultTCB->max_class_unit = 1;
    g_defaultTCB->classList = new CUnitClass*;
    g_defaultTCB->classList[0] = g_defaultClass;
    g_defaultTCB->hEvent = (HANDLE)CreateEvent(NULL, TRUE, FALSE, NULL);

    packetPool = NULL;
    g_defaultClass->isAwake = true;
    g_defaultClass->server = this;
}

CGameServer::~CGameServer()
{
    Stop();
}

bool CGameServer::MoveClass(const WCHAR* tagName, DWORD64 sessionID, CPacket* packet, WORD classIdx)
{
    SESSION* session = AcquireSession(sessionID);
    int tcbIdx;
    WORD unitIdx;
    CUSTOM_TCB* tcb = NULL;
    CUnitClass* destUnit = NULL;
    MOVE_INFO info;

    if (session == NULL) {
        return false;
    }

    //thread 탐색
    for (tcbIdx = 0; tcbIdx < tcbCnt; ++tcbIdx) {
        if (wcscmp(tagName, tcbArray[tcbIdx].tagName) != 0)
            continue;

        tcb = &tcbArray[tcbIdx];

        //class index가 지정된 경우
        if (classIdx != (WORD)-1) {
            if (InterlockedIncrement16((short*)&tcb->classList[classIdx]->currentUser) > tcb->classList[classIdx]->maxUser) {
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

                if (InterlockedIncrement16((short*)&tcb->classList[unitIdx]->currentUser) > tcb->classList[unitIdx]->maxUser) {
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
        info.sessionID = sessionID;
        info.packet = packet;

        InterlockedExchange8(&session->isMoving, 1);
        session->belongClass->leaveQ->Enqueue(info);
        InterlockedDecrement16((short*)&session->belongClass->currentUser);

        session->belongThread = tcb;
        session->belongClass = destUnit;

        destUnit->isAwake = true;

        //이동 클래스에 입장 입력
        session->belongClass->joinQ->Enqueue(info);
        SetEvent(session->belongThread->hEvent);
    }

    LoseSession(session);

    return destUnit == NULL;
}

bool CGameServer::FollowClass(DWORD64 targetID, DWORD64 followID, CPacket* packet)
{
    SESSION* target = AcquireSession(targetID);
    SESSION* follower = AcquireSession(followID);
    MOVE_INFO info;
    bool res;

    InterlockedExchange8(&follower->isMoving, 1);

    do {
        if (target == NULL || follower == NULL) {
            res = false;
            break;
        }

        if (InterlockedIncrement16((short*)&target->belongClass->currentUser) > target->belongClass->maxUser) {
            InterlockedDecrement16((short*)&target->belongClass->currentUser);
            res = false;
            break;
        }

        info.sessionID = followID;
        info.packet = packet;

        follower->belongClass->leaveQ->Enqueue(info);
        InterlockedDecrement16((short*)&follower->belongClass->currentUser);

        follower->belongThread = target->belongThread;
        follower->belongClass = target->belongClass;

        //이동 클래스에 입장 신호
        follower->belongClass->joinQ->Enqueue(info);
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
    int poolIdx;

    if (classPtr->server != nullptr) {
        _FILE_LOG(LOG_LEVEL_ERROR, L"User_Error", L"UnitClass Already Have Another Game Server");
        CRASH();
    }

    classPtr->server = this;
    for (cnt = 0; cnt < tcbCnt; ++cnt) {
        if (wcscmp(tagName, tcbArray[cnt].tagName) != 0) 
            continue;

        unitIdx = InterlockedIncrement16((short*)&tcbArray[cnt].currentUnits) - 1;
        if (unitIdx >= tcbArray[cnt].max_class_unit) {
            InterlockedDecrement16((short*)&tcbArray[cnt].currentUnits);
            continue;
        }

        //기존에 있는 스레드 정보에 부착 성공
        tcbArray[cnt].classList[unitIdx] = classPtr;
       
        return;
    }

    //새로운 tcb의 생성 과정
    tcbIdx = InterlockedIncrement16((short*)&tcbCnt) - 1;
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
    listenSock = WSASocket(AF_INET, SOCK_STREAM, NULL, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (listenSock == INVALID_SOCKET) {
        err = WSAGetLastError();
        return false;
    }
    _LOG(LOG_LEVEL_SYSTEM, L"Server Socket Made");

    //setsockopt
    LINGER optval;
    int sndSize = 65536;

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

    hThreads = new HANDLE[createThreads];
    threadCnt = createThreads;

    hAccept = (HANDLE)_beginthreadex(NULL, 0, AcceptProc, this, NULL, NULL);
    if (hAccept == INVALID_HANDLE_VALUE) {
        _FILE_LOG(LOG_LEVEL_ERROR, L"Server_Error", L"GameServer Thread Create Failed");
        return false;
    }

    hTimer = (HANDLE)_beginthreadex(NULL, 0, TimerProc, this, NULL, NULL);
    if (hTimer == INVALID_HANDLE_VALUE) {
        _FILE_LOG(LOG_LEVEL_ERROR, L"Server_Error", L"GameServer Thread Create Failed");
        return false;
    }


    for (cnt = 0; cnt < createThreads; cnt++) {
        hThreads[cnt] = (HANDLE)_beginthreadex(NULL, 0, WorkProc, this, NULL, NULL);
    }

    for (cnt = 0; cnt < createThreads; cnt++) {
        if (hThreads[cnt] == INVALID_HANDLE_VALUE) {
            _FILE_LOG(LOG_LEVEL_ERROR, L"Server_Error", L"GameServer Thread Create Failed");
            return false;
        }
    }
    _LOG(LOG_LEVEL_SYSTEM, L"NetServer Thread Created");

    return true;
}

void CGameServer::NetClose()
{
    closesocket(listenSock);

    for (int idx = 0; idx < maxConnection; idx++) {
        Disconnect(sessionArr[idx].sessionID);
    }
}

void CGameServer::ThreadClose()
{
    PostQueuedCompletionStatus(hIOCP, 0, 0, (LPOVERLAPPED)&g_serverEnd_overlap);

    WaitForSingleObject(hAccept, INFINITE);
    WaitForSingleObject(hTimer, INFINITE);
    WaitForMultipleObjects(threadCnt, hThreads, true, INFINITE);
}


void CGameServer::HeaderAlloc(CPacket* packet)
{
    if (packet->isEncoded) return;

    GAME_PACKET_HEADER* header = (GAME_PACKET_HEADER*)packet->GetBufferPtr();
    header->staticCode = STATIC_CODE;
    header->len = packet->GetDataSize() - sizeof(GAME_PACKET_HEADER);
    header->randomKey = rand();
    header->checkSum = MakeCheckSum(packet);
}

BYTE CGameServer::MakeCheckSum(CPacket* packet)
{
    BYTE ret = 0;
    char* ptr = packet->GetBufferPtr();
    int len = packet->GetDataSize();
    int cnt = sizeof(GAME_PACKET_HEADER);

    for (cnt; cnt < len; ++cnt) {
        ret += ptr[cnt];
    }

    return ret;
}

void CGameServer::Encode(CPacket* packet)
{
    if (packet->isEncoded) return;

    packet->isEncoded = true;

    GAME_PACKET_HEADER* header = (GAME_PACKET_HEADER*)packet->GetBufferPtr();
    BYTE* ptr = (BYTE*)&header->checkSum;
    BYTE key = STATIC_KEY + 1;
    WORD len = header->len;
    BYTE randKey = header->randomKey + 1;

    WORD cnt;
    WORD cur;

    ptr[0] ^= randKey;
    for (cur = 0, cnt = 1; cnt <= len; ++cur, ++cnt) {
        ptr[cnt] ^= ptr[cur] + randKey + cnt;
    }

    ptr[0] ^= key;
    for (cur = 0, cnt = 1; cnt <= len; ++cur, ++cnt) {
        ptr[cnt] ^= ptr[cur] + key + cnt;
    }
}

void CGameServer::Decode(CPacket* packet)
{
    GAME_PACKET_HEADER* header = (GAME_PACKET_HEADER*)packet->GetBufferPtr();
    BYTE* ptr = (BYTE*)&header->checkSum;
    BYTE key = STATIC_KEY + 1;
    WORD len = header->len;
    BYTE randKey = header->randomKey + 1;

    WORD cnt;

    for (cnt = len; cnt > 0; --cnt) {
        ptr[cnt] ^= ptr[cnt - 1] + key + cnt;
    }
    ptr[0] ^= key;

    for (cnt = len; cnt > 0; --cnt) {
        ptr[cnt] ^= ptr[cnt - 1] + randKey + cnt;
    }
    ptr[0] ^= randKey;
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

    session->sock = sock;

    wmemmove_s(session->IP, 16, IP, 16);
    session->sessionID = *ID = sessionID;

    ZeroMemory(&session->recvOver, sizeof(session->recvOver));
    session->recvOver.type = OV_RECV;
    ZeroMemory(&session->sendOver, sizeof(session->sendOver));
    session->sendOver.type = OV_SEND_FIN;

    session->lastTime = currentTime;

    session->belongClass = g_defaultClass;
    session->belongThread = g_defaultTCB;

    //recv용 ioCount증가
    InterlockedIncrement(&session->ioCnt);
    InterlockedAnd64((__int64*)&session->ioCnt, ~RELEASE_FLAG);

    //iocp match
    h = CreateIoCompletionPort((HANDLE)session->sock, hIOCP, (ULONG_PTR)session, 0);
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
    CUnitClass* classPtr = session->belongClass;
    MOVE_INFO info;

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

    info.packet = NULL;
    info.sessionID = session->sessionID;
    classPtr->disconnectQ->Enqueue(info);

    InterlockedDecrement(&sessionCnt);

    PostQueuedCompletionStatus(hIOCP, 0, (ULONG_PTR)session, (LPOVERLAPPED)&g_disconnect_overlap);
}

unsigned int __stdcall CGameServer::WorkProc(void* arg)
{
    CGameServer* server = (CGameServer*)arg;
    server->_WorkProc();

    return 0;
}

unsigned int __stdcall CGameServer::AcceptProc(void* arg)
{
    CGameServer* server = (CGameServer*)arg;
    server->_AcceptProc();

    return 0;
}

unsigned int __stdcall CGameServer::TimerProc(void* arg)
{
    CGameServer* server = (CGameServer*)arg;
    server->_TimerProc();

    return 0;
}

unsigned int __stdcall CGameServer::UnitProc(void* arg)
{
    TCB_TO_THREAD* info = (TCB_TO_THREAD*)arg;
    CGameServer* server = info->thisPtr;

    server->_UnitProc(info->tcb);

    delete arg;

    return 0;
}

void CGameServer::_WorkProc()
{
    int ret;
    int err;

    DWORD bytes;
    SESSION* session;
    OVERLAPPEDEX* overlap = NULL;

    for(;;) {
        ret = GetQueuedCompletionStatus(hIOCP, &bytes, (PULONG_PTR)&session, (LPOVERLAPPED*)&overlap, INFINITE);

        //gqcs is false and overlap is NULL
        //case totally failed
        if (overlap == NULL) {
            err = WSAGetLastError();
            _FILE_LOG(LOG_LEVEL_ERROR, L"LibraryLog", L"GQCS return NULL ovelap");
            CRASH();
            continue;
        }

        switch (overlap->type) {
        case OV_RECV:
        {
			if (ret == false || bytes == 0) {
				LoseSession(session);
				InterlockedDecrement16(&session->isRecving);
			}
			else
			{
				session->recvQ.MoveRear(bytes);
				InterlockedDecrement16(&session->isRecving);
				RecvProc(session);
				//스레드 깨우기용 이벤트 설정
				SetEvent(session->belongThread->hEvent);
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
            
            InterlockedExchange8((char*)&session->isSending, false);
            
            if (session->sendQ.GetSize() > 0) {
                if (InterlockedExchange8((char*)&session->isSending, true) == false) {
                    SendPost(session);
                }
                else {
                    LoseSession(session);
                }
            }
            else {
                LoseSession(session);
            }
        }
        break;
        case OV_DISCONNECT:
        {
            InterlockedDecrement16((short*)&session->belongClass->currentUser);
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

void CGameServer::_AcceptProc()
{
    SOCKADDR_IN addr;
    SOCKET sock;
    WCHAR IP[16];

    SESSION* session;
    DWORD64 sessionID;
    int addrLen = sizeof(addr);

    while (isServerOn) {
        sock = accept(listenSock, (sockaddr*)&addr, &addrLen);
        ++totalAccept;
        if (sock == INVALID_SOCKET) {
            continue;
        }

        InetNtop(AF_INET, &addr.sin_addr, IP, 16);

        if (OnConnectionRequest(IP, ntohs(addr.sin_port)) == false) {
            closesocket(sock);
            continue;
        }

        if (MakeSession(IP, sock, &sessionID) == false) {
            closesocket(sock);
            continue;
        }

        session = FindSession(sessionID);

        if (OnClientJoin(sessionID) == false) {
            LoseSession(session);
            continue;
        }

        InterlockedIncrement(&sessionCnt);
    }
}

void CGameServer::_TimerProc()
{
    SESSION* session;
    int cnt;

    while (isServerOn) {
        currentTime = timeGetTime();

        for (cnt = 0; cnt < maxConnection; ++cnt) {
            session = &sessionArr[cnt];

            if (session->ioCnt & RELEASE_FLAG) continue;

            if (currentTime - session->lastTime > session->timeOutVal) {
                Disconnect(session->sessionID);
                session->belongClass->OnTimeOut(session->sessionID);
            }
        }

        Sleep(TIMER_PRECISION);
    }

}

void CGameServer::_UnitProc(CUSTOM_TCB* tcb)
{
    CUnitClass* unit;

    int cnt;
    int lim;

    while (isServerOn) {
        WaitForSingleObject(tcb->hEvent, TIMER_PRECISION);
        ResetEvent(tcb->hEvent);

        lim = min(tcb->currentUnits, tcb->max_class_unit);

        for (cnt = 0; cnt < lim; cnt++) {
            unit = tcb->classList[cnt];

            if (unit->isAwake == false) continue;

            //jobQ에 들어온 메세지 처리함수
            unit->MsgUpdate();
            
            if (currentTime - unit->lastTime >= unit->frameDelay) {
                unit->lastTime = currentTime;
                //frameDelay 이상의 시간이 지난 경우 작동
                //frameDelay 미만의 시간이 지난 경우 바로 return
                unit->FrameUpdate();
            }

            UnitJoinLeaveProc(unit);
        }

    }

}

void CGameServer::RecvProc(SESSION* session)
{
	//Packet 떼기 (packetHeader 제거)
	GAME_PACKET_HEADER packetHeader;
	GAME_PACKET_HEADER* header;
	DWORD len;
	CRingBuffer* recvQ = &session->recvQ;
	CPacket* packet;
    CUnitClass* classPtr = session->belongClass;

	WCHAR errText[100];

	session->lastTime = currentTime;
    
    len = recvQ->GetUsedSize();
	for (;;) {
		//길이 판별
		if (sizeof(packetHeader) > len) {
			break;
		}

		//넷헤더 추출
		recvQ->Peek((char*)&packetHeader, sizeof(packetHeader));

        //길이 판별
        if (sizeof(packetHeader) + packetHeader.len > len) {
            break;
        }

		if (packetHeader.len > packetSize) {
			Disconnect(session->sessionID);
			_FILE_LOG(LOG_LEVEL_ERROR, L"LibraryLog", L"Unacceptable Length from %s", session->IP);
			session->belongClass->OnError(-1, L"Unacceptable Length");
			LoseSession(session);
			return;
		}

        packet = PacketAlloc();

		//헤더영역 dequeue
		len -= recvQ->Dequeue((char*)packet->GetBufferPtr(), sizeof(packetHeader) + packetHeader.len);
		packet->MoveWritePos(packetHeader.len);

		if (packetHeader.staticCode != STATIC_CODE) {
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
		header = (GAME_PACKET_HEADER*)packet->GetBufferPtr();
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
		packet->MoveReadPos(sizeof(packetHeader));

        classPtr->OnRecv(session->sessionID, packet);

        InterlockedIncrement64((__int64*)&totalRecv);
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

    if (InterlockedIncrement16(&session->isRecving) != 1) {
        InterlockedDecrement16(&session->isRecving);
        return false;
    }

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
            case 10022:
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
                session->belongClass->OnError(err, L"RecvPost Error");
            }
			LoseSession(session);
            return false;
        }
    }
    else {
        //recv synchronous
    }

    return true;
}

bool CGameServer::SendPost(SESSION* session)
{
    int ret;
    int err;
    WORD cnt;
    int sendCnt = 0;

    CLockFreeQueue<CPacket*>* sendQ;
    CPacket** sendBuf = session->sendBuf;
    CPacket* packet;

    WSABUF pBuf[SEND_PACKET_MAX];

    sendQ = &session->sendQ;
    while (sendCnt == 0) {
        sendCnt = sendQ->GetSize();
    }
    sendCnt = min(sendCnt, SEND_PACKET_MAX);
    session->sendCnt = sendCnt;
    MEMORY_CLEAR(pBuf, sizeof(WSABUF) * SEND_PACKET_MAX);

    for (cnt = 0; cnt < sendCnt; cnt++) {
        sendQ->Dequeue(&packet);
        sendBuf[cnt] = packet;
        pBuf[cnt].buf = packet->GetBufferPtr();
        pBuf[cnt].len = packet->GetDataSize();
    }

    ret = WSASend(session->sock, pBuf, sendCnt, NULL, 0, (LPWSAOVERLAPPED)&session->sendOver, NULL);

    if (ret == SOCKET_ERROR) {
        err = WSAGetLastError();

        if (err == WSA_IO_PENDING) {
            //good
            InterlockedAdd64((__int64*)&totalSend, sendCnt);
        }
        else {
            switch (err) {
            case 10004:
            case 10022:
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
                _FILE_LOG(LOG_LEVEL_ERROR, L"LibraryLog", L"SendPost Error %d || sock %llu || sendCnt %d", err, session->sock, sendCnt);
            }
            InterlockedExchange8((char*)&session->isSending, false);
            LoseSession(session);
            return false;
        }
    }
    else {
        //sent in time
        InterlockedAdd64((__int64*)&totalSend, sendCnt);
    }

    return true;
}

void CGameServer::UnitJoinLeaveProc(CUnitClass* unit)
{
    MOVE_INFO info;
    SESSION* session;

    while (unit->joinQ->Dequeue(&info)) {
        unit->OnClientJoin(info.sessionID, info.packet);
        session = FindSession(info.sessionID);
        RecvPost(session);
        InterlockedExchange8(&session->isMoving, 0);

        if(info.packet)
            PacketFree(info.packet);
    }

    while (unit->leaveQ->Dequeue(&info)) {
        unit->OnClientLeave(info.sessionID);
    }

    while (unit->disconnectQ->Dequeue(&info)) {
        unit->OnClientDisconnected(info.sessionID);
    }

    if (unit->currentUser == 0) {
        
    }
}

bool CGameServer::Start(WCHAR* IP, DWORD port, DWORD createThreads, DWORD runningThreads, bool isNagle, DWORD maxConnect, int packetSize)
{
    if (NetInit(IP, port, isNagle) == false) {
        return false;
    }

    isServerOn = true;
    sessionArr = new SESSION[maxConnect];

    totalAccept = 0;
    sessionCnt = 0;
    maxConnection = maxConnect;

    tcbArray = new CUSTOM_TCB[TCB_MAX];
    //default thread 생성
    TCB_TO_THREAD* arg = new TCB_TO_THREAD{ this, g_defaultTCB };
    _beginthreadex(NULL, 0, UnitProc, arg, 0, NULL);

    this->packetSize = packetSize;
	packetPool = new CTLSMemoryPool<CPacket>;

    for (int cnt = 0; cnt < maxConnect; cnt++) {
        sessionStack.Push(cnt);
    }

    if (ThreadInit(createThreads, runningThreads) == false) {
        isServerOn = false;
        return false;
    }

    return true;
}

void CGameServer::Stop()
{
    OnStop();

    isServerOn = false;

    NetClose();

    ThreadClose();

    delete packetPool;
    delete sessionArr;
    delete hThreads;
}

int CGameServer::GetSessionCount()
{
    return sessionCnt;
}

bool CGameServer::Disconnect(DWORD64 sessionID)
{
    SESSION* session = AcquireSession(sessionID);
    if (session == NULL) {
        return false;
    }

    CancelIoEx((HANDLE)InterlockedExchange64((__int64*)&session->sock, RELEASE_FLAG), NULL);

    if (session->belongClass == g_defaultClass) {
        LoseSession(session);
    }

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

    if (packet->isEncoded == false) {
        HeaderAlloc(packet);
        Encode(packet);
    }
    session->sendQ.Enqueue(packet);

    if (session->isSending == false) {
        if (InterlockedExchange8((char*)&session->isSending, true) == false) {
            PostQueuedCompletionStatus(hIOCP, 0, (ULONG_PTR)session, (LPOVERLAPPED)&g_sendReq_overlap);
        }
        else {
            LoseSession(session);
        }
    }
    else {
        LoseSession(session);
    }
    return true;
}

bool CGameServer::SendAndDisconnect(DWORD64 sessionID, CPacket* packet, DWORD timeOutVal)
{
    SetTimeOut(sessionID, timeOutVal);
    return SendPacket(sessionID, packet);
}

CPacket* CGameServer::PacketAlloc()
{
    CPacket* packet = packetPool->Alloc();
    if (packet->GetBufferSize() != packetSize) {
        packet->~CPacket();
        new (packet)CPacket(packetSize);
    }

    packet->AddRef(1);
    packet->Clear();
    packet->MoveWritePos(sizeof(GAME_PACKET_HEADER));
    packet->isEncoded = false;
    return packet;
}

CPacket* CGameServer::InfoAlloc()
{
    CPacket* packet = packetPool->Alloc();
    
    packet->AddRef(1);
    packet->Clear();

    return packet;
}

void CGameServer::PacketFree(CPacket* packet)
{
    if (packet->SubRef() == 0) {
        packetPool->Free(packet);
    }
}

int CGameServer::GetPacketPoolCapacity()
{
    return packetPool->GetCapacityCount();
}

int CGameServer::GetPacketPoolUse()
{
    return packetPool->GetUseCount();
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

void CGameServer::SetNetMode(NETMODE mode)
{
    netMode = mode;
}

bool CGameServer::IsServerOn()
{
    return isServerOn;
}

DWORD64 CGameServer::GetTotalAccept()
{
    return totalAccept;
}

DWORD64 CGameServer::GetAcceptTPS()
{
    DWORD64 ret = totalAccept - lastAccept;
    lastAccept = totalAccept;
    return ret;
}

DWORD64 CGameServer::GetRecvTPS()
{
    DWORD64 ret = totalRecv - lastRecv;
    lastRecv = totalRecv;
    return ret;
}

DWORD64 CGameServer::GetSendTPS()
{
    DWORD64 ret = totalSend - lastSend;
    lastSend = totalSend;
    return ret;
}

