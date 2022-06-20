#pragma once
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

	//�⺻ ����ī��Ʈ 1�ο� �� �ʱ�ȭ ����
	CPacket* PacketAlloc();
	void	PacketFree(CPacket* packet);
	
	//�õ��Լ� �ۼ���
	virtual void Init() = 0;
	//connect���� ȣ��
	virtual bool OnConnect() = 0;
	//message �м� ����
	virtual void OnRecv(CPacket* packet) = 0;
	//������ ��� ����(�Ƹ� ����Ȯ���� disconnect)
	virtual void OnExpt() = 0;

	virtual void OnDisconnect() = 0;

	virtual void OnError(int error, const WCHAR* msg) = 0;

	//�����Լ� �ۼ���
	virtual void OnStop() = 0;
private:
	//packet�� header �Ҵ�
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

