#include <DirectXMath.h>
#include <Windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <tchar.h>

#include <vector>

#ifdef _DEBUG
#include <iostream>
#endif

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

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

IDXGIFactory6* _dxgiFactory = nullptr;
ID3D12Device* _dev = nullptr;
ID3D12CommandAllocator* _cmdAllocator = nullptr;
ID3D12GraphicsCommandList* _cmdList = nullptr;
ID3D12CommandQueue* _cmdQueue = nullptr;
IDXGISwapChain4* _swapchain = nullptr;

void EnableDebugLayer() {
  ID3D12Debug* debugLayer = nullptr;
  if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugLayer)))) {
    debugLayer->EnableDebugLayer();
    debugLayer->Release();
  }
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
  HWND hwnd = CreateWindow(w.lpszClassName,                 //クラス名指定
                           _T("DX12 Simple Polygon Test"),  // タイトルバーの文字
                                                            // TODO:  日本語が使えなかった？
                           WS_OVERLAPPEDWINDOW,   //タイトルバーと境界線があるウィンドウです
                           CW_USEDEFAULT,         //表示X座標はOSにお任せします
                           CW_USEDEFAULT,         //表示Y座標はOSにお任せします
                           wrc.right - wrc.left,  //ウィンドウ幅
                           wrc.bottom - wrc.top,  //ウィンドウ高
                           nullptr,               //親ウィンドウハンドル
                           nullptr,               //メニューハンドル
                           w.hInstance,           //呼び出しアプリケーションハンドル
                           nullptr);              //追加パラメータ

  //////////////////////////
  // Initialize DirectX12 //
  //////////////////////////

#ifdef _DEBUG
  // デバッグレイヤーをオンに
  // デバイス生成時前にやっておかないと、デバイス生成後にやるとデバイスがロストしてしまうので注意
  EnableDebugLayer();
#endif

  // D3D12CreateDevice() 関数の第1引数を nullptr にしてしまうと,
  // 予期したグラフィックスボードが選ばれるとは限らないため,
  // アダプターを明示的に指定する.

  // DXGI (DirectX Graphics Infrastructure)
  HRESULT result = S_OK;
#ifdef _DEBUG
  if (FAILED(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&_dxgiFactory)))) {
    // CreateDXGIFactory2 に失敗した場合は, DEBUG フラグを外して再実行
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&_dxgiFactory)))) {
      // それでも FAIL する場合は, error return.
      return -1;
    }
  }
#else
  result = CreateDXGIFactory1(IID_PPV_ARGS(&_dxgiFactory));
#endif

  // enumerate adapters
  std::vector<IDXGIAdapter*> adapters;
  IDXGIAdapter* tmpAdapter = nullptr;
  for (UINT i = 0; _dxgiFactory->EnumAdapters(i, &tmpAdapter) != DXGI_ERROR_NOT_FOUND; ++i) {
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
    if (D3D12CreateDevice(tmpAdapter, l, IID_PPV_ARGS(&_dev)) == S_OK) {
      featureLevel = l;
      break;
    }
  }
  if (_dev == nullptr) {
    std::exit(EXIT_FAILURE);
  }

  // Create command list and command allocator
  result = _dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&_cmdAllocator));
  result = _dev->CreateCommandList(0,  // 0 is single-GPU operation
                                   D3D12_COMMAND_LIST_TYPE_DIRECT,
                                   _cmdAllocator,  // この Command Allocator に対応する
                                   nullptr,        //  nulltpr : dummy initial pipeline state
                                   IID_PPV_ARGS(&_cmdList));

  // command queue
  D3D12_COMMAND_QUEUE_DESC cmdQueueDesc = {};
  cmdQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;  // タイムアウトなし
  cmdQueueDesc.NodeMask = 0;
  cmdQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;  // プライオリティ特に指定なし
  cmdQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;           // same as Command List.
  result = _dev->CreateCommandQueue(&cmdQueueDesc, IID_PPV_ARGS(&_cmdQueue));

  // swap chain
  DXGI_SWAP_CHAIN_DESC1 swapchainDesc = {};
  swapchainDesc.Width = window_width;
  swapchainDesc.Height = window_height;
  swapchainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  swapchainDesc.Stereo = false;
  swapchainDesc.SampleDesc.Count = 1;
  swapchainDesc.SampleDesc.Quality = 0;
  swapchainDesc.BufferUsage = DXGI_USAGE_BACK_BUFFER;
  swapchainDesc.BufferCount = 2;                 // double buffering
  swapchainDesc.Scaling = DXGI_SCALING_STRETCH;  // window mode だから？
  swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  swapchainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
  swapchainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

  // 先に作った ``hwnd`` の Swap Chain を作成
  result = _dxgiFactory->CreateSwapChainForHwnd(_cmdQueue,                      // command queue
                                                hwnd,                           // a handle to a window
                                                &swapchainDesc,                 // desc
                                                nullptr,                        // window mode
                                                nullptr,                        // not restrict content
                                                (IDXGISwapChain1**)&_swapchain  // out parameter
  );

  // discriptor heap
  D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
  heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;    //レンダーターゲットビューなので当然RTV
  heapDesc.NodeMask = 0;                             // only use single GPU
  heapDesc.NumDescriptors = 2;                       // double buffering (front, back buffer)
  heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;  // 特に指定なし
  ID3D12DescriptorHeap* rtvHeaps = nullptr;          // out parameter 用
  result = _dev->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&rtvHeaps));

  // Discriptor と スワップチェーン上のバックバッファと関連付け

  DXGI_SWAP_CHAIN_DESC swcDesc = {};
  result = _swapchain->GetDesc(&swcDesc);
  std::vector<ID3D12Resource*> _backBuffers(swcDesc.BufferCount);
  D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvHeaps->GetCPUDescriptorHandleForHeapStart();
  for (size_t i = 0; i < swcDesc.BufferCount; ++i) {
    result = _swapchain->GetBuffer(static_cast<UINT>(i), IID_PPV_ARGS(&_backBuffers[i]));
    _dev->CreateRenderTargetView(_backBuffers[i], nullptr, handle);
    handle.ptr += _dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  }

  ///////////
  // Fence //
  ///////////

  ID3D12Fence* _fence = nullptr;
  UINT64 _fenceVal = 0;
  result = _dev->CreateFence(_fenceVal, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_fence));
  // if (result != S_OK) {
  //   std::exit(EXIT_FAILURE);
  // }

  /////////////////
  // Show Window //
  /////////////////

  ShowWindow(hwnd, SW_SHOW);

  ////////////
  // Shader //
  ////////////

  DirectX::XMFLOAT3 vertices[] = {
      {-1.0f, -1.0f, 0.0f},  // 左下
      {-1.0f, 1.0f, 0.0f},   // 左上
      {1.0f, -1.0f, 0.0f},   // 右下
  };

  D3D12_HEAP_PROPERTIES heapprop = {};
  heapprop.Type = D3D12_HEAP_TYPE_UPLOAD;
  heapprop.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
  heapprop.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

  D3D12_RESOURCE_DESC resdesc = {};
  resdesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  resdesc.Width = sizeof(vertices);
  resdesc.Height = 1;
  resdesc.DepthOrArraySize = 1;
  resdesc.MipLevels = 1;
  resdesc.Format = DXGI_FORMAT_UNKNOWN;
  resdesc.SampleDesc.Count = 1;
  resdesc.Flags = D3D12_RESOURCE_FLAG_NONE;
  resdesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

  // UPLOAD(確保は可能)
  ID3D12Resource* vertBuff = nullptr;
  result = _dev->CreateCommittedResource(&heapprop,  // heap description
                                         D3D12_HEAP_FLAG_NONE,
                                         &resdesc,  // resource description
                                         D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertBuff));

  // copy vertices data to vertex buffer
  DirectX::XMFLOAT3* vertMap = nullptr;
  vertBuff->Map(0, nullptr, (void**)&vertMap);
  std::copy(std::begin(vertices), std::end(vertices), vertMap);
  vertBuff->Unmap(0, nullptr);

  // create vertex buffer view
  D3D12_VERTEX_BUFFER_VIEW vbView = {};
  vbView.BufferLocation = vertBuff->GetGPUVirtualAddress();
  vbView.SizeInBytes = sizeof(vertices);
  vbView.StrideInBytes = sizeof(vertices[0]);

  // load shader files
  ID3DBlob* _vsBlob = nullptr;
  ID3DBlob* _psBlob = nullptr;

  ID3DBlob* errorBlob = nullptr;
  result = D3DCompileFromFile(L"BasicVertexShader.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "BasicVS",
                              "vs_5_0", D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION, 0, &_vsBlob, &errorBlob);
  if (FAILED(result)) {
    if (result == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) {
      ::OutputDebugStringA("File was not found!");
    } else {
      std::string errstr;
      errstr.resize(errorBlob->GetBufferSize());
      std::copy_n((char*)errorBlob->GetBufferPointer(), errorBlob->GetBufferSize(), errstr.begin());
      errstr += "\n";
      OutputDebugStringA(errstr.c_str());
    }
    exit(EXIT_FAILURE);
  }
  result = D3DCompileFromFile(L"BasicPixelShader.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "BasicPS", "ps_5_0",
                              D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION, 0, &_psBlob, &errorBlob);
  if (FAILED(result)) {
    if (result == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) {
      ::OutputDebugStringA("File was not found!");
    } else {
      std::string errstr;
      errstr.resize(errorBlob->GetBufferSize());
      std::copy_n((char*)errorBlob->GetBufferPointer(), errorBlob->GetBufferSize(), errstr.begin());
      errstr += "\n";
      OutputDebugStringA(errstr.c_str());
    }
    exit(EXIT_FAILURE);
  }

  // Vertex Layout
  D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  };

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

    // DirectX処理
    //バックバッファのインデックスを取得
    auto bbIdx = _swapchain->GetCurrentBackBufferIndex();

    D3D12_RESOURCE_BARRIER BarrierDesc = {};
    BarrierDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    BarrierDesc.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    BarrierDesc.Transition.pResource = _backBuffers[bbIdx];
    BarrierDesc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    BarrierDesc.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    BarrierDesc.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    _cmdList->ResourceBarrier(1, &BarrierDesc);

    //レンダーターゲットを指定
    auto rtvH = rtvHeaps->GetCPUDescriptorHandleForHeapStart();
    rtvH.ptr += static_cast<ULONG_PTR>(bbIdx * _dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV));
    _cmdList->OMSetRenderTargets(1, &rtvH, false, nullptr);

    //画面クリア
    float clearColor[] = {1.0f, 1.0f, 0.0f, 1.0f};  //黄色
    _cmdList->ClearRenderTargetView(rtvH, clearColor, 0, nullptr);

    BarrierDesc.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    BarrierDesc.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    _cmdList->ResourceBarrier(1, &BarrierDesc);

    //命令のクローズ
    _cmdList->Close();

    //コマンドリストの実行
    ID3D12CommandList* cmdlists[] = {_cmdList};
    _cmdQueue->ExecuteCommandLists(1, cmdlists);

    ////待ち
    _cmdQueue->Signal(_fence, ++_fenceVal);

    if (_fence->GetCompletedValue() != _fenceVal) {
      auto event = CreateEvent(nullptr, false, false, nullptr);
      _fence->SetEventOnCompletion(_fenceVal, event);
      if (event != 0) {
        WaitForSingleObject(event, INFINITE);
        CloseHandle(event);
      }
    }

    _cmdAllocator->Reset();                   //キューをクリア
    _cmdList->Reset(_cmdAllocator, nullptr);  //再びコマンドリストをためる準備

    //フリップ
    _swapchain->Present(1, 0);
  }

  // もうクラス使わんから登録解除してや
  UnregisterClass(w.lpszClassName, w.hInstance);
  return 0;
}
