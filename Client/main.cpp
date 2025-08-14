#define _WIN32_WINNT 0x0A00
#include "resource.h"
#include <ws2tcpip.h>
#include <winsock2.h>
#include <windows.h>
#include <strsafe.h>

#define SERVERIP L"127.0.0.1"
#define SERVERPORT 9000
#define BUFSIZE 0x1000 

HWND hEdit1,hEdit2,hSend;
HANDLE hReadEvent, hSendEvent, hRecvThread, hSendThread;

SOCKET sock;
wchar_t SendBuffer[BUFSIZE+1];
wchar_t RecvBuffer[BUFSIZE+1];

void ErrorQuit(const wchar_t* msg){
	LPVOID lpMsgBuf;

	FormatMessage(
				FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
				NULL,
				WSAGetLastError(),
				MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
				(wchar_t*)&lpMsgBuf,
                0,
                NULL
			);

	MessageBox(NULL, (const wchar_t*)lpMsgBuf, msg, MB_ICONERROR);
	LocalFree(lpMsgBuf);
	exit(1);
}

void DisplayText(const wchar_t* fmt, ...){
    wchar_t buf[BUFSIZE];

	va_list arg;
	va_start(arg, fmt);
	StringCbVPrintf(buf, sizeof(buf), fmt, arg);
	va_end(arg);

	int Length = GetWindowTextLength(hEdit2);
	SendMessage(hEdit2, EM_SETSEL, Length, Length);
	SendMessage(hEdit2, EM_REPLACESEL, FALSE, (LPARAM)buf);
}

void DisplayError(const wchar_t* err){
	LPVOID lpMsgBuf;
	wchar_t buf[BUFSIZE];

	FormatMessage(
				FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
				NULL,
				WSAGetLastError(),
				MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
				(wchar_t*)&lpMsgBuf,
				0,
				NULL
			);

	DisplayText(L"[%s] %s\r\n", err, buf);
	LocalFree(lpMsgBuf);
}

DWORD WINAPI RecvProc(LPVOID lpArg){
    while(1){
		int ret = recv(sock, (char*)RecvBuffer, sizeof(wchar_t) * BUFSIZE, 0);

		if(ret == SOCKET_ERROR){
			DisplayText(L"recv()");
			break;
		}else if(ret == 0){
			break;
		}

		RecvBuffer[ret / sizeof(wchar_t)] = 0;
		DisplayText(L"[Recv] %s\r\n", RecvBuffer);
    }

    return 0;
}

DWORD WINAPI SendProc(LPVOID lpArg){
	while(1){
		WaitForSingleObject(hSendEvent, INFINITE);

		// 보낼 데이터를 입력하지 않았을 때
		if(wcslen(SendBuffer) == 0){
			continue;
		}

        SendBuffer[wcslen(SendBuffer)] = 0;
		int ret = send(sock, (char*)SendBuffer, sizeof(wchar_t) * wcslen(SendBuffer), 0);
		if(ret == SOCKET_ERROR){DisplayText(L"send()"); break;}
		DisplayText(L"[Send] %s\r\n", SendBuffer);
	}

	return 0;
}

INT_PTR CALLBACK DlgProc(HWND hWnd, UINT iMessage, WPARAM wParam, LPARAM lParam){
	switch(iMessage){
		case WM_INITDIALOG:
			hEdit1 = GetDlgItem(hWnd, IDC_EDIT1);
			hEdit2 = GetDlgItem(hWnd, IDC_EDIT2);
			hSend = GetDlgItem(hWnd, IDOK);
			SendMessage(hEdit1, EM_SETLIMITTEXT, BUFSIZE+1, 0);
			return TRUE;

		case WM_COMMAND:
			switch(LOWORD(wParam)){
				case IDOK:
					GetDlgItemText(hWnd, IDC_EDIT1, SendBuffer, BUFSIZE+1);
					SetEvent(hSendEvent);
					SetFocus(hEdit1);
					SendMessage(hEdit1, EM_SETSEL, 0, -1);
					return TRUE;

				case IDCANCEL:
					closesocket(sock);
					EndDialog(hWnd, IDCANCEL);
					return TRUE;
			}
	}
	return FALSE;
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpszCmdLine, int nCmdShow){
	WSADATA wsa;
	if(WSAStartup(MAKEWORD(2,2), &wsa) != 0){ return 1; }

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if(sock == INVALID_SOCKET){ErrorQuit(L"socket()");}

	struct sockaddr_in serveraddr;
	serveraddr.sin_port = htons(SERVERPORT);

    IN_ADDR addr;
    InetPton(AF_INET, SERVERIP, &addr);
	serveraddr.sin_addr.s_addr = addr.s_addr;
	serveraddr.sin_family = AF_INET;

	int ret = connect(sock, (struct sockaddr*)&serveraddr, sizeof(serveraddr));
	if(ret == SOCKET_ERROR){ErrorQuit(L"connect()");}

	hReadEvent = CreateEvent(NULL, FALSE, TRUE, NULL);
	hSendEvent= CreateEvent(NULL, FALSE, FALSE, NULL);

	DWORD dwThreadID[2];	
	hRecvThread = CreateThread(NULL, 0, RecvProc, 0, 0, &dwThreadID[0]);
    hSendThread = CreateThread(NULL, 0, SendProc, 0, 0, &dwThreadID[1]);
    CloseHandle(hRecvThread);
    CloseHandle(hSendThread);

	// Modal Dialog
	DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_DIALOG), HWND_DESKTOP, (DLGPROC)DlgProc);

	CloseHandle(hReadEvent);
	CloseHandle(hSendEvent);

	if(sock){closesocket(sock);}
	WSACleanup();
	return 0;
}
