#include "pch.h"
#include "LanClient.h"
#include "LanCommon.h"

bool CLanClient::Start(bool isNagle)
{
    if (isClientOn) return false;

    //net start
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    this->isClientOn = true;
    this->isNagle = isNagle;

    clientArr = new CLIENT[CLIENT_MAX];
    MEMORY_CLEAR(clientArr, sizeof(clientArr));

    //make thread
    hThread = (HANDLE)_beginthreadex(NULL, 0, WorkProc, this, 0, NULL);
    if (hThread == INVALID_HANDLE_VALUE) {
        OnError(-1, L"Create Thread Failed");
        return false;
    }

    workEvent = (HANDLE)CreateEvent(NULL, TRUE, FALSE, NULL);
    return true;
}

void CLanClient::Stop()
{
    OnStop();

    isClientOn = false;

    WaitForSingleObject(hThread, INFINITE);

    //소켓 정리
    int cnt;
    for (cnt = 0; cnt < CLIENT_MAX; cnt++) {
        if(clientArr[cnt].isAlive)
            closesocket(clientArr[cnt].sock);
    }
    delete clientArr;
    
    CloseHandle(hThread);
    CloseHandle(workEvent);
}

bool CLanClient::Disconnect(const WCHAR* connectName)
{
    CLIENT* client;
    int clientIdx;
    for (clientIdx = 0; clientIdx < CLIENT_MAX; clientIdx++) {
        client = &clientArr[clientIdx];
        if (client->isAlive && wcscmp(client->connectionName, connectName) == 0)
        {
            closesocket(client->sock);
            client->isAlive = false;
            client->isConnected = false;
            return true;
        }
    }

    return false;
}

bool CLanClient::Connect(const WCHAR* connectName, const WCHAR* IP, const DWORD port)
{
    int ret;
    int err;
    int clientIdx;
    CLIENT* client;

	do {
		for (clientIdx = 0; clientIdx < CLIENT_MAX; clientIdx++) {
			if (wcscmp(clientArr[clientIdx].connectionName, connectName) == 0)
			{
				client = &clientArr[clientIdx];
				break;
			}

			if (clientArr[clientIdx].isAlive == false)
			{
				client = &clientArr[clientIdx];
				break;
			}
		}
	} while (0);

    if (clientIdx == CLIENT_MAX) return false;

    //이미 연결된 경우 연결 해제
    if (client->isConnected) {
        client->isConnected = false;
        closesocket(client->sock);
    }

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
    sockAddr.sin_port = htons(port);
    InetPton(AF_INET, IP, &sockAddr.sin_addr);

    ret = connect(client->sock, (sockaddr*)&sockAddr, sizeof(sockAddr));
    if (ret == SOCKET_ERROR) {
        err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK) {
            OnError(WSAGetLastError(), L"Connect Failed");
			return false;
        }
    }

    wmemmove_s(client->connectIP, 16, IP, 16);
    memmove_s(&client->connectPort, sizeof(DWORD), &port, sizeof(DWORD));

    SetEvent(workEvent);
    return true;
}

unsigned int __stdcall CLanClient::WorkProc(void* arg)
{
    CLanClient* client = (CLanClient*)arg;
    client->_WorkProc();

    return 0;
}

void CLanClient::_WorkProc()
{
    fd_set readSet;
    fd_set writeSet;
    fd_set exptSet;

    int cnt;
    int ret;
    while (isClientOn) {
        WaitForSingleObject(workEvent, INFINITE);
        
        FD_ZERO(&readSet);
        FD_ZERO(&writeSet);
        FD_ZERO(&exptSet);

        for (cnt = 0; cnt < CLIENT_MAX; cnt++) {
            if (clientArr[cnt].isAlive == false) continue;

			FD_SET(clientArr[cnt].sock, &readSet);
			FD_SET(clientArr[cnt].sock, &writeSet);
			FD_SET(clientArr[cnt].sock, &exptSet);
        }
        
        ret = select(0, &readSet, &writeSet, &exptSet, NULL);

        if (ret == SOCKET_ERROR) {
            OnError(WSAGetLastError(), L"Select Failed");
            continue;
        }

        for (cnt = 0; ret && cnt < CLIENT_MAX; cnt++) {
            if (clientArr[cnt].isAlive == false) continue;

            if (FD_ISSET(clientArr[cnt].sock, &readSet)) {


                ret--;
            }

            if (FD_ISSET(clientArr[cnt].sock, &writeSet)) {
                if (clientArr[cnt].isConnected == false) {
                    clientArr[cnt].isConnected = true;
                }
                ret--;
            }

            if (FD_ISSET(clientArr[cnt].sock, &exptSet)) {
                if (clientArr[cnt].isAlive) {
                    ReConnect(cnt);
                }
                else {
                    SendPost(cnt);
                }
                ret--;
            }
        }

        
    }
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

void CLanClient::ReConnect(int idx)
{
    int ret;
    int err;
    CLIENT* client;

    client = &clientArr[idx];

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
        return;
    }

    u_long nonblock = 1;
    ret = ioctlsocket(client->sock, FIONBIO, &nonblock);
    if (ret == SOCKET_ERROR) {
        OnError(WSAGetLastError(), L"socket Option Failed");
        return;
    }

    if (isNagle == false) {
        ret = setsockopt(client->sock, IPPROTO_TCP, TCP_NODELAY, (char*)&isNagle, sizeof(isNagle));
        if (ret == SOCKET_ERROR) {
            OnError(WSAGetLastError(), L"socket Option Failed");
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
            OnError(WSAGetLastError(), L"Connect Failed");
            return;
        }
    }

    SetEvent(workEvent);
}

void CLanClient::Disconnect(int idx)
{
    CLIENT* client;
	client = &clientArr[idx];
	closesocket(client->sock);
	client->isConnected = false;
}
