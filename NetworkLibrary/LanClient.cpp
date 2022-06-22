#include "pch.h"
#include "LanClient.h"
#include "LanCommon.h"  

struct CLIENT {
    enum {
        NAME_MAX = 32,
    };
    bool isConnected;
    WCHAR connectionName[NAME_MAX];
    WCHAR connectIP[16];
    DWORD connectPort;
    SOCKET sock;

    CLanClient* belongClient;
    CRingBuffer recvQ;
    CLockFreeQueue<CPacket*> sendQ;
    CPacket* savedPacket;
};

struct CONNECT_JOB {
    enum {
        Connect,
        Disconnect,
        ReConnect
    };
    BYTE type;
    CLIENT* target;
};

enum {
    //64���Ϸ� ����
    CLIENT_MAX = 64,
};

//�����Լ� �۵��� ���� Ŭ���̾�Ʈ 
class CMasterClient : public CLanClient {
    //�õ��Լ� �ۼ���
    virtual void Init() {};
    //connect���� ȣ��
    virtual bool OnConnect() { return true; };
    //message �м� ����
    virtual void OnRecv(CPacket* packet) {};
    //������ ��� ����(�Ƹ� ����Ȯ���� disconnect)
    virtual void OnExpt() {};

    virtual void OnError(int error, const WCHAR* msg) {};

    virtual void OnDisconnect() {};

    //�����Լ� �ۼ���
    virtual void OnStop() {};
};

CLockFreeQueue<CONNECT_JOB> connectQ;
//array for Client
CLIENT** clientArr;

//readonly
bool isClientOn;
bool isNagle;

int currentClient = 0;

HANDLE workEvent;
HANDLE hThread;

CMasterClient masterClient;

bool CLanClient::Start(bool _isNagle)
{
    if (isClientOn) {
        return isNagle == _isNagle;
    }

    //net start
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    isClientOn = true;
    isNagle = _isNagle;

    clientArr = new CLIENT*[CLIENT_MAX];
    MEMORY_CLEAR(clientArr, sizeof(void*) * CLIENT_MAX);

    //make thread
    hThread = (HANDLE)_beginthreadex(NULL, 0, masterClient.WorkProc, &masterClient, 0, NULL);
    if (hThread == INVALID_HANDLE_VALUE) {
        OnError(-1, L"Create Thread Failed");
        return false;
    }

    workEvent = (HANDLE)CreateEvent(NULL, TRUE, FALSE, NULL);
    return true;
}

void CLanClient::Stop()
{
    if (myClient->isConnected) {
        OnError(-1, L"Client is Connected. Disconnect First.");
        return;
    }

    OnStop();

    if (currentClient > 0) return;
    
    isClientOn = false;

    WaitForSingleObject(hThread, INFINITE);

    delete clientArr;
    
    CloseHandle(hThread);
    CloseHandle(workEvent);
}

void CLanClient::Connect(const WCHAR* connectName, const WCHAR* IP, const DWORD port)
{
    CONNECT_JOB job;
    CLIENT* client = new CLIENT;

    client->isConnected = false;
    wmemmove_s(client->connectionName, CLIENT::NAME_MAX, connectName, CLIENT::NAME_MAX);
    wmemmove_s(client->connectIP, 16, IP, 16);
    client->connectPort = port;
    client->belongClient = this;
    client->savedPacket = NULL;

    job.type = CONNECT_JOB::Connect;
    job.target = client;

    connectQ.Enqueue(job);
    SetEvent(workEvent);
}

void CLanClient::ReConnect()
{
    if (myClient == NULL) return;

    CONNECT_JOB job;

    job.type = CONNECT_JOB::ReConnect;
    job.target = myClient;

    connectQ.Enqueue(job);
}

bool CLanClient::Disconnect()
{
    CONNECT_JOB job;

    if (myClient == NULL) return false;

    job.type = CONNECT_JOB::Disconnect;
    job.target = myClient;

    connectQ.Enqueue(job);

    return true;
}

bool CLanClient::SendPacket(CPacket* packet)
{
    if (myClient == NULL) return false;

    HeaderAlloc(packet);
    myClient->sendQ.Enqueue(packet);

    return true;
}

void CLanClient::ConnectProc()
{
    CONNECT_JOB job;
    while (connectQ.Dequeue(&job)) {
        switch (job.type) {
        case CONNECT_JOB::Connect:
            if (_Connect(job.target) == false) 
            {
                delete job.target;
            }
            break;

        case CONNECT_JOB::Disconnect:
            _Disconnect(job.target);
            break;

        case CONNECT_JOB::ReConnect:
            _ReConnect(job.target);
            break;
        }
    }
}
//����
bool CLanClient::_Connect(CLIENT* client)
{
    int ret;
    int err;
    int clientIdx;
    
    //�ߺ� Ȯ��
	for (clientIdx = 0; clientIdx < CLIENT_MAX; clientIdx++) {
		if (clientArr[clientIdx] != NULL && clientArr[clientIdx]->belongClient == client->belongClient)
		{
            client->belongClient->OnError(-1, L"Already Connected");
            return false;
		}
	}
    //����� Ž��
    for (clientIdx = 0; clientIdx < CLIENT_MAX; clientIdx++) {
        if (clientArr[clientIdx] == NULL)
        {
            clientArr[clientIdx] = client;
            break;
        }
    }

    if (clientIdx == CLIENT_MAX) return false;

    client->sock = socket(AF_INET, SOCK_STREAM, NULL);
    if (client->sock == INVALID_SOCKET) {
        _FILE_LOG(LOG_LEVEL_ERROR, L"LibraryLog", L"Socket Make Failed");
        client->belongClient->OnError(-1, L"Socket Make Failed");
    }

    //setsockopt
    LINGER optval;
    optval.l_linger = 0;
    optval.l_onoff = 1;

    ret = setsockopt(client->sock, SOL_SOCKET, SO_LINGER, (char*)&optval, sizeof(optval));
    if (ret == SOCKET_ERROR) {
        client->belongClient->OnError(WSAGetLastError(), L"socket Option Failed");
        return false;
    }

    u_long nonblock = 1;
    ret = ioctlsocket(client->sock, FIONBIO, &nonblock);
    if (ret == SOCKET_ERROR) {
        client->belongClient->OnError(WSAGetLastError(), L"socket Option Failed");
        return false;
    }

    if (isNagle == false) {
        ret = setsockopt(client->sock, IPPROTO_TCP, TCP_NODELAY, (char*)&isNagle, sizeof(isNagle));
        if (ret == SOCKET_ERROR) {
            client->belongClient->OnError(WSAGetLastError(), L"socket Option Failed");
            return false;
        }
    }

    //connect
    SOCKADDR_IN sockAddr;
    sockAddr.sin_family = AF_INET;
    sockAddr.sin_port = htons(client->connectPort);
    InetPton(AF_INET, client->connectIP, &sockAddr.sin_addr);

    ret = connect(client->sock, (sockaddr*)&sockAddr, sizeof(sockAddr));
    if (ret == SOCKET_ERROR) {
        err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK) {
            client->belongClient->OnError(WSAGetLastError(), L"Connect Failed");
			return false;
        }
    }

    clientArr[clientIdx] = client;
    return true;
}

unsigned int __stdcall CLanClient::WorkProc(void* arg)
{
    CLanClient* client = (CLanClient*)arg;
    client->_WorkProc();

    return 0;
}
//����
void CLanClient::_WorkProc()
{
    fd_set readSet;
    fd_set writeSet;
    fd_set exptSet;

    timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 200;

    int cnt;
    int ret;
    WaitForSingleObject(workEvent, INFINITE);
    while (isClientOn) {
        ConnectProc();
        
        FD_ZERO(&readSet);
        FD_ZERO(&writeSet);
        FD_ZERO(&exptSet);

        for (cnt = 0; cnt < CLIENT_MAX; cnt++) {
            if (clientArr[cnt] == NULL) continue;

            if(clientArr[cnt]->isConnected == false || clientArr[cnt]->sendQ.GetSize())
			    FD_SET(clientArr[cnt]->sock, &writeSet);
            if (clientArr[cnt]->isConnected) {
                FD_SET(clientArr[cnt]->sock, &readSet);
                FD_SET(clientArr[cnt]->sock, &exptSet);
            }
        }
        
        ret = select(0, &readSet, &writeSet, &exptSet, &tv);

        if (ret == 0) continue;

        if (ret == SOCKET_ERROR) {
            _LOG(LOG_LEVEL_SYSTEM, L"Select Failed. Code : %d", WSAGetLastError());
            continue;
        }

        for (cnt = 0; ret && cnt < CLIENT_MAX; cnt++) {
            if (clientArr[cnt] == NULL) continue;

            if (FD_ISSET(clientArr[cnt]->sock, &readSet)) {
                RecvProc(clientArr[cnt]);
                ret--;
            }

            if (FD_ISSET(clientArr[cnt]->sock, &writeSet)) {
                if (clientArr[cnt]->isConnected == false) {
                    clientArr[cnt]->isConnected = true;
                    clientArr[cnt]->belongClient->myClient = clientArr[cnt];
                    clientArr[cnt]->belongClient->OnConnect();
                    currentClient++;
                }

                if (clientArr[cnt]->sendQ.GetSize()) {
                    SendPost(clientArr[cnt]);
                }
                ret--;
            }

            if (FD_ISSET(clientArr[cnt]->sock, &exptSet)) {
                clientArr[cnt]->isConnected = false;
                clientArr[cnt]->belongClient->OnExpt();
				ret--;
            }
        }

        
    }
}

void CLanClient::RecvProc(CLIENT* client)
{
    //Packet ���� (lanHeader ����)
    LAN_HEADER lanHeader;
    LAN_HEADER* header;
    DWORD len;
    int ret;
    int err;

    CLanClient* destClient = client->belongClient;
    CRingBuffer* recvQ = &client->recvQ;
    CPacket* packet;

    //recv
    for (int cnt = 0; cnt < 2; cnt++) {
        len = recvQ->DirectEnqueueSize();

        if (len == 0) {
            break;
        }

        ret = recv(client->sock, recvQ->GetRearBufferPtr(), len, 0);

        if (ret == SOCKET_ERROR) {
            err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) {
                break;
            }
            else {
                client->isConnected = false;
                destClient->OnError(err, L"Recv Error");
                return;
            }
        }

        if (ret == 0) {
            client->isConnected = false;
            destClient->OnDisconnect();
            return;
        }

        recvQ->MoveRear(ret);
    }

    //msgProc
    for (;;) {
        len = recvQ->GetUsedSize();
        //���� �Ǻ�
        if (sizeof(lanHeader) > len) {
            break;
        }

        //����� ����
        recvQ->Peek((char*)&lanHeader, sizeof(lanHeader));
        //���� �Ǻ�
        if (sizeof(lanHeader) + lanHeader.len > len) {
            break;
        }

        packet = PacketAlloc();

        //������� dequeue
        recvQ->Dequeue((char*)packet->GetBufferPtr(), sizeof(lanHeader) + lanHeader.len);
        packet->MoveWritePos(lanHeader.len);

        //����� net��� ��ŵ
        packet->MoveReadPos(sizeof(lanHeader));

        destClient->OnRecv(packet);


    }
}

bool CLanClient::SendPost(CLIENT* client)
{
    char sendBuf[1460];
    int sendSize = 0;
    int len;
    int ret;
    int err;
    CPacket* packet;

    if (client->savedPacket != NULL) {
        packet = client->savedPacket;
        client->savedPacket = NULL;

        len = packet->GetDataSize();

        packet->GetData(sendBuf + sendSize, len);

        sendSize += len;
        PacketFree(client->savedPacket);
    }

    for(;;) {
        if (client->sendQ.Dequeue(&packet) == false) break;

        len = packet->GetDataSize();

        if (sendSize + len > 1460) {
            client->savedPacket = packet;
            break;
        }

        packet->GetData(sendBuf + sendSize, len);

        sendSize += len;
        PacketFree(packet);
    }

    ret = send(client->sock, sendBuf, sendSize, 0);
    if (ret == SOCKET_ERROR) {
        err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK) {
            client->belongClient->OnError(err, L"Send Failed");
            client->isConnected = false;
            return false;
        }
    }

    return true;
}

CPacket* CLanClient::PacketAlloc()
{
    CPacket* packet = g_PacketPool.Alloc();
    packet->AddRef(1);
    packet->Clear();
    packet->MoveWritePos(sizeof(LAN_HEADER));
    return packet;
}

void CLanClient::PacketFree(CPacket* packet)
{
    if (packet->SubRef() == 0) {
        g_PacketPool.Free(packet);
    }
}

void CLanClient::HeaderAlloc(CPacket* packet)
{
    LAN_HEADER* header = (LAN_HEADER*)packet->GetBufferPtr();
    header->len = packet->GetDataSize() - sizeof(LAN_HEADER);
}

void CLanClient::_ReConnect(CLIENT* client)
{
    int ret;
    int err;

    CLanClient* destClient = client->belongClient;

    if (client->isConnected) {
        closesocket(client->sock);
    }

    client->sock = socket(AF_INET, SOCK_STREAM, NULL);
    if (client->sock == INVALID_SOCKET) {
        _FILE_LOG(LOG_LEVEL_ERROR, L"LibraryLog", L"Socket Make Failed");
        destClient->OnError(-1, L"Socket Make Failed");
    }

    //setsockopt
    LINGER optval;
    optval.l_linger = 0;
    optval.l_onoff = 1;

    ret = setsockopt(client->sock, SOL_SOCKET, SO_LINGER, (char*)&optval, sizeof(optval));
    if (ret == SOCKET_ERROR) {
        destClient->OnError(WSAGetLastError(), L"socket Option Failed");
        return;
    }

    u_long nonblock = 1;
    ret = ioctlsocket(client->sock, FIONBIO, &nonblock);
    if (ret == SOCKET_ERROR) {
        destClient->OnError(WSAGetLastError(), L"socket Option Failed");
        return;
    }

    if (isNagle == false) {
        ret = setsockopt(client->sock, IPPROTO_TCP, TCP_NODELAY, (char*)&isNagle, sizeof(isNagle));
        if (ret == SOCKET_ERROR) {
            destClient->OnError(WSAGetLastError(), L"socket Option Failed");
            return;
        }
    }

    //connect
    SOCKADDR_IN sockAddr;
    sockAddr.sin_family = AF_INET;
    sockAddr.sin_port = htons(client->connectPort);
    InetPton(AF_INET, client->connectIP, &sockAddr.sin_addr);

    ret = connect(client->sock, (sockaddr*)&sockAddr, sizeof(sockAddr));
    if (ret == SOCKET_ERROR) {
        err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK) {
            destClient->OnError(WSAGetLastError(), L"Connect Failed");
            return;
        }
    }

    SetEvent(workEvent);
}

void CLanClient::_Disconnect(CLIENT* client)
{
    for (int cnt = 0; cnt < CLIENT_MAX; cnt++) {
        if (clientArr[cnt] == client) {
            clientArr[cnt] = NULL;
            closesocket(client->sock);
            client->isConnected = false;
            client->belongClient->OnDisconnect();
            client->belongClient->myClient = NULL;
            delete client;
            break;
        }
    }
}
