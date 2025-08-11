#ifndef __SERVER_WINDOW_H_
#define __SERVER_WINDOW_H_
#include <ws2tcpip.h>
#include <winsock2.h>
#include <mswsock.h>
#include "BaseWindow.h"
#include "Queue.h"

class ServerWindow : public BaseWindow<ServerWindow> {
    class ClientSession;

private:
    enum class IOEventType : UCHAR { CONNECT, DISCONNECT, ACCEPT, RECV, SEND };
    class IOEvent{
        public:
            IOEvent() : Session(NULL) { ResetTasks(); }
            void ResetTasks() { memset(&ov, 0, sizeof(ov)); }

        public:
            OVERLAPPED ov;
            IOEventType Type;
            ClientSession *Session;
    };

private:
    // 액세스 지정자를 private으로 하여 외부에선 접근할 수 없다.
    // 즉, ClientSession의 모든 함수와 멤버를 public으로 선언해도 상관은 없으나 이미 작성해둔거 그냥 두기로 한다.
    class ClientSession {
        private:
            static const int PreReceiveDataLength = 0;
            static const int LocalAddressLength = sizeof(sockaddr_in) + 16;
            static const int RemoteAddressLength = sizeof(sockaddr_in) + 16;
            static const int OutputBufferLength = LocalAddressLength + RemoteAddressLength + PreReceiveDataLength;
            static const int DefaultSize = 0x1000;
            
        private:
            int Front, Rear, Capacity;

        private:
            LONG bConnected;
            SOCKET client_sock;
            sockaddr_in LocalAddress;       // Server
            sockaddr_in RemoteAddress;      // Client

        private:
            wchar_t *RecvBuffer;
            wchar_t *SendBuffer;
            wchar_t AcceptBuffer[OutputBufferLength];

        private:
            LONG bSending;

        public:
            // private으로 액세스 지정자를 변경하는 것이 좋다.
            // 다만, 이렇게 되면 여러가지로 번거롭다.
            // 현재 작업 스레드의 로직이 단순한 switch case문이고,
            // 다형성 지원이나 클래스를 확장할 생각은 없으므로
            // 간단하게 직접 접근이 가능한 public 액세스 지정자를 사용했다.
            IOEvent RecvEvent;
            IOEvent SendEvent;
            IOEvent ConnectEvent;
            IOEvent DisconnectEvent;

        public:
            void ResetEvent();

        public:
            ClientSession() : Front(0), Rear(0), Capacity(DefaultSize * 10), bConnected(0), bSending(0){ 
                Init();
                memset(AcceptBuffer, 0, sizeof(AcceptBuffer));
                RecvBuffer = (wchar_t*)malloc(sizeof(wchar_t) * Capacity);
                SendBuffer = (wchar_t*)malloc(sizeof(wchar_t) * Capacity);
            }

            ~ClientSession(){
                if(RecvBuffer){ free(RecvBuffer); }
                if(SendBuffer){ free(SendBuffer); }
                if(client_sock){ closesocket(client_sock); }
            }

        public:
            BOOL IsConnected() const { return InterlockedCompareExchange((LONG*)&bConnected, bConnected, bConnected); }
            void SetConnected(BOOL bValue) { InterlockedExchange(&bConnected, bValue ? 1 : 0); }

        public:
            sockaddr_in GetLocalAddress() { return LocalAddress; }
            sockaddr_in GetRemoteAddress() { return RemoteAddress; }
            void SetLocalAddress(sockaddr_in NewLocalAddress) { LocalAddress = NewLocalAddress; }
            void SetRemoteAddress(sockaddr_in NewRemoteAddress) { RemoteAddress = NewRemoteAddress; }

        public:
            void SetSocket(SOCKET NewSocket){ client_sock = NewSocket; } 
            SOCKET GetSocket(){ return client_sock; } 

        public:
            wchar_t *GetAcceptBuffer(){ return AcceptBuffer; }
            wchar_t *GetRecvBuffer(){ return RecvBuffer; }
            wchar_t *GetSendBuffer(){ return SendBuffer; }

        public:
            wchar_t *GetReadPosition(){ return &RecvBuffer[Rear]; }
            wchar_t *GetWritePosition(){ return &RecvBuffer[Front]; }

        public:
            int DataSize() const { return Front - Rear; }
            int FreeSize() const { return Capacity - Front; }
            int GetCapacity() const { return Capacity; }
            static int GetOutputBufferLength() { return OutputBufferLength; }

        public:
            void InitEvent(){
                RecvEvent.ResetTasks();
                SendEvent.ResetTasks();
                ConnectEvent.ResetTasks();
                DisconnectEvent.ResetTasks();

                RecvEvent.Type = IOEventType::RECV;
                SendEvent.Type = IOEventType::SEND;
                ConnectEvent.Type = IOEventType::CONNECT;
                DisconnectEvent.Type = IOEventType::DISCONNECT;
            }

            void InitThis(){
                RecvEvent.Session = this;
                SendEvent.Session = this;
                ConnectEvent.Session = this;
                DisconnectEvent.Session = this;
            }

            void Init(){
                InitEvent();
                InitThis();
            }

        public:
            BOOL IsSending() const { return InterlockedCompareExchange((LONG*)&bSending, bSending, bSending); }
            void SetIOState(BOOL bValue) { InterlockedExchange(&bSending, bValue ? 1 : 0); }
    };

public:
    class Exception{
        int ErrorCode;

    public:
        Exception(BOOL WSAError = TRUE){
            GetErrorMessage(WSAError);
        }

    public:
        int GetErrorCode() { return ErrorCode; }
        void GetErrorMessage(BOOL WSAError);
    };

private:
    // Reserved Windows Messages
    static const int _nMsg = 0x400;
    static const int SERVERPORT = 9000;

    typedef struct tag_MSGMAP{
        UINT iMessage;
        LRESULT(ServerWindow::* lpfnWndProc)(WPARAM, LPARAM);
    }MSGMAP;

    // QUERYENDSESSION
    MSGMAP MainMsg[_nMsg] = {
        {WM_SIZE, &ServerWindow::OnSize},
        {WM_PAINT, &ServerWindow::OnPaint},
        {WM_COMMAND, &ServerWindow::OnCommand},
        {WM_CREATE, &ServerWindow::OnCreate},
        {WM_DESTROY, &ServerWindow::OnDestroy},
    };

private:
    BOOL bRunning;
    
    HANDLE hShutdownEvent;
    CRITICAL_SECTION cs;
    BOOL bCritical;

    HANDLE hcp, *hThread;
    DWORD *dwThreadID;

private:
    HWND hPannel, hChatEdit;
    RECT rcPannel, rcChatEdit;

private:
    WSADATA wsa;
    SOCKET listen_sock, Dummy;
    int nLogicalProcessors, ret;

    DWORD dwKeepAliveOption;
    DWORD dwReuseAddressOption;
    struct linger l_LingerOption;
    struct sockaddr_in ServerAddress;

private:
    Queue *BroadCastQ, *ReleaseQ;

private:
    static LPFN_ACCEPTEX lpfnAcceptEx;
    static LPFN_CONNECTEX lpfnConnectEx;
    static LPFN_DISCONNECTEX lpfnDisconnectEx;

private:
    HBITMAP hBitmap;

private:
    LPCWSTR ClassName() const { return L"ChatHub Window Class"; }
    LRESULT OnSize(WPARAM wParam, LPARAM lParam);
    LRESULT OnPaint(WPARAM wParam, LPARAM lParam);
    LRESULT OnCommand(WPARAM wParam, LPARAM lParam);
    LRESULT OnCreate(WPARAM wParam, LPARAM lParam);
    LRESULT OnDestroy(WPARAM wParam, LPARAM lParam);

private:
    SOCKET CreateSocket();
    BOOL BindWSAFunction(SOCKET Socket, GUID Serial, LPVOID* lpfn);

private:
    BOOL Listening();
    void PostAccept();
    void PostConnect(ClientSession *Session);
    void PostDisconnect(ClientSession *Session);

private:
    LONG *bUse;
    ClientSession **SessionPool;
    void CreateSessionPool(int Count);
    void DeleteSessionPool(int Count);

private:
    void DebugMessage(LPCWSTR fmt, ...);
    void ErrorHandler(DWORD dwError, LPVOID lpArgs);
    void TypeHandler(IOEventType Type);

private:
    ClientSession* GetSession(int Count);
    void ReleaseSession(int Count);

private:
    void BroadCast(int Count);
    void SafeInit(ClientSession *Session, IOEventType EventType);

public:
    ServerWindow();
    ~ServerWindow();

    LRESULT Handler(UINT iMessage, WPARAM wParam, LPARAM lParam);
    static DWORD WINAPI WorkerThreadHandler(LPVOID lpArg);
    void Processing();
};

#endif
