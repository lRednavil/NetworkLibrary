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
    //64이하로 설정
    CLIENT_MAX = 64,
};

class CMasterClient : public CLanClient {
    //시동함수 작성용
    virtual void Init() {};
    //accept 직후, IP filterinig 등의 목적
    virtual bool OnConnect() { return true; };
    //message 분석 역할
    virtual void OnRecv(CPacket* packet) {};
    //예외일 경우 선택(아마 높은확률로 disconnect)
    virtual void OnExpt() {};

    virtual void OnError(int error, const WCHAR* msg) {};

    virtual void OnDisconnect() {};

    //종료함수 작성용
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
    MEMORY_CLEAR(clientArr, sizeof(clientArr));

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

    return false;
}

bool CLanClient::SendPacket(CPacket* packet)
{
    if (myClient == NULL) return false;

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
//공통
bool CLanClient::_Connect(CLIENT* client)
{
    int ret;
    int err;
    int clientIdx;
    
    //중복 확인
	for (clientIdx = 0; clientIdx < CLIENT_MAX; clientIdx++) {
		if (clientArr[clientIdx] != NULL && clientArr[clientIdx]->belongClient == client->belongClient)
		{
            client->belongClient->OnError(-1, L"Already Connected");
            return false;
		}
	}
    //빈공간 탐색
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
        OnError(-1, L"Socket Make Failed");
    }

    //setsockopt
    LINGER optval;
    optval.l_linger = 0;
    optval.l_onoff = 1;

    ret = setsockopt(client->sock, SOL_SOCKET, SO_LINGER, (char*)&optval, sizeof(optval));
    if (ret == SOCKET_ERROR) {
        OnError(WSAGetLastError(), L"socket Option Failed");
        return false;
    }

    u_long nonblock = 1;
    ret = ioctlsocket(client->sock, FIONBIO, &nonblock);
    if (ret == SOCKET_ERROR) {
        OnError(WSAGetLastError(), L"socket Option Failed");
        return false;
    }

    if (isNagle == false) {
        ret = setsockopt(client->sock, IPPROTO_TCP, TCP_NODELAY, (char*)&isNagle, sizeof(isNagle));
        if (ret == SOCKET_ERROR) {
            OnError(WSAGetLastError(), L"socket Option Failed");
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
            OnError(WSAGetLastError(), L"Connect Failed");
			return false;
        }
    }

    SetEvent(workEvent);
    return true;
}

unsigned int __stdcall CLanClient::WorkProc(void* arg)
{
    CLanClient* client = (CLanClient*)arg;
    client->_WorkProc();

    return 0;
}
//공통
void CLanClient::_WorkProc()
{
    fd_set readSet;
    fd_set writeSet;
    fd_set exptSet;

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

			FD_SET(clientArr[cnt]->sock, &readSet);
            if(clientArr[cnt]->isConnected == false || clientArr[cnt]->sendQ.GetSize())
			    FD_SET(clientArr[cnt]->sock, &writeSet);
			FD_SET(clientArr[cnt]->sock, &exptSet);
        }
        
        ret = select(0, &readSet, &writeSet, &exptSet, NULL);

        if (ret == SOCKET_ERROR) {
            OnError(WSAGetLastError(), L"Select Failed");
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
    //Packet 떼기 (lanHeader 제거)
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
                destClient->OnError(err, L"Recv Error");
                return;
            }
        }

        if (ret == 0) {
            destClient->OnDisconnect();
            return;
        }

        recvQ->MoveRear(ret);
    }

    //msgProc
    for (;;) {
        len = recvQ->GetUsedSize();
        //길이 판별
        if (sizeof(lanHeader) > len) {
            break;
        }

        //넷헤더 추출
        recvQ->Peek((char*)&lanHeader, sizeof(lanHeader));
        //길이 판별
        if (sizeof(lanHeader) + lanHeader.len > len) {
            break;
        }

        packet = PacketAlloc();
        
        //헤더영역 dequeue
        recvQ->Dequeue((char*)packet->GetBufferPtr(), sizeof(lanHeader) + lanHeader.len);
        packet->MoveWritePos(lanHeader.len);

        //사용전 net헤더 스킵
        packet->MoveReadPos(sizeof(lanHeader));

        OnRecv(packet);
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
    }

    ret = send(myClient->sock, sendBuf, sendSize, 0);
    if (ret == SOCKET_ERROR) {
        err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK) {
            OnError(err, L"Send Failed");
            return false;
        }
    }

    return false;
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
