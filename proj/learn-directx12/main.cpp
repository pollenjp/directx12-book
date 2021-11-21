#include <DirectXMath.h>
#include <DirectXTex.h>
#include <Windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <d3dx12.h>
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

#pragma pack(push, 1)
struct PMD_VERTEX {
  DirectX::XMFLOAT3 pos;
  DirectX::XMFLOAT3 normal;
  DirectX::XMFLOAT2 uv;
  uint16_t bone_no[2];
  uint8_t weight;
  uint8_t EdgeFlag;
  uint16_t dummy;
};
#pragma pack(pop)

/**
 * @brief アライメントに揃えたサイズを返す
 * @param size 元のサイズ
 * @param alignment アライメントサイズ
 * @return アライメントをそろえたサイズ
 */

size_t AlignmentedSize(size_t size, size_t alignment) { return size + alignment - size % alignment; }

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
  HRESULT result;
  // 以下を書いておかないとCOMが旨く動かずWICが正常に動作しないことがあります。
  // (書かなくても動くときもあります)
  // TODO: ???
  // result = CoInitializeEx(0, COINIT_MULTITHREADED);

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

    //////////////
    // load pmd //
    //////////////

    /**
     * @brief PMD ヘッダー構造体
     *
     */
    struct PMDHeader {
      float version;        // 例：00 00 80 3F == 1.00
      char model_name[20];  // モデル名
      char comment[256];    // モデルコメント
    };

    char signature[3];  // 先頭 Pmd (3 bytes)
    PMDHeader pmdheader = {};
    FILE* fp;
    auto err = _wfopen_s(&fp, L"Model/初音ミク.pmd", L"rb");
    if (fp == nullptr) {
      wchar_t strerr[256];
      _wcserror_s(strerr, 256, err);
      LPCWSTR ptr = strerr;
      MessageBox(hwnd, ptr, L"Open Error", MB_ICONERROR);
      return -1;
    }
    fread(signature, sizeof(signature), 1, fp);
    fread(&pmdheader, sizeof(pmdheader), 1, fp);

    // header の直後に頂点数
    unsigned int vertex_num(0);  // 頂点数
    fread(&vertex_num, sizeof(vertex_num), 1, fp);

    {
      wchar_t msgbuf[256];
      swprintf(msgbuf, sizeof(msgbuf) / sizeof(msgbuf[0]), L"vertex num is %u\n", vertex_num);
      OutputDebugStringW(msgbuf);
    }

    constexpr size_t pmdvertex_size(38);           // 頂点 1 つあたりのサイズ
    std::vector<PMD_VERTEX> vertices(vertex_num);  // バッファーの確保
    for (auto i = 0; i < vertex_num; i++) {
      fread(&vertices[i], pmdvertex_size, 1, fp);
    }

    fclose(fp);

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
    result = S_OK;
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

    // Create Render Target View

    // SRGB レンダーターゲットビュー設定
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;  // ガンマ補正あり (sRGB)
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

    // get address
    D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvHeaps->GetCPUDescriptorHandleForHeapStart();

    for (size_t i = 0; i < swcDesc.BufferCount; ++i) {
      result = _swapchain->GetBuffer(static_cast<UINT>(i), IID_PPV_ARGS(&_backBuffers[i]));
      _dev->CreateRenderTargetView(_backBuffers[i], &rtvDesc, handle);
      handle.ptr += _dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    }

    ///////////
    // Fence //
    ///////////

    ID3D12Fence* _fence = nullptr;
    UINT64 _fenceVal = 0;
    result = _dev->CreateFence(_fenceVal, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_fence));
    if (result != S_OK) {
      throw std::runtime_error("Failed to create fence");
    }

    /////////////////
    // Show Window //
    /////////////////

    ShowWindow(hwnd, SW_SHOW);

    ////////////
    // Shader //
    ////////////

    ID3D12Resource* vertBuff = nullptr;
    auto heapprop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    auto resdesc = CD3DX12_RESOURCE_DESC::Buffer(vertices.size() * sizeof(vertices[0]));
    result = _dev->CreateCommittedResource(&heapprop, D3D12_HEAP_FLAG_NONE, &resdesc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                           nullptr, IID_PPV_ARGS(&vertBuff));

    // copy vertices data to vertex buffer
    PMD_VERTEX* vertMap = nullptr;
    result = vertBuff->Map(0, nullptr, (void**)&vertMap);
    if (FAILED(result)) {
      throw std::runtime_error("Failed to map vertex buffer");
    }
    std::copy(std::begin(vertices), std::end(vertices), vertMap);
    vertBuff->Unmap(0, nullptr);

    // create vertex buffer view
    D3D12_VERTEX_BUFFER_VIEW vbView = {};
    vbView.BufferLocation = vertBuff->GetGPUVirtualAddress();
    vbView.SizeInBytes = static_cast<UINT>(vertices.size() * sizeof(vertices[0]));  // 全バイト数
    vbView.StrideInBytes = sizeof(PMD_VERTEX);                                      // 1頂点あたりのバイト数

    //////////////////
    // index buffer //
    //////////////////

    unsigned short indices[] = {
        0, 1, 2,  // 左下三角
        2, 1, 3   // 右上三角
    };

    ID3D12Resource* idxBuff = nullptr;
    resdesc.Width = sizeof(indices);
    heapprop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    resdesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(vertices));
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
        {
            "POSITION",
            0,
            DXGI_FORMAT_R32G32B32_FLOAT,  // 4 (FLOAT) * 3 (RGB) bytes
            0,
            D3D12_APPEND_ALIGNED_ELEMENT,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            0,
        },
        {
            "NORMAL",
            0,
            DXGI_FORMAT_R32G32B32_FLOAT,  // 4 (FLOAT) * 3 (RGB) bytes
            0,
            D3D12_APPEND_ALIGNED_ELEMENT,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            0,
        },
        {
            "TEXCOORD",
            0,
            DXGI_FORMAT_R32G32_FLOAT,  // 4 (FLOAT) * 2 (RGB) bytes
            0,
            D3D12_APPEND_ALIGNED_ELEMENT,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            0,
        },
        {
            "BONE_NO",
            0,
            DXGI_FORMAT_R16G16_UINT,  // 4 (UINT) * 2 (RGB) bytes
            0,
            D3D12_APPEND_ALIGNED_ELEMENT,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            0,
        },
        {
            "WEIGHT",
            0,
            DXGI_FORMAT_R8_UINT,  // 4 (UINT) * 1 (RGB) bytes
            0,
            D3D12_APPEND_ALIGNED_ELEMENT,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            0,
        },
        {
            "EDGE_FLG",
            0,
            DXGI_FORMAT_R8_UINT,  // 4 (UINT) * 1 (RGB) bytes
            0,
            D3D12_APPEND_ALIGNED_ELEMENT,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            0,
        },
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
    gpipeline.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;  // 0～1 に正規化されたRGBA (SRGB)

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

    D3D12_ROOT_PARAMETER rootparam = {};
    {
      // descriptor range
      D3D12_DESCRIPTOR_RANGE descriptor_range[2] = {};  // テクスチャと定数の２つ

      descriptor_range[0].NumDescriptors = 1;                           // テクスチャひとつ
      descriptor_range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;  // 種別はテクスチャ
      descriptor_range[0].BaseShaderRegister = 0;                       // 0番スロットから
      descriptor_range[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

      descriptor_range[1].NumDescriptors = 1;                           // 定数ひとつ
      descriptor_range[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;  // 種別は定数
      descriptor_range[1].BaseShaderRegister = 0;                       // 0番スロットから
      descriptor_range[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

      // root parameter
      rootparam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      rootparam.DescriptorTable.pDescriptorRanges = descriptor_range;
      rootparam.DescriptorTable.NumDescriptorRanges = 2;         // 2つ分を一回で指定
      rootparam.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;  // pixel shader, vertex shader can be used.
    }

    rootSignatureDesc.pParameters = &rootparam;  // ルートパラメータの先頭アドレス
    rootSignatureDesc.NumParameters = 1;         // ルートパラメータ数

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

    ID3D12Resource* uploadbuff = nullptr;  // 中間バッファ作成
    ID3D12Resource* texbuff = nullptr;     // テクスチャバッファ作成
    {
      // まずは中間バッファとしてのUploadヒープ設定
      D3D12_HEAP_PROPERTIES upload_heap_property = {};
      upload_heap_property.Type = D3D12_HEAP_TYPE_UPLOAD;  // Upload用
      upload_heap_property.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
      upload_heap_property.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
      upload_heap_property.CreationNodeMask = 0;  //単一アダプタのため0
      upload_heap_property.VisibleNodeMask = 0;   //単一アダプタのため0

      D3D12_RESOURCE_DESC resource_description = {};
      resource_description.Format = DXGI_FORMAT_UNKNOWN;
      resource_description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;  //単なるバッファとして
      auto pixelsize = scratchImg.GetPixelsSize();
      resource_description.Width =
          AlignmentedSize(img->rowPitch, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT) * img->height;  //データサイズ

      resource_description.Height = 1;            //
      resource_description.DepthOrArraySize = 1;  //
      resource_description.MipLevels = 1;
      resource_description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;  //連続したデータですよ
      resource_description.Flags = D3D12_RESOURCE_FLAG_NONE;         //とくにフラグなし
      resource_description.SampleDesc.Count = 1;    //通常テクスチャなのでアンチェリしない
      resource_description.SampleDesc.Quality = 0;  //

      result = _dev->CreateCommittedResource(&upload_heap_property,
                                             D3D12_HEAP_FLAG_NONE,  //特に指定なし
                                             &resource_description,
                                             D3D12_RESOURCE_STATE_GENERIC_READ,  // CPUから書き込み可能
                                             nullptr, IID_PPV_ARGS(&uploadbuff));

      //次にテクスチャのためのヒープ設定
      D3D12_HEAP_PROPERTIES texHeapProp = {};
      texHeapProp.Type = D3D12_HEAP_TYPE_DEFAULT;  //テクスチャ用
      texHeapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
      texHeapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
      texHeapProp.CreationNodeMask = 0;  // 単一アダプタのため0
      texHeapProp.VisibleNodeMask = 0;   // 単一アダプタのため0

      //リソース設定(変数は使いまわし)
      resource_description.Format = metadata.format;
      resource_description.Width = static_cast<UINT>(metadata.width);                   // 幅
      resource_description.Height = static_cast<UINT>(metadata.height);                 // 高さ
      resource_description.DepthOrArraySize = static_cast<UINT16>(metadata.arraySize);  // 2Dで配列でもないので１
      resource_description.MipLevels = static_cast<UINT16>(metadata.mipLevels);  // ミップマップしないのでミップ数は１つ
      resource_description.Dimension = static_cast<D3D12_RESOURCE_DIMENSION>(metadata.dimension);  // 2Dテクスチャ用
      resource_description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

      result = _dev->CreateCommittedResource(&texHeapProp,
                                             D3D12_HEAP_FLAG_NONE,  // 特に指定なし
                                             &resource_description,
                                             D3D12_RESOURCE_STATE_COPY_DEST,  // コピー先
                                             nullptr, IID_PPV_ARGS(&texbuff));
    }

    {
      uint8_t* mapforImg = nullptr;                              // image->pixelsと同じ型にする
      result = uploadbuff->Map(0, nullptr, (void**)&mapforImg);  // マップ
      auto srcAddress = img->pixels;
      auto rowPitch = AlignmentedSize(img->rowPitch, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
      for (int y = 0; y < img->height; ++y) {
        std::copy_n(srcAddress, rowPitch, mapforImg);  // コピー
        // 1行ごとの辻褄を合わせてやる
        srcAddress += img->rowPitch;
        mapforImg += rowPitch;
      }
      uploadbuff->Unmap(0, nullptr);  // アンマップ
    }

    D3D12_TEXTURE_COPY_LOCATION src = {}, dst = {};
    dst.pResource = texbuff;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    src.pResource = uploadbuff;                           // 中間バッファ
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;  // フットプリント指定
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT nrow;
    UINT64 rowsize, size;
    auto desc = texbuff->GetDesc();
    _dev->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &nrow, &rowsize, &size);
    src.PlacedFootprint = footprint;
    src.PlacedFootprint.Offset = 0;
    src.PlacedFootprint.Footprint.Width = static_cast<UINT>(metadata.width);
    src.PlacedFootprint.Footprint.Height = static_cast<UINT>(metadata.height);
    src.PlacedFootprint.Footprint.Depth = static_cast<UINT>(metadata.depth);
    src.PlacedFootprint.Footprint.RowPitch =
        static_cast<UINT>(AlignmentedSize(img->rowPitch, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT));
    src.PlacedFootprint.Footprint.Format = img->format;

    {
      _cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

      D3D12_RESOURCE_BARRIER BarrierDesc = {};
      BarrierDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      BarrierDesc.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
      BarrierDesc.Transition.pResource = texbuff;
      BarrierDesc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      BarrierDesc.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
      BarrierDesc.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

      _cmdList->ResourceBarrier(1, &BarrierDesc);
      _cmdList->Close();
      // コマンドリストの実行
      ID3D12CommandList* cmdlists[] = {_cmdList};
      _cmdQueue->ExecuteCommandLists(1, cmdlists);
      // 待ち
      _cmdQueue->Signal(_fence, ++_fenceVal);

      if (_fence->GetCompletedValue() != _fenceVal) {
        auto event = CreateEvent(nullptr, false, false, nullptr);
        _fence->SetEventOnCompletion(_fenceVal, event);
        WaitForSingleObject(event, INFINITE);
        CloseHandle(event);
      }
      _cmdAllocator->Reset();  // キューをクリア
      _cmdList->Reset(_cmdAllocator, nullptr);
    }

    DirectX::XMMATRIX* mapMatrix;  // マップ先を示すポインター
    ID3D12Resource* constBuff = nullptr;
    DirectX::XMMATRIX worldMat;  // 4x4
    DirectX::XMMATRIX viewMat;   // 4x4
    DirectX::XMMATRIX projMat;   // 4x4
    {
      // Homography

      // worldMat = DirectX::XMMatrixRotationY(DirectX::XM_PIDIV4);
      worldMat = DirectX::XMMatrixIdentity();
      DirectX::XMFLOAT3 eye(0, 10, -15);
      DirectX::XMFLOAT3 target(0, 10, 0);
      DirectX::XMFLOAT3 up(0, 1, 0);
      viewMat = DirectX::XMMatrixLookAtLH(DirectX::XMLoadFloat3(&eye), DirectX::XMLoadFloat3(&target),
                                          DirectX::XMLoadFloat3(&up));
      projMat = DirectX::XMMatrixPerspectiveFovLH(
          DirectX::XM_PIDIV2,                                                    // 画角は90°
          static_cast<float>(window_width) / static_cast<float>(window_height),  // アスペクト比
          1.0f,                                                                  // 近いほう
          100.0f                                                                 // 遠いほう
      );

      auto heap_propertiy = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
      auto resource_description = CD3DX12_RESOURCE_DESC::Buffer((sizeof(worldMat) + 0xff) & ~0xff);
      result = _dev->CreateCommittedResource(&heap_propertiy, D3D12_HEAP_FLAG_NONE, &resource_description,
                                             D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&constBuff));
      result = constBuff->Map(0, nullptr, (void**)&mapMatrix);  // constant buffer に ``mapMatrix`` の内容をコピー
    }

    // texture's descriptor heap
    ID3D12DescriptorHeap* basic_descriptor_heap = nullptr;
    D3D12_DESCRIPTOR_HEAP_DESC descHeapDesc = {};
    descHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;  // シェーダーから見えるように
    descHeapDesc.NodeMask = 0;                                       // マスクは 0

    // SRV (Shader Resouce View), CBV (Constant Buffer View)
    descHeapDesc.NumDescriptors = 2;
    // SRV (Shader Resouce View), CBV (Constant Buffer View), UAV (Unordered Access View)
    descHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;

    result = _dev->CreateDescriptorHeap(&descHeapDesc, IID_PPV_ARGS(&basic_descriptor_heap));

    // デスクリプタの先頭ハンドルを取得しておく
    auto basicHeapHandle = basic_descriptor_heap->GetCPUDescriptorHandleForHeapStart();

    {
      D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
      srvDesc.Format = metadata.format;  // DXGI_FORMAT_R8G8B8A8_UNORM;//RGBA(0.0f～1.0fに正規化)
      srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;  // 2D テクスチャ
      srvDesc.Texture2D.MipLevels = 1;                        // ミップマップは使用しないので1

      // シェーダリソースビューの作成
      _dev->CreateShaderResourceView(
          texbuff,   // ビューと関連付けるバッファー
          &srvDesc,  // 先ほど設定したテクスチャ設定情報
          basic_descriptor_heap->GetCPUDescriptorHandleForHeapStart()  // ヒープのどこに割り当てるか
      );
    }

    basicHeapHandle.ptr += _dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    {
      D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
      cbvDesc.BufferLocation = constBuff->GetGPUVirtualAddress();
      cbvDesc.SizeInBytes = static_cast<UINT>(constBuff->GetDesc().Width);
      // 定数バッファビューの作成
      _dev->CreateConstantBufferView(&cbvDesc, basicHeapHandle);
    }

    //////////////////
    // message loop //
    //////////////////

    MSG msg = {};
    unsigned int frame = 0;
    float angle(0.0f);
    while (true) {
      if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
      }
      //もうアプリケーションが終わるって時にmessageがWM_QUITになる
      if (msg.message == WM_QUIT) {
        break;
      }

      // angle += 0.1f;
      // worldMat = DirectX::XMMatrixRotationY(angle);
      *mapMatrix = worldMat * viewMat * projMat;  // row-major

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

      // 画面クリア
      constexpr float clearColor[] = {1.0f, 1.0f, 1.0f, 1.0f};  // while
      _cmdList->ClearRenderTargetView(rtvH, clearColor, 0, nullptr);

      _cmdList->RSSetViewports(1, &viewport);
      _cmdList->RSSetScissorRects(1, &scissorrect);
      _cmdList->SetGraphicsRootSignature(rootsignature);

      _cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      _cmdList->IASetVertexBuffers(0, 1, &vbView);
      _cmdList->IASetIndexBuffer(&ibView);

      _cmdList->SetGraphicsRootSignature(rootsignature);
      _cmdList->SetDescriptorHeaps(1, &basic_descriptor_heap);

      _cmdList->SetGraphicsRootDescriptorTable(
          0,                                                             // root parameter index for SRV
          basic_descriptor_heap->GetGPUDescriptorHandleForHeapStart());  // ヒープアドレス

      // _cmdList->DrawIndexedInstanced(6, 1, 0, 0, 0);
      _cmdList->DrawInstanced(vertex_num, 1, 0, 0);

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
