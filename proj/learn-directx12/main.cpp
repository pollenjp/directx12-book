#include <DirectXMath.h>
#include <DirectXTex.h>
#include <Windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <d3dx12.h>
#include <dxgi1_6.h>
#include <tchar.h>

#include <cmath>
#include <filesystem>
#include <functional>
#include <istream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#ifdef _DEBUG
#include <iostream>
#endif

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "DirectXTex.lib")
namespace fs = std::filesystem;

const unsigned int window_width = 1280;
const unsigned int window_height = 720;

std::map<std::string, std::function<HRESULT(const std::wstring&, DirectX::TexMetadata*, DirectX::ScratchImage&)>>
    loadLambdaTable;

/**
 * @brief
 * @param str マルチバイト文字列
 * @return 変換されたワイド文字列
 */
std::wstring GetWideStringFromString(const std::string& str, UINT code_page = CP_ACP) {
  // 文字列数
  auto num1 = MultiByteToWideChar(code_page, MB_PRECOMPOSED | MB_ERR_INVALID_CHARS, str.c_str(), -1, nullptr, 0);
  wchar_t* buf = new wchar_t[static_cast<std::size_t>(num1) + 1];
  auto num2 = MultiByteToWideChar(code_page, MB_PRECOMPOSED | MB_ERR_INVALID_CHARS, str.c_str(), -1, buf, num1);
  assert(num1 == num2);
  std::wstring ret_wstr(buf);
  delete[] buf;
  return ret_wstr;
}

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

/**
 * @brief PMD マテリアル構造体 ( fread 用)
 *
 */
struct PMDMaterial {
  DirectX::XMFLOAT3 diffuse;   // 4 bytes * 3 ディフューズ色
  float alpha;                 // 4 bytes      ディフューズα
  float specularity;           // 4 bytes      スペキュラの強さ (乗算値)
  DirectX::XMFLOAT3 specular;  // 4 bytes * 3 スペキュラ色
  DirectX::XMFLOAT3 ambient;   // 4 bytes * 3 アンビエント色
  unsigned char toonIdx;       // 1 byte      トゥーン番号 (後述)
  unsigned char edgeFlg;       // 1 byte      マテリアルごとの輪郭線フラグ
  unsigned int indicesNum;     // 4 bytes     このマテリアルが割り当てられる
                               // インデックス数
  char texFilePath[20];        // 1 byte * 20 テクスチャファイルパス + alpha
};
#pragma pack(pop)

/**
 * @brief シェーダー側に投げられるマテリアルデータ
 *
 */
struct MaterialForHlsl {
  DirectX::XMFLOAT3 diffuse;   // 4 bytes * 3 ディフューズ色
  float alpha;                 // 4 bytes     ディフューズα
  DirectX::XMFLOAT3 specular;  // 4 bytes * 3 スペキュラ色
  float specularity;           // 4 bytes     スペキュラの強さ（乗算値）
  DirectX::XMFLOAT3 ambient;   // 4 bytes * 3 アンビエント色
};

/**
 * @brief それ以外のマテリアルデータ
 *
 */
struct AdditionalMaterial {
  std::string texPath;  // テクスチャファイルパス
  int toonIdx;          // トゥーン番号
  bool edgeFlg;         // マテリアルごとの輪郭線フラグ
};

/**
 * @brief 全体をまとめるデータ
 *
 */
struct Material {
  unsigned int indicesNum;  // インデックス数
  MaterialForHlsl material;
  AdditionalMaterial additional;
};

/**
 * @brief シェーダー側に渡すための基本的な行列データ
 *
 */
struct SceneMatrices {
  DirectX::XMMATRIX world;  // World Matrix
  DirectX::XMMATRIX view;   // View Transformation Matrix
  DirectX::XMMATRIX proj;   // Projection Matrix
  DirectX::XMFLOAT3 eye;    // Eye Position
};

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

// ファイル名パスとリソースのマップテーブル
std::map<std::wstring, ID3D12Resource*> resource_table;

/**
 * @brief
 * @details upload buffer (中間バッファ) を挟んで read only な texture buffer にしないのはなぜか？
 *
 * @param tex_path_wstr
 * @return ID3D12Resource*
 *         If tex_path_wstr is empty wstring (""), return nullptr.
 *         If failed to load, return nullptr.
 */
ID3D12Resource* LoadTextureFromFile(const std::wstring& tex_path_wstr) {
  auto it = resource_table.find(tex_path_wstr);
  if (it != resource_table.end()) {
    return it->second;
  }

  // WIC テクスチャのロード
  DirectX::TexMetadata metadata = {};
  DirectX::ScratchImage scratchImg = {};
  if (tex_path_wstr == L"") {
#ifdef _DEBUG
    {  // debug
      std::wstringstream ss;
      ss << L"LoadTextureFromFile's argument tex_path_wstr is invalid: \"" << tex_path_wstr.c_str() << L"\""
         << std::endl;
      OutputDebugStringW(ss.str().c_str());
    }
#endif
    return nullptr;
  }
  fs::path tex_path(tex_path_wstr);
  auto result = loadLambdaTable[tex_path.extension().string()](tex_path_wstr, &metadata, scratchImg);
  if (FAILED(result)) {
#ifdef _DEBUG
    {  // debug
      std::wstringstream ss;
      ss << L"Failed to load a file: \"" << tex_path_wstr.c_str() << L"\"" << std::endl;
      OutputDebugStringW(ss.str().c_str());
    }
#endif
    return nullptr;
  }
#ifdef _DEBUG
  {  // debug
    std::wstringstream ss;
    ss << L"Load a file: \"" << tex_path_wstr.c_str() << L"\"" << std::endl;
    OutputDebugStringW(ss.str().c_str());
  }
#endif

  auto img = scratchImg.GetImage(0, 0, 0);  // 生データ抽出

  // Create Texture Buffer
  ID3D12Resource* texbuff = nullptr;
  {
    // WriteToSubresource で転送する用のヒープ設定
    D3D12_HEAP_PROPERTIES texHeapProp = {};
    texHeapProp.Type = D3D12_HEAP_TYPE_CUSTOM;
    texHeapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
    texHeapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
    texHeapProp.CreationNodeMask = 0;  // 単一アダプタのため0
    texHeapProp.VisibleNodeMask = 0;   // 単一アダプタのため0

    D3D12_RESOURCE_DESC resDesc = {};
    resDesc.Format = metadata.format;
    resDesc.Width = metadata.width;    // 幅
    resDesc.Height = metadata.height;  // 高さ
    resDesc.DepthOrArraySize = metadata.arraySize;
    resDesc.SampleDesc.Count = 1;    // 通常テクスチャなのでアンチエイリアシングしない
    resDesc.SampleDesc.Quality = 0;  // クオリティは最低
    resDesc.MipLevels = metadata.mipLevels;
    resDesc.Dimension = static_cast<D3D12_RESOURCE_DIMENSION>(metadata.dimension);
    resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;  // レイアウトは決定しない
    resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;       // 特にフラグなし

    result = _dev->CreateCommittedResource(&texHeapProp,
                                           D3D12_HEAP_FLAG_NONE,  // 特に指定なし
                                           &resDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
                                           IID_PPV_ARGS(&texbuff));
    if (FAILED(result)) {
      return nullptr;
    }
    result = texbuff->WriteToSubresource(0,
                                         nullptr,         // 全領域へコピー
                                         img->pixels,     // 元データアドレス
                                         img->rowPitch,   // 1 ラインサイズ
                                         img->slicePitch  // 全サイズ
    );
    if (FAILED(result)) {
      return nullptr;
    }
  }
  resource_table[tex_path_wstr] = texbuff;
  return texbuff;
}

/**
 * @brief Create a texture object
 *
 * @return ID3D12Resource*
 */
ID3D12Resource* CreateOneValueTexture(unsigned int fill_value) {
  D3D12_HEAP_PROPERTIES texHeapProp = {};
  texHeapProp.Type = D3D12_HEAP_TYPE_CUSTOM;
  texHeapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
  texHeapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
  texHeapProp.CreationNodeMask = 0;
  texHeapProp.VisibleNodeMask = 0;

  // texture's minimum size is 4x4
  int width = 4;
  int height = 4;

  D3D12_RESOURCE_DESC resDesc = {};
  resDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  resDesc.Width = width;    // 幅
  resDesc.Height = height;  // 高さ
  resDesc.DepthOrArraySize = 1;
  resDesc.SampleDesc.Count = 1;
  resDesc.SampleDesc.Quality = 0;
  resDesc.MipLevels = 1;
  resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

  ID3D12Resource* whiteBuff = nullptr;
  auto result = _dev->CreateCommittedResource(&texHeapProp,
                                              D3D12_HEAP_FLAG_NONE,  // 特に指定なし
                                              &resDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
                                              IID_PPV_ARGS(&whiteBuff));
  if (FAILED(result)) {
    return nullptr;
  }
  std::vector<unsigned char> data(width * height * 4);  // 4: RGBA
  std::fill(data.begin(), data.end(), fill_value);      // 全部 255 で埋める
  // データ転送
  result = whiteBuff->WriteToSubresource(0, nullptr, data.data(), width * height, data.size());
  return whiteBuff;
}

// デフォルトグラデーションテクスチャ
ID3D12Resource* CreateGrayGradationTexture() {
  D3D12_HEAP_PROPERTIES texHeapProp = {};
  texHeapProp.Type = D3D12_HEAP_TYPE_CUSTOM;
  texHeapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
  texHeapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
  texHeapProp.CreationNodeMask = 0;
  texHeapProp.VisibleNodeMask = 0;

  int width = 4;  // 4: RGBA
  int height = 256;

  D3D12_RESOURCE_DESC resDesc = {};
  resDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  resDesc.Width = width;    // 幅
  resDesc.Height = height;  // 高さ
  resDesc.DepthOrArraySize = 1;
  resDesc.SampleDesc.Count = 1;
  resDesc.SampleDesc.Quality = 0;
  resDesc.MipLevels = 1;
  resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

  ID3D12Resource* gradation_buffer = nullptr;
  auto result = _dev->CreateCommittedResource(&texHeapProp,
                                              D3D12_HEAP_FLAG_NONE,  // 特に指定なし
                                              &resDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
                                              IID_PPV_ARGS(&gradation_buffer));

  // 上が白くて下が黒いテクスチャデータを作成
  std::vector<unsigned int> data(width * height);
  auto it = data.begin();
  unsigned int c = 0xff;  // 4 bytes (8*4=32 bits) : 0x000000ff
  for (; it != data.end(); it += width) {
    // RGBAが逆並びしているためRGBマクロと0xff<<24を用いて表す
    // A : 0xff
    // B : c
    // G : c
    // R : c
    auto col = (0xff << 24) | RGB(c, c, c);
    std::fill(it, it + width, col);
    --c;
  }
  result = gradation_buffer->WriteToSubresource(0, nullptr, data.data(), width * sizeof(unsigned int),
                                                sizeof(unsigned int) * data.size());
  return gradation_buffer;
}

/**
 * @brief split string by delimiter
 *
 * @param text_str
 * @param splitter
 * @return std::vector<std::string>
 */
std::vector<std::string> SplitString(const std::string& text_str, const char splitter = '*') {
  std::vector<std::string> v;
  std::stringstream ss{text_str};
  std::string buf;
  while (std::getline(ss, buf, splitter)) {
    v.push_back(buf);
  }
  return v;
}

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

    // Init Utils

    loadLambdaTable[".sph"] = loadLambdaTable[".spa"] = loadLambdaTable[".bmp"] = loadLambdaTable[".png"] =
        loadLambdaTable[".jpg"] =
            [](const std::wstring& path, DirectX::TexMetadata* meta, DirectX::ScratchImage& img) -> HRESULT {
      return DirectX::LoadFromWICFile(path.c_str(), DirectX::WIC_FLAGS_NONE, meta, img);
    };
    loadLambdaTable[".tga"] = [](const std::wstring& path, DirectX::TexMetadata* meta,
                                 DirectX::ScratchImage& img) -> HRESULT {
      return DirectX::LoadFromTGAFile(path.c_str(), meta, img);
    };
    loadLambdaTable[".dds"] = [](const std::wstring& path, DirectX::TexMetadata* meta,
                                 DirectX::ScratchImage& img) -> HRESULT {
      return DirectX::LoadFromDDSFile(path.c_str(), DirectX::DDS_FLAGS_NONE, meta, img);
    };

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

    FILE* fp;
    // const fs::path model_filepath = fs::absolute(L"Model/初音ミク.pmd");
    // const fs::path model_filepath = fs::absolute(L"Model/巡音ルカ.pmd");
    const fs::path model_filepath = fs::absolute(L"Model/初音ミクmetal.pmd");
    {  // debug
      std::wstringstream ss;
      ss << L"Model filepath is \"" << model_filepath.wstring() << L"\"" << std::endl;
      OutputDebugStringW(ss.str().c_str());
    }
    {
      auto err = _wfopen_s(&fp, model_filepath.wstring().c_str(), L"rb");
      if (fp == nullptr) {
        wchar_t strerr[256];
        _wcserror_s(strerr, 256, err);
        LPCWSTR ptr = strerr;
        MessageBox(hwnd, ptr, L"Open Error", MB_ICONERROR);
        return -1;
      }
    }

    char signature[3];  // 先頭 Pmd (3 bytes)
    PMDHeader pmdheader = {};
    fread(signature, sizeof(signature), 1, fp);
    fread(&pmdheader, sizeof(pmdheader), 1, fp);

    // header の直後に頂点数
    unsigned int vertex_num(0);  // 頂点数
    fread(&vertex_num, sizeof(vertex_num), 1, fp);
    {  // debug
      std::wstringstream ss;
      ss << L"vertex num is " << vertex_num << std::endl;
      OutputDebugStringW(ss.str().c_str());
    }

    constexpr size_t pmdvertex_size(38);           // 頂点 1 つあたりのサイズ
    std::vector<PMD_VERTEX> vertices(vertex_num);  // バッファーの確保
    for (auto i = 0; i < vertex_num; i++) {
      fread(&vertices[i], pmdvertex_size, 1, fp);
    }

    // indices
    unsigned int indices_num;  // インデックス数
    fread(&indices_num, sizeof(indices_num), 1, fp);

    std::vector<unsigned short> indices(indices_num);
    fread(indices.data(), indices_num * sizeof(unsigned short), 1, fp);

    // texture
    unsigned int num_material;  // マテリアル数
    fread(&num_material, sizeof(num_material), 1, fp);
    {  // debug
      std::wstringstream ss;
      ss << L"material num is " << num_material << std::endl;
      OutputDebugStringW(ss.str().c_str());
    }
    std::vector<PMDMaterial> pmd_materials(num_material);
    std::vector<ID3D12Resource*> texture_resources(num_material);
    std::vector<ID3D12Resource*> sph_resources(num_material);
    std::vector<ID3D12Resource*> spa_resources(num_material);
    std::vector<ID3D12Resource*> toon_resources(num_material);
    fread(pmd_materials.data(), pmd_materials.size() * sizeof(PMDMaterial), 1, fp);
#ifdef _DEBUG
    {  // debug
      for (unsigned int i = 0; i < pmd_materials.size(); i++) {
        std::string tmp_str(pmd_materials[i].texFilePath);
        auto tmp_tex_fpath_relative = fs::path(GetWideStringFromString(tmp_str, CP_ACP));
        auto tmp_filepath = model_filepath.parent_path() / tmp_tex_fpath_relative;
        std::wstringstream ss;
        ss << L"idx " << i << L" : texture file path: \"" << tmp_filepath.wstring().c_str() << L"\"" << std::endl;
        OutputDebugStringW(ss.str().c_str());
      }
#endif
    }

    std::vector<Material> materials(pmd_materials.size());
    for (int i = 0; i < pmd_materials.size(); ++i) {
      materials[i].indicesNum = pmd_materials[i].indicesNum;
      materials[i].material.diffuse = pmd_materials[i].diffuse;
      materials[i].material.alpha = pmd_materials[i].alpha;
      materials[i].material.specular = pmd_materials[i].specular;
      materials[i].material.specularity = pmd_materials[i].specularity;
      materials[i].material.ambient = pmd_materials[i].ambient;
#ifdef _DEBUG
      {  // debug
        std::wstringstream ss;
        ss << L"idx " << i << L" : ambient: (" << pmd_materials[i].ambient.x << ", " << pmd_materials[i].ambient.y
           << ", " << pmd_materials[i].ambient.z << L")" << std::endl;
        OutputDebugStringW(ss.str().c_str());
      }
#endif
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

    {
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
    }

    ////////////////////////
    // Render Target View //
    ////////////////////////

    DXGI_SWAP_CHAIN_DESC swcDesc = {};
    result = _swapchain->GetDesc(&swcDesc);
    std::vector<ID3D12Resource*> _backBuffers(swcDesc.BufferCount);

    ID3D12DescriptorHeap* rtvHeaps = nullptr;  // out parameter 用

    {
      // discriptor heap
      D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
      heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;    //レンダーターゲットビューなので当然RTV
      heapDesc.NodeMask = 0;                             // only use single GPU
      heapDesc.NumDescriptors = 2;                       // double buffering (front, back buffer)
      heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;  // 特に指定なし
      result = _dev->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&rtvHeaps));

      // Discriptor と スワップチェーン上のバックバッファと関連付け

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

    ////////////////////////////////////////
    // vertex buffer / vertex buffer view //
    ////////////////////////////////////////

    D3D12_VERTEX_BUFFER_VIEW vbView = {};
    {
      ID3D12Resource* vertBuff = nullptr;
      auto heapprop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
      auto resdesc = CD3DX12_RESOURCE_DESC::Buffer(vertices.size() * sizeof(vertices[0]));
      result = _dev->CreateCommittedResource(&heapprop, D3D12_HEAP_FLAG_NONE, &resdesc,
                                             D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertBuff));

      // copy vertices data to vertex buffer
      PMD_VERTEX* vertMap = nullptr;
      result = vertBuff->Map(0, nullptr, (void**)&vertMap);
      if (FAILED(result)) {
        throw std::runtime_error("Failed to map vertex buffer");
      }
      std::copy(std::begin(vertices), std::end(vertices), vertMap);
      vertBuff->Unmap(0, nullptr);

      // create vertex buffer view
      vbView.BufferLocation = vertBuff->GetGPUVirtualAddress();
      vbView.SizeInBytes = static_cast<UINT>(vertices.size() * sizeof(vertices[0]));  // 全バイト数
      vbView.StrideInBytes = sizeof(PMD_VERTEX);                                      // 1頂点あたりのバイト数
    }

    //////////////////////////////////////
    // index buffer / index buffer view //
    //////////////////////////////////////

    D3D12_INDEX_BUFFER_VIEW ibView = {};
    {
      ID3D12Resource* idxBuff = nullptr;
      auto heapprop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
      auto resdesc = CD3DX12_RESOURCE_DESC::Buffer(indices.size() * sizeof(indices[0]));
      result = _dev->CreateCommittedResource(&heapprop, D3D12_HEAP_FLAG_NONE, &resdesc,
                                             D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&idxBuff));

      // 作ったバッファにインデックスデータをコピー
      unsigned short* mappedIdx = nullptr;
      idxBuff->Map(0, nullptr, (void**)&mappedIdx);
      std::copy(indices.begin(), indices.end(), mappedIdx);
      idxBuff->Unmap(0, nullptr);

      // インデックスバッファビューを作成
      ibView.BufferLocation = idxBuff->GetGPUVirtualAddress();
      ibView.Format = DXGI_FORMAT_R16_UINT;
      ibView.SizeInBytes = indices.size() * sizeof(indices[0]);
    }

    // Depth buffer / Depth buffer view //
    ID3D12DescriptorHeap* dsvHeap = nullptr;
    ID3D12Resource* depthBuffer = nullptr;
    {
      // create depth buffer

      D3D12_RESOURCE_DESC depthResDesc = {};
      depthResDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;  // 2 次元のテクスチャデータ
      depthResDesc.Width = window_width;            // 幅と高さはレンダーターゲットと同じ
      depthResDesc.Height = window_height;          // 同上
      depthResDesc.DepthOrArraySize = 1;            // テクスチャ配列でも、3D テクスチャでもない
      depthResDesc.Format = DXGI_FORMAT_D32_FLOAT;  // 深度値書き込み用フォーマット
      depthResDesc.SampleDesc.Count = 1;            // サンプルは1 ピクセルあたり1 つ
      depthResDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;  // デプスステンシルとして使用

      D3D12_HEAP_PROPERTIES depthHeapProp = {};
      depthHeapProp.Type = D3D12_HEAP_TYPE_DEFAULT;  // DEFAULT なのであとはUNKNOWN でよい
      depthHeapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
      depthHeapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

      D3D12_CLEAR_VALUE depthClearValue = {};
      depthClearValue.DepthStencil.Depth = 1.0f;       // 深さ1.0f (最大値) でクリア
      depthClearValue.Format = DXGI_FORMAT_D32_FLOAT;  // 32 ビットfloat 値としてクリア

      result = _dev->CreateCommittedResource(&depthHeapProp, D3D12_HEAP_FLAG_NONE, &depthResDesc,
                                             D3D12_RESOURCE_STATE_DEPTH_WRITE,  // 深度値書き込みに使用
                                             &depthClearValue, IID_PPV_ARGS(&depthBuffer));

      // depth buffer view

      // 深度のためのディスクリプタヒープ作成
      D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};        // 深度に使うことがわかればよい
      dsvHeapDesc.NumDescriptors = 1;                     // 深度ビューは1 つ
      dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;  // デプスステンシルビューとして使う

      result = _dev->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsvHeap));

      // 深度ビュー作成
      D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
      dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;                 // 深度値に32 ビット使用
      dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;  // 2D テクスチャ
      dsvDesc.Flags = D3D12_DSV_FLAG_NONE;                    // フラグは特になし

      _dev->CreateDepthStencilView(depthBuffer, &dsvDesc, dsvHeap->GetCPUDescriptorHandleForHeapStart());
    }

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
            DXGI_FORMAT_R32G32_FLOAT,  // 4 (FLOAT) * 2 (uv) bytes
            0,
            D3D12_APPEND_ALIGNED_ELEMENT,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            0,
        },
        {
            "BONE_NO",
            0,
            DXGI_FORMAT_R16G16_UINT,  // 4 (UINT) * 2 bytes
            0,
            D3D12_APPEND_ALIGNED_ELEMENT,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            0,
        },
        {
            "WEIGHT",
            0,
            DXGI_FORMAT_R8_UINT,  // 4 (UINT) * 1 bytes
            0,
            D3D12_APPEND_ALIGNED_ELEMENT,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            0,
        },
        {
            "EDGE_FLG",
            0,
            DXGI_FORMAT_R8_UINT,  // 4 (UINT) * 1 bytes
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

    gpipeline.DepthStencilState.DepthEnable = true;                           // 深度バッファーを使う
    gpipeline.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;  // 書き込む
    gpipeline.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;       // 小さいほうを採用

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
    gpipeline.DSVFormat = DXGI_FORMAT_D32_FLOAT;

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

    D3D12_ROOT_PARAMETER rootparam[2] = {};
    {
      // descriptor range
      D3D12_DESCRIPTOR_RANGE descriptor_range[4] = {};  // テクスチャと定数の２つ

      // CBV 1st (transform matrix) : register(b0)
      descriptor_range[0].NumDescriptors = 1;                           // 定数ひとつ
      descriptor_range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;  // 種別は定数
      descriptor_range[0].BaseShaderRegister = 0;                       // 0番スロットから
      descriptor_range[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

      // CBV 2nd (material) : register(b1)
      descriptor_range[1].NumDescriptors = 1;  // ディスクリプタヒープは複数だが一度に使うのは1 つ
      descriptor_range[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;  // 種別は定数
      descriptor_range[1].BaseShaderRegister = 1;                       // 1番スロットから
      descriptor_range[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

      // textures
      // register(t0) : texture
      // register(t1) : sph texture
      // register(t2) : spa texture
      // register(t3) : toon texture
      descriptor_range[2].NumDescriptors = 4;                           // テクスチャ2つ
      descriptor_range[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;  // 種別はテクスチャ
      descriptor_range[2].BaseShaderRegister = 0;                       // 0 番スロットから
      descriptor_range[2].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

      ////////////////////
      // root parameter //
      ////////////////////

      // CBV (transform matrix)
      rootparam[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      rootparam[0].DescriptorTable.pDescriptorRanges = &descriptor_range[0];
      rootparam[0].DescriptorTable.NumDescriptorRanges = 1;
      rootparam[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

      // CBV (material) + textures
      rootparam[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      rootparam[1].DescriptorTable.pDescriptorRanges = &descriptor_range[1];  // レンジの先頭アドレス
      rootparam[1].DescriptorTable.NumDescriptorRanges = 2;
      rootparam[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }

    rootSignatureDesc.pParameters = rootparam;  // ルートパラメータの先頭アドレス
    rootSignatureDesc.NumParameters = 2;        // ルートパラメータ数

    // sampler
    D3D12_STATIC_SAMPLER_DESC samplerDesc[2] = {};

    samplerDesc[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;                 // 横方向の繰り返し
    samplerDesc[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;                 // 縦方向の繰り返し
    samplerDesc[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;                 // 奥行きの繰り返し
    samplerDesc[0].BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;  // ボーダーは黒
    samplerDesc[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;                   // 線形補間
    samplerDesc[0].MaxLOD = D3D12_FLOAT32_MAX;                                 // ミップマップ最大値
    samplerDesc[0].MinLOD = 0.0f;                                              // ミップマップ最小値
    samplerDesc[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;  // ピクセルシェーダーから見える
    samplerDesc[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;      // リサンプリングしない
    samplerDesc[0].ShaderRegister = 0;                                // register(s0)

    samplerDesc[1] = samplerDesc[0];                             // 変更点以外をコピー
    samplerDesc[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;  // 繰り返さない
    samplerDesc[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;  // 繰り返さない
    samplerDesc[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;  // 繰り返さない
    samplerDesc[1].ShaderRegister = 1;                           // register(s1)

    rootSignatureDesc.pStaticSamplers = samplerDesc;
    rootSignatureDesc.NumStaticSamplers = 2;

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

    ////////////////////////////////////////////
    // Material buffer / Material Buffer View //
    ////////////////////////////////////////////

    // transform matrix
    // material
    // sph texture
    // spa texture
    // toon texture
    auto cbv_rsv_count_per_material = 5;

    ID3D12DescriptorHeap* materialDescHeap = nullptr;
    D3D12_CONSTANT_BUFFER_VIEW_DESC matCBVDesc = {};
    std::size_t material_buff_size;
    {
      ID3D12Resource* material_buffer = nullptr;
      material_buff_size = sizeof(MaterialForHlsl);
      material_buff_size = (material_buff_size + 0xff) & ~0xff;
      auto heapProp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
      // TODO: 勿体ないけど仕方ないですね
      auto resDesc = CD3DX12_RESOURCE_DESC::Buffer(material_buff_size * num_material);
      result =
          _dev->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                        nullptr, IID_PPV_ARGS(&material_buffer));

      // マップマテリアルにコピー
      char* map_material = nullptr;
      result = material_buffer->Map(0, nullptr, (void**)&map_material);
      for (auto& m : materials) {
        *((MaterialForHlsl*)map_material) = m.material;  // データコピー
        map_material += material_buff_size;  // 次のアライメント位置まで進める (256 の倍数)
      }
      material_buffer->Unmap(0, nullptr);

      //////////////////////////
      // material buffer view //
      //////////////////////////

      D3D12_DESCRIPTOR_HEAP_DESC matDescHeapDesc = {};
      matDescHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
      matDescHeapDesc.NodeMask = 0;
      matDescHeapDesc.NumDescriptors = num_material * cbv_rsv_count_per_material;  // マテリアル数を指定
      matDescHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
      result = _dev->CreateDescriptorHeap(&matDescHeapDesc, IID_PPV_ARGS(&materialDescHeap));

      matCBVDesc.BufferLocation = material_buffer->GetGPUVirtualAddress();  // バッファーアドレス
      matCBVDesc.SizeInBytes = material_buff_size;  // マテリアルの256 アライメントサイズ

      ////////////////////
      // Texture Buffer //
      ////////////////////

      for (int i = 0; i < pmd_materials.size(); ++i) {
        // トゥーン番号とトゥーンテクスチャのファイル名との関係
        //  _________________________
        // | toon index | filename   |
        // |        255 | toon00.bmp |
        // |          0 | toon01.bmp |
        // |          1 | toon02.bmp |
        // |          2 | toon03.bmp |
        //  _________________________

        // トゥーンリソースの読み込み
        std::wstringstream ss;
        ss << L"toon" << std::setw(2) << std::setfill(L'0') << ((pmd_materials[i].toonIdx + 1) & 0xff) << L".bmp";
        fs::path toon_filepath = model_filepath.parent_path() / fs::path("toon") / fs::path(ss.str());
#ifdef _DEBUG
        {
          std::wstringstream ss;
          ss << L"Toon file : \"" << toon_filepath.wstring() << L"\" (" << (pmd_materials[i].toonIdx + 1) << L")"
             << std::endl;
          OutputDebugStringW(ss.str().c_str());
        }
#endif
        toon_resources[i] = LoadTextureFromFile(toon_filepath.wstring());

        if (strlen(pmd_materials[i].texFilePath) == 0) {
          texture_resources[i] = nullptr;
        }
        // モデルとテクスチャパスからアプリケーションからのテクスチャパスを得る
        fs::path tex_filepath;
        fs::path sph_filepath;
        fs::path spa_filepath;
        char splitter('*');
        // split して std::vector に格納
        auto file_list = SplitString(pmd_materials[i].texFilePath, splitter);
        for (auto& filename_str : file_list) {
          fs::path filepath = model_filepath.parent_path() / fs::path(GetWideStringFromString(filename_str, CP_ACP));
#ifdef _DEBUG
          {  // debug
            std::wstringstream ss;
            ss << L"Try to load a file: \"" << filepath.wstring().c_str() << L"\"" << std::endl;
            OutputDebugStringW(ss.str().c_str());
          }
          {
            std::wstringstream ss;
            for (std::size_t i = 0; i < filepath.extension().wstring().size(); i++) {
              ss << std::hex << (unsigned short)filepath.extension().wstring()[i] << " : " << std::endl;
            }
            std::wstring tmp_wstr(L".sph");
            for (std::size_t i = 0; i < tmp_wstr.size(); i++) {
              ss << std::hex << (unsigned short)tmp_wstr[i] << " : " << tmp_wstr[i] << std::endl;
            }
            OutputDebugStringW(ss.str().c_str());
          }
#endif

          if (filepath.extension().wstring() == L".sph") {
            if (sph_filepath.wstring() != L"") {
              std::wcerr << "Error: multiple sph filepath : " << filepath.wstring() << std::endl;
            }
            sph_filepath = filepath;
          } else if (filepath.extension().wstring() == L".spa") {
            if (spa_filepath.wstring() != L"") {
              std::wcerr << "Error: multiple spa filepath : " << filepath.wstring() << std::endl;
            }
            spa_filepath = filepath;
          } else {
            if (tex_filepath.wstring() != L"") {
              std::wcerr << L"Error: multiple tex filepath : " << filepath.wstring() << std::endl;
            }
            tex_filepath = filepath;
          }
        }

#ifdef _DEBUG
        {  // debug
          std::wstringstream ss;
          ss << L"tex_filepath : \"" << tex_filepath.wstring().c_str() << L"\"" << std::endl
             << L"sph_filepath : \"" << sph_filepath.wstring().c_str() << L"\"" << std::endl
             << L"spa_filepath : \"" << spa_filepath.wstring().c_str() << L"\"" << std::endl;
          OutputDebugStringW(ss.str().c_str());
        }
#endif

        texture_resources[i] = LoadTextureFromFile(tex_filepath.wstring());
        sph_resources[i] = LoadTextureFromFile(sph_filepath.wstring());
        spa_resources[i] = LoadTextureFromFile(spa_filepath.wstring());
      }

      ///////////////////////////////////////
      // Create SRV (shader resource view) //
      ///////////////////////////////////////

      // 通常テクスチャビュー作成
      {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;  // デフォルト
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;  // 2D テクスチャ
        srvDesc.Texture2D.MipLevels = 1;                        // ミップマップは使用しないので1

        auto white_tex = CreateOneValueTexture(0xff);
        auto black_tex = CreateOneValueTexture(0x00);
        auto gradation_tex = CreateGrayGradationTexture();
        auto matDescHeapH = materialDescHeap->GetCPUDescriptorHandleForHeapStart();
        auto inc_size = _dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        for (int i = 0; i < num_material; ++i) {
          // マテリアル固定バッファービュー
          // register(b1)
          _dev->CreateConstantBufferView(&matCBVDesc, matDescHeapH);
          matDescHeapH.ptr += inc_size;
          matCBVDesc.BufferLocation += material_buff_size;

          // register(t0)
          if (texture_resources[i] == nullptr) {
            srvDesc.Format = white_tex->GetDesc().Format;
            _dev->CreateShaderResourceView(white_tex, &srvDesc, matDescHeapH);
          } else {
            srvDesc.Format = texture_resources[i]->GetDesc().Format;
            _dev->CreateShaderResourceView(texture_resources[i], &srvDesc, matDescHeapH);
          }
          matDescHeapH.ptr += inc_size;

          // register(t1)
          if (sph_resources[i] == nullptr) {
            srvDesc.Format = white_tex->GetDesc().Format;
            _dev->CreateShaderResourceView(white_tex, &srvDesc, matDescHeapH);
          } else {
            srvDesc.Format = sph_resources[i]->GetDesc().Format;
            _dev->CreateShaderResourceView(sph_resources[i], &srvDesc, matDescHeapH);
          }
          matDescHeapH.ptr += inc_size;

          // register(t2)
          if (spa_resources[i] == nullptr) {
            srvDesc.Format = black_tex->GetDesc().Format;
            _dev->CreateShaderResourceView(black_tex, &srvDesc, matDescHeapH);
          } else {
            srvDesc.Format = spa_resources[i]->GetDesc().Format;
            _dev->CreateShaderResourceView(spa_resources[i], &srvDesc, matDescHeapH);
          }
          matDescHeapH.ptr += inc_size;

          if (toon_resources[i] == nullptr) {
            srvDesc.Format = gradation_tex->GetDesc().Format;
            _dev->CreateShaderResourceView(gradation_tex, &srvDesc, matDescHeapH);
          } else {
            srvDesc.Format = toon_resources[i]->GetDesc().Format;
            _dev->CreateShaderResourceView(toon_resources[i], &srvDesc, matDescHeapH);
          }
          matDescHeapH.ptr += inc_size;
        }
      }
    }

    //////////////////////
    // Transform Matrix //
    //////////////////////

    SceneMatrices* mapMatrix;  // マップ先を示すポインター
    ID3D12Resource* constBuff = nullptr;
    DirectX::XMFLOAT3 eye(0, 17, -5);
    DirectX::XMMATRIX worldMat;  // 4x4
    DirectX::XMMATRIX viewMat;   // 4x4
    DirectX::XMMATRIX projMat;   // 4x4
    ID3D12DescriptorHeap* basic_descriptor_heap = nullptr;
    {
      // Homography

      // worldMat = DirectX::XMMatrixRotationY(DirectX::XM_PIDIV4);
      worldMat = DirectX::XMMatrixIdentity();
      DirectX::XMFLOAT3 target(0, 17, 0);
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
      auto resource_description = CD3DX12_RESOURCE_DESC::Buffer((sizeof(SceneMatrices) + 0xff) & ~0xff);
      result = _dev->CreateCommittedResource(&heap_propertiy, D3D12_HEAP_FLAG_NONE, &resource_description,
                                             D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&constBuff));
      result = constBuff->Map(0, nullptr, (void**)&mapMatrix);  // constant buffer に mapMatrix の Map

      D3D12_DESCRIPTOR_HEAP_DESC descHeapDesc = {};
      descHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;  // シェーダーから見えるように
      descHeapDesc.NodeMask = 0;                                       // マスクは 0
      descHeapDesc.NumDescriptors = 1;                                 // CBV (transforrm matrix)
      // SRV (Shader Resouce View), CBV (Constant Buffer View), UAV (Unordered Access View)
      descHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;

      result = _dev->CreateDescriptorHeap(&descHeapDesc, IID_PPV_ARGS(&basic_descriptor_heap));

      // デスクリプタの先頭ハンドルを取得しておく
      auto basicHeapHandle = basic_descriptor_heap->GetCPUDescriptorHandleForHeapStart();
      {
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = constBuff->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = static_cast<UINT>(constBuff->GetDesc().Width);
        // 定数バッファビューの作成
        // register(b0)
        _dev->CreateConstantBufferView(&cbvDesc, basicHeapHandle);
      }
    }

    //////////////////
    // message loop //
    //////////////////

    MSG msg = {};
    unsigned int frame = 0;
    float angle(0.0f);
    float angle_radian(0.0f);
    while (true) {
      if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
      }
      //もうアプリケーションが終わるって時にmessageがWM_QUITになる
      if (msg.message == WM_QUIT) {
        break;
      }

      angle += 2.0f;
      angle = std::fmodf(angle, 360.0f);
      angle_radian = angle * DirectX::XM_PI / 180.0f;
      // mapMatrix->world = DirectX::XMMatrixRotationY(angle_radian) * worldMat;
      mapMatrix->world = worldMat;
      mapMatrix->view = DirectX::XMMatrixRotationY(angle_radian) * viewMat;
      mapMatrix->proj = projMat;
      mapMatrix->eye = eye;

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
      auto dsvH = dsvHeap->GetCPUDescriptorHandleForHeapStart();
      _cmdList->OMSetRenderTargets(1, &rtvH, false, &dsvH);
      _cmdList->ClearDepthStencilView(dsvH, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

      // 画面クリア
      constexpr float clearColor[] = {1.0f, 1.0f, 1.0f, 1.0f};  // while
      _cmdList->ClearRenderTargetView(rtvH, clearColor, 0, nullptr);

      _cmdList->RSSetViewports(1, &viewport);
      _cmdList->RSSetScissorRects(1, &scissorrect);

      _cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      _cmdList->IASetVertexBuffers(0, 1, &vbView);
      _cmdList->IASetIndexBuffer(&ibView);

      _cmdList->SetGraphicsRootSignature(rootsignature);

      // WVP matrix (World View Projection Matrix)
      _cmdList->SetDescriptorHeaps(1, &basic_descriptor_heap);
      _cmdList->SetGraphicsRootDescriptorTable(
          0,                                                             // root parameter index for SRV
          basic_descriptor_heap->GetGPUDescriptorHandleForHeapStart());  // ヒープアドレス

      _cmdList->SetDescriptorHeaps(1, &materialDescHeap);  // material

      auto material_descriptor_handle = materialDescHeap->GetGPUDescriptorHandleForHeapStart();
      unsigned int idxOffset = 0;
      auto cbvsrvIncSize =
          _dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) * cbv_rsv_count_per_material;
      for (auto& m : materials) {
        _cmdList->SetGraphicsRootDescriptorTable(1, material_descriptor_handle);
        _cmdList->DrawIndexedInstanced(m.indicesNum, 1, idxOffset, 0, 0);
        material_descriptor_handle.ptr += cbvsrvIncSize;
        idxOffset += m.indicesNum;
      }

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
