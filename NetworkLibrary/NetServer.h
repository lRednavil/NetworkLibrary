#pragma once
struct SESSION;
class CPacket;

class CProcessMonitor;
class CProcessorMonitor;

class CNetServer
{
public:
	//���� IP / ��Ʈ / ��Ŀ������ ��(������, ���׼�) / ���ۿɼ� / �ִ������� ��
	bool Start(const WCHAR * IP, DWORD port, DWORD createThreads, DWORD runningThreads, bool isNagle, DWORD maxConnect, DWORD snapLatency, int packetSize = CPacket::eBUFFER_DEFAULT);
	void Stop();

	int GetSessionCount();

	bool Disconnect(DWORD64 sessionID);
	bool SendPacket(DWORD64 sessionID, CPacket* packet);
	//������ �������� send
	void SendPacketToAll(CPacket* packet);
	//������ enq�� �� ���
	bool SendEnQ(DWORD64 sessionID, CPacket* packet);
	bool SendAndDisconnect(DWORD64 sessionID, CPacket* packet);
	bool SendAndDisconnect(DWORD64 sessionID, CPacket* packet, DWORD timeOutVal);

	//�⺻ ����ī��Ʈ 1�ο� �� �ʱ�ȭ ����
	CPacket* PacketAlloc();
	void	PacketFree(CPacket* packet);
	int		GetPacketPoolCapacity();
	int		GetPacketPoolUse();

	void SetTimeOut(DWORD64 sessionID, DWORD timeVal, bool recvTimeReset = false);

	DWORD64 GetAcceptTPS();

	//�õ��Լ� �ۼ���
	virtual void Init() = 0;
	//accept ����, IP filterinig ���� ����
	virtual bool OnConnectionRequest(WCHAR* IP, DWORD Port) = 0;
	//return false; �� Ŭ���̾�Ʈ �ź�.
	//return true; �� ���� ���
	virtual bool OnClientJoin(DWORD64 sessionID) = 0;
	virtual bool OnClientLeave(DWORD64 sessionID) = 0;

	//message �м� ����
	//�޼��� ����� �˾Ƽ� ������ ��
	virtual void OnRecv(DWORD64 sessionID, CPacket* packet) = 0;

	virtual void OnTimeOut(DWORD64 sessionID, int reason) = 0;

	virtual void OnError(int error, const WCHAR* msg) = 0;

	//�����Լ� �ۼ���
	virtual void OnStop() = 0;
private:

	bool NetInit(const WCHAR * IP, DWORD port, bool isNagle);
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
	static unsigned int __stdcall SendProc(void* arg);
	void _WorkProc();
	void _AcceptProc();
	void _TimerProc();
	void _SendProc();

	void RecvProc(SESSION* session);
	bool RecvPost(SESSION* session);
	bool SendPost(SESSION* session);

protected:
	//sessionID ���
	DWORD64 totalAccept = 0;
	alignas(64)
	//tps������ ���
	DWORD64 lastAccept = 0;

	//�ð� ���
	DWORD currentTime;

private:
	//array for session
	SESSION* sessionArr;
	//stack for session index
	CLockFreeStack<int> sessionStack;
	CLockFreeQueue<DWORD64> sendSessionQ;

	//monitor
	DWORD sessionCnt;
	DWORD maxConnection;
	BYTE netMode; // << ���߿� ȭ��Ʈ����Ʈ ��� ��� �����
	bool isServerOn;

	CProcessMonitor* myMonitor;
	CProcessorMonitor* totalMonitor;

	//readonly
	LPFN_TRANSMITPACKETS transFn;
	CTLSMemoryPool<CPacket>* packetPool;
	int packetSize;
	SOCKET listenSock;
	DWORD snapLatency;
	HANDLE hIOCP;
	int threadCnt;

	HANDLE* hThreads;
};

