#include "pch.h"
#include "LanServer.h"


bool CLanServer::Start(WCHAR* IP, DWORD port, DWORD createThreads, DWORD runningThreads, bool isNagle, DWORD maxConnect)
{
    if (NetInit(IP, port, isNagle) == false) {
        return false;
    }

    if (ThreadInit(createThreads, runningThreads) == false) {
        return false;
    }

    //sessionArr = new SESSION[maxConnect];
    InitializeSRWLock(&sessionMapLock);
    isServerOn = true;

    return true;
}

void CLanServer::Stop()
{
}

int CLanServer::GetSessionCount()
{
    return sessionCnt;
}

bool CLanServer::Disconnect(DWORD64 SessionID)
{
    return false;
}

bool CLanServer::SendPacket(DWORD64 SessionID, CPacket* packet)
{
    return false;
}

bool CLanServer::NetInit(WCHAR* IP, DWORD port, bool isNagle)
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

bool CLanServer::ThreadInit(const DWORD createThreads, const DWORD runningThreads)
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

CLanServer::SESSION* CLanServer::FindSession(DWORD64 sessionID)
{
    CLock _lock(&sessionMapLock, 0);

    if (sessionMap.find(sessionID) == sessionMap.end()) {
        return NULL;
    }

    return sessionMap[sessionID];
}

void CLanServer::AcceptProc()
{
    while (isServerOn) {

    }
}

void CLanServer::RecvProc()
{
    int ret;
    DWORD ioCnt;

    DWORD bytes;
    DWORD64 sessionID;
    OVERLAPPEDEX* overlap = NULL;
    WCHAR* msg;

    while (isServerOn) {
        ret = GetQueuedCompletionStatus(hIOCP, &bytes, (PULONG_PTR)&sessionID, (LPOVERLAPPED*)&overlap, INFINITE);

        if (ret == false) {
            lastError = WSAGetLastError();
            msg = new WCHAR[]{ L"GQCS False" };
            OnError(lastError, msg);
        }

        if (overlap == NULL) {
            continue; // session 판단 불가
        }

                  //세션 찾아야하는디...
    }


}
