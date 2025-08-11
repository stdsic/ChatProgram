#include "ServerWindow.h"
#include <strsafe.h>
#include <math.h>
#define min(a,b) (((a) < (b)) ? (a) : (b))
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

    AllocConsole();
    Listening();
    PostAccept();
    return 0;
}

LRESULT ServerWindow::OnDestroy(WPARAM wParam, LPARAM lParam){
    FreeConsole();
    if(GetWindowLongPtr(_hWnd, GWL_STYLE) & WS_OVERLAPPEDWINDOW){
        PostQuitMessage(0);
    }
    delete this;
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
        BroadCastQ = CreateQueue(0x1000);
        ReleaseQ = CreateQueue(nLogicalProcessors * 4);

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

    if(ReleaseQ){ DestroyQueue(ReleaseQ); }
    if(BroadCastQ){ DestroyQueue(BroadCastQ); }
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

    FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL,
            ErrorCode,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPSTR)&lpMsgBuf,
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

BOOL ServerWindow::Listening(){
    // 초기화 및 listen 전용 소켓 생성
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

    CreateIoCompletionPort((HANDLE)listen_sock, hcp, 0, 0);
    return TRUE;
}

void ServerWindow::PostAccept(){
    DWORD dwRecvBytes;

    // 세션을 하나씩만 생성
    ClientSession *NewSession = GetSession(nLogicalProcessors * 4);
    if(NewSession != NULL){
        NewSession->SetSocket(CreateSocket());
        
        IOEvent *NewEvent = new IOEvent();
        NewEvent->ResetTasks();
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
                ReleaseSession(nLogicalProcessors * 4);
                delete NewEvent;
            }
        } 
    }
}

void ServerWindow::Processing(){
    DWORD dwTrans = 0;
    ULONG_PTR Key;

    IOEvent *NewEvent = NULL;
    ClientSession *Session = NULL;
    WSABUF buf;
    DWORD dwError, dwBytes, dwFlags;

    int cbSize;
    wchar_t IP[INET_ADDRSTRLEN];
    struct sockaddr_in NewRemoteAddress;

    while(1){
        BOOL bWork = GetQueuedCompletionStatus(hcp, &dwTrans, (ULONG_PTR*)&Key, (OVERLAPPED**)&NewEvent, INFINITE);

        Session = NewEvent->Session;
        IOEventType EventType = NewEvent->Type;
        if(!bWork && NewEvent == NULL){ break; }

        if(bWork){
            switch(EventType){
                case IOEventType::CONNECT:
                    DebugMessage(L"CONNECT 이벤트가 발생하였습니다.\n");
                    if(Session == NULL){ DebugMessage(L"세션에 대한 포인터가 유효하지 않습니다.\n"); break;}
                    if(Session->IsConnected()){ DebugMessage(L"이미 연결된 세션을 대상으로 비정상적인 CONNECT 이벤트가 발생하였습니다.\n"); break; }
                    if(Session->GetSocket() == INVALID_SOCKET){ 
                        DebugMessage(L"세션이 가진 소켓이 유효하지 않습니다.\n세션을 해제합니다.\n");
                        PostDisconnect(Session); 
                        break;
                    }

                    SafeInit(Session, IOEventType::RECV);
                    CreateIoCompletionPort((HANDLE)Session->GetSocket(), hcp, 0, 0);
                    Session->SetConnected(TRUE);

                    buf.buf = (char*)Session->GetRecvBuffer();
                    buf.len = sizeof(wchar_t) * Session->GetCapacity();

                    dwBytes = dwFlags = 0;
                    ret = WSARecv(
                            Session->GetSocket(),
                            &buf,
                            1,
                            &dwBytes,
                            &dwFlags,
                            (OVERLAPPED*)&Session->RecvEvent,
                            NULL
                            );

                    dwError = WSAGetLastError();
                    if(ret == SOCKET_ERROR && dwError != WSA_IO_PENDING){
                        ErrorHandler(dwError, Session);
                    }
                    break;

                case IOEventType::DISCONNECT:
                    DebugMessage(L"DISCONNECT 이벤트가 발생하였습니다.\n");
                    if(Session == NULL){ DebugMessage(L"세션에 대한 포인터가 유효하지 않습니다.\n"); break;}
                    if(!Enqueue(ReleaseQ, Session)){ DebugMessage(L"세션 해제 큐에 세션을 등록하지 못했습니다.\n"); break;}
                    ReleaseSession(nLogicalProcessors * 4);
                    break;

                case IOEventType::ACCEPT:
                    DebugMessage(L"ACCEPT 이벤트가 발생하였습니다.\n");
                    if(Session == NULL){ DebugMessage(L"세션에 대한 포인터가 유효하지 않습니다.\n"); break;}

                    delete NewEvent;
                    if(setsockopt(
                                Session->GetSocket(),
                                SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
                                (const char*)&listen_sock,
                                sizeof(listen_sock)
                                ) == SOCKET_ERROR){
                        DebugMessage(L"setsockopt 함수가 실패하였습니다.\n세션을 해제합니다.\n");
                        delete Session;
                        Session = NULL;
                        PostAccept();
                        break;
                    }

                    cbSize = sizeof(NewRemoteAddress);
                    if(getpeername(
                                Session->GetSocket(),
                                (struct sockaddr*)&NewRemoteAddress,
                                &cbSize) == SOCKET_ERROR){
                        DebugMessage(L"getpeername 함수가 실패하였습니다.\n세션을 해제합니다.\n");
                        delete Session;
                        Session = NULL;
                        PostAccept();
                        break;
                    }

                    if(InetNtopW(AF_INET, &NewRemoteAddress.sin_addr, IP, INET_ADDRSTRLEN) != NULL){
                        // Show IP Address

                        // Post ConnectEvent
                        Session->SetRemoteAddress(NewRemoteAddress);
                        PostConnect(Session);
                    }
                    PostAccept();
                    break;

                case IOEventType::RECV:
                    DebugMessage(L"RECV 이벤트가 발생하였습니다.\n");
                    if(Session == NULL){ DebugMessage(L"세션에 대한 포인터가 유효하지 않습니다.\n"); break;}

                    if(dwTrans == 0){ 
                        DebugMessage(L"클라이언트가 연결을 정상적으로 종료하였습니다(closesocket/shutdown).\n세션을 해제합니다.\n");
                        PostDisconnect(Session);
                        break;
                    }else{
                        if(Session->GetSocket() == INVALID_SOCKET){
                            DebugMessage(L"세션이 가진 소켓이 유효하지 않습니다.\n세션을 해제합니다.\n");
                            PostDisconnect(Session);
                            break;
                        }
                        if(!Enqueue(BroadCastQ, Session)){ DebugMessage(L"브로드 캐스트 전용 큐에 세션을 등록하지 못했습니다.\n"); break; }
                        BroadCast(nLogicalProcessors * 4);

                        SafeInit(Session, IOEventType::RECV);
                        Session->SetConnected(TRUE);
                        memset(Session->GetRecvBuffer(), 0, sizeof(wchar_t) * Session->GetCapacity());

                        buf.buf = (char*)Session->GetRecvBuffer();
                        buf.len = sizeof(wchar_t) * Session->GetCapacity();

                        DebugMessage(L"WSARecv 함수를 호출하였습니다.\n");
                        dwBytes = 0, dwFlags = 0;
                        ret = WSARecv(
                                Session->GetSocket(),
                                &buf,
                                1,
                                &dwBytes,
                                &dwFlags,
                                (OVERLAPPED*)&Session->RecvEvent,
                                NULL
                                );

                        dwError = WSAGetLastError();
                        if(ret == SOCKET_ERROR && dwError != WSA_IO_PENDING){
                            ErrorHandler(dwError, Session);
                        }
                    }
                    break;

                case IOEventType::SEND:
                    Session = NewEvent->Session;
                    if(Session->IsSending() == TRUE){ Session->SetIOState(FALSE); }
                    DebugMessage(L"SEND 이벤트가 발생하였습니다.\n");
                    break;
            }
        }else{
            DWORD dwError = GetLastError();

            switch(dwError){
                case ERROR_SUCCESS:
                    DebugMessage(L"클라이언트가 정상적으로 접속을 종료하였습니다(FALSE + ERROR_SUCCESS)\n");
                    PostDisconnect(Session);
                    break;

                case ERROR_NETNAME_DELETED:         // 클라이언트가 연결을 끊음
                    DebugMessage(L"클라이언트와의 연결이 비정상적으로 종료되었습니다.\n프로세스 강제 종료 또는 방화벽, NAT, 네트워크 단절 등이 원인일 수 있습니다.\n");
                    PostDisconnect(Session);
                    break;

                case ERROR_CONNECTION_ABORTED:      // 연결이 비정상적으로 종료됨
                    DebugMessage(L"연결이 비정상적으로 종료되었습니다.\n");
                    PostDisconnect(Session);
                    break;

                case ERROR_OPERATION_ABORTED:       // 작업이 취소됨 (CancelIoEx 등)
                    DebugMessage(L"I/O 작업이 취소되었습니다.\n");
                    PostDisconnect(Session);
                    break;

                default:
                    DebugMessage(L"알 수 없는 에러가 발생하였습니다.\n세션과의 연결을 종료합니다.\n");
                    PostDisconnect(Session);
                    break;
            }
        }
    }
}

void ServerWindow::PostConnect(ClientSession *Session){
    if(Session == NULL){return;}
    Session->ConnectEvent.ResetTasks();
    Session->ConnectEvent.Session = Session;
    Session->ConnectEvent.Type = IOEventType::CONNECT;
    PostQueuedCompletionStatus(hcp, 0, (ULONG_PTR)Session, &Session->ConnectEvent.ov);
}

void ServerWindow::PostDisconnect(ClientSession *Session){
    if(Session == NULL){return;}
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
        SessionPool[i] = NULL;
    }
    delete [] SessionPool;
    delete [] bUse;
}

ServerWindow::ClientSession* ServerWindow::GetSession(int Count){
    for(int i=0; i<Count; i++){
        if(InterlockedCompareExchange((LONG*)&bUse[i], 1, 0) == 0){
            if(SessionPool[i] == NULL){ SessionPool[i] = new ClientSession; }
            return SessionPool[i];
        }
    }

    // 아래에 동적으로 세션을 추가하는 코드를 작성해도 좋다.
    // 만약 세션을 동적으로 관리할 예정이라면 CRITICAL_SECTION을 이용해서 동기화 해야 한다.
    return NULL;
}

void ServerWindow::ReleaseSession(int Count){
    // 동일한 세션에 대해 같은 동작을 할 가능성이 있으므로 
    // DISCONNECT 이벤트 발생시 ReleaseSession을 호출하기 전에 큐에 세션을 밀어넣고
    // 하나씩 빼서 필요한 처리를 하도록 구조를 변경할 필요가 있다.
    ClientSession* Session = (ClientSession*)Dequeue(ReleaseQ);

    EnterCriticalSection(&cs);
    if(Session != NULL){
        DebugMessage(L"세션을 해제합니다.\n");
        if(Session->GetSocket()){ 
            shutdown(Session->GetSocket(), SD_BOTH);
            closesocket(Session->GetSocket());
            Session->SetSocket(INVALID_SOCKET);
        }

        for(int i=0; i<Count; i++){ if(SessionPool[i] == Session){ bUse[i] = 0; break; } } 
        memset(Session->GetRecvBuffer(), 0, sizeof(wchar_t) * Session->GetCapacity());
        memset(Session->GetSendBuffer(), 0, sizeof(wchar_t) * Session->GetCapacity());
        Session->SetConnected(FALSE);
        Session->SetIOState(FALSE);
        Session->SetRemain(FALSE);
        Session->Init();
    }
    LeaveCriticalSection(&cs);
}

void ServerWindow::ErrorHandler(DWORD dwError, LPVOID lpArgs){
    ClientSession* Session = (ClientSession*)lpArgs;
    if(Session == NULL){return;}

    switch(dwError){
        case WSA_IO_PENDING:
            DebugMessage(L"Error Handler: 비동기 입출력으로 인한 I/O PENDING 상태입니다.\nI/O 작업이 정상적으로 실행됩니다.\n");
            break;

        case WSAECONNRESET:
            // 클라이언트가 연결을 강제로 종료
            DebugMessage(L"Error Handler: 클라이언트가 연결을 강제로 종료하였습니다.\n세션을 해제합니다.\n");
            PostDisconnect(Session);
            break;

        case WSAESHUTDOWN:
            // 소켓이 이미 닫힘
            DebugMessage(L"Error Handler: 이미 닫힌 소켓에 작업을 시도하였습니다.\n무효한 소켓이므로 세션을 해제합니다.\n");
            PostDisconnect(Session);
            break;

        case WSAENOTCONN:
            // 연결되지 않은 소켓
            DebugMessage(L"Error Handler: 연결되지 않은 소켓에 작업을 시도하였습니다.\n무효한 소켓이므로 세션을 해제합니다.\n");
            PostDisconnect(Session);
            break;

        case WSAENOTSOCK:
            // 유효하지 않은 소켓
            DebugMessage(L"Error Handler: 소켓 리소스가 유효하지 않습니다.\n무효한 소켓이므로 세션을 해제합니다.\n");
            PostDisconnect(Session);
            break;

        case WSAEMSGSIZE:
            // 버퍼 크기 초과
            DebugMessage(L"Error Handler: 송수신 작업에 필요한 데이터의 크기가 버퍼 크기를 초과하였습니다.\n세션을 해제합니다.\n");
            PostDisconnect(Session);
            break;

        case WSAEWOULDBLOCK:
            // 비동기 통신에서도 드물게 발생함
            DebugMessage(L"Error Handler: 논블로킹 소켓에서 recv() 또는 send(), connect() 함수를 호출하였습니다.\n데이터를 주고 받을 준비가 완료되지 않은 상태이므로 다시 시도합니다.\n");
            break;

        default:
            // 알 수 없는 에러
            DebugMessage(L"Error Handler: 알 수 없는 에러가 발생하였습니다.\n세션을 해제합니다.\n");
            PostDisconnect(Session);
            break;
    }
}

void ServerWindow::DebugMessage(LPCWSTR fmt, ...){
    HANDLE hInput, hOutput, hError;

    hInput = GetStdHandle(STD_INPUT_HANDLE);
    hOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    hError = GetStdHandle(STD_ERROR_HANDLE);

    WCHAR Debug[0x1000];
    va_list arg;
    va_start(arg, fmt);
    StringCbVPrintf(Debug, sizeof(Debug), fmt, arg);
    va_end(arg);

    DWORD dwWritten;
    WriteConsole(hOutput, Debug, wcslen(Debug), &dwWritten, NULL);
}

void ServerWindow::TypeHandler(IOEventType Type){
    switch(Type){
        case IOEventType::CONNECT:
            DebugMessage(L"IOEventType: CONNECT\n");
            break;

        case IOEventType::DISCONNECT:
            DebugMessage(L"IOEventType: DISCONNECT\n");
            break;

        case IOEventType::ACCEPT:
            DebugMessage(L"IOEventType: ACCEPT\n");
            break;

        case IOEventType::RECV:
            DebugMessage(L"IOEventType: RECV\n");
            break;

        case IOEventType::SEND:
            DebugMessage(L"IOEventType: SEND\n");
            break;
    }
}

// CompletionPort 서버 모델에서 큐를 사용하지 않고 BroadCast를 구현할 때의 문제점을 살펴보자.
void ServerWindow::BroadCast(int Count){
    // 여러 클라이언트가 접속한 상황이라 가정한다.
    // 아래 함수의 구조를 보면 WSASend는 접속해 있는 세션들을 대상으로 WSASend를 여러 번 호출한다.
    // 하나의 클라이언트가 메세지를 보냈을 때 서버에서는 받아온 데이터를 접속해 있는 전체 세션에 전달하는데, 이때 문제가 발생한다.
    // 클라이언트 하나가 데이터를 보내면 N개의 클라이언트를 대상으로 WSASend를 호출하고,
    // 클라이언트 두 개가 데이터를 보내면 2N개의 클라이언트를 대상으로 WSASend를 호출한다.
    // 이렇게만 해도 CompletionPort의 이벤트 큐에서 처리하지 못한 데이터가 쌓여 있을 수 있다.
    // 때문에 CompletionPort 서버 모델은 작업 큐를 가지는게 기본이다.

    // 애초에 공식 문서에서 다음과 같은 문구를 찾아볼 수 있다.
    // “Processes that handle many concurrent asynchronous I/O requests can do so more quickly and efficiently by using I/O completion ports in conjunction with a pre-allocated thread pool…”
    // “The system queues completed I/O requests to the completion port, and threads wait for these queued requests.”
    // 영어를 잘 하진 않지만 뉘앙스를 보면, "서버 로직도 큐잉 기반으로 설계하면 CompletionPort 모델의 병렬성과 효율성을 최대한 활용할 수 있다"고 해석할 수 있다.
    // 따라서, BroadCast 따위의 함수를 만들 때에는 큐 자료구조를 활용하는 것이 일반적이며, 권장되는 모델이라고 볼 수 있다.

    // 현재 프로젝트는 최대 32~128개의 세션만을 수용하는 작은 규모의 서버이므로 간단한 구조로 설계해도 충분하다.
    // for문 안의 마지막 if 분기를 보면 보내야 할 작업이 남아있는 경우 bRemaining 변수를 TRUE로 변경한다.
    // 

    ClientSession* Session = (ClientSession*)Dequeue(BroadCastQ);
    if(Session == NULL){return;}
    
    EnterCriticalSection(&cs);
    for(int i=0; i<Count; i++){
        if(SessionPool[i] == NULL){ continue; }
        if(SessionPool[i] == Session){ continue; }
        if(!SessionPool[i]->IsConnected()){ continue; }
        if(SessionPool[i]->GetSocket() == INVALID_SOCKET){ continue; }
        if(SessionPool[i]->IsSending()){ SessionPool[i]->SetRemain(TRUE); continue; }

        memset(SessionPool[i]->GetSendBuffer(), 0, sizeof(wchar_t) * SessionPool[i]->GetCapacity());

        int Capacity = Session->GetCapacity();
        int Length = wcslen(Session->GetRecvBuffer());
        wchar_t *TempBuffer = Session->GetRecvBuffer();
        TempBuffer[Length] = 0;

        wcsncpy(SessionPool[i]->GetSendBuffer(), TempBuffer, min(Length, Capacity - 1));

        SafeInit(SessionPool[i], IOEventType::SEND);

        WSABUF buf;
        buf.buf = (char*)SessionPool[i]->GetSendBuffer();
        buf.len = sizeof(wchar_t) * wcslen(SessionPool[i]->GetSendBuffer());

        DebugMessage(L"WSASend 함수를 호출하였습니다.\n");

        DWORD dwBytes = 0, dwFlags = 0;
        ret = WSASend(
                SessionPool[i]->GetSocket(),
                &buf,
                1,
                &dwBytes,
                dwFlags,
                (OVERLAPPED*)&SessionPool[i]->SendEvent,
                NULL
                );

        DWORD dwError = WSAGetLastError();
        if(ret == SOCKET_ERROR && dwError != WSA_IO_PENDING){
            ErrorHandler(dwError, Session);
        }
        SessionPool[i]->SetIOState(TRUE);
    }
    LeaveCriticalSection(&cs);
}

void ServerWindow::SafeInit(ClientSession *Session, IOEventType EventType){
    if(Session == NULL){return;}

    switch(EventType){
        case IOEventType::RECV:
            Session->RecvEvent.ResetTasks();
            Session->RecvEvent.Type = EventType;
            Session->RecvEvent.Session = Session;
            break;
        case IOEventType::SEND:
            Session->SendEvent.ResetTasks();
            Session->SendEvent.Type = EventType;
            Session->SendEvent.Session = Session;
            break;
        case IOEventType::CONNECT:
            Session->ConnectEvent.ResetTasks();
            Session->ConnectEvent.Type = EventType;
            Session->ConnectEvent.Session = Session;
            break;
        case IOEventType::DISCONNECT:
            Session->DisconnectEvent.ResetTasks();
            Session->DisconnectEvent.Type = EventType;
            Session->DisconnectEvent.Session = Session;
            break;
    }
}
