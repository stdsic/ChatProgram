#ifndef __BASE_WINDOW_H_
#define __BASE_WINDOW_H_
#include <windows.h>

// Encapsulation
template <class DERIVED_TYPE>
class BaseWindow {
	protected:
		HWND _hWnd = NULL;

		virtual LPCWSTR ClassName() const = 0;
		virtual LRESULT Handler(UINT iMessage, WPARAM wParam, LPARAM lParam) = 0;

	public:
        void RunMessageLoop(){
            MSG msg;
            while(GetMessage(&msg, NULL, 0, 0)){
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }

		static LRESULT CALLBACK WndProc(HWND hWnd, UINT iMessage, WPARAM wParam, LPARAM lParam){
			DERIVED_TYPE *ptr = NULL;

			if(iMessage == WM_NCCREATE){
				CREATESTRUCT* pCS = (CREATESTRUCT*)lParam;
				ptr = (DERIVED_TYPE*)pCS->lpCreateParams;
				SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)ptr);

				ptr->_hWnd = hWnd;
			}else{
				ptr = (DERIVED_TYPE*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
			}

			if(ptr){
				return ptr->Handler(iMessage, wParam, lParam);
			}else{
				return DefWindowProc(hWnd, iMessage, wParam, lParam);
			}
		}

		HWND Window() const { return _hWnd; }

		BOOL Create(
				PCWSTR lpszWindowName,
				DWORD dwStyle = WS_OVERLAPPEDWINDOW,
				DWORD dwExStyle = 0,
				LONG x = CW_USEDEFAULT,
				LONG y = CW_USEDEFAULT,
				LONG Width = CW_USEDEFAULT,
				LONG Height = CW_USEDEFAULT,
				HWND hWndParent = HWND_DESKTOP,
				HMENU hMenu = NULL
		){
			HINSTANCE hInst = GetModuleHandle(NULL);
			WNDCLASSEX wcex{};
			// WNDCLASSEX wcex = {0,};

			// 클래스가 이미 등록되어 있는 경우 함수는 실패하고 새로운 리소스를 할당하지 않는다.
			// 다만, 메모리 누수로 인한 리소스 사용과 이로인한 잠금 현상, 충돌 현상 등이 발생해 cpu의 사용량이 증가한다.
			if(!(GetClassInfoEx(hInst, ClassName(), &wcex))){
				wcex.cbSize = sizeof(wcex);
				wcex.style = CS_HREDRAW | CS_VREDRAW;
				wcex.lpfnWndProc = DERIVED_TYPE::WndProc;
				wcex.cbClsExtra = 0;
				wcex.cbWndExtra = 0;
				wcex.hInstance = hInst;
				wcex.hIcon = LoadIcon(NULL, IDI_APPLICATION);
				wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
				wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
				wcex.lpszMenuName = NULL;
				wcex.lpszClassName = ClassName();
				wcex.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

				RegisterClassEx(&wcex);
			}

			_hWnd = CreateWindowEx(
				dwExStyle,
				ClassName(),
				lpszWindowName,
				dwStyle,
				x, y, Width, Height,
				hWndParent,
				hMenu,
				GetModuleHandle(NULL),
				this
			);

			return ((_hWnd) ? TRUE : FALSE);
		}
};
#endif
