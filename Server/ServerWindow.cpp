#include "ServerWindow.h"
#include <strsafe.h>
#include <math.h>
#define BUFSIZE 0x400

LRESULT ServerWindow::Handler(UINT iMessage, WPARAM wParam, LPARAM lParam){
    for(DWORD i=0; i<sizeof(MainMsg) / sizeof(MainMsg[0]); i++) {
        if (MainMsg[i].iMessage == iMessage) {
            return (this->*MainMsg[i].lpfnWndProc)(wParam, lParam);
        }
    }
    return DefWindowProc(_hWnd, iMessage, wParam, lParam);
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
    hPannel = CreateWindow(L"static", NULL, WS_VISIBLE | WS_CHILD | WS_BORDER | WS_CLIPSIBLINGS, 0,0,0,0, _hWnd, NULL, GetModuleHandle(NULL), NULL);
    hChatEdit = CreateWindow(L"Edit", NULL, WS_VISIBLE | WS_CHILD | WS_BORDER | WS_CLIPSIBLINGS | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL , 0,0,0,0, _hWnd, NULL, GetModuleHandle(NULL), NULL);

    Listening();
    PostAccept();
    return 0;
}

LRESULT ServerWindow::OnDestroy(WPARAM wParam, LPARAM lParam){
    if(GetWindowLongPtr(_hWnd, GWL_STYLE) & WS_OVERLAPPEDWINDOW){
        PostQuitMessage(0);
    }
    return 0;
}

DWORD WINAPI ServerWindow::WorkerThreadHandler(LPVOID lpArg){
    ServerWindow *Server = (ServerWindow*)lpArg;
    Server->Processing();
    return 0;
}

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

LPFN_ACCEPTEX ServerWindow::lpfnAcceptEx = NULL;
LPFN_CONNECTEX ServerWindow::lpfnConnectEx = NULL;
LPFN_DISCONNECTEX ServerWindow::lpfnDisconnectEx = NULL;

ServerWindow::ServerWindow() : bCritical(FALSE), bRunning(FALSE), dwKeepAliveOption(1), dwReuseAddressOption(1), bUse(NULL), SessionPool(NULL){
    if(WSAStartup(MAKEWORD(2,2), &wsa) == 0){
        Dummy = CreateSocket();
        if(!Dummy){ throw Exception(); }
        if(!BindWSAFunction(Dummy, WSAID_ACCEPTEX, (LPVOID*)&ServerWindow::lpfnAcceptEx)){ throw Exception(); }
        if(!BindWSAFunction(Dummy, WSAID_CONNECTEX, (LPVOID*)&ServerWindow::lpfnConnectEx)){ throw Exception(); }
        if(!BindWSAFunction(Dummy, WSAID_DISCONNECTEX, (LPVOID*)&ServerWindow::lpfnDisconnectEx)){ throw Exception(); }
        if(!(bCritical = InitializeCriticalSectionEx(&cs, 4000, 0))){ throw Exception(FALSE); }

        memset(&l_LingerOption, 0, sizeof(linger));
        memset(&ServerAddress, 0, sizeof(sockaddr_in));

        SYSTEM_INFO si;
        GetSystemInfo(&si);
        nLogicalProcessors = si.dwNumberOfProcessors;

        hThread = (HANDLE*)malloc(sizeof(HANDLE) * (nLogicalProcessors * 2));
        dwThreadID = (DWORD*)malloc(sizeof(DWORD) * (nLogicalProcessors * 2));

        if(!hThread){ throw Exception(FALSE); }
        if(!dwThreadID){ throw Exception(FALSE); }

        hcp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0,0);
        if(!hcp){ throw Exception(FALSE); }

        CreateSessionPool(nLogicalProcessors * 4);
        for(int i=0; i < nLogicalProcessors * 2; i++){
            hThread[i] = CreateThread(NULL, 0, WorkerThreadHandler, this, 0, &dwThreadID[i]);
        }
    }
}

ServerWindow::~ServerWindow(){
    if(hShutdownEvent){ SetEvent(hShutdownEvent); }

    for(int i=0; i<nLogicalProcessors * 2; i++){
        if(hcp){ PostQueuedCompletionStatus(hcp, 0,0, NULL); }
    }

    for(int i=0; i<nLogicalProcessors * 2; i++){
        if(hThread[i]){
            WaitForSingleObject(hThread[i], INFINITE);
            CloseHandle(hThread[i]);
        }
    }

    if(SessionPool){ DeleteSessionPool(nLogicalProcessors * 4); }
    if(hThread){ free(hThread); }
    if(dwThreadID){ free(dwThreadID); }
    if(bCritical){ DeleteCriticalSection(&cs); }
    if(hcp){ CloseHandle(hcp); }
    if(listen_sock){ closesocket(listen_sock); }
    if(Dummy){ closesocket(Dummy); }
    if(hShutdownEvent){ CloseHandle(hShutdownEvent); }

    WSACleanup();
}

void ServerWindow::Exception::GetErrorMessage(BOOL WSAError){
    LPVOID lpMsgBuf = NULL;

    if(WSAError){
        ErrorCode = WSAGetLastError();
    }else{
        ErrorCode = GetLastError();
    }

    FormatMessage(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL,
            ErrorCode,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPWSTR)&lpMsgBuf,
            0,
            NULL
            );

    if(lpMsgBuf){
        MessageBox(HWND_DESKTOP, (const wchar_t*)lpMsgBuf, L"Error", MB_ICONERROR | MB_OK);
        LocalFree(lpMsgBuf);
        return;
    }else{
        MessageBox(HWND_DESKTOP, L"알 수 없는 오류가 발생했습니다.", L"Error", MB_ICONERROR | MB_OK);
    }
}

SOCKET ServerWindow::CreateSocket(){
    return WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
}

BOOL ServerWindow::BindWSAFunction(SOCKET Socket, GUID Serial, LPVOID* lpfn){
    DWORD dwBytes = 0;
    // 포인터 타입의 매개변수는 입출력 인수이며 GUID를 전달하면 확장 함수의 주소를 리턴하여
    // 사용자가 포인터로 그 주소 공간에 접근할 수 있도록 만들었다.
    return WSAIoctl(
            Socket,                                     // 대상 소켓
            SIO_GET_EXTENSION_FUNCTION_POINTER,         // 입출력 컨트롤 옵션이자 제어 코드
            &Serial,                                    // 입력 버퍼에 대한 포인터
            sizeof(Serial),                             // 입력 버퍼의 크기(바이트)
            lpfn,                                       // 출력 버퍼에 대한 포인터
            sizeof(*lpfn),                              // 출력 버퍼의 크기(바이트)
            &dwBytes,                                   // 실제 입출력 바이트 수
            NULL,                                       // 비동기 구조체
            NULL) == 0;                                 // 콜백 함수

}

// 초기화 및 listen 전용 소켓 생성
BOOL ServerWindow::Listening(){
    listen_sock = CreateSocket();
    if(!listen_sock){ return FALSE; }

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

    // AcceptEx 윈도우 전용 함수는 OVERLAPPED 작업이 커널 수준에서 대기하다가 연결이 들어오면 IOCP를 통해 비동기 통지를 보낸다.
    // 기존 accept 함수를 사용하던 때에는 연결이 들어올 때마다 매번 CreateIoCompletionPort 함수를 호출하여 소켓을 등록해야 했다.
    CreateIoCompletionPort((HANDLE)listen_sock, hcp, 0, 0);

    return TRUE;
}

// 사용자 정의 메세지 정의 필요
// WSARecv가 곧장 리턴하는지 확인
// 매번 WSARecv를 새로 호출해야하는지 확인
void ServerWindow::PostAccept(){
    DWORD dwRecvBytes;

    for(int i=0; i<nLogicalProcessors; i++){
        ClientSession *NewSession = GetSession(nLogicalProcessors * 4);
        if(!NewSession){ throw Exception(); }
        NewSession->SetSocket(CreateSocket());

        IOEvent *NewEvent = new IOEvent();
        memset(&NewEvent->ov, 0, sizeof(OVERLAPPED));
        NewEvent->Type = IOEventType::ACCEPT;
        NewEvent->Session = NewSession;

        // [ 클라이언트 주소 공간 ] + [ 서버 주소 공간 ] + [ 선택적 데이터 공간 ]
        if(NewSession->GetSocket() != SOCKET_ERROR){
            ret = lpfnAcceptEx(
                    listen_sock,                                // 듣기 전용 소켓
                    NewSession->GetSocket(),                    // 연결 전용 소켓
                    NewSession->GetAcceptBuffer(),              // AcceptEx 내부적으로 사용됨, 주소를 저장할 버퍼
                    0,
                    sizeof(struct sockaddr_in) + 16,            // 로컬 주소 버퍼 크기
                    sizeof(struct sockaddr_in) + 16,            // 원격 주소 버퍼 크기
                    &dwRecvBytes,
                    &NewEvent->ov
                    );

            if(ret == 0 && WSAGetLastError() != ERROR_IO_PENDING){
                ReleaseSession(NewSession, nLogicalProcessors);
                delete NewEvent;
            }
        }
    }
}

void ServerWindow::Processing(){
    DWORD dwTrans = 0;
    ULONG_PTR Key;

    IOEvent *NewEvent = NULL;
    BOOL bWork = GetQueuedCompletionStatus(hcp, &dwTrans, (PULONG_PTR)&Key, (OVERLAPPED**)&NewEvent, INFINITE);
    ClientSession *Session = NewEvent->Session;
    IOEventType EventType = NewEvent->Type;

    if(!bWork){
        DWORD dwError = GetLastError();

        switch(dwError){
            case ERROR_NETNAME_DELETED:         // 클라이언트가 연결을 끊음
            case ERROR_CONNECTION_ABORTED:      // 연결이 비정상적으로 종료됨
            case ERROR_OPERATION_ABORTED:       // 작업이 취소됨 (CancelIoEx 등)
                PostDisconnect(Session);
                break;

            default:
                // LogUnexpectedError(dwError);
                PostDisconnect(Session);
                break;
        }
    }else if(bWork && dwTrans == 0 && EventType != IOEventType::DISCONNECT && EventType != IOEventType::CONNECT){
        // 정상 종료
        PostDisconnect(Session);
    }else{
        switch(NewEvent->Type){
            case IOEventType::CONNECT:
                Session->SetConnected(TRUE);
                // 세션 풀로 관리하는 구조가 아니라면 여기서 세션을 추가하면 된다.
                // AppendSession();

                if(Session->IsConnected()){
                    WSABUF buf;
                    buf.buf = (char*)Session->GetWritePosition();
                    buf.len = sizeof(wchar_t) * Session->FreeSize();

                    DWORD dwBytes = 0, dwFlags = 0;
                    ret = WSARecv(
                            Session->GetSocket(),
                            &buf,
                            1,
                            &dwBytes,
                            &dwFlags,
                            (OVERLAPPED*)&Session->RecvEvent.ov,
                            NULL
                            );
                    if(ret == SOCKET_ERROR){
                        DWORD dwError = WSAGetLastError();
                        switch(dwError){
                            case WSA_IO_PENDING:
                                // 정상
                                break;

                            case WSAECONNRESET:
                                // 클라이언트가 연결을 강제로 종료
                                PostDisconnect(Session);
                                break;

                            case WSAESHUTDOWN:
                                // 소켓이 이미 닫힘
                                PostDisconnect(Session);
                                break;

                            case WSAENOTCONN:
                                // 연결되지 않은 소켓
                                PostDisconnect(Session);
                                break;

                            case WSAENOTSOCK:
                                // 유효하지 않은 소켓
                                PostDisconnect(Session);
                                break;

                            case WSAEMSGSIZE:
                                // 버퍼 크기 초과
                                break;

                            case WSAEWOULDBLOCK:
                                // 비동기 통신에서도 드물게 발생함
                                break;

                            default:
                                // 알 수 없는 에러
                                PostDisconnect(Session);
                                break;
                        }
                    }
                }
                break;

            case IOEventType::DISCONNECT:
                ReleaseSession(Session, nLogicalProcessors);
                break;

            case IOEventType::ACCEPT:
                if(Session){
                    delete NewEvent;
                    if(setsockopt(
                                Session->GetSocket(),
                                SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
                                (const char*)&listen_sock,
                                sizeof(listen_sock)
                                ) == SOCKET_ERROR){
                        delete Session;
                        PostAccept();
                        break;
                    }

                    struct sockaddr_in NewRemoteAddress;
                    int cbSize = sizeof(NewRemoteAddress);

                    if(getpeername(
                                Session->GetSocket(),
                                (struct sockaddr*)&NewRemoteAddress,
                                &cbSize) == SOCKET_ERROR){
                        delete Session;
                        PostAccept();
                        break;
                    }

                    wchar_t IP[INET_ADDRSTRLEN];
                    if(InetNtopW(AF_INET, &NewRemoteAddress.sin_addr, IP, INET_ADDRSTRLEN) != NULL){
                        // Show IP Address


                        // Post ConnectEvent
                        Session->SetRemoteAddress(NewRemoteAddress);
                        PostConnect(Session);
                    }
                }
                PostAccept();
                break;

            case IOEventType::RECV:
                break;

            case IOEventType::SEND:
                break;
        }
    }
}

void ServerWindow::PostConnect(ClientSession *Session){
    Session->ConnectEvent.ResetTasks();
    Session->ConnectEvent.Session = Session;
    Session->ConnectEvent.Type = IOEventType::CONNECT;
    PostQueuedCompletionStatus(hcp, 0, (ULONG_PTR)Session, &Session->ConnectEvent.ov);
}

void ServerWindow::PostDisconnect(ClientSession *Session){
    Session->DisconnectEvent.ResetTasks();
    Session->DisconnectEvent.Session = Session;
    Session->DisconnectEvent.Type = IOEventType::DISCONNECT;
    PostQueuedCompletionStatus(hcp, 0, (ULONG_PTR)Session, &Session->DisconnectEvent.ov);
}

void ServerWindow::CreateSessionPool(int Count){
    bUse = new LONG[Count];
    memset(bUse, 0, sizeof(LONG) * Count);

    SessionPool = new ClientSession*[Count];
    for(int i=0; i<Count; i++){
        SessionPool[i] = new ClientSession();
    }
}

void ServerWindow::DeleteSessionPool(int Count){
    for(int i=0; i<Count; i++){
        delete SessionPool[i];
    }
    delete [] SessionPool;
    delete [] bUse;
}

ServerWindow::ClientSession* ServerWindow::GetSession(int Count){
    for(int i=0; i<Count; i++){
        if(InterlockedCompareExchange((LONG*)&bUse[i], 1, 0) == 0){
            return SessionPool[i];
        }
    }

    // 아래에 동적으로 세션을 추가하는 코드를 작성해도 좋다.
    // 만약 세션을 동적으로 관리할 예정이라면 CRITICAL_SECTION을 이용해서 동기화 해야 한다.
    return NULL;
}

void ServerWindow::ReleaseSession(ClientSession* Session, int Count){
    for(int i=0; i<Count; i++){
        if(SessionPool[i] == Session){
            if(SessionPool[i]->GetSocket()){ 
                closesocket(SessionPool[i]->GetSocket());
            }

            // 경쟁 조건(bUse[i] == 1)이 있을 때에는 인터락 함수를 활용해야 하나,
            // 그렇지 않은 경우에는 곧장 값을 대입해도 상관없다.
            // InterlockedCompareExchange((LONG*)&bUse[i], 0, 1);

            bUse[i] = 0;
        }
    }
}

