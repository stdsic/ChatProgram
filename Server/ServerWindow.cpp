#include "ServerWindow.h"
#include <strsafe.h>
#include <math.h>
#define min(a,b) (((a) < (b)) ? (a) : (b))
#define BUFSIZE 0x400
#define IDC_BTNSTART    40001
#define IDC_BTNSTOP     40002
#define EXIT_KEY        0xFFFFFFFF

LPFN_ACCEPTEX ServerWindow::lpfnAcceptEx = NULL;
LPFN_CONNECTEX ServerWindow::lpfnConnectEx = NULL;
LPFN_DISCONNECTEX ServerWindow::lpfnDisconnectEx = NULL;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//// *Message Procedures
////
////
////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
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
        SetRect(&rcStatus, 10, 10, crt.right - 20, 20);
        SetRect(&rcStartBtn, 10, 40, 50, 25);
        SetRect(&rcStopBtn, 60, 40, 50, 25);

        SetWindowPos(hPannel, NULL, rcPannel.left, rcPannel.top, rcPannel.right, rcPannel.bottom, SWP_NOZORDER);
        SetWindowPos(hChatEdit, NULL, rcChatEdit.left, rcChatEdit.top, rcChatEdit.right, rcChatEdit.bottom, SWP_NOZORDER);
        SetWindowPos(hStatusText, NULL, rcStatus.left, rcStatus.top, rcStatus.right, rcStatus.bottom, SWP_NOZORDER);
        SetWindowPos(hStartBtn, NULL, rcStartBtn.left, rcStartBtn.top, rcStartBtn.right, rcStartBtn.bottom, SWP_NOZORDER);
        SetWindowPos(hStopBtn, NULL, rcStopBtn.left, rcStopBtn.top, rcStopBtn.right, rcStopBtn.bottom, SWP_NOZORDER);
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
        case IDC_BTNSTART:
            DebugMessage(L"서버를 활성화 하였습니다.\r\n");
            StartListening();
            PostAccept();
            StartThreads();
            break;

        case IDC_BTNSTOP:
            DebugMessage(L"서버를 종료합니다.\r\n");
            StopListening();
            for(int i=0; i<nSessions; i++){
                if(SessionPool[i]){
                    // DebugMessage(L"Call Enqueue = %d\r\n", i);
                    Enqueue(ReleaseQ, SessionPool[i]);
                }
            }
            for(int i=0; i<nSessions; i++){
                // DebugMessage(L"Call ReleaseSession = %d\r\n", i);
                ReleaseSession();
            }
            StopThreads();
            break;
    }
    return 0;
}

LRESULT ServerWindow::OnTimer(WPARAM wParam, LPARAM lParam){
    switch(wParam){
        case 1:
            OnDraw();
            break;
    }
    return 0;
}

LRESULT ServerWindow::OnCreate(WPARAM wParam, LPARAM lParam){
    hPannel = CreateWindow(L"static", NULL, WS_VISIBLE | WS_CHILD | WS_BORDER | WS_CLIPSIBLINGS, 0,0,0,0, _hWnd, NULL, GetModuleHandle(NULL), NULL);
    hChatEdit = CreateWindow(L"Edit", NULL, WS_VISIBLE | WS_CHILD | WS_BORDER | WS_CLIPSIBLINGS | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, 0,0,0,0, _hWnd, NULL, GetModuleHandle(NULL), NULL);
    hStatusText = CreateWindowEx(0, L"static", L"🟢 Server Running | Clients: 0", WS_CHILD | WS_VISIBLE | SS_LEFT, 0,0,0,0, _hWnd, NULL, GetModuleHandle(NULL), NULL);
    hStartBtn = CreateWindowEx(0, L"button", L"Start", WS_CHILD | WS_VISIBLE, 0,0,0,0, _hWnd, (HMENU)(INT_PTR)IDC_BTNSTART, GetModuleHandle(NULL), NULL);
    hStopBtn = CreateWindowEx(0, L"button", L"Stop", WS_CHILD | WS_VISIBLE, 0,0,0,0, _hWnd, (HMENU)(INT_PTR)IDC_BTNSTOP, GetModuleHandle(NULL), NULL);
    SetTimer(_hWnd, 1, 10, NULL);
    return 0;
}

LRESULT ServerWindow::OnDestroy(WPARAM wParam, LPARAM lParam){
    KillTimer(_hWnd, 1);
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


///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//// *ServerWindow Constructor
////
////
////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

ServerWindow::ServerWindow() : bCritical(FALSE), dwKeepAliveOption(1), dwReuseAddressOption(1), bUse(NULL), SessionPool(NULL), nConnected(0){
    if(WSAStartup(MAKEWORD(2,2), &wsa) == 0){
        Dummy = CreateSocket();
        if(!Dummy){ throw Exception(); }
        if(!BindWSAFunction(Dummy, WSAID_ACCEPTEX, (LPVOID*)&ServerWindow::lpfnAcceptEx)){ throw Exception(); }
        if(!BindWSAFunction(Dummy, WSAID_CONNECTEX, (LPVOID*)&ServerWindow::lpfnConnectEx)){ throw Exception(); }
        if(!BindWSAFunction(Dummy, WSAID_DISCONNECTEX, (LPVOID*)&ServerWindow::lpfnDisconnectEx)){ throw Exception(); }
        if(!(bCritical = InitializeCriticalSectionEx(&cs, 4000, 0))){ throw Exception(FALSE); }

        memset(&l_LingerOption, 0, sizeof(linger));
        memset(&ServerAddress, 0, sizeof(sockaddr_in));
        BroadCastQ = CreateQueue(0x100);
        ReleaseQ = CreateQueue(0x100);

        SYSTEM_INFO si;
        GetSystemInfo(&si);
        nLogicalProcessors = si.dwNumberOfProcessors;
        nWorkerThreads = nLogicalProcessors * 2;
        nSessions = nLogicalProcessors * 4;
        ret = 0;

        hThread = (HANDLE*)malloc(sizeof(HANDLE) * nWorkerThreads);
        dwThreadID = (DWORD*)malloc(sizeof(DWORD) * nWorkerThreads);

        if(!hThread){ throw Exception(FALSE); }
        if(!dwThreadID){ throw Exception(FALSE); }

        hcp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0,0);
        if(!hcp){ throw Exception(FALSE); }

        CreateSessionPool();
    }
}

ServerWindow::~ServerWindow(){
    StopListening();
    for(int i=0; i<nSessions; i++){
        if(SessionPool[i]){
            // DebugMessage(L"Call Enqueue = %d\r\n", i);
            Enqueue(ReleaseQ, SessionPool[i]);
        }
    }
    for(int i=0; i<nSessions; i++){
        // DebugMessage(L"Call ReleaseSession = %d\r\n", i);
        ReleaseSession();
    }
    StopThreads();

    if(ReleaseQ){ DestroyQueue(ReleaseQ); }
    if(BroadCastQ){ DestroyQueue(BroadCastQ); }
    if(SessionPool){ DeleteSessionPool(); }
    if(hThread){ free(hThread); }
    if(dwThreadID){ free(dwThreadID); }
    if(bCritical){ DeleteCriticalSection(&cs); }
    if(listen_sock){ closesocket(listen_sock); }
    if(Dummy){ closesocket(Dummy); }
    if(hcp){ CloseHandle(hcp); }

    WSACleanup();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//// *ServerWindow Utilities
////
////
////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

/*
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
 */

void ServerWindow::DebugMessage(LPCWSTR fmt, ...){
    wchar_t Debug[0x200];

    va_list arg;
    va_start(arg, fmt);
    StringCbVPrintf(Debug, sizeof(Debug), fmt, arg);
    va_end(arg);

    SYSTEMTIME st;
    GetLocalTime(&st);

    wchar_t Time[0x40];
    swprintf(Time, 0x40, L"[%04d-%02d-%02d %02d:%02d:%02d]", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    wchar_t Log[0x300];
    StringCbPrintf(Log,  sizeof(Log), L"[%s]: %s", Time, Debug);

    // 큐 동작이나 기타 동기화 객체를 사용하는 부분에서 호출이 겹치면 교착 상태가 된다.
    // EnterCriticalSection(&cs);
    int Length = GetWindowTextLength(hChatEdit);
    SendMessage(hChatEdit, EM_SETSEL, Length, Length);
    SendMessage(hChatEdit, EM_REPLACESEL, FALSE, (LPARAM)Log);
    // LeaveCriticalSection(&cs);
}

void ServerWindow::PrintEventType(IOEventType Type){
    switch(Type){
        case IOEventType::ACCEPT:
            DebugMessage(L"[INFO] ACCPET 이벤트가 발생하였습니다.\r\n");
            break;
        case IOEventType::CONNECT:
            DebugMessage(L"[INFO] CONNECT 이벤트가 발생하였습니다.\r\n");
            break;
        case IOEventType::RECV:
            DebugMessage(L"[INFO] RECV 이벤트가 발생하였습니다.\r\n");
            break;
        case IOEventType::SEND:
            DebugMessage(L"[INFO] SEND 이벤트가 발생하였습니다.\r\n");
            break;
        case IOEventType::DISCONNECT:
            DebugMessage(L"[INFO] DISCONNECT 이벤트가 발생하였습니다.\r\n");
            break;
    }
}

void ServerWindow::HandleError(DWORD dwError, LPVOID lpArgs){
    IOEvent *Event = (IOEvent*)lpArgs;
    ClientSession *Session = Event->Session;

    switch(dwError){
        // WSARecv, WSASend
        case WSAECONNRESET:
        case WSAECONNABORTED:
        // GetQueuedCompletionStatus
        case ERROR_NETNAME_DELETED:
        case ERROR_CONNECTION_ABORTED:
            if(Session != NULL){
                if(Enqueue(ReleaseQ, Session)){
                    ReleaseSession();
                    InterlockedDecrement((LONG*)&nConnected);
                }
            }
            break;
    }

    switch(dwError){
        case WSA_IO_PENDING:
            DebugMessage(L"[ERR] 비동기 입출력으로 인한 I/O PENDING 상태입니다. I/O 작업이 정상적으로 실행됩니다.\r\n");
            break;

        case WSAECONNRESET:
            // 클라이언트가 연결을 강제로 종료
            DebugMessage(L"[ERR] 클라이언트가 연결을 강제로 종료하였습니다.\r\n");
            break;

        case WSAESHUTDOWN:
            // 소켓이 이미 닫힘
            DebugMessage(L"[ERR] 이미 닫힌 소켓에 작업을 시도하였습니다.\r\n");
            break;

        case WSAENOTCONN:
            // 연결되지 않은 소켓
            DebugMessage(L"[ERR] 연결되지 않은 소켓에 작업을 시도하였습니다.\r\n");
            break;

        case WSAENOTSOCK:
            // 유효하지 않은 소켓
            DebugMessage(L"[ERR] 소켓 리소스가 유효하지 않습니다.\r\n");
            break;

        case WSAEMSGSIZE:
            // 버퍼 크기 초과
            DebugMessage(L"[ERR] 송수신 작업에 필요한 데이터의 크기가 버퍼 크기를 초과하였습니다.\r\n");
            break;

        case WSAEWOULDBLOCK:
            // 비동기 통신에서도 드물게 발생함
            DebugMessage(L"[ERR] 논블로킹 소켓에서 recv() 또는 send(), connect() 함수를 호출하였습니다. 데이터를 주고 받을 준비가 완료되지 않은 상태이므로 다시 시도합니다.\r\n");
            break;

        case ERROR_SUCCESS:
            DebugMessage(L"[ERR] 클라이언트가 정상적으로 접속을 종료하였습니다(FALSE + ERROR_SUCCESS)\r\n");
            break;

        case ERROR_NETNAME_DELETED:         // 클라이언트가 연결을 끊음
            DebugMessage(L"[ERR] 클라이언트와의 연결이 비정상적으로 종료되었습니다. 프로세스 강제 종료 또는 방화벽, NAT, 네트워크 단절 등이 원인일 수 있습니다.\r\n");
            break;

        case ERROR_CONNECTION_ABORTED:      // 연결이 비정상적으로 종료됨
            DebugMessage(L"[ERR] 연결이 비정상적으로 종료되었습니다.\r\n");
            break;

        case ERROR_OPERATION_ABORTED:       // 작업이 취소됨 (CancelIoEx 등)
            DebugMessage(L"[ERR] I/O 작업이 취소되었습니다.\r\n");
            break;

        default:
            DebugMessage(L"[ERR] 알 수 없는 에러가 발생하였습니다.\r\n");
            break;
    }
}

void ServerWindow::HandlePacket(DWORD dwTransferred, LPVOID lpArgs){
    if(dwTransferred == 0){
        // 종료 신호
        ClientSession *Session = (ClientSession*)lpArgs;
        Enqueue(ReleaseQ, Session);
        ReleaseSession();
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//// *ServerWindow IOCP Wrapper Functions
////
////
////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

BOOL ServerWindow::StartListening(){
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

void ServerWindow::StopListening(){
    if(listen_sock != INVALID_SOCKET){
        CancelIoEx((HANDLE)listen_sock, NULL); // 보류 AcceptEx 취소
        closesocket(listen_sock);
        listen_sock = INVALID_SOCKET;
    }
}

void ServerWindow::PostAccept(){
    DWORD dwError = 0, dwRecvBytes = 0;

    // 세션을 하나씩만 생성
    ClientSession *NewSession = GetSession();
    if(NewSession != NULL){
        NewSession->SetSocket(CreateSocket());
        // [ 클라이언트 주소 공간 ] + [ 서버 주소 공간 ] + [ 선택적 데이터 공간 ]
        if(NewSession->GetSocket() != INVALID_SOCKET){
            SafeInit(NewSession, IOEventType::ACCEPT);

            NewSession->AddPending();
            // DebugMessage(L"Session Pending = %d\r\n", NewSession->iPending);
            ret = lpfnAcceptEx(
                    listen_sock,                                // 듣기 전용 소켓
                    NewSession->GetSocket(),                    // 연결 전용 소켓
                    NewSession->GetAcceptBuffer(),              // AcceptEx 내부적으로 사용됨, 주소를 저장할 버퍼
                    0,
                    sizeof(struct sockaddr_in) + 16,            // 로컬 주소 버퍼 크기
                    sizeof(struct sockaddr_in) + 16,            // 원격 주소 버퍼 크기
                    &dwRecvBytes,
                    (OVERLAPPED*)&NewSession->AcceptEvent
                    );

            dwError = WSAGetLastError();
            if(ret == 0 && dwError != ERROR_IO_PENDING){
                // DebugMessage(L"Session Pending = %d\r\n", NewSession->iPending);
                NewSession->SubPending();
                HandleError(dwError, &NewSession->AcceptEvent);
            }
        } 
    }
}

void ServerWindow::CreateSessionPool(){
    bUse = new LONG[nSessions];
    if(bUse == NULL){ return; }

    SessionPool = new ClientSession*[nSessions];
    if(SessionPool == NULL){ return; }

    memset(bUse, 0, sizeof(LONG) * nSessions);
    memset(SessionPool, 0, sizeof(ClientSession*) * nSessions);

    for(int i=0; i<nSessions; i++){
        SessionPool[i] = new ClientSession();
        if(SessionPool[i] == NULL){ continue; }
    }
}

void ServerWindow::DeleteSessionPool(){
    for(int i=0; i<nSessions; i++){
        if(SessionPool[i] != NULL){ 
            delete SessionPool[i];
            SessionPool[i] = NULL;
        }
    }

    if(SessionPool != NULL){ delete [] SessionPool; }
    if(bUse != NULL){ delete [] bUse; }
}

ServerWindow::ClientSession* ServerWindow::GetSession(){
    for(int i=0; i<nSessions; i++){
        if(InterlockedCompareExchange(&bUse[i], 1, 0) == 0){
            return SessionPool[i];
        }
    }

    // 아래에 동적으로 세션을 추가하는 코드를 작성해도 좋다.
    // 만약 세션을 동적으로 관리할 예정이라면 CRITICAL_SECTION을 이용해서 동기화 해야 한다.
    return NULL;
}

void ServerWindow::ReleaseSession(){
    ClientSession* Session = (ClientSession*)Dequeue(ReleaseQ);

    if(Session != NULL){
        EnterCriticalSection(&cs);
        // DebugMessage(L"세션을 정리합니다.\r\n");

        if(Session->GetSocket() != INVALID_SOCKET){ 
            // DebugMessage(L"세션과 연결된 통신 단말의 송수신 기능을 모두 종료합니다.\r\n");
            CancelIoEx((HANDLE)Session->GetSocket(), NULL);
            shutdown(Session->GetSocket(), SD_BOTH);
            closesocket(Session->GetSocket());
            Session->SetSocket(INVALID_SOCKET);
        }

        memset(Session->GetRecvBuffer(), 0, sizeof(wchar_t) * Session->GetCapacity());
        memset(Session->GetSendBuffer(), 0, sizeof(wchar_t) * Session->GetCapacity());
        Session->SetConnected(FALSE);
        Session->Init(); // 안전

        for(int i=0; i<nSessions; i++){ if(SessionPool[i] == Session){ bUse[i] = 0; break; } } 
        LeaveCriticalSection(&cs);
    }

    if(listen_sock != INVALID_SOCKET){
        PostAccept();
    }
}

void ServerWindow::BroadCast(){
    // WSASend와 같은 비동기 함수를 호출할 때 주의해야할 것이 있다.
    // OVERLAPPED 구조와 버퍼가 독립적이지 않은 구조일 경우 덮어쓰는 등의 문제가 발생할 수 있다.
    // 해서, bSending 따위의 변수를 만들어 중첩 호출되지 않도록 관리한다.

    // 또한, WSASend가 처리되지 않은 상태, 즉 bSending이 TRUE일 때 보내지 못한 데이터는
    // 별도의 큐에 보관해두었다가 콜백 함수나 작업자 스레드를 이용해 처리하도록 만들면 무난히 동작하는 서버 프로그램을 설계할 수 있다.

    // 어처피 서버를 확장할 일이 생기면 구조를 더 세분화하여 전체 로직을 수정해야 하므로 일단은 단순하게만 구현하기로 하자.

    ClientSession* Session = (ClientSession*)Dequeue(BroadCastQ);
    if(Session == NULL){return;}

    EnterCriticalSection(&cs);
    for(int i=0; i<nSessions; i++){
        if(SessionPool[i] == NULL){ continue; }
        if(SessionPool[i] == Session){ continue; }
        if(!SessionPool[i]->IsConnected()){ continue; }
        if(SessionPool[i]->GetSocket() == INVALID_SOCKET){ continue; }

        memset(SessionPool[i]->GetSendBuffer(), 0, sizeof(wchar_t) * SessionPool[i]->GetCapacity());

        wchar_t *TempRecvBuffer = Session->GetRecvBuffer();
        wchar_t *TempSendBuffer = SessionPool[i]->GetSendBuffer();

        int Capacity = Session->GetCapacity();
        int Length = wcslen(Session->GetRecvBuffer());

        wcsncpy(TempSendBuffer, TempRecvBuffer, min(Length, Capacity -1));
        TempSendBuffer[Length] = 0;

        SafeInit(SessionPool[i], IOEventType::SEND);

        WSABUF buf;
        buf.buf = (char*)TempSendBuffer;
        buf.len = sizeof(wchar_t) * Length;

        // DebugMessage(L"[INFO] WSASend 함수를 호출하였습니다.\r\n");

        Session->AddPending();
        // DebugMessage(L"Session Pending = %d\r\n", Session->iPending);
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
            Session->SubPending();
            // DebugMessage(L"Session Pending = %d\r\n", Session->iPending);
            HandleError(dwError, &SessionPool[i]->SendEvent);
        }
    }
    
    LeaveCriticalSection(&cs);
}

void ServerWindow::SafeInit(ClientSession *Session, IOEventType EventType){
    if(Session == NULL){ 
        DebugMessage(L"세션이 유효하지 않아 이벤트 객체를 초기화할 수 없습니다.\r\n");
        return;
    }

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

        case IOEventType::ACCEPT:
            Session->AcceptEvent.ResetTasks();
            Session->AcceptEvent.Type = EventType;
            Session->AcceptEvent.Session = Session;
            break;
    }
}

void ServerWindow::ClientSession::InitEvent(){
    RecvEvent.ResetTasks();
    SendEvent.ResetTasks();
    ConnectEvent.ResetTasks();
    DisconnectEvent.ResetTasks();
    AcceptEvent.ResetTasks();

    RecvEvent.Type = IOEventType::RECV;
    SendEvent.Type = IOEventType::SEND;
    ConnectEvent.Type = IOEventType::CONNECT;
    DisconnectEvent.Type = IOEventType::DISCONNECT;
    AcceptEvent.Type = IOEventType::ACCEPT;
}

void ServerWindow::ClientSession::InitThis(){
    RecvEvent.Session = this;
    SendEvent.Session = this;
    ConnectEvent.Session = this;
    DisconnectEvent.Session = this;
    AcceptEvent.Session = this;
}

void ServerWindow::ClientSession::Init(){
    InitEvent();
    InitThis();
}

void ServerWindow::StartThreads(){
    for(int i=0; i < nWorkerThreads; i++){
        hThread[i] = CreateThread(NULL, 0, WorkerThreadHandler, this, 0, &dwThreadID[i]);
    }
}

void ServerWindow::StopThreads(){
    if(listen_sock != INVALID_SOCKET){ 
        CancelIoEx((HANDLE)listen_sock, NULL);
        listen_sock = INVALID_SOCKET;
    }

    for(int i=0; i<nSessions; i++){
        if(SessionPool[i] == NULL){ continue; }
        SessionPool[i]->SetConnected(FALSE);
        if(SessionPool[i]->GetSocket() != INVALID_SOCKET){
            CancelIoEx((HANDLE)SessionPool[i]->GetSocket(), NULL);
        }
    }

    for(int i=0; i<nSessions; i++){
        if(SessionPool[i] == NULL){ continue; }
        /*
        if(WaitForSingleObject(SessionPool[i]->hDownEvent, 4000) == WAIT_TIMEOUT){
            // 세션 드레인 타임아웃
        }
        */

        if(SessionPool[i]->GetSocket() != INVALID_SOCKET){
            shutdown(SessionPool[i]->GetSocket(), SD_BOTH);
            closesocket(SessionPool[i]->GetSocket());
            SessionPool[i]->SetSocket(INVALID_SOCKET);
        }
    }

    for(int i=0; i<nWorkerThreads; i++){
        PostQueuedCompletionStatus(hcp, 0, EXIT_KEY, NULL);
    }

    for(int i=0; i<nWorkerThreads; i++){
        if(hThread[i]){
            // WaitForSingleObject(hThread[i], INFINITE);
            CloseHandle(hThread[i]);
            hThread[i] = NULL;
        }
    }
}

void ServerWindow::PostConnect(ClientSession *Session){
    SafeInit(Session, IOEventType::CONNECT);
    Session->AddPending();
    PostQueuedCompletionStatus(hcp, 0, (ULONG_PTR)Session, (OVERLAPPED*)&Session->ConnectEvent);
}

void ServerWindow::PostDisconnect(ClientSession *Session){
    SafeInit(Session, IOEventType::DISCONNECT);
    Session->AddPending();
    // DebugMessage(L"Session Pending = %d\r\n", Session->iPending);
    PostQueuedCompletionStatus(hcp, 0, (ULONG_PTR)Session, (OVERLAPPED*)&Session->DisconnectEvent);
}

void ServerWindow::OnAccept(ClientSession* Session){
    int cbSize = 0;
    wchar_t IP[INET_ADDRSTRLEN];
    sockaddr_in NewRemoteAddress;

    if(setsockopt(
                Session->GetSocket(),
                SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
                (const char*)&listen_sock,
                sizeof(listen_sock)
        ) == SOCKET_ERROR){
        DebugMessage(L"[ERR] setsockopt 함수가 실패하였습니다. 세션을 해제합니다.\r\n");

        delete Session;
        Session = NULL;

        PostAccept();
        return;
    }

    cbSize = sizeof(NewRemoteAddress);
    if(getpeername(
                Session->GetSocket(),
                (struct sockaddr*)&NewRemoteAddress,
                &cbSize) == SOCKET_ERROR){
        DebugMessage(L"[ERR] getpeername 함수가 실패하였습니다. 세션을 해제합니다.\r\n");

        delete Session;
        Session = NULL;

        PostAccept();
        return;
    }

    if(InetNtopW(AF_INET, &NewRemoteAddress.sin_addr, IP, INET_ADDRSTRLEN) != NULL){
        // Show IP Address
        InterlockedIncrement((LONG*)&nConnected);
        DebugMessage(L"[INFO] 클라이언트가 접속하였습니다. Address Family: IPv4, IP Address: %s, Port: %d\r\n", IP, ntohs(NewRemoteAddress.sin_port));

        // Post ConnectEvent
        Session->SetRemoteAddress(NewRemoteAddress);
        PostConnect(Session);
    }

    PostAccept();
}

void ServerWindow::OnConnect(ClientSession* Session){
    WSABUF buf;
    DWORD dwError, dwBytes, dwFlags;

    if(Session->IsConnected()){
        DebugMessage(L"[ERR] 이미 연결된 세션을 대상으로 비정상적인 CONNECT 이벤트가 발생하였습니다.\r\n"); 
        return;
    }

    SafeInit(Session, IOEventType::RECV);
    CreateIoCompletionPort((HANDLE)Session->GetSocket(), hcp, 0, 0);
    Session->SetConnected(TRUE);

    memset(Session->GetRecvBuffer(), 0, sizeof(wchar_t) * Session->GetCapacity());
    buf.buf = (char*)Session->GetRecvBuffer();
    buf.len = sizeof(wchar_t) * Session->GetCapacity();

    Session->AddPending();
    // DebugMessage(L"Session Pending = %d\r\n", Session->iPending);
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
        Session->SubPending();
        // DebugMessage(L"Session Pending = %d\r\n", Session->iPending);
        HandleError(dwError, &Session->RecvEvent);
    }
}

void ServerWindow::OnDisconnect(ClientSession* Session){
    if(!Enqueue(ReleaseQ, Session)){
        DebugMessage(L"[WARN] 세션 해제 큐에 세션을 등록하지 못했습니다.\r\n"); 
        return;
    }

    ReleaseSession();
}

void ServerWindow::OnRecv(ClientSession* Session){
    WSABUF buf;
    sockaddr_in TempAddress;
    wchar_t IP[INET_ADDRSTRLEN];
    DWORD dwError, dwBytes, dwFlags;

    if(Session->GetSocket() == INVALID_SOCKET){
        DebugMessage(L"[ERR] 세션이 가진 소켓이 유효하지 않습니다. 세션을 해제합니다.\r\n");
        return;
    }

    if(!Enqueue(BroadCastQ, Session)){
        DebugMessage(L"[WARN] 브로드 캐스트 전용 큐에 세션을 등록하지 못했습니다.\r\n");
        return;
    }

    TempAddress = Session->GetRemoteAddress();
    if(InetNtopW(AF_INET, &TempAddress.sin_addr, IP, INET_ADDRSTRLEN) != NULL){
        wchar_t *TempRecvBuffer = Session->GetRecvBuffer();
        int Length = wcslen(Session->GetRecvBuffer());
        int Capacity = Session->GetCapacity();

        TempRecvBuffer[Length] = 0;
        DebugMessage(L"[ MSG - ( %s, %d ) ]: %s\r\n", IP, ntohs(TempAddress.sin_port), TempRecvBuffer);
    }
    BroadCast();

    SafeInit(Session, IOEventType::RECV);
    Session->SetConnected(TRUE);
    memset(Session->GetRecvBuffer(), 0, sizeof(wchar_t) * Session->GetCapacity());

    buf.buf = (char*)Session->GetRecvBuffer();
    buf.len = sizeof(wchar_t) * Session->GetCapacity();

    // DebugMessage(L"[INFO] WSARecv 함수를 호출하였습니다.\r\n");
    Session->AddPending();
    // DebugMessage(L"Session Pending = %d\r\n", Session->iPending);
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
        Session->SubPending();
        // DebugMessage(L"Session Pending = %d\r\n", Session->iPending);
        HandleError(dwError, &Session->RecvEvent);
    }
}

void ServerWindow::OnSend(ClientSession* Session){

}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//// *WorkerThread Procedure
////
////
////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ServerWindow::Processing(){
    DWORD dwTrans = 0, dwError;
    ULONG_PTR Key = 0;

    IOEvent *Event = NULL;
    ClientSession *Session = NULL;
    IOEventType EventType;

    while(1){
        // DebugMessage(L"GetQueuedCompletionStatus로 스레드가 대기 상태에 들어갑니다.\r\n");
        BOOL bIOCP = GetQueuedCompletionStatus(hcp, &dwTrans, &Key, (OVERLAPPED**)&Event, INFINITE);
        // DebugMessage(L"GetQueuedCompletionStatus로 스레드가 깨어났습니다.\r\n");
        if(Event == NULL){
            if(!bIOCP){
                dwError = GetLastError();
                if(dwError == WAIT_TIMEOUT){ break; }
                if(dwError == ERROR_ABANDONED_WAIT_0){ break; }
                HandleError(dwError, Event);
                continue;
            }

            if(Key == EXIT_KEY){
                // DebugMessage(L"종료 신호가 발생하였습니다. 작업자 스레드를 종료합니다.\r\n");
                break;
            }

            HandlePacket(dwTrans, NULL);
            continue;
        }

        Session = Event->Session;
        if(Session == NULL){
            DebugMessage(L"[ERR] 유효한 세션이 아닙니다.\r\n");
            continue;
        }

        Session->SubPending();
        // DebugMessage(L"Session Pending = %d\r\n", Session->iPending);
        EventType = Event->Type;

        if(!bIOCP){
            dwError = GetLastError(); // 취소면 ERROR_OPERATION_ABORTED(995)
            HandleError(dwError, Event);
            continue;
        }

        // I/O 완료 처리
        if(Session->GetSocket() == INVALID_SOCKET){
            DebugMessage(L"[ERR] 세션이 소유한 소켓 핸들이 유효하지 않습니다.\r\n");
            continue;
        }

        // PrintEventType(EventType);
        switch(EventType){
            case IOEventType::ACCEPT:
                OnAccept(Session);
                break;

            case IOEventType::CONNECT:
                OnConnect(Session);
                break;

            case IOEventType::DISCONNECT:
                OnDisconnect(Session);
                break;

            case IOEventType::RECV:
                OnRecv(Session);
                break;

            case IOEventType::SEND:
                OnSend(Session);
                break;

            default:
                DebugMessage(L"[INFO] 처리할 수 있는 유형의 이벤트가 아닙니다.\r\n");
                break;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//// *ServerWindow Idletime(e.g. UI)
////
////
////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ServerWindow::OnDraw(){
    static wchar_t Status[0x100] = {0,};
    static wchar_t PrevStatus[0x100] = {0,};

    GetWindowText(hStatusText, PrevStatus, 0x100);
    StringCbPrintf(Status, sizeof(Status), L"🟢 Server Running | Clients: %d", nConnected);

    if(wcscmp(Status, PrevStatus) == 0){ return; }
    SetWindowText(hStatusText, Status);
}
