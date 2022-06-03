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
