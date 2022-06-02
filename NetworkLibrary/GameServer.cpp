#include "pch.h"
#include "GameServer.h"
#include "GameCommon.h"

class CTest : public CUnitClass {

};

inline bool CUnitClass::SendPacket(DWORD64 sessionID, CPacket* packet)
{
    return server->SendPacket(sessionID, packet);
}

inline CPacket* CUnitClass::PacketAlloc()
{
    return g_PacketPool.Alloc();
}

void CGameServer::Attatch(const WCHAR* tagName, CUnitClass* const classPtr, const WORD maxUnitCnt)
{
    int cnt;
    WORD tcbIdx;
    WORD unitIdx;

    for (cnt = 0; cnt < tcbCnt; ++cnt) {
        if (wcscmp(tagName, tcbArray[cnt].tagName) != 0) 
            continue;

        unitIdx = InterlockedIncrement16((short*)&tcbArray[cnt].currentUnits);
        if (unitIdx >= tcbArray[cnt].max_class_unit) {
            InterlockedDecrement16((short*)&tcbArray[cnt].currentUnits);
            continue;
        }

        //������ �ִ� ������ ������ ���� ����
        tcbArray[cnt].classList[unitIdx] = classPtr;
        return;
    }

    //���ο� tcb�� ���� ����
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
        //disconnect�� ���
        if ((__int64)overlap == OV_DISCONNECT) {
            session->belongClass->OnClientLeave(sessionID);
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
            if (session->isDisconnectReserved) {
                server->Disconnect(sessionID);
            }
            else if (ret != false) {
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

            //jobQ�� ���� �޼��� ó���Լ�
            //unit->MessageProc();
            
            //frameDelay �̻��� �ð��� ���� ��� �۵�
            //frameDelay �̸��� �ð��� ���� ��� �ٷ� return
            unit->Update();
        }
    }

    return 0;
}

void CGameServer::RecvProc(SESSION* session)
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
			_FILE_LOG(LOG_LEVEL_ERROR, L"LibraryLog", L"Unacceptable Length from %s", session->IP);
			session->belongClass->OnError(-1, L"Unacceptable Length");
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
			_FILE_LOG(LOG_LEVEL_ERROR, L"LibraryLog", L"Packet Code Error from %s", session->IP);
            session->belongClass->OnError(-1, errText);
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
			_FILE_LOG(LOG_LEVEL_ERROR, L"LibraryLog", L"Packet Checksum Error from %s", session->IP);
            session->belongClass->OnError(-1, errText);
			//üũ�� ������ ���� ����
			Disconnect(session->sessionID);
			LoseSession(session);
			return;
		}
		//����� net��� ��ŵ
		packet->MoveReadPos(sizeof(netHeader));

        session->belongClass->OnRecv(session->sessionID, packet);
	}

	RecvPost(session);
}
