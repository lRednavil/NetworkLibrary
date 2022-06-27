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
    //session refCnt�� ����
    alignas(64)
        DWORD64 ioCnt;
    alignas(64)
        short isSending;
    alignas(64)
        short isRecving;
    alignas(64)
        char isMoving;

    //��Ʈ��ũ �޼����� ���۵�
    alignas(64)
        CRingBuffer recvQ;
    alignas(64)
        CLockFreeQueue<CPacket*> sendQ;
    alignas(64)
        SOCKET sock; 

    //timeOut�� ������
    DWORD lastTime;
    DWORD timeOutVal;

    //send �� ������
    CPacket* sendBuf[SEND_PACKET_MAX];

    //monitor
    DWORD sendCnt; // << ���� �޼����� Ȯ��

    //readonly
    alignas(64)
    DWORD64 sessionID;
    WCHAR IP[16];

    //gameServer��
    CUSTOM_TCB* belongThread;
    CUnitClass* belongClass;

    SESSION() {
        ioCnt = RELEASE_FLAG;
        isSending = 0;
    }
};
//���� ���ӽ� ���� ���� Ŭ����
class CDefautClass : public CUnitClass {
    //virtual�Լ� ����
    virtual void OnClientJoin(DWORD64 sessionID, CPacket* packet) {};
    virtual void OnClientLeave(DWORD64 sessionID) {};

    virtual void OnClientDisconnected(DWORD64 sessionID) {};

    //message �м� ����
    //�޼��� ����� �˾Ƽ� ������ ��
    //������Ʈ ������ ó�� �ʿ�� jobQ�� enQ�Ұ�
    virtual void OnRecv(DWORD64 sessionID, CPacket* packet) {};

    virtual void OnTimeOut(DWORD64 sessionID) {};

    virtual void OnError(int error, const WCHAR* msg) {};

    //gameserver��
    //jobQ�� EnQ�� �޼����� ó��
    virtual void MsgUpdate() {};
    //frame������ ������Ʈ ó��
    virtual void FrameUpdate() {};
};

#pragma region UnitClass
CUnitClass::CUnitClass()
{
    joinQ = new CLockFreeQueue<MOVE_INFO>;
    leaveQ = new CLockFreeQueue<MOVE_INFO>;
}
CUnitClass::~CUnitClass()
{
    delete joinQ;
    delete leaveQ;
}
void CUnitClass::InitClass(WORD targetFrame, BYTE endOpt, WORD maxUser)
{
    frameDelay = 1000 / targetFrame;
    endOption = endOpt;
    this->maxUser = maxUser;
}

bool CUnitClass::MoveClass(const WCHAR* className, DWORD64 sessionID, CPacket* packet, WORD classIdx)
{
    return server->MoveClass(className, sessionID, packet, classIdx);
}

//bool CUnitClass::MoveClass(const WCHAR* className, DWORD64* sessionIDs, WORD sessionCnt, WORD classIdx)
//{
//    if (sessionCnt == 0) return false;
//
//    return MoveClass(className, sessionIDs, sessionCnt, classIdx);
//}

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

    //default tcb�� ���� ����
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

    //thread Ž��
    for (tcbIdx = 0; tcbIdx < tcbCnt; ++tcbIdx) {
        if (wcscmp(tagName, tcbArray[tcbIdx].tagName) != 0)
            continue;

        tcb = &tcbArray[tcbIdx];

        //class index�� ������ ���
        if (classIdx != (WORD)-1) {
            if (InterlockedIncrement16((short*)&tcb->classList[classIdx]->currentUser) >= tcb->classList[classIdx]->maxUser) {
                InterlockedDecrement16((short*)&tcb->classList[classIdx]->currentUser);
                break;
            }

            destUnit = tcb->classList[classIdx];
            goto END;
        }
        else {
            //class ��ȸ
            for (unitIdx = 0; unitIdx < tcb->max_class_unit; unitIdx++) {
                //���� ������ �̵� ����
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
        //���� Ŭ������ ���� ��ȣ
        info.sessionID = sessionID;
        info.packet = packet;

        InterlockedExchange8(&session->isMoving, 1);
        session->belongClass->leaveQ->Enqueue(info);
        InterlockedDecrement16((short*)&session->belongClass->currentUser);

        session->belongThread = tcb;
        session->belongClass = destUnit;

        destUnit->isAwake = true;

        //�̵� Ŭ������ ���� �Է�
        session->belongClass->joinQ->Enqueue(info);
        SetEvent(session->belongThread->hEvent);
    }

    LoseSession(session);

    return destUnit == NULL;
}

//bool CGameServer::MoveClass(const WCHAR* tagName, DWORD64* sessionIDs, WORD sessionCnt, WORD classIdx)
//{
//    SESSION** sessionArr = new SESSION * [sessionCnt];
//    WORD sessionIdx;
//    int tcbIdx;
//    WORD unitIdx;
//    CUSTOM_TCB* tcb = NULL;
//    CUnitClass* destUnit = NULL;
//
//    for (sessionIdx = 0; sessionIdx < sessionCnt; sessionIdx++) {
//        sessionArr[sessionIdx] = AcquireSession(sessionIDs[sessionIdx]);
//        if (sessionArr[sessionIdx] == NULL) {
//            delete[] sessionArr;
//            break;
//        }
//    }
//
//    //thread Ž��
//    for (tcbIdx = 0; tcbIdx < tcbCnt; ++tcbIdx) {
//        if (wcscmp(tagName, tcbArray[tcbIdx].tagName) != 0)
//            continue;
//
//        tcb = &tcbArray[tcbIdx];
//
//        //class index�� ������ ���
//        if (classIdx != -1) {
//            if (InterlockedAdd((LONG*)&tcb->classList[classIdx]->currentUser, sessionCnt) >= tcb->classList[classIdx]->maxUser) {
//                InterlockedAdd((LONG*)&tcb->classList[classIdx]->currentUser, -sessionCnt);
//                break;
//            }
//
//            destUnit = tcb->classList[classIdx];
//            destUnit->isAwake = true;
//            goto END;
//        }
//        else {
//            //class ��ȸ
//            for (unitIdx = 0; unitIdx < tcb->max_class_unit; unitIdx++) {
//                if (InterlockedAdd((LONG*)&tcb->classList[classIdx]->currentUser, sessionCnt) >= tcb->classList[classIdx]->maxUser) {
//                    InterlockedAdd((LONG*)&tcb->classList[classIdx]->currentUser, -sessionCnt);
//                    continue;
//                }
//
//                destUnit = tcb->classList[unitIdx];
//                destUnit->isAwake = true;
//                goto END;
//            }
//        }
//    }
//
//END:
//
//    
//	for (sessionIdx = 0; sessionIdx < sessionCnt; sessionIdx++) {
//		if (destUnit != NULL) {
//			sessionArr[sessionIdx]->isMoving = true;
//			//���� Ŭ������ ���� ��ȣ
//			sessionArr[sessionIdx]->belongClass->leaveQ->Enqueue(sessionIDs[sessionIdx]);
//			InterlockedDecrement16((short*)&sessionArr[sessionIdx]->belongClass->currentUser);
//
//			sessionArr[sessionIdx]->belongThread = tcb;
//			sessionArr[sessionIdx]->belongClass = destUnit;
//
//			//�̵� Ŭ������ ���� �Է�
//			sessionArr[sessionIdx]->belongClass->joinQ->Enqueue(sessionIDs[sessionIdx]);
//		}
//
//		LoseSession(sessionArr[sessionIdx]);
//	}
//
//	if (destUnit != NULL)
//		SetEvent(tcb->hEvent);
//
//    return destUnit == NULL;
//}

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

        if (InterlockedIncrement16((short*)&target->belongClass->currentUser) >= target->belongClass->maxUser) {
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

        //�̵� Ŭ������ ���� ��ȣ
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

        //������ �ִ� ������ ������ ���� ����
        tcbArray[cnt].classList[unitIdx] = classPtr;
       
        return;
    }

    //���ο� tcb�� ���� ����
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
    hThreads[2] = (HANDLE)_beginthreadex(NULL, 0, CGameServer::SendThread, this, NULL, NULL);

    for (cnt = ACCEPT_THREAD + TIMER_THREAD + SEND_THREAD; cnt < createThreads + ACCEPT_THREAD + TIMER_THREAD + SEND_THREAD; cnt++) {
        hThreads[cnt] = (HANDLE)_beginthreadex(NULL, 0, CGameServer::WorkProc, this, NULL, NULL);
    }

    for (cnt = 0; cnt < createThreads + ACCEPT_THREAD + TIMER_THREAD + SEND_THREAD; cnt++) {
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

    //���� �������� ��Ȯ��
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

    session->lastTime = currentTime;

    session->belongClass = g_defaultClass;
    session->belongThread = g_defaultTCB;

    //recv�� ioCount����
    InterlockedIncrement(&session->ioCnt);
    InterlockedAnd64((__int64*)&session->ioCnt, ~RELEASE_FLAG);

    //iocp match
    h = CreateIoCompletionPort((HANDLE)session->sock, hIOCP, (ULONG_PTR)sessionID, 0);
    if (h != hIOCP) {
        _FILE_LOG(LOG_LEVEL_ERROR, L"LibraryLog", L"IOCP to SOCKET Failed");
        //crash �뵵
        CRASH();
        return false;
    }


    return true;
}

void CGameServer::ReleaseSession(SESSION* session)
{
    //cas�� �÷��� ��ȯ
    if (InterlockedCompareExchange64((long long*)&session->ioCnt, RELEASE_FLAG, 0) != 0) {
        return;
    }

    SOCKET sock = session->sock;
    int leftCnt;
    CPacket* packet;
    CUnitClass* classPtr = session->belongClass;

    closesocket(sock & ~RELEASE_FLAG);

    //���� Q ��� ����
    while (session->sendQ.Dequeue(&packet))
    {
        classPtr->PacketFree(packet);
    }

    //sendBuffer�� ���� ��� ����
    for (leftCnt = 0; leftCnt < session->sendCnt; ++leftCnt) {
        packet = session->sendBuf[leftCnt];
        classPtr->PacketFree(packet);
    }
    session->sendCnt = 0;

    session->recvQ.ClearBuffer();

    session->isSending = 0;
    InterlockedDecrement(&sessionCnt);

    PostQueuedCompletionStatus(hIOCP, 0, session->sessionID, (LPOVERLAPPED)OV_DISCONNECT);
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

unsigned int __stdcall CGameServer::SendThread(void* arg)
{
    CGameServer* server = (CGameServer*)arg;
    server->_SendThread();

    return 0;
}

unsigned int __stdcall CGameServer::UnitProc(void* arg)
{
    TCB_TO_THREAD* info = (TCB_TO_THREAD*)arg;
    CGameServer* server = info->thisPtr;

    server->_UnitProc(info->tcb);

    return 0;
}

void CGameServer::_WorkProc()
{
    int ret;
    int err;
    DWORD ioCnt;

    DWORD bytes;
    DWORD64 sessionID;
    SESSION* session;
    OVERLAPPEDEX* overlap = NULL;

    while (isServerOn) {
        ret = GetQueuedCompletionStatus(hIOCP, &bytes, (PULONG_PTR)&sessionID, (LPOVERLAPPED*)&overlap, INFINITE);

        //gqcs is false and overlap is NULL
        //case totally failed
        if (overlap == NULL) {
            err = WSAGetLastError();
            _FILE_LOG(LOG_LEVEL_ERROR, L"LibraryLog", L"GQCS return NULL ovelap");
            CRASH();
            continue;
        }

        session = FindSession(sessionID);

        if (session == NULL) {
            continue;
        }
        //disconnect�� ���
        if ((__int64)overlap == OV_DISCONNECT) {
			session->belongClass->OnClientDisconnected(sessionID);
			InterlockedDecrement16((short*)&session->belongClass->currentUser);
			sessionStack.Push(session->sessionID >> MASK_SHIFT);
            continue;
        }

        //recvd
        if (overlap->type == 0) {
            if (ret == false || bytes == 0) {
                LoseSession(session);
                InterlockedDecrement16(&session->isRecving);
            }
            else
            {
                session->recvQ.MoveRear(bytes);
                InterlockedDecrement16(&session->isRecving);
                RecvProc(session);
                //������ ������ �̺�Ʈ ����
                SetEvent(session->belongThread->hEvent);
            }
        }
        //sent
        if (overlap->type == 1) {
            while (session->sendCnt) {
                --session->sendCnt;
                session->belongClass->PacketFree(session->sendBuf[session->sendCnt]);
            }

            if (session->sendQ.GetSize() != 0) {
                AcquireSession(sessionID);
                SendPost(session);
            }
            else {
                InterlockedDecrement16(&session->isSending);
            }
            //�۾� �Ϸῡ ���� lose
            LoseSession(session);
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

        //RecvPost(session);
    }
}

void CGameServer::_TimerProc()
{
    SESSION* session;
    int cnt;
    //�ʱ���� ��������
    currentTime = timeGetTime();
    Sleep(250);

    while (isServerOn) {
        currentTime = timeGetTime();

        for (cnt = 0; cnt < maxConnection; ++cnt) {
            session = &sessionArr[cnt];

            if (session->ioCnt & RELEASE_FLAG) continue;

            if ((DWORD)(currentTime - session->lastTime) >= session->timeOutVal) {
                Disconnect(session->sessionID);
                session->belongClass->OnTimeOut(session->sessionID);
            }
        }

        Sleep(TIMER_PRECISION);
    }

}

void CGameServer::_SendThread()
{
    int lim;
    SESSION* session;
    DWORD64 sessionID;
    while (isServerOn) {
        lim = sendSessionQ.GetSize();
        while (lim) {
            sendSessionQ.Dequeue(&sessionID);
            session = AcquireSession(sessionID);

            if (session != NULL) {
                SendPost(session);
            }
            --lim;
        }

        Sleep(sendLatency);
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

            //jobQ�� ���� �޼��� ó���Լ�
            unit->MsgUpdate();
            
            if (currentTime - unit->lastTime >= unit->frameDelay) {
                unit->lastTime = currentTime;
                //frameDelay �̻��� �ð��� ���� ��� �۵�
                //frameDelay �̸��� �ð��� ���� ��� �ٷ� return
                unit->FrameUpdate();
            }

            UnitJoinLeaveProc(unit);
        }

    }

}

void CGameServer::RecvProc(SESSION* session)
{
	//Packet ���� (packetHeader ����)
	GAME_PACKET_HEADER packetHeader;
	GAME_PACKET_HEADER* header;
	DWORD len;
	CRingBuffer* recvQ = &session->recvQ;
	CPacket* packet;
    CUnitClass* classPtr = session->belongClass;

	WCHAR errText[100];

	session->lastTime = currentTime;

	for (;;) {
		len = recvQ->GetUsedSize();
		//���� �Ǻ�
		if (sizeof(packetHeader) > len) {
			break;
		}

		//����� ����
		recvQ->Peek((char*)&packetHeader, sizeof(packetHeader));

        //���� �Ǻ�
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

		//������� dequeue
		recvQ->Dequeue((char*)packet->GetBufferPtr(), sizeof(packetHeader) + packetHeader.len);
		packet->MoveWritePos(packetHeader.len);

		if (packetHeader.staticCode != STATIC_CODE) {
            PacketFree(packet);
			swprintf_s(errText, L"%s %s", L"Packet Code Error", session->IP);
			_FILE_LOG(LOG_LEVEL_ERROR, L"LibraryLog", L"Packet Code Error from %s", session->IP);
            session->belongClass->OnError(-1, errText);
			//����ڵ� ������ ���� ����
			Disconnect(session->sessionID);
			LoseSession(session);
			return;
		}

		Decode(packet);
		//checksum����
		header = (GAME_PACKET_HEADER*)packet->GetBufferPtr();
		if (header->checkSum != MakeCheckSum(packet)) {
            PacketFree(packet);
			swprintf_s(errText, L"%s %s", L"Packet Code Error", session->IP);
			_FILE_LOG(LOG_LEVEL_ERROR, L"LibraryLog", L"Packet Checksum Error from %s", session->IP);
            session->belongClass->OnError(-1, errText);
			//üũ�� ������ ���� ����
			Disconnect(session->sessionID);
			LoseSession(session);
			return;
		}
		//����� net��� ��ŵ
		packet->MoveReadPos(sizeof(packetHeader));

        classPtr->OnRecv(session->sessionID, packet);
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

    ret = WSARecv(InterlockedAdd64((__int64*)&session->sock, 0), pBuf, 2, NULL, &flag, (LPWSAOVERLAPPED)&session->recvOver, NULL);

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
                session->belongClass->OnError(err, L"RecvPost Error");
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

    sendQ = &session->sendQ;
    sendCnt = sendQ->GetSize();

    session->sendCnt = min(SEND_PACKET_MAX, sendCnt);
    ZeroMemory(pBuf, sizeof(WSABUF) * SEND_PACKET_MAX);

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
}

bool CGameServer::Start(WCHAR* IP, DWORD port, DWORD createThreads, DWORD runningThreads, bool isNagle, DWORD maxConnect, int sendLatency, int packetSize)
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
    //default thread ����
    TCB_TO_THREAD* arg = new TCB_TO_THREAD{ this, g_defaultTCB };
    _beginthreadex(NULL, 0, UnitProc, arg, 0, NULL);

    this->sendLatency = sendLatency;
    this->packetSize = packetSize;
    if (packetSize != CPacket::eBUFFER_DEFAULT) {
        packetPool = &g_PacketPool;
    }
    else {
        packetPool = new CTLSMemoryPool<CPacket>;
    }

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

bool CGameServer::Disconnect(DWORD64 sessionID)
{
    SESSION* session = AcquireSession(sessionID);
    if (session == NULL) {
        return false;
    }

    CancelIoEx((HANDLE)InterlockedOr64((__int64*)&session->sock, RELEASE_FLAG), NULL);

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

    if (InterlockedIncrement16(&session->isSending) == 1) {
        sendSessionQ.Enqueue(sessionID);
    }
    else {
        InterlockedDecrement16(&session->isSending);
    }

    LoseSession(session);
    return true;
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

