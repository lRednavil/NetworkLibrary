#pragma once
struct SESSION;
struct CLIENT;
struct CONNECT_JOB;
class CPacket;

class CLanClient
{
public:
	bool Start(bool _isNagle);
	void Stop();

	void Connect(const WCHAR* connectName, const WCHAR* IP, const DWORD port);
	void ReConnect();
	bool Disconnect();
	
	bool SendPacket(CPacket* packet);

	//기본 참조카운트 1부여 및 초기화 실행
	CPacket* PacketAlloc();
	void	PacketFree(CPacket* packet);
	
	//시동함수 작성용
	virtual void Init() = 0;
	//accept 직후, IP filterinig 등의 목적
	virtual bool OnConnect() = 0;
	//message 분석 역할
	virtual void OnRecv(CPacket* packet) = 0;
	//예외일 경우 선택(아마 높은확률로 disconnect)
	virtual void OnExpt() = 0;

	virtual void OnDisconnect() = 0;

	virtual void OnError(int error, const WCHAR* msg) = 0;

	//종료함수 작성용
	virtual void OnStop() = 0;
private:
	//packet에 header 할당
	void	HeaderAlloc(CPacket* packet);
	
	void	ConnectProc();
	bool	_Connect(CLIENT* client);
	void	_ReConnect(CLIENT* client);
	void	_Disconnect(CLIENT* client);

	static unsigned int __stdcall WorkProc(void* arg);
	void _WorkProc();

	void RecvProc(CLIENT* client);
	bool SendPost(CLIENT* client);

private:
	CLIENT* myClient = NULL;
};

