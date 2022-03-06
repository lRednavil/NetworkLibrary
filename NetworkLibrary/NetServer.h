#pragma once
class CNetServer
{
public:
	//오픈 IP / 포트 / 워커스레드 수(생성수, 러닝수) / 나글옵션 / 최대접속자 수
	bool Start(WCHAR* IP, DWORD port, DWORD createThreads, DWORD runningThreads, bool isNagle, DWORD maxConnect);
	void Stop();
	int GetSessionCount();

	bool Disconnect(DWORD64 SessionID);
	bool SendPacket(DWORD64 SessionID, CPacket* packet);

	virtual bool OnConnectionRequest(WCHAR* IP, DWORD Port) = 0; //< accept 직후
	//return false; 시 클라이언트 거부.
	//return true; 시 접속 허용
	virtual void OnClientJoin() = 0;
	virtual void OnClientLeave() = 0;
	
	virtual void OnRecv(DWORD64 sessionID, CPacket* packet) = 0;

	virtual void OnError(int error, WCHAR* msg) = 0;
	
private:
	bool NetInit(WCHAR* IP, DWORD port, bool isNagle);
	bool ThreadInit(const DWORD createThreads, const DWORD runningThreads);

	void NetClose();
	void ThreadClose();

	void AcceptProc();
	void RecvProc();



private:
	struct OVERLAPPEDEX {
		OVERLAPPED overlap;
		WORD type;
	};

	struct SESSION {
		OVERLAPPEDEX recvOver;
		OVERLAPPEDEX sendOver;
		DWORD ioCnt;
		bool isSending;
		SRWLOCK sessionLock;
		CRingBuffer recvQ;
		CRingBuffer sendQ;

		//monitor
		DWORD sendCnt; // << 보낸 메세지수 확보
		DWORD sessionCnt;

		//readonly
		SOCKET sock;
		WCHAR IP[16];
		WORD netMode; // << 나중에 화이트리스트 모드 등등 변경용
	};


private:
	SOCKET listenSock;
	HANDLE hIOCP;
	
	HANDLE* hThreads;
	SESSION* sessionArr;

	int lastError;

};

