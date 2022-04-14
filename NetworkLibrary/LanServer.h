#pragma once
struct SESSION;
class CPacket;

class CLanServer
{
public:
	//���� IP / ��Ʈ / ��Ŀ������ ��(������, ���׼�) / ���ۿɼ� / �ִ������� ��
	bool Start(WCHAR* IP, DWORD port, DWORD createThreads, DWORD runningThreads, bool isNagle, DWORD maxConnect);
	void Stop();
	int GetSessionCount();

	bool Disconnect(DWORD64 sessionID);
	bool SendPacket(DWORD64 sessionID, CPacket* packet);

	virtual bool OnConnectionRequest(WCHAR* IP, DWORD Port) = 0; //< accept ����
	//return false; �� Ŭ���̾�Ʈ �ź�.
	//return true; �� ���� ���
	virtual bool OnClientJoin() = 0;
	virtual bool OnClientLeave() = 0;

	virtual void OnRecv(DWORD64 sessionID, CPacket* packet) = 0;

	virtual void OnError(int error, WCHAR* msg) = 0;

private:
	//CLanServer();
	//~CLanServer();

private:
	bool NetInit(WCHAR* IP, DWORD port, bool isNagle);
	bool ThreadInit(const DWORD createThreads, const DWORD runningThreads);

	void NetClose();
	void ThreadClose();

	SESSION* FindSession(DWORD64 sessionID);
	bool	MakeSession(DWORD64 sessionID, WCHAR* IP, SOCKET sock);
	void	ReleaseSession(DWORD64 sessionID, SESSION* session);

	static unsigned int __stdcall WorkProc(void* arg);
	static unsigned int __stdcall AcceptProc(void* arg);
	void RecvProc(SESSION* session);
	bool RecvPost(SESSION* session);
	bool SendPost(SESSION* session);

private:
	//SESSION* sessionArr;
	std::unordered_map<DWORD64, SESSION*> sessionMap; //���߿� ����
	SESSION* sessionArr;
	

	DWORD64 g_sessionID = 0;
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

