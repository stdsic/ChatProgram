#include "ServerWindow.h"
#include <stdio.h>

#define UNUSED(v) (void)(v)

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow){
    UNUSED(hInst);
    UNUSED(nCmdShow);

    try{
        ServerWindow Server;
        if(Server.Create(L"ChatHub", WS_OVERLAPPEDWINDOW | WS_VISIBLE, WS_EX_CLIENTEDGE, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL)){
            Server.RunMessageLoop();
        }
    }catch(const ServerWindow::Exception& e){

    }
    return 0;
}
