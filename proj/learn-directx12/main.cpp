#include <Windows.h>
#include <tchar.h>
#ifdef _DEBUG
#include <iostream>
#endif

const unsigned int window_width = 1280;
const unsigned int window_height = 720;

/**
 * @brief コンソール画面にフォーマット付き文字列を表示
 * @param format フォーマット（%d とか%f とかの）
 * @param 可変長引数
 * @remarks この関数はデバッグ用です。デバッグ時にしか動作しません
 */
void DebugOutputFormatString(const char* format, ...) {
#ifdef _DEBUG
  va_list valist;
  va_start(valist, format);
  vprintf(format, valist);
  va_end(valist);
#endif
}

//面倒ですが、ウィンドウプロシージャは必須なので書いておきます
LRESULT WindowProcedure(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  if (msg == WM_DESTROY) {  //ウィンドウが破棄されたら呼ばれます
    PostQuitMessage(0);  // OSに対して「もうこのアプリは終わるんや」と伝える
    return 0;
  }
  return DefWindowProc(hwnd, msg, wparam, lparam);  //規定の処理を行う
}

#ifdef _DEBUG
int main() {
#else
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
#endif
  DebugOutputFormatString("Show window test.");
  // ウィンドウクラス生成＆登録
  WNDCLASSEX w = {};
  w.cbSize = sizeof(WNDCLASSEX);
  w.lpfnWndProc = (WNDPROC)WindowProcedure;  //コールバック関数の指定
  // アプリケーションクラス名(適当でいいです)
  w.lpszClassName = _T("DirectXTest");
  w.hInstance = GetModuleHandle(0);  //ハンドルの取得
  // アプリケーションクラス(こういうの作るからよろしくってOSに予告する)
  RegisterClassEx(&w);

  RECT wrc = {0, 0, window_width, window_height};  //ウィンドウサイズを決める
  // ウィンドウのサイズはちょっと面倒なので関数を使って補正する
  AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);
  // ウィンドウオブジェクトの生成
  HWND hwnd = CreateWindow(
      w.lpszClassName,  //クラス名指定
      _T("DX12 Test"),  // タイトルバーの文字
                        // TODO:  日本語が使えなかった？
      WS_OVERLAPPEDWINDOW,  //タイトルバーと境界線があるウィンドウです
      CW_USEDEFAULT,         //表示X座標はOSにお任せします
      CW_USEDEFAULT,         //表示Y座標はOSにお任せします
      wrc.right - wrc.left,  //ウィンドウ幅
      wrc.bottom - wrc.top,  //ウィンドウ高
      nullptr,               //親ウィンドウハンドル
      nullptr,               //メニューハンドル
      w.hInstance,           //呼び出しアプリケーションハンドル
      nullptr);              //追加パラメータ

  // ウィンドウ表示
  ShowWindow(hwnd, SW_SHOW);
  MSG msg = {};
  while (true) {
    if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
    //もうアプリケーションが終わるって時にmessageがWM_QUITになる
    if (msg.message == WM_QUIT) {
      break;
    }
  }

  // もうクラス使わんから登録解除してや
  UnregisterClass(w.lpszClassName, w.hInstance);
  return 0;
}
