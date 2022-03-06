#pragma once
class CNetServer
{
public:
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
		DWORD sendCnt; // << ���� �޼����� Ȯ��
		DWORD sessionCnt;

		//readonly
		SOCKET sock;
		WCHAR IP[16];
		WORD netMode; // << ���߿� ȭ��Ʈ����Ʈ ��� ��� �����
	};


private:
	SOCKET listenSock;
	HANDLE hIOCP;
	
	HANDLE* hThreads;
	SESSION* sessionArr;

	int lastError;

};

