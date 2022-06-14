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
    if (client->isConnected) closesocket(client->sock);

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
        OnError(WSAGetLastError(), L"Connect Failed");
        return false;
    }

    ret = setsockopt(client->sock, SOL_SOCKET, SO_LINGER, (char*)&optval, sizeof(optval));
    if (ret == SOCKET_ERROR) {
        OnError(WSAGetLastError(), L"Connect Failed");
        return false;
    }

    if (isNagle == false) {
        ret = setsockopt(client->sock, IPPROTO_TCP, TCP_NODELAY, (char*)&isNagle, sizeof(isNagle));
        if (ret == SOCKET_ERROR) {
            OnError(WSAGetLastError(), L"Connect Failed");
            return false;
        }
    }

    //connect
    SOCKADDR_IN sockAddr;
    sockAddr.sin_family = AF_INET;
    sockAddr.sin_port = htons(port);
    InetPton(AF_INET, IP, &sockAddr.sin_addr);

    ret = connect(client->sock, (sockaddr*)&sockAddr, sizeof(sockAddr));
    if (ret != 0) {
        OnError(WSAGetLastError(), L"Connect Failed");
        return false;
    }

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
    while (isClientOn) {

        //select();
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
