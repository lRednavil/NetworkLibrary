#include "pch.h"
#include "NetServer.h"

bool CNetServer::Start(WCHAR* IP, DWORD port, DWORD createThreads, DWORD runningThreads, bool isNagle, DWORD maxConnect)
{
    if (NetInit(IP, port, isNagle) == false) {
        return false;
    }

    if (ThreadInit(createThreads, runningThreads) == false) {
        return false;
    }

    return true;
}

void CNetServer::Stop()
{
}

int CNetServer::GetSessionCount()
{
    return 0;
}

bool CNetServer::Disconnect(DWORD64 SessionID)
{
    return false;
}

bool CNetServer::SendPacket(DWORD64 SessionID, CPacket* packet)
{
    return false;
}

bool CNetServer::OnConnectionRequest(WCHAR* IP, DWORD Port)
{
    return false;
}

bool CNetServer::NetInit(WCHAR* IP, DWORD port, bool isNagle)
{
    int ret;

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    //socket
    listenSock = socket(AF_INET, SOCK_STREAM, NULL);
    if (listenSock == INVALID_SOCKET) {
        lastError = WSAGetLastError();
        return false;
    }

    //setsockopt
    LINGER optval;
    int sndSize = 0;

    optval.l_linger = 0;
    optval.l_onoff = 1;

    ret = setsockopt(listenSock, SOL_SOCKET, SO_LINGER, (char*)&optval, sizeof(optval));
    if (ret == SOCKET_ERROR) {
        lastError = WSAGetLastError();
        return false;
    }

    ret = setsockopt(listenSock, SOL_SOCKET, SO_SNDBUF, (char*)&sndSize, sizeof(sndSize));
    if (ret == SOCKET_ERROR) {
        lastError = WSAGetLastError();
        return false;
    }

    if (isNagle == false) {
        ret = setsockopt(listenSock, IPPROTO_TCP, TCP_NODELAY, (char*)&isNagle, sizeof(isNagle));
        if (ret == SOCKET_ERROR) {
            lastError = WSAGetLastError();
            return false;
        }
    }

    //bind
    SOCKADDR_IN sockAddr;
    sockAddr.sin_family = AF_INET;
    sockAddr.sin_port = htons(port);
    InetPton(AF_INET, IP, &sockAddr.sin_addr);
    
    ret = bind(listenSock, (sockaddr*)&sockAddr, sizeof(sockAddr));
    if (ret == SOCKET_ERROR) {
        lastError = WSAGetLastError();
        return false;
    }

    //listen
    ret = listen(listenSock, SOMAXCONN);
    if (ret == SOCKET_ERROR) {
        lastError = WSAGetLastError();
        return false;
    }

    return true;
}

bool CNetServer::ThreadInit(DWORD createThreads, DWORD runningThreads)
{
    int cnt;
    //add 1 for accept thread
    hThreads = new HANDLE[createThreads + 1];

    //_beginthreadex(NULL, 0, AcceptProc, NULL, NULL, NULL);

    for (cnt = 1; cnt <= createThreads; cnt++) {
        //hThreads = _beginthreadex(NULL, 0, WorkProc, NULL, NULL, NULL);
    }

    hIOCP = CreateIoCompletionPort(NULL, NULL, NULL, runningThreads);

    if (hIOCP == INVALID_HANDLE_VALUE) {
        return false;
    }

    for (cnt = 0; cnt <= createThreads; cnt++) {
        if (hThreads[cnt] == INVALID_HANDLE_VALUE) {
            return false;
        }
    }

    return true;
}

void CNetServer::NetClose()
{
}

void CNetServer::ThreadClose()
{
}
