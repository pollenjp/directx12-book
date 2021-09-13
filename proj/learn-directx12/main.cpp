#include <DirectXMath.h>
#include <DirectXTex.h>
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
#pragma comment(lib, "DirectXTex.lib")

const unsigned int window_width = 1280;
const unsigned int window_height = 720;

struct Vertex {
  DirectX::XMFLOAT3 pos;  // x,y,z coordinate
  DirectX::XMFLOAT2 uv;   // uv coordinate
};

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

  try {
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
      return EXIT_FAILURE;
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

    Vertex vertices[] = {
        {{-0.4f, -0.7f, 0.0f}, {0.0f, 1.0f}},  // 左下
        {{-0.4f, 0.7f, 0.0f}, {0.0f, 0.0f}},   // 左上
        {{0.4f, -0.7f, 0.0f}, {1.0f, 1.0f}},   // 右下
        {{0.4f, 0.7f, 0.0f}, {1.0f, 0.0f}},    // 右上
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

    ID3D12Resource* vertBuff = nullptr;
    result = _dev->CreateCommittedResource(&heapprop,  // heap description
                                           D3D12_HEAP_FLAG_NONE,
                                           &resdesc,  // resource description
                                           D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertBuff));

    // copy vertices data to vertex buffer
    Vertex* vertMap = nullptr;
    result = vertBuff->Map(0, nullptr, (void**)&vertMap);
    if (FAILED(result)) {
      throw std::runtime_error("Failed to map vertex buffer");
    }
    std::copy(std::begin(vertices), std::end(vertices), vertMap);
    vertBuff->Unmap(0, nullptr);

    // create vertex buffer view
    D3D12_VERTEX_BUFFER_VIEW vbView = {};
    vbView.BufferLocation = vertBuff->GetGPUVirtualAddress();
    vbView.SizeInBytes = sizeof(vertices);
    vbView.StrideInBytes = sizeof(vertices[0]);

    //////////////////
    // index buffer //
    //////////////////

    unsigned short indices[] = {
        0, 1, 2,  // 左下三角
        2, 1, 3   // 右上三角
    };

    ID3D12Resource* idxBuff = nullptr;
    // 設定は、バッファのサイズ以外頂点バッファの設定を使いまわしてOK
    resdesc.Width = sizeof(indices);
    result = _dev->CreateCommittedResource(&heapprop, D3D12_HEAP_FLAG_NONE, &resdesc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                           nullptr, IID_PPV_ARGS(&idxBuff));

    // 作ったバッファにインデックスデータをコピー
    unsigned short* mappedIdx = nullptr;
    idxBuff->Map(0, nullptr, (void**)&mappedIdx);
    std::copy(std::begin(indices), std::end(indices), mappedIdx);
    idxBuff->Unmap(0, nullptr);

    // インデックスバッファビューを作成
    D3D12_INDEX_BUFFER_VIEW ibView = {};
    ibView.BufferLocation = idxBuff->GetGPUVirtualAddress();
    ibView.Format = DXGI_FORMAT_R16_UINT;
    ibView.SizeInBytes = sizeof(indices);

    ///////////////////////
    // load shader files //
    ///////////////////////

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
      return EXIT_FAILURE;
    }
    result = D3DCompileFromFile(L"BasicPixelShader.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "BasicPS",
                                "ps_5_0", D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION, 0, &_psBlob, &errorBlob);
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
      return EXIT_FAILURE;
    }

    // Vertex Layout
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        // position
        {
            "POSITION",                                  // SemanticName
            0,                                           // SemanticIndex
            DXGI_FORMAT_R32G32B32_FLOAT,                 // Format
            0,                                           // InputSlot
            D3D12_APPEND_ALIGNED_ELEMENT,                // AlignedByteOffset
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,  // InputSlotClass
            0                                            // InstanceDataStepRate
        },
        // uv
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    ///////////////////////
    // Graphics Pipeline //
    ///////////////////////

    D3D12_GRAPHICS_PIPELINE_STATE_DESC gpipeline = {};

    // root signature

    gpipeline.pRootSignature = nullptr;

    // VS, vertex shader

    gpipeline.VS.pShaderBytecode = _vsBlob->GetBufferPointer();
    gpipeline.VS.BytecodeLength = _vsBlob->GetBufferSize();

    // PS, pixel shader

    gpipeline.PS.pShaderBytecode = _psBlob->GetBufferPointer();
    gpipeline.PS.BytecodeLength = _psBlob->GetBufferSize();

    // DS, domain shader

    // HS , hull shader

    // GS , geometry shader

    // TODO:
    // StreamOutput

    // BlendState

    gpipeline.BlendState.AlphaToCoverageEnable = false;  // alpha test, alpha blend
    gpipeline.BlendState.IndependentBlendEnable = false;

    D3D12_RENDER_TARGET_BLEND_DESC renderTargetBlendDesc = {};
    renderTargetBlendDesc.BlendEnable = false;  // Disable alpha blending
    renderTargetBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    renderTargetBlendDesc.LogicOpEnable = false;  // Disable logic op
    gpipeline.BlendState.RenderTarget[0] = renderTargetBlendDesc;

    // SampleMask

    gpipeline.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;  // D3D12_DEFAULT_SAMPLE_MASK : 0xffffffff

    // RasterizerState

    gpipeline.RasterizerState.MultisampleEnable = false;        // disable anti-aliasing
    gpipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;  // disable culling
    gpipeline.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    gpipeline.RasterizerState.DepthClipEnable = true;  // 深度方向のクリッピングは有効に
    gpipeline.RasterizerState.FrontCounterClockwise = false;
    gpipeline.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    gpipeline.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    gpipeline.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    gpipeline.RasterizerState.AntialiasedLineEnable = false;
    gpipeline.RasterizerState.ForcedSampleCount = 0;
    gpipeline.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    // DepthStencilState

    gpipeline.DepthStencilState.DepthEnable = false;
    gpipeline.DepthStencilState.StencilEnable = false;

    // InputLayout

    gpipeline.InputLayout.pInputElementDescs = inputLayout;  // レイアウト先頭アドレス
    gpipeline.InputLayout.NumElements = _countof(inputLayout);

    // IBStripCutValue
    // D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED : トライアングルストリップ時に切り離さない
    gpipeline.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;

    // PrimitiveTopologyType
    gpipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    // NumRenderTargets
    gpipeline.NumRenderTargets = 1;

    // RTVFormats *
    gpipeline.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;  // 0～1 に正規化されたRGBA

    // DSVFormat
    // default

    // SampleDesc

    gpipeline.SampleDesc.Count = 1;    // 1 sample per pixel (disable anti-aliasing)
    gpipeline.SampleDesc.Quality = 0;  // lowest quality

    ////////////////////
    // Root Signature //
    ////////////////////

    ID3D12RootSignature* rootsignature = nullptr;
    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
    // D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT : 頂点情報 (入力アセンブラ) がある
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    // descriptor range
    D3D12_DESCRIPTOR_RANGE descTblRange = {};
    descTblRange.NumDescriptors = 1;                           // テクスチャ1 つ
    descTblRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;  // 種別はテクスチャ
    descTblRange.BaseShaderRegister = 0;                       // 0 番スロットから
    descTblRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // root parameter
    D3D12_ROOT_PARAMETER rootparam = {};
    rootparam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootparam.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;   // ピクセルシェーダーから見える
    rootparam.DescriptorTable.pDescriptorRanges = &descTblRange;  // ディスクリプタレンジのアドレス
    rootparam.DescriptorTable.NumDescriptorRanges = 1;            // ディスクリプタレンジ数

    rootSignatureDesc.pParameters = &rootparam;
    rootSignatureDesc.NumParameters = 1;

    // sampler
    D3D12_STATIC_SAMPLER_DESC samplerDesc = {};
    samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;                 // 横方向の繰り返し
    samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;                 // 縦方向の繰り返し
    samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;                 // 奥行きの繰り返し
    samplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;  // ボーダーは黒
    samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;                   // 線形補間
    samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;                                 // ミップマップ最大値
    samplerDesc.MinLOD = 0.0f;                                              // ミップマップ最小値
    samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;  // ピクセルシェーダーから見える
    samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;      // リサンプリングしない

    rootSignatureDesc.pStaticSamplers = &samplerDesc;
    rootSignatureDesc.NumStaticSamplers = 1;

    ID3DBlob* rootSigBlob = nullptr;
    result = D3D12SerializeRootSignature(&rootSignatureDesc,              // ルートシグネチャ設定
                                         D3D_ROOT_SIGNATURE_VERSION_1_0,  // ルートシグネチャバージョン
                                         &rootSigBlob, &errorBlob);
    result = _dev->CreateRootSignature(0,  // nodemask
                                       rootSigBlob->GetBufferPointer(), rootSigBlob->GetBufferSize(),
                                       IID_PPV_ARGS(&rootsignature));
    rootSigBlob->Release();

    gpipeline.pRootSignature = rootsignature;

    //////////////////////////////
    // Create Graphics Pipeline //
    //////////////////////////////

    ID3D12PipelineState* _pipelinestate = nullptr;
    result = _dev->CreateGraphicsPipelineState(&gpipeline, IID_PPV_ARGS(&_pipelinestate));

    //////////////
    // Viewport //
    //////////////

    D3D12_VIEWPORT viewport = {};
    viewport.Width = window_width;    // 出力先の幅(ピクセル数)
    viewport.Height = window_height;  // 出力先の高さ(ピクセル数)
    viewport.TopLeftX = 0;            // 出力先の左上座標X
    viewport.TopLeftY = 0;            // 出力先の左上座標Y
    viewport.MaxDepth = 1.0f;         // 深度最大値
    viewport.MinDepth = 0.0f;         // 深度最小値

    ///////////////////////
    // Scissor Rectangle //
    ///////////////////////

    D3D12_RECT scissorrect = {};
    scissorrect.top = 0;                                   // 切り抜き上座標
    scissorrect.left = 0;                                  // 切り抜き左座標
    scissorrect.right = scissorrect.left + window_width;   // 切り抜き右座標
    scissorrect.bottom = scissorrect.top + window_height;  // 切り抜き下座標

    ////////////////////
    // Texture Buffer //
    ////////////////////

    // WICテクスチャのロード
    DirectX::TexMetadata metadata = {};
    DirectX::ScratchImage scratchImg = {};
    result = DirectX::LoadFromWICFile(L"img/textest.png", DirectX::WIC_FLAGS_NONE, &metadata, scratchImg);
    auto img = scratchImg.GetImage(0, 0, 0);  // 生データ抽出
    // ノイズテクスチャの作成
    // struct TexRGBA {
    //   unsigned char R, G, B, A;
    // };
    // std::vector<TexRGBA> texturedata(256 * 256);

    // for (auto& rgba : texturedata) {
    //   rgba.R = rand() % 256;
    //   rgba.G = rand() % 256;
    //   rgba.B = rand() % 256;
    //   rgba.A = 255;  // アルファは1.0という事にします。
    // }

    // WriteToSubresourceで転送する用のヒープ設定
    D3D12_HEAP_PROPERTIES texHeapProp = {};
    texHeapProp.Type = D3D12_HEAP_TYPE_CUSTOM;  //特殊な設定なのでdefaultでもuploadでもなく
    texHeapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;  //ライトバックで
    texHeapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;           //転送がL0つまりCPU側から直で
    texHeapProp.CreationNodeMask = 0;                                  //単一アダプタのため0
    texHeapProp.VisibleNodeMask = 0;                                   //単一アダプタのため0

    D3D12_RESOURCE_DESC resDesc = {};
    resDesc.Format = metadata.format;                     // DXGI_FORMAT_R8G8B8A8_UNORM;//RGBAフォーマット
    resDesc.Width = static_cast<UINT>(metadata.width);    // 幅
    resDesc.Height = static_cast<UINT>(metadata.height);  // 高さ
    resDesc.DepthOrArraySize = static_cast<uint16_t>(metadata.arraySize);  // 2Dで配列でもないので１
    resDesc.SampleDesc.Count = 1;    // 通常テクスチャなのでアンチェリしない
    resDesc.SampleDesc.Quality = 0;  // クオリティは最低
    resDesc.MipLevels = static_cast<uint16_t>(metadata.mipLevels);  // ミップマップしないのでミップ数は１つ
    resDesc.Dimension = static_cast<D3D12_RESOURCE_DIMENSION>(metadata.dimension);  // 2Dテクスチャ用
    resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;  // レイアウトについては決定しない
    resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;       // とくにフラグなし

    ID3D12Resource* texbuff = nullptr;
    result = _dev->CreateCommittedResource(
        &texHeapProp,
        D3D12_HEAP_FLAG_NONE,  //特に指定なし
        &resDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,  //テクスチャ用(ピクセルシェーダから見る用)
        nullptr, IID_PPV_ARGS(&texbuff));

    // result = texbuff->WriteToSubresource(0,
    //                                      nullptr,                              // 全領域へコピー
    //                                      texturedata.data(),                   // 元データアドレス
    //                                      sizeof(TexRGBA) * 256,                // 1 ラインサイズ
    //                                      sizeof(TexRGBA) * texturedata.size()  // 全サイズ
    // );
    result = texbuff->WriteToSubresource(0,
                                         nullptr,                            //全領域へコピー
                                         img->pixels,                        //元データアドレス
                                         static_cast<UINT>(img->rowPitch),   // 1ラインサイズ
                                         static_cast<UINT>(img->slicePitch)  //全サイズ
    );

    // texture's descriptor heap
    ID3D12DescriptorHeap* texDescHeap = nullptr;
    D3D12_DESCRIPTOR_HEAP_DESC descHeapDesc = {};
    descHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;  // シェーダーから見えるように
    descHeapDesc.NodeMask = 0;                                       // マスクは 0
    descHeapDesc.NumDescriptors = 1;                                 // ビューは今のところ1 つだけ
    descHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;      // シェーダーリソースビュー用
    result = _dev->CreateDescriptorHeap(&descHeapDesc, IID_PPV_ARGS(&texDescHeap));

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = metadata.format;  // DXGI_FORMAT_R8G8B8A8_UNORM;//RGBA(0.0f～1.0fに正規化)
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;  // 2D テクスチャ
    srvDesc.Texture2D.MipLevels = 1;                        // ミップマップは使用しないので1

    _dev->CreateShaderResourceView(texbuff,   // ビューと関連付けるバッファー
                                   &srvDesc,  // 先ほど設定したテクスチャ設定情報
                                   texDescHeap->GetCPUDescriptorHandleForHeapStart()  // ヒープのどこに割り当てるか
    );

    //////////////////
    // message loop //
    //////////////////

    MSG msg = {};
    unsigned int frame = 0;
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

      if (_pipelinestate != nullptr) {
        _cmdList->SetPipelineState(_pipelinestate);
      }

      //レンダーターゲットを指定
      auto rtvH = rtvHeaps->GetCPUDescriptorHandleForHeapStart();
      rtvH.ptr +=
          static_cast<ULONG_PTR>(bbIdx * _dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV));
      _cmdList->OMSetRenderTargets(1, &rtvH, false, nullptr);

      //画面クリア
      float clearColor[] = {1.0f, 1.0f, 0.0f, 1.0f};  //黄色
      _cmdList->ClearRenderTargetView(rtvH, clearColor, 0, nullptr);

      _cmdList->RSSetViewports(1, &viewport);
      _cmdList->RSSetScissorRects(1, &scissorrect);
      _cmdList->SetGraphicsRootSignature(rootsignature);

      _cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      _cmdList->IASetVertexBuffers(0, 1, &vbView);
      _cmdList->IASetIndexBuffer(&ibView);

      _cmdList->SetGraphicsRootSignature(rootsignature);
      _cmdList->SetDescriptorHeaps(1, &texDescHeap);
      _cmdList->SetGraphicsRootDescriptorTable(0,  // ルートパラメーターインデックス
                                               texDescHeap->GetGPUDescriptorHandleForHeapStart());  // ヒープアドレス

      _cmdList->DrawIndexedInstanced(6, 1, 0, 0, 0);

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

      _cmdAllocator->Reset();                          //キューをクリア
      _cmdList->Reset(_cmdAllocator, _pipelinestate);  //再びコマンドリストをためる準備

      //フリップ
      _swapchain->Present(1, 0);
    }

    // もうクラス使わんから登録解除してや
    UnregisterClass(w.lpszClassName, w.hInstance);

    return EXIT_SUCCESS;
  } catch (const std::exception& e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  } catch (...) {
    return EXIT_FAILURE;
  }
}
