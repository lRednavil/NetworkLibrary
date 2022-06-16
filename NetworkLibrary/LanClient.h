#pragma once
struct SESSION;
struct CLIENT;

class CLanClient
{
public:
	bool Start(bool isNagle);
	void Stop();

	bool Connect(const WCHAR* connectName, const WCHAR* IP, const DWORD port);
	bool Disconnect(const WCHAR* connectName);
	
	bool SendPacket(const WCHAR* connectName, CPacket* packet);

	//기본 참조카운트 1부여 및 초기화 실행
	CPacket* PacketAlloc();
	void	PacketFree(CPacket* packet);
	
	//시동함수 작성용
	virtual void Init() = 0;
	//accept 직후, IP filterinig 등의 목적
	virtual bool OnConnect() = 0;
	//message 분석 역할
	virtual void OnRecv(DWORD64 sessionID, CPacket* packet) = 0;
	//
	virtual void OnExpt() = 0;

	virtual void OnError(int error, const WCHAR* msg) = 0;

private:
	//종료함수 작성용
	virtual void OnStop() = 0;

	void NetClose();

	//packet에 header 할당
	void	HeaderAlloc(CPacket* packet);
	
	void	ReConnect(int idx);
	void	Disconnect(int idx);

	SESSION* FindSession(DWORD64 sessionID);
	bool	MakeSession(WCHAR* IP, SOCKET sock, DWORD64* ID);
	void	ReleaseSession(SESSION* session);

	static unsigned int __stdcall WorkProc(void* arg);
	void _WorkProc();

	void RecvProc(int idx);
	bool RecvPost(int idx);
	bool SendPost(int idx);

private:
	enum {
		//64이하로 설정
		CLIENT_MAX = 64,
	};
	//array for client
	CLIENT* clientArr;

	//readonly
	bool isClientOn;
	bool isNagle;

	HANDLE workEvent;
	HANDLE hThread;
};

