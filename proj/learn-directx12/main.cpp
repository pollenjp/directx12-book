#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <tchar.h>

#include <vector>

#ifdef _DEBUG
#include <iostream>
#endif

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

const unsigned int window_width = 1280;
const unsigned int window_height = 720;

// DXGI (DirectX Graphics Infrastructure)
IDXGIFactory6* dxgiFactory_ = nullptr;
IDXGISwapChain4* swapchain_ = nullptr;

// Direct3D
ID3D12Device* dev_ = nullptr;

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
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  switch (msg) {
    case WM_DESTROY:
      PostQuitMessage(0);  // OSに対して「もうこのアプリは終わるんや」と伝える
      return 0;

    default:
      break;
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
  w.lpfnWndProc = (WNDPROC)WindowProc;  //コールバック関数の指定
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

  /////////////////////////
  // Initialie DirectX12 //
  /////////////////////////

  // D3D12CreateDevice() 関数の第1引数を nullptr にしてしまうと,
  // 予期したグラフィックスボードが選ばれるとは限らないため,
  // アダプターを明示的に指定する.

  // enumerate adapters
  HRESULT result = S_OK;
  if (FAILED(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG,
                                IID_PPV_ARGS(&dxgiFactory_)))) {
    // CreateDXGIFactory2 に失敗した場合は, DEBUG フラグを外して再実行
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&dxgiFactory_)))) {
      // それでも FAIL する場合は, error return.
      return -1;
    }
  }

  std::vector<IDXGIAdapter*> adapters;
  IDXGIAdapter* tmpAdapter = nullptr;
  for (UINT i = 0;
       dxgiFactory_->EnumAdapters(i, &tmpAdapter) != DXGI_ERROR_NOT_FOUND;
       ++i) {
    adapters.push_back(tmpAdapter);
  }
  for (auto adpt : adapters) {
    DXGI_ADAPTER_DESC adesc = {};
    adpt->GetDesc(&adesc);
    std::wstring strDesc = adesc.Description;
    if (strDesc.find(L"NVIDIA") != std::string::npos) {
      tmpAdapter = adpt;
      break;
    }
  }

  //フィーチャレベル列挙
  D3D_FEATURE_LEVEL levels[] = {
      D3D_FEATURE_LEVEL_12_1,
      D3D_FEATURE_LEVEL_12_0,
      D3D_FEATURE_LEVEL_11_1,
      D3D_FEATURE_LEVEL_11_0,
  };

  // Direct3Dデバイスの初期化
  D3D_FEATURE_LEVEL featureLevel;
  for (auto l : levels) {
    if (D3D12CreateDevice(tmpAdapter, l, IID_PPV_ARGS(&dev_)) == S_OK) {
      featureLevel = l;
      break;
    }
  }

  ID3D12CommandAllocator* cmdAllocator_ = nullptr;
  ID3D12GraphicsCommandList* cmdList_ = nullptr;
  // Create command list and command allocator
  result = dev_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                        IID_PPV_ARGS(&cmdAllocator_));
  result =
      dev_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAllocator_,
                              nullptr, IID_PPV_ARGS(&cmdList_));

  /////////////////
  // Show Window //
  /////////////////

  ShowWindow(hwnd, SW_SHOW);

  //////////////////
  // message loop //
  //////////////////

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
