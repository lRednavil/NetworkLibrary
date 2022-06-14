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
	
	bool SendPacket(CPacket* packet);

	//�⺻ ����ī��Ʈ 1�ο� �� �ʱ�ȭ ����
	CPacket* PacketAlloc();
	void	PacketFree(CPacket* packet);
	
	//�õ��Լ� �ۼ���
	virtual void Init() = 0;
	//accept ����, IP filterinig ���� ����
	virtual bool OnConnect() = 0;
	//message �м� ����
	virtual void OnRecv(DWORD64 sessionID, CPacket* packet) = 0;
	//
	virtual void OnExpt() = 0;

	virtual void OnError(int error, const WCHAR* msg) = 0;

private:
	//�����Լ� �ۼ���
	virtual void OnStop() = 0;

	void NetClose();

	//packet�� header �Ҵ�
	void	HeaderAlloc(CPacket* packet);

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
	void _WorkProc();

	void RecvProc(SESSION* session);
	bool RecvPost(SESSION* session);
	bool SendPost(SESSION* session);

private:
	enum {
		//64���Ϸ� ����
		CLIENT_MAX = 64,
	};
	//array for client
	CLIENT* clientArr;

	//readonly
	bool isClientOn;
	bool isNagle;

	HANDLE hThread;
};

