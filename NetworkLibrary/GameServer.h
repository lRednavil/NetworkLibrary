#pragma once
#pragma once
//struct SESSION;
struct OVERLAPPEDEX {
	OVERLAPPED overlap;
	WORD type;
};


struct SESSION {
	OVERLAPPEDEX recvOver;
	OVERLAPPEDEX sendOver;
	//session refCnt의 역할
	DWORD64 ioCnt;
	bool isSending;

	//네트워크 메세지용 버퍼들
	CRingBuffer recvQ;
	CLockFreeQueue<CPacket*> sendQ;
	DWORD64 sessionID;

	//timeOut용 변수들
	DWORD lastTime;
	DWORD timeOutVal;

	//send 후 해제용
	//CPacket* sendBuf[SEND_PACKET_MAX];
	//monitor
	DWORD sendCnt; // << 보낸 메세지수 확보

	//readonly
	SOCKET sock;
	WCHAR IP[16];
	
	//gameServer용
	CUnitClass* belongClass;

	SESSION() {
		//ioCnt = RELEASE_FLAG;
	}
};

class CUnitClass {
	bool Disconnect(DWORD64 sessionID);
	bool SendPacket(DWORD64 sessionID, CPacket* packet);

	//기본 참조카운트 1부여 및 초기화 실행
	CPacket* PacketAlloc();
	void	PacketFree(CPacket* packet);

	void SetTimeOut(DWORD64 sessionID, DWORD timeVal);

	//accept 직후, IP filterinig 등의 목적
	virtual bool OnConnectionRequest(WCHAR* IP, DWORD Port) = 0;
	//return false; 시 클라이언트 거부.
	//return true; 시 접속 허용
	virtual bool OnClientJoin(DWORD64 sessionID) = 0;
	virtual bool OnClientLeave(DWORD64 sessionID) = 0;

	//message 분석 역할
	//메세지 헤더는 알아서 검증할 것
	virtual void OnRecv(DWORD64 sessionID, CPacket* packet) = 0;

	virtual void OnTimeOut(DWORD64 sessionID) = 0;

	virtual void OnError(int error, const WCHAR* msg) = 0;

	//gameserver용
	virtual void Update() = 0;

	//classInfos
	//bool isAwake
	//WORD targetFrame
	//DWORD lastTime
	//SESSION_LIST


};

struct CUSTOM_TCB {
	//thread를 tagName에 따라서 생성
	WCHAR tagName[64];
	WORD MAX_CLASS_UNIT;
	WORD currentUnits;
	CUnitClass* classList;
};

//_beginthreadex를 할거면 부를 함수 정하기 -> Attatch
//

///
/// Attatch(const WCHAR* tagName){
///		if(tagName.Exists == false) goto begin;
///		
///		if(TCB.currentUnits < MAX_CLASS_UNIT){
///			classList[++currentUnits] = this;
///			
///			return;
///		}
/// }
///


class CPacket;

class CGameServer
{
public:
	//오픈 IP / 포트 / 워커스레드 수(생성수, 러닝수) / 나글옵션 / 최대접속자 수
	bool Start(WCHAR* IP, DWORD port, DWORD createThreads, DWORD runningThreads, bool isNagle, DWORD maxConnect);
	void Stop();
	int GetSessionCount();
	//모니터링용 함수
	void Monitor();

	bool Disconnect(DWORD64 sessionID);
	bool SendPacket(DWORD64 sessionID, CPacket* packet);

	//기본 참조카운트 1부여 및 초기화 실행
	CPacket* PacketAlloc();
	void	PacketFree(CPacket* packet);

	void SetTimeOut(DWORD64 sessionID, DWORD timeVal);

private:
	bool NetInit(WCHAR* IP, DWORD port, bool isNagle);
	bool ThreadInit(const DWORD createThreads, const DWORD runningThreads);

	void NetClose();
	void ThreadClose();

	//packet에 header 할당
	void HeaderAlloc(CPacket* packet);

	//send시 체크섬 작성
	//recv시 체크섬 검증
	BYTE MakeCheckSum(CPacket* packet);
	void Encode(CPacket* packet);
	void Decode(CPacket* packet);

	//return NULL for fail
	//FindSession + Check Flag + Session 재확인
	//반드시 LoseSession과 페어를 맞출 것
	SESSION* AcquireSession(DWORD64 sessionID);
	//ioCnt 차감 이후 Release 세션 진입
	//반드시 AcquireSession 이나 ioCnt 증가 후에 사용
	void LoseSession(SESSION* session);

	SESSION* FindSession(DWORD64 sessionID);
	bool	MakeSession(WCHAR* IP, SOCKET sock, DWORD64* ID);
	void	ReleaseSession(SESSION* session);

	static unsigned int __stdcall WorkProc(void* arg);
	static unsigned int __stdcall AcceptProc(void* arg);
	static unsigned int __stdcall TimerProc(void* arg);
	void RecvProc(SESSION* session);
	bool RecvPost(SESSION* session);
	bool SendPost(SESSION* session);

protected:
	//sessionID 겸용
	DWORD64 totalAccept = 0;
	DWORD64 totalSend = 0;
	DWORD64 totalRecv = 0;
	DWORD64 totalRelease = 0;
	//tps측정용 기억
	DWORD64 lastAccept = 0;
	DWORD64 lastSend = 0;
	DWORD64 lastRecv = 0;
	DWORD64 lastRelease = 0;

	DWORD64 recvBytes = 0;
	DWORD64 sendBytes = 0;

	//시간 기억
	DWORD currentTime;

private:
	//array for session
	SESSION* sessionArr;
	//stack for session index
	CLockFreeStack<int> sessionStack;

	//monitor
	DWORD sessionCnt;
	DWORD maxConnection;
	BYTE netMode; // << 나중에 화이트리스트 모드 등등 변경용
	bool isServerOn;

	CProcessMonitor* myMonitor;
	CProcessorMonitor* totalMonitor;

	//readonly
	SOCKET listenSock;
	HANDLE hIOCP;

	HANDLE* hThreads;
};

