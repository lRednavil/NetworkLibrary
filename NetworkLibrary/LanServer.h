#pragma once
class CLanServer
{
public:
	CLanServer();
	~CLanServer();

	//���� IP / ��Ʈ / ��Ŀ������ ��(������, ���׼�) / ���ۿɼ� / �ִ������� ��
	bool Start(WCHAR* IP, DWORD port, DWORD createThreads, DWORD runningThreads, bool isNagle, DWORD maxConnect);
	void Stop();
	int GetSessionCount();

	bool Disconnect(DWORD64 SessionID);
	bool SendPacket(DWORD64 SessionID, CPacket* packet);

	virtual bool OnConnectionRequest(WCHAR* IP, DWORD Port) = 0; //< accept ����
	//return false; �� Ŭ���̾�Ʈ �ź�.
	//return true; �� ���� ���
	virtual void OnClientJoin() = 0;
	virtual void OnClientLeave() = 0;

	virtual void OnRecv(DWORD64 sessionID, CPacket* packet) = 0;

	virtual void OnError(int error, WCHAR* msg) = 0;

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
		DWORD sendCnt; // << ���� �޼����� Ȯ��

		//readonly
		SOCKET sock;
		WCHAR IP[16];
	};

private:
	bool NetInit(WCHAR* IP, DWORD port, bool isNagle);
	bool ThreadInit(const DWORD createThreads, const DWORD runningThreads);

	void NetClose();
	void ThreadClose();

	SESSION* FindSession(DWORD64 sessionID);
	bool	MakeSession(DWORD64 sessionID, WCHAR* IP, SOCKET sock);
	void	ReleaseSession(DWORD64 sessionID, SESSION* session);

	static unsigned int _stdcall AcceptStartFunc(void* classPtr);
	static unsigned int _stdcall WorkStartFunc(void* classPtr);
	unsigned int __stdcall WorkProc(void* arg);
	unsigned int __stdcall AcceptProc(void* arg);
	void RecvProc(SESSION* session);
	bool RecvPost(SESSION* session);
	bool SendPost(SESSION* session);

private:
	//SESSION* sessionArr;
	std::unordered_map<DWORD64, SESSION*> sessionMap; //���߿� ����

	int lastError;
	SRWLOCK sessionMapLock;

	//monitor
	DWORD sessionCnt;
	BYTE netMode; // << ���߿� ȭ��Ʈ����Ʈ ��� ��� �����

	//readonly
	SOCKET listenSock;
	HANDLE hIOCP;

	HANDLE* hThreads;
	bool isServerOn;
};

