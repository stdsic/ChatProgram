#ifndef __SERVER_WINDOW_H_
#define __SERVER_WINDOW_H_
#include "BaseWindow.h"

class ServerWindow : public BaseWindow<ServerWindow> {
	// Window Reserved Message
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

    HANDLE hcp, *hThread;
    DWORD *dwThreadID;

private:
    HWND hPannel, hChatEdit;
    RECT rcPannel, rcChatEdit;

private:
	WSADATA wsa;
    SOCKET listen_sock;
    int nLogicalProcessors, ret;

    DWORD dwKeepAliveOption;
    DWORD dwReuseAddressOption;
    struct linger l_LingerOption;
    struct sockaddr_in ServerAddress;

private:
	HBITMAP hBitmap;

private:
    void ShowText(const wchar_t* fmt, ...);

private:
    LPCWSTR ClassName() const { return L"ChatHub Window Class"; }
    LRESULT OnSize(WPARAM wParam, LPARAM lParam);
    LRESULT OnPaint(WPARAM wParam, LPARAM lParam);
    LRESULT OnCommand(WPARAM wParam, LPARAM lParam);
    LRESULT OnCreate(WPARAM wParam, LPARAM lParam);
    LRESULT OnDestroy(WPARAM wParam, LPARAM lParam);

public:
    BOOL Listening();
    void WaitForConnections();

public:
	ServerWindow();
    ~ServerWindow();

    LRESULT Handler(UINT iMessage, WPARAM wParam, LPARAM lParam);
    static DWORD WINAPI WorkerThreadHandler(LPVOID lpArg);
};

#endif
