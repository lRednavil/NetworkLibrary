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
		//64���Ϸ� ����
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

