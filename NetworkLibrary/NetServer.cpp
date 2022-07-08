#include "pch.h"
#include "NetServer.h"
#include "NetCommon.h"
#include <timeapi.h>

struct SESSION {
	OVERLAPPEDEX recvOver;
	OVERLAPPEDEX sendOver;

	//session refCnt의 역할
	alignas(64)
		DWORD64 ioCnt;
	alignas(64)
		bool isSending;
	//send 후 해제용
	CPacket* sendBuf[SEND_PACKET_MAX];
	//monitor
	DWORD sendCnt; // << 보낸 메세지수 확보

	//네트워크 메세지용 버퍼들
	alignas(64)
		CRingBuffer recvQ;
	alignas(64)
		CLockFreeQueue<CPacket*> sendQ;
	alignas(64)
		SOCKET sock;

	//readonly
	alignas(64)
		DWORD64 sessionID;

	//timeOut용 변수들
	DWORD lastTime;
	DWORD timeOutVal;

	WCHAR IP[16];

	SESSION() {
		ioCnt = RELEASE_FLAG;
		timeOutVal = 1000;
	}
};

//#define PROFILE_MODE
#include "TimeTracker.h"
CNetServer::CNetServer()
{
}
CNetServer::~CNetServer()
{
	Stop();
}

bool CNetServer::Start(const WCHAR * IP, DWORD port, DWORD createThreads, DWORD runningThreads, bool isNagle, DWORD maxConnect, int packetSize)
{
	if (isServerOn) {
		return false;
	}

	if (NetInit(IP, port, isNagle) == false) {
		return false;
	}

	isServerOn = true;
	sessionArr = new SESSION[maxConnect];

	totalAccept = 0;
	sessionCnt = 0;
	maxConnection = maxConnect;
	
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

void CNetServer::Stop()
{
	OnStop();

	isServerOn = false;
	
	NetClose();

	ThreadClose();
	
	delete packetPool;
	delete sessionArr;
	delete hThreads;
}

int CNetServer::GetSessionCount()
{
	return sessionCnt;
}

bool CNetServer::Disconnect(DWORD64 sessionID)
{
	SESSION* session = AcquireSession(sessionID);
	if (session == NULL) {
		return false;
	}

	CancelIoEx((HANDLE)InterlockedExchange64((__int64*)&session->sock, session->sock | RELEASE_FLAG), NULL);

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

	if (packet->isEncoded == false) {
		HeaderAlloc(packet);
		Encode(packet);
	}
	session->sendQ.Enqueue(packet);

	if (InterlockedExchange8((char*)&session->isSending, true) == false) {
		PostQueuedCompletionStatus(hIOCP, 0, (ULONG_PTR)session, (LPOVERLAPPED)&g_sendReq_overlap);
	}
	else {
		LoseSession(session);
	}

	return true;
}

void CNetServer::SendPacketToAll(CPacket* packet)
{
	int idx;
	SESSION* session;

	packet->AddRef(maxConnection);
	for (idx = 0; idx < maxConnection; idx++) {
		SendPacket(sessionArr[idx].sessionID, packet);
	}
	PacketFree(packet);
}

bool CNetServer::SendAndDisconnect(DWORD64 sessionID, CPacket* packet, DWORD timeOutVal)
{
	SetTimeOut(sessionID, timeOutVal, true);
	return SendPacket(sessionID, packet);
}

CPacket* CNetServer::PacketAlloc()
{
	CPacket* packet = packetPool->Alloc();
	if (packet->GetBufferSize() != packetSize) {
		packet->~CPacket();
		new (packet)CPacket(packetSize);
	}
	packet->AddRef(1);
	packet->Clear();
	packet->MoveWritePos(sizeof(NET_HEADER));
	packet->isEncoded = false;
	return packet;
}

void CNetServer::HeaderAlloc(CPacket* packet)
{
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
	packet->isEncoded = true;

	NET_HEADER* header = (NET_HEADER*)packet->GetBufferPtr();
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

void CNetServer::Decode(CPacket* packet)
{
	NET_HEADER* header = (NET_HEADER*)packet->GetBufferPtr();
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

void CNetServer::PacketFree(CPacket* packet)
{
	if (packet->SubRef() == 0) {
		packetPool->Free(packet);
	}
}

int CNetServer::GetPacketPoolCapacity()
{
	return packetPool->GetCapacityCount();
}

int CNetServer::GetPacketPoolUse()
{
	return packetPool->GetUseCount();
}

void CNetServer::SetTimeOut(DWORD64 sessionID, DWORD timeVal, bool recvTimeReset)
{
	SESSION* session = AcquireSession(sessionID);

	if (session == NULL) {
		return;
	}

	session->timeOutVal = timeVal;
	if (recvTimeReset) session->lastTime = currentTime;
	LoseSession(session);
}

void CNetServer::SetNetMode(NETMODE mode)
{
	netMode = mode;
}

bool CNetServer::NetInit(const WCHAR* IP, DWORD port, bool isNagle)
{
	int ret;
	int err;

	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);

	//socket
	listenSock = socket(AF_INET, SOCK_STREAM, NULL);
	if (listenSock == INVALID_SOCKET) {
		err = WSAGetLastError();
		_FILE_LOG(LOG_LEVEL_ERROR, L"LibraryErrorLog", L"Listen Socket Error %d", err);
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

	_FILE_LOG(LOG_LEVEL_SYSTEM, L"LibraryLog", L"Server Net Init");
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

void CNetServer::NetClose()
{
	closesocket(listenSock);

	for (int idx = 0; idx < maxConnection; idx++) {
		Disconnect(sessionArr[idx].sessionID);
	}
}

void CNetServer::ThreadClose()
{
	PostQueuedCompletionStatus(hIOCP, 0, 0, (LPOVERLAPPED)&g_serverEnd_overlap);

	WaitForSingleObject(hAccept, INFINITE);
	WaitForSingleObject(hTimer, INFINITE);
	WaitForMultipleObjects(threadCnt, hThreads, true, INFINITE);

	CloseHandle(hAccept);
	CloseHandle(hTimer);
	for (int idx = 0; idx < threadCnt; idx++) {
		CloseHandle(hThreads[idx]);
	}
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
		_FILE_LOG(LOG_LEVEL_SYSTEM, L"LibraryLog", L"All Session is in Use");
		OnError(-1, L"All Session is in Use");
		return false;
	}

	sessionID = (totalAccept & SESSION_MASK);
	sessionID |= ((__int64)sessionID_high << MASK_SHIFT);

	session = &sessionArr[sessionID_high];

	session->sock = sock;

	wmemmove_s(session->IP, 16, IP, 16);
	session->sessionID = *ID = sessionID;

	MEMORY_CLEAR(&session->recvOver, sizeof(session->recvOver));
	session->recvOver.type = OV_RECV;
	MEMORY_CLEAR(&session->sendOver, sizeof(session->sendOver));
	session->sendOver.type = OV_SEND_FIN;

	session->lastTime = currentTime;

	//iocp match
	h = CreateIoCompletionPort((HANDLE)session->sock, hIOCP, (ULONG_PTR)session, 0);
	if (h != hIOCP) {
		_FILE_LOG(LOG_LEVEL_ERROR, L"LibraryLog", L"IOCP to SOCKET Failed");
		OnError(-1, L"IOCP to SOCKET Failed");
		//crash 용도
		CRASH();
		return false;
	}

	//recv용 ioCount증가
	InterlockedIncrement(&session->ioCnt);
	InterlockedAnd64((__int64*)&session->ioCnt, ~RELEASE_FLAG);

	return true;
}

void CNetServer::ReleaseSession(SESSION* session)
{
	//cas로 플래그 전환
	if (InterlockedCompareExchange64((long long*)&session->ioCnt, RELEASE_FLAG, 0) != 0) {
		return;
	}

	int leftCnt;
	CPacket* packet;

	closesocket(session->sock & ~RELEASE_FLAG);

	//남은 Q 찌꺼기 제거
	while (session->sendQ.Dequeue(&packet))
	{
		PacketFree(packet);
	}

	//sendBuffer에 남은 찌꺼기 제거
	leftCnt = session->sendCnt;
	session->sendCnt = 0;
	while(leftCnt){
		--leftCnt;
		packet = session->sendBuf[leftCnt];
		PacketFree(packet);
	}

	session->isSending = false;
	session->recvQ.ClearBuffer();

	InterlockedDecrement(&sessionCnt);

	PostQueuedCompletionStatus(hIOCP, 0, (ULONG_PTR)session, (LPOVERLAPPED)&g_disconnect_overlap);
	
}

unsigned int __stdcall CNetServer::WorkProc(void* arg)
{
	CNetServer* server = (CNetServer*)arg;
	server->_WorkProc();

	return 0;
}

unsigned int __stdcall CNetServer::AcceptProc(void* arg)
{
	CNetServer* server = (CNetServer*)arg;
	server->_AcceptProc();

	return 0;
}

unsigned int __stdcall CNetServer::TimerProc(void* arg)
{
	CNetServer* server = (CNetServer*)arg;
	server->_TimerProc();

	return 0;
}

void CNetServer::_WorkProc()
{
	int ret;
	int err;
	DWORD ioCnt;

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
			OnError(err, L"GQCS return NULL ovelap");

			continue;
		}

		switch (overlap->type) {
		case OV_RECV:
		{
			if (ret == false || bytes == 0) {
				LoseSession(session);
			}
			else
			{
				session->recvQ.MoveRear(bytes);
				RecvProc(session);
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

			if (session->sendQ.GetSize() > 0) {
				SendPost(session);
			}
			else {
				InterlockedExchange8((char*)&session->isSending, false);
				//작업 완료에 대한 lose
				LoseSession(session);
			}
		}
		break;
		case OV_DISCONNECT:
		{
			OnClientLeave(session->sessionID);
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

void CNetServer::_AcceptProc()
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

		RecvPost(session);
	}

}

void CNetServer::_TimerProc()
{
	SESSION* session;
	DWORD cnt;

	while (isServerOn) {
		currentTime = timeGetTime();

		for (cnt = 0; cnt < maxConnection; ++cnt) {
			session = &sessionArr[cnt];

			if (session->ioCnt & RELEASE_FLAG) continue;

			if (currentTime - session->lastTime > session->timeOutVal) {
				Disconnect(session->sessionID);
				OnTimeOut(session->sessionID);
			}
		}
		Sleep(1000);
	}
}

void CNetServer::RecvProc(SESSION* session)
{
	//Packet 떼기 (netHeader 제거)
	NET_HEADER netHeader;
	NET_HEADER* header;
	DWORD len;
	CRingBuffer* recvQ = &session->recvQ;
	CPacket* packet;

	WCHAR errText[100];

	session->lastTime = currentTime;
	
	len = recvQ->GetUsedSize();
	for (;;) {
		//길이 판별
		if (sizeof(netHeader) > len) {
			break;
		}

		//넷헤더 추출
		recvQ->Peek((char*)&netHeader, sizeof(netHeader));
		//길이 판별
		if (sizeof(netHeader) + netHeader.len > len) {
			break;
		}

		//공격 방어용도
		if (netHeader.len > packetSize) {
			Disconnect(session->sessionID);
			_FILE_LOG(LOG_LEVEL_ERROR, L"LibraryLog", L"Unacceptable Length from %s", session->IP);
			OnError(-1, L"Unacceptable Length");
			LoseSession(session);
			return;
		}
		
		packet = PacketAlloc();

		//헤더영역 dequeue
		len -= recvQ->Dequeue((char*)packet->GetBufferPtr(), sizeof(netHeader) + netHeader.len);
		packet->MoveWritePos(netHeader.len);

		if (netHeader.staticCode != STATIC_CODE) {
			PacketFree(packet);
			swprintf_s(errText, L"%s %s", L"Packet Code Error", session->IP);
			_FILE_LOG(LOG_LEVEL_ERROR, L"LibraryLog", L"Packet Code Error from %s", session->IP);
			OnError(-1, errText);
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
			OnError(-1, errText);
			//체크섬 변조시 접속 제거
			Disconnect(session->sessionID);
			LoseSession(session);
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
	CPacket** sendBuf = session->sendBuf;
	CPacket* packet;

	WSABUF pBuf[SEND_PACKET_MAX];

	sendQ = &session->sendQ;
	sendCnt = min(sendQ->GetSize(), SEND_PACKET_MAX);
	if (session->sendCnt > 0) {
		CRASH();
	}

	session->sendCnt = sendCnt;
	MEMORY_CLEAR(pBuf, sizeof(WSABUF) * SEND_PACKET_MAX);

	for (cnt = 0; cnt < sendCnt; ++cnt) {
		sendQ->Dequeue(&packet);
		session->sendBuf[cnt] = packet;
		pBuf[cnt].buf = packet->GetBufferPtr();
		pBuf[cnt].len = packet->GetDataSize();
	}

	ret = WSASend(session->sock, pBuf, sendCnt, NULL, 0, (LPWSAOVERLAPPED)&session->sendOver, NULL);

	if (ret == SOCKET_ERROR) {
		err = WSAGetLastError();

		if (err == WSA_IO_PENDING || err == 0) {
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
				_FILE_LOG(LOG_LEVEL_ERROR, L"LibraryLog", L"SendPost Error %d", err);
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

DWORD64 CNetServer::GetTotalAccept()
{
	return totalAccept;
}

DWORD64 CNetServer::GetAcceptTPS()
{
	DWORD64 ret = totalAccept - lastAccept;
	lastAccept = totalAccept;
	return ret;
}
