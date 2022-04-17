#include <Windows.h>

LRESULT CALLBACK Win32WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  LRESULT result = 0;

  switch (message) {
    case WM_CLOSE:
      DestroyWindow(window);
      break;

    case WM_DESTROY:
      PostQuitMessage(0);
      break;

    case WM_PAINT: {
      RECT cr;
      GetClientRect(window, &cr);

      int width = cr.right - cr.left;
      int height = cr.bottom - cr.top;

      PAINTSTRUCT ps;
      HDC dc = BeginPaint(window, &ps);
      PatBlt(dc, 0, 0, width, height, BLACKNESS);
      EndPaint(window, &ps);
      break;
    }

    default:
      result = DefWindowProcW(window, message, wparam, lparam);
      break;
  }

  return result;
}

int CALLBACK wWinMain(HINSTANCE instance, HINSTANCE previousInstance, LPWSTR cmdLine, int showCode) {
  WNDCLASSW wc = {};
  wc.hInstance = instance;
  wc.hIcon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(1));
  wc.lpfnWndProc = Win32WindowProc;
  wc.lpszClassName = L"clazz";
  RegisterClassW(&wc);

  HWND window = CreateWindowW(L"clazz", L"fta2", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 720, nullptr, nullptr, instance, nullptr);
  if (window) {
    ShowWindow(window, SW_SHOW);

    MSG msg = { };
    while (msg.message != WM_QUIT) {
      if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }
    }
  }

  return 0;
}
