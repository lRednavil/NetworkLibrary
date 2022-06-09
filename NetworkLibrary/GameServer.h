#pragma once

struct SESSION;
class CPacket;

class CProcessMontior;
class CProcessorMontior;

class CGameServer;

//��ӹ޾Ƽ� ���� �� ���
class CUnitClass{
	friend class CGameServer;

public:
	enum END_OPTION {
		OPT_DESTROY,
		OPT_STOP,
		OPT_RESET
	};

public:
	void InitClass(WORD targetFrame, BYTE endOpt);
	
	//server���� �Լ���
	
	//1�� �̵���
	inline bool MoveClass(const WCHAR* className, DWORD64 sessionID, WORD classIdx);
	//�ټ� �̵���
	inline bool MoveClass(const WCHAR* className, DWORD64* sessionIDs, WORD sessionCnt, WORD classIdx);
	inline bool FollowClass(DWORD64 targetID, DWORD64 followID);

	inline bool Disconnect(DWORD64 sessionID);
	inline bool SendPacket(DWORD64 sessionID, CPacket* packet);

	//�⺻ ����ī��Ʈ 1�ο� �� �ʱ�ȭ ����
	inline CPacket* PacketAlloc();
	inline void	PacketFree(CPacket* packet);

	inline void SetTimeOut(DWORD64 sessionID, DWORD timeVal);

	//virtual�Լ� ����
	virtual void OnClientJoin(DWORD64 sessionID) = 0;
	virtual void OnClientLeave(DWORD64 sessionID) = 0;

	//message �м� ����
	//�޼��� ����� �˾Ƽ� ������ ��
	//������Ʈ ������ ó�� �ʿ�� jobQ�� enQ�Ұ�
	virtual void OnRecv(DWORD64 sessionID, CPacket* packet) = 0;

	virtual void OnTimeOut(DWORD64 sessionID) = 0;

	virtual void OnError(int error, const WCHAR* msg) = 0;

	//gameserver��
	//jobQ�� EnQ�� �޼����� ó��
	virtual void MsgUpdate() = 0;
	//frame������ ������Ʈ ó��
	virtual void FrameUpdate() = 0;

private:
	//classInfos
	bool isAwake;
	WORD frameDelay; //1�� / targetFrame
	WORD currentUser;
	WORD maxUser;
	DWORD lastTime;

	BYTE endOption;
	CGameServer* server = nullptr;
};

struct CUSTOM_TCB {
	//thread�� tagName�� ���� ����
	WCHAR tagName[128];
	WORD max_class_unit;
	WORD currentUnits;
	CUnitClass** classList;
	//������ ������
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
	//���� IP / ��Ʈ / ��Ŀ������ ��(������, ���׼�) / ���ۿɼ� / �ִ������� ��
	bool Start(WCHAR* IP, DWORD port, DWORD createThreads, DWORD runningThreads, bool isNagle, DWORD maxConnect);
	void Stop();

	int GetSessionCount();
	//����͸��� �Լ�
	void Monitor();

	bool Disconnect(DWORD64 sessionID);
	bool SendPacket(DWORD64 sessionID, CPacket* packet);

	//�⺻ ����ī��Ʈ 1�ο� �� �ʱ�ȭ ����
	inline CPacket* PacketAlloc();
	inline void	PacketFree(CPacket* packet);

	void SetTimeOut(DWORD64 sessionID, DWORD timeVal);

	//virtual�Լ� ����
	//accept ����, IP filterinig ���� ����
	virtual bool OnConnectionRequest(WCHAR* IP, DWORD Port) = 0;
	//return false; �� Ŭ���̾�Ʈ �ź�.
	//return true; �� ���� ���
	virtual bool OnClientJoin(DWORD64 sessionID) = 0;
	virtual bool OnClientLeave(DWORD64 sessionID) = 0;

	
	//gameServer�� �Լ�
	bool MoveClass(const WCHAR* tagName, DWORD64 sessionID, WORD classIdx);
	bool MoveClass(const WCHAR* tagName, DWORD64* sessionIDs, WORD sessionCnt, WORD classIdx);
	bool FollowClass(DWORD64 targetID, DWORD64 followID);

	//���� tagName�� tcb����� ��ȿ���� �Ǵ� �� ���� or ���ο� tcb ���� �� ������ ����
	void AttatchClass(const WCHAR* tagName, CUnitClass* const classPtr, const WORD maxUnitCnt = 1);

private:
	bool NetInit(WCHAR* IP, DWORD port, bool isNagle);
	bool ThreadInit(const DWORD createThreads, const DWORD runningThreads);

	void NetClose();
	void ThreadClose();

	//packet�� header �Ҵ�
	void HeaderAlloc(CPacket* packet);

	//send�� üũ�� �ۼ�
	//recv�� üũ�� ����
	BYTE MakeCheckSum(CPacket* packet);
	void Encode(CPacket* packet);
	void Decode(CPacket* packet);

	//return NULL for fail
	//FindSession + Check Flag + Session ��Ȯ��
	//�ݵ�� LoseSession�� �� ���� ��
	SESSION* AcquireSession(DWORD64 sessionID);
	//ioCnt ���� ���� Release ���� ����
	//�ݵ�� AcquireSession �̳� ioCnt ���� �Ŀ� ���
	void LoseSession(SESSION* session);

	SESSION* FindSession(DWORD64 sessionID);
	bool	MakeSession(WCHAR* IP, SOCKET sock, DWORD64* ID);
	void	ReleaseSession(SESSION* session);

	static unsigned int __stdcall WorkProc(void* arg);
	static unsigned int __stdcall AcceptProc(void* arg);
	static unsigned int __stdcall TimerProc(void* arg);
	//������Ʈ �����忡 �ش��ϴ� �Լ�
	static unsigned int __stdcall UnitProc(void* arg);
	void RecvProc(SESSION* session);
	bool RecvPost(SESSION* session);
	bool SendPost(SESSION* session);

protected:
	//sessionID ���
	DWORD64 totalAccept = 0;
	DWORD64 totalSend = 0;
	DWORD64 totalRecv = 0;
	//tps������ ���
	DWORD64 lastAccept = 0;
	DWORD64 lastSend = 0;
	DWORD64 lastRecv = 0;

	DWORD64 recvBytes = 0;
	DWORD64 sendBytes = 0;

	//�ð� ���
	DWORD currentTime;
	
	//gameserver���� �����̳�
	CUSTOM_TCB tcbArray[500];
	DWORD tcbCnt = 0;

private:
	//array for session
	SESSION* sessionArr;
	//stack for session index
	CLockFreeStack<int> sessionStack;

	//monitor
	DWORD sessionCnt;
	DWORD maxConnection;
	BYTE netMode; // << ���߿� ȭ��Ʈ����Ʈ ��� ��� �����
	bool isServerOn;

	CProcessMonitor* myMonitor;
	CProcessorMonitor* totalMonitor;

	//readonly
	SOCKET listenSock;
	HANDLE hIOCP;

	HANDLE* hThreads;
};

