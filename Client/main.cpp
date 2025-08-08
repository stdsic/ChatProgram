#define _WIN32_WINNT 0x0A00
#include <ws2tcpip.h>
#include <windows.h>
#include <winsock2.h>
#include <stdio.h>
#include <string.h>

#define SERVERPORT 9000
#define BUFSIZE 40960

wchar_t *SERVERIP = L"127.0.0.1";

int wmain(int argc, wchar_t *argv[]){
	WSADATA wsa;
	if(WSAStartup(MAKEWORD(2,2), &wsa) != 0){return 1;};

	int retval;

	if(argc > 1){SERVERIP = argv[1];}
	
	SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
	if(sock == INVALID_SOCKET){printf("Invalidate Socket Handle\n"); exit(0);}

	struct sockaddr_in serveraddr={0};
	serveraddr.sin_family = AF_INET;
	InetPton(AF_INET, SERVERIP, &serveraddr.sin_addr);
	serveraddr.sin_port = htons(SERVERPORT);

	retval = connect(sock, (struct sockaddr*)&serveraddr, sizeof(serveraddr));
	if(retval == SOCKET_ERROR){printf("connect error\n"); exit(0);}

	int len;
	wchar_t buf[BUFSIZE+1];

	while(1){
		printf("\n[Input] : ");
		if(fgetws(buf, sizeof(wchar_t) * BUFSIZE+1, stdin) == NULL){break;}

		len = wcslen(buf);
		if(buf[len-1] == '\r'){buf[len-1] = 0;}
		if(wcslen(buf) == 0){break;}

		retval = send(sock, (char*)buf, sizeof(wchar_t) * wcslen(buf), 0);
		if(retval == SOCKET_ERROR){printf("send error\n"); break;}

		printf("[TCP Client] %d Bytes Sending.\n", retval);

		retval = recv(sock, (char*)buf, retval, MSG_WAITALL);
		if(retval == SOCKET_ERROR){
			printf("recv error\n");
			break;
		}else if(retval == 0){break;}

		buf[retval] = 0;
		printf("[TCP Client] %d Bytes Reciving.\n", retval);
		printf("[Recv Data] %s\n", buf);
	}

	closesocket(sock);

	WSACleanup();
	return 0;
}
