#include "ServerWindow.h"

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow){
    try{
        ServerWindow Server;
        if(Server.Create(L"ChatHub", WS_OVERLAPPEDWINDOW | WS_VISIBLE, WS_EX_CLIENTEDGE, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL)){
            Server.RunMessageLoop();
        }
    }catch(const ){

    }
    return 0;
}
