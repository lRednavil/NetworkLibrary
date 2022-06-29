#pragma once

struct SESSION;
class CPacket;

class CProcessMonitor;
class CProcessorMonitor;

class CGameServer;

struct MOVE_INFO;

//상속받아서 구현 및 사용
class CUnitClass{
	friend class CGameServer;

public:
	enum END_OPTION {
		OPT_DESTROY,
		OPT_STOP,
		OPT_RESET
	};

public:
	CUnitClass();
	virtual ~CUnitClass();

	void InitClass(WORD targetFrame, BYTE endOpt, WORD maxUser);
	
	//server참조 함수들
	
	//1인 이동용
	//전해야 하는 정보가 있을 경우 packet을 통해 전달할 것
	bool MoveClass(const WCHAR* className, DWORD64 sessionID, CPacket* packet = NULL, WORD classIdx = -1);
	//다수 이동용
	//bool MoveClass(const WCHAR* className, DWORD64* sessionIDs, WORD sessionCnt, WORD classIdx = -1);
	bool FollowClass(DWORD64 targetID, DWORD64 followID, CPacket* packet = NULL);

	bool Disconnect(DWORD64 sessionID);
	bool SendPacket(DWORD64 sessionID, CPacket* packet);

	//기본 참조카운트 1부여 및 초기화 실행
	CPacket* PacketAlloc();
	CPacket* InfoAlloc();
	void	PacketFree(CPacket* packet);
	int		GetPacketPoolCapacity();
	int		GetPacketPoolUse();

	void SetTimeOut(DWORD64 sessionID, DWORD timeVal);

	//virtual함수 영역
	//packet이 있는 경우 사용만 하고 해제하지 말 것
	virtual void OnClientJoin(DWORD64 sessionID, CPacket* packet) = 0;
	//packet이 있는 경우 사용만 하고 해제하지 말 것
	virtual void OnClientLeave(DWORD64 sessionID) = 0;

	virtual void OnClientDisconnected(DWORD64 sessionID) = 0;

	//message 분석 역할
	//메세지 헤더는 알아서 검증할 것
	//업데이트 스레드 처리 필요시 jobQ에 enQ할것
	virtual void OnRecv(DWORD64 sessionID, CPacket* packet) = 0;

	virtual void OnTimeOut(DWORD64 sessionID) = 0;

	virtual void OnError(int error, const WCHAR* msg) = 0;

	//gameserver용
	//jobQ에 EnQ된 메세지들 처리
	virtual void MsgUpdate() = 0;
	//frame단위의 업데이트 처리
	virtual void FrameUpdate() = 0;

private:
	//classInfos
	bool isAwake = false;
	WORD currentUser = 0;
	WORD maxUser;
	DWORD lastTime;
	
	//readonly
	alignas(64)
	CLockFreeQueue<MOVE_INFO>* joinQ;
	CLockFreeQueue<MOVE_INFO>* leaveQ;
	WORD frameDelay; //1초 / targetFrame
	BYTE endOption;
	CGameServer* server = nullptr;
};

struct CUSTOM_TCB {
	//thread를 tagName에 따라서 생성
	WCHAR tagName[128];
	WORD max_class_unit;
	WORD currentUnits;
	CUnitClass** classList;
	//스레드 깨우기용
	HANDLE hEvent;
};

struct TCB_TO_THREAD {
	CGameServer* thisPtr;
	CUSTOM_TCB* tcb;
};


class CGameServer
{
private:
	enum {
		TAG_NAME_MAX = 128
	};

public:
	CGameServer();
	virtual ~CGameServer();

	//오픈 IP / 포트 / 워커스레드 수(생성수, 러닝수) / 나글옵션 / 최대접속자 수
	bool Start(WCHAR* IP, DWORD port, DWORD createThreads, DWORD runningThreads, bool isNagle, DWORD maxConnect, int sendLatency, int packetSize = CPacket::eBUFFER_DEFAULT);
	void Stop();

	int GetSessionCount();

	bool Disconnect(DWORD64 sessionID);
	bool SendPacket(DWORD64 sessionID, CPacket* packet);

	//기본 참조카운트 1부여 및 초기화 실행
	CPacket* PacketAlloc();
	CPacket* InfoAlloc();
	void	PacketFree(CPacket* packet);
	int		GetPacketPoolCapacity();
	int		GetPacketPoolUse();

	void SetTimeOut(DWORD64 sessionID, DWORD timeVal);

	bool IsServerOn();
	DWORD64 GetTotalAccept();
	DWORD64 GetAcceptTPS();
	DWORD64 GetRecvTPS();
	DWORD64 GetSendTPS();

	//virtual함수 영역
	//시동함수 작성용
	virtual void Init() = 0;
	//accept 직후, IP filterinig 등의 목적
	virtual bool OnConnectionRequest(WCHAR* IP, DWORD Port) = 0;
	//return false; 시 클라이언트 거부.
	//return true; 시 접속 허용
	virtual bool OnClientJoin(DWORD64 sessionID) = 0;
	virtual bool OnClientLeave(DWORD64 sessionID) = 0;

	//종료함수 작성용
	virtual void OnStop() = 0;
	
	//gameServer용 함수
	bool MoveClass(const WCHAR* tagName, DWORD64 sessionID, CPacket* packet = NULL, WORD classIdx = -1);
	//bool MoveClass(const WCHAR* tagName, DWORD64* sessionIDs, WORD sessionCnt, WORD classIdx = -1);
	bool FollowClass(DWORD64 targetID, DWORD64 followID, CPacket* packet = NULL);

	//같은 tagName의 tcb존재시 유효여부 판단 후 부착 or 새로운 tcb 생성 및 스레드 생성
	void AttatchClass(const WCHAR* tagName, CUnitClass* const classPtr, const WORD maxUnitCnt = 1);

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
	static unsigned int __stdcall SendThread(void* arg);
	//업데이트 스레드에 해당하는 함수
	static unsigned int __stdcall UnitProc(void* arg);

	void _WorkProc();
	void _AcceptProc();
	void _TimerProc();
	void _SendThread();
	void _UnitProc(CUSTOM_TCB* tcb);

	void RecvProc(SESSION* session);
	bool RecvPost(SESSION* session);
	bool SendPost(SESSION* session);

	//Class이동 보충용 함수
	void UnitJoinLeaveProc(CUnitClass* unit);

protected:
	//sessionID 겸용
	DWORD64 totalAccept = 0;
	alignas(64)
		DWORD64 totalSend = 0;
	alignas(64)
		DWORD64 totalRecv = 0;
	//tps측정용 기억
	DWORD64 lastAccept = 0;
	DWORD64 lastSend = 0;
	DWORD64 lastRecv = 0;

	//시간 기억
	DWORD currentTime;
	
	//gameserver전용 컨테이너
	CUSTOM_TCB* tcbArray;
	DWORD tcbCnt = 0;

private:
	//array for session
	SESSION* sessionArr;
	//stack for session index
	CLockFreeStack<int> sessionStack;
	CLockFreeQueue<DWORD64> sendSessionQ;

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
	CTLSMemoryPool<CPacket>* packetPool;
	int packetSize;
	int sendLatency;

	HANDLE* hThreads;
};

