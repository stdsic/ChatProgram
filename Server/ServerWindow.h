#ifndef __SERVER_WINDOW_H_
#define __SERVER_WINDOW_H_
#include <ws2tcpip.h>
#include <winsock2.h>
#include <mswsock.h>
#include "BaseWindow.h"

class ServerWindow : public BaseWindow<ServerWindow> {
private:
    // 액세스 지정자를 private으로 하여 외부에선 접근할 수 없다.
    // 즉, ClientSession의 모든 함수와 멤버를 public으로 선언해도 상관은 없으나 이미 작성해둔거 그냥 두기로 한다.
    class ClientSession {
        private:
            static const int PreReceiveDataLength = 0;
            static const int LocalAddressLength = sizeof(sockaddr_in) + 16;
            static const int RemoteAddressLength = sizeof(sockaddr_in) + 16;
            static const int OutputBufferLength = LocalAddressLength + RemoteAddressLength + PreReceiveDataLength;
        private:
            int Front, Rear, Capacity;
            const int DefaultSize = 0x1000;

        private:
            SOCKET client_sock;
            volatile LONG bConnected;
            sockaddr_in LocalAddress;       // Server
            sockaddr_in RemoteAddress;      // Client

        private:
            wchar_t *RecvBuffer;
            wchar_t *SendBuffer;
            wchar_t AcceptBuffer[OutputBufferLength];
            Queue Q;

        public:
            ClientSession() : Front(0), Rear(0), Capacity(0), bConnected(0) {
                Capacity = DefaultSize * 10;
                memset(AcceptBuffer, 0, sizeof(AcceptBuffer));
                RecvBuffer = (wchar_t*)malloc(sizeof(wchar_t) * Capacity);
                SendBuffer = (wchar_t*)malloc(sizeof(wchar_t) * Capacity);
            }

            ~ClientSession(){
                if(RecvBuffer){ free(RecvBuffer); }
                if(SendBuffer){ free(SendBuffer); }
            }

        public:
            sockaddr_in GetLocalAddress() { return LocalAddress; }
            sockaddr_in GetRemoteAddress() { return RemoteAddress; }
            BOOL IsConnected() { return bConnected; }

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
            static const int GetOutputBufferLength() { return OutputBufferLength; }
    };

private:
    enum class IOEventType : UCHAR { CONNECT, DISCONNECT, ACCEPT, RECV, SEND };
    class IOEvent{
        public:
            OVERLAPPED ov;
            IOEventType Type;
            ClientSession *Session;
            wchar_t TempBuffer[0x1000];
            wchar_t SendBuffers[10][0x1000];
            LONG RefCount;
    };

private:
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
    volatile BOOL bRunning;
    
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

public:
    ServerWindow();
    ~ServerWindow();

    LRESULT Handler(UINT iMessage, WPARAM wParam, LPARAM lParam);
    static DWORD WINAPI WorkerThreadHandler(LPVOID lpArg);
    void Processing();
};

#endif
