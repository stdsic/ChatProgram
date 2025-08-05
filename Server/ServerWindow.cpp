#include <ws2tcpip.h>
#include "ServerWindow.h"
#include <winsock2.h>
#include <strsafe.h>
#include <math.h>
#define BUFSIZE 0x400

/*
   입출력 완료 포트 모델은 소켓 모드 설정 불필요
   설정 후 Async I/O 오류 발생, 커널 수준의 비동기 입출력과 비동기 통지를 이용하므로 넌블로킹 소켓일 경우 충돌이 발생한다.
   비동기 함수를 이용해 MSG_WAITALL 등의 플래그로 확인해본 결과 오류가 발생하지 않는 것으로 보아
   넌블로킹 소켓이 아님을 알 수 있으며 기본적으로 블로킹 소켓을 필요로 한다.

   ULONG On = 1;
   ret = ioctlsocket(sock, AF_INET, FIONBIO, &On);
   if(ret == SOCKET_ERROR){ShowError(L"ioctlsocket()"); return 1;}
 */
/*
   KeepAlive
   | OS        | 최초 검사 대기시간 | 패킷 간격 | 재시도 횟수 |
   |-----------|--------------------|-----------|-------------|
   | Windows   | 약 2시간           | 1초       | 10회        |
   | Linux     | 7200초             | 75초      | 9회         |
 */

/*
struct ClientSession {
    SOCKET sock;
    WSABUF wsaBuf;
    OVERLAPPED overlapped;
    char recvBuffer[BUF_SIZE];

    // 상태 관련 정보
    bool isConnected;
    std::string clientIP;

    // 유저 정의 데이터 등
    int userId;
    std::vector<char> sendQueue;
};
*/

LRESULT ServerWindow::Handler(UINT iMessage, WPARAM wParam, LPARAM lParam){
    for(DWORD i=0; i<sizeof(MainMsg) / sizeof(MainMsg[0]); i++) {
        if (MainMsg[i].iMessage == iMessage) {
            return (this->*MainMsg[i].lpfnWndProc)(wParam, lParam);
        }
    }
    return DefWindowProc(_hWnd, iMessage, wParam, lParam);
}

ServerWindow::ServerWindow() : bRunning(FALSE), dwKeepAliveOption(1), dwReuseAddressOption(1) {
    if(WSAStartup(MAKEWORD(2,2), &wsa) == 0){
        InitializeCriticalSection(&cs);

        memset(&l_LingerOption, 0, sizeof(linger));
        memset(&ServerAddress, 0, sizeof(sockaddr_in));

        SYSTEM_INFO si;
        GetSystemInfo(&si);
        nLogicalProcessors = si.dwNumberOfProcessors;

        hThread = (HANDLE*)malloc(sizeof(HANDLE) * (nLogicalProcessors * 2));
        dwThreadID = (DWORD*)malloc(sizeof(DWORD) * (nLogicalProcessors * 2));

        hcp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0,0);

        for(int i=0; i < nLogicalProcessors * 2; i++){
            hThread[i] = CreateThread(NULL, 0, WorkerThreadHandler, hcp, 0, &dwThreadID[i]);
        }
    }
}

ServerWindow::~ServerWindow(){
    SetEvent(hShutdownEvent);

    for(int i=0; i<nLogicalProcessors * 2; i++){
        PostQueuedCompletionStatus(hcp, 0,0, NULL);
    }

    for(int i=0; i<nLogicalProcessors * 2; i++){
        WaitForSingleObject(hThread[i], INFINITE);
        if(hThread[i]){ CloseHandle(hThread[i]); }
    }

    free(hThread);
    free(dwThreadID);

    if(hcp){ CloseHandle(hcp); }
	DeleteCriticalSection(&cs);
    CloseHandle(hShutdownEvent);
    WSACleanup();
}

BOOL ServerWindow::Listening(){
    listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    ServerAddress.sin_port = htons(SERVERPORT);
    ServerAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    ServerAddress.sin_family = AF_INET;

    ret = setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&dwReuseAddressOption, sizeof(dwReuseAddressOption));
    if(ret == SOCKET_ERROR){return FALSE;}

    ret = bind(listen_sock, (struct sockaddr*)&ServerAddress, sizeof(ServerAddress));
    if(ret == SOCKET_ERROR){return FALSE;}

    ret = listen(listen_sock, SOMAXCONN);
    if(ret == SOCKET_ERROR){return FALSE;}

    ret = setsockopt(listen_sock, SOL_SOCKET, SO_KEEPALIVE, (const char*)&dwKeepAliveOption, sizeof(dwKeepAliveOption));
    if(ret == SOCKET_ERROR){return FALSE;}

    l_LingerOption.l_onoff = 1;
    l_LingerOption.l_linger = 10;
    ret = setsockopt(listen_sock, SOL_SOCKET, SO_LINGER, (const char*)&l_LingerOption, sizeof(l_LingerOption));
    if(ret == SOCKET_ERROR){return FALSE;}

    return TRUE;
}

void ServerWindow::WaitForConnections(){
    SOCKET Client_Socket;
    struct sockaddr_in Client;
    int cbAddress;

    while(bRunning){
        cbAddress = sizeof(Client);
        Client_Socket = accept(listen_sock, (struct sockaddr*)&Client, &cbAddress);
        if(Client_Socket == INVALID_SOCKET){ break; }

        char IP[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &Client.sin_addr, IP, sizeof(IP));
        // ShowText("\r\n[TCP Server] Client Accept : IP = %s(%d)\r\n", IP, ntohs(Client.sin_port));

        CreateIoCompletionPort((HANDLE)Client_Socket, hcp, 0, 0);
        // SocketInfo* NewSocketInfo = CreateSocketInfo(Client_Socket, RECV);

        WSABUF wsabuf;
        wchar_t buf[1024];
        wsabuf.buf = (char*)buf;
        wsabuf.len = BUFSIZE;

        WSAOVERLAPPED ov;
        DWORD Flags = 0, cbRecv = 0;
        ret = WSARecv(Client_Socket, &wsabuf, 1, &cbRecv, &Flags, &ov, NULL);
        if(ret == SOCKET_ERROR){
            if(WSAGetLastError() != WSA_IO_PENDING){
                // ShowError("WSARecv()");
            }
            continue;
        }
    }
}

LRESULT ServerWindow::OnSize(WPARAM wParam, LPARAM lParam){
    RECT crt;

    if(wParam != SIZE_MINIMIZED){
        if(hBitmap){
            DeleteObject(hBitmap);
            hBitmap = NULL;
        }

        GetClientRect(_hWnd, &crt);
        SetRect(&rcPannel, crt.left, crt.top, crt.right, crt.bottom / 3);
        SetRect(&rcChatEdit, crt.left, crt.bottom / 3, crt.right, crt.bottom / 3 * 2);

        SetWindowPos(hPannel, NULL, rcPannel.left, rcPannel.top, rcPannel.right, rcPannel.bottom, SWP_NOZORDER);
        SetWindowPos(hChatEdit, NULL, rcChatEdit.left, rcChatEdit.top, rcChatEdit.right, rcChatEdit.bottom, SWP_NOZORDER);
    }
    return 0;
}

LRESULT ServerWindow::OnPaint(WPARAM wParam, LPARAM lParam){
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(_hWnd, &ps);
    EndPaint(_hWnd, &ps);
    return 0;
}

LRESULT ServerWindow::OnCommand(WPARAM wParam, LPARAM lParam){
    switch(LOWORD(wParam)){
        case 0:
            break;
    }
    return 0;
}

LRESULT ServerWindow::OnCreate(WPARAM wParam, LPARAM lParam){
    hPannel = CreateWindow(L"static", NULL, WS_VISIBLE | WS_CHILD | WS_BORDER | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0,0,0,0, _hWnd, NULL, GetModuleHandle(NULL), NULL);
    hChatEdit = CreateWindow(L"Edit", NULL, WS_VISIBLE | WS_CHILD | WS_BORDER | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0,0,0,0, _hWnd, NULL, GetModuleHandle(NULL), NULL);

    return 0;
}

LRESULT ServerWindow::OnDestroy(WPARAM wParam, LPARAM lParam){
    if(GetWindowLongPtr(_hWnd, GWL_STYLE) & WS_OVERLAPPEDWINDOW){
        PostQuitMessage(0);
    }
    return 0;
}

DWORD WINAPI ServerWindow::WorkerThreadHandler(LPVOID lpArg){
    return 0;
}
