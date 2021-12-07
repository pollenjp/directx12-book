// 頂点シェーダーからピクセルシェーダーへのやり取りに使用する構造体
struct Output
{
    float4 svpos : SV_POSITION; // システム用頂点座標
    float4 normal: NORMAL;
    float2 uv :TEXCOORD;        // uv 値
};

Texture2D<float4> tex : register(t0); // 0 番スロットに設定されたテクスチャ
Texture2D<float4> sph : register(t1); // 1 番スロットに設定されたテクスチャ
Texture2D<float4> spa : register(t2); // 2 番スロットに設定されたテクスチャ
SamplerState smp : register(s0); // 0 番スロットに設定されたサンプラー

// 変換をまとめた構造体
cbuffer cbuff0 : register(b0) // 定数バッファー
{
    // matrix, float4x4, matrix<float, 4, 4> (same expression)
    float4x4 world_matrix;
    float4x4 viewproj_matrix;
};

// 定数バッファー1
// マテリアル用
cbuffer Material : register(b1)
{
    float4 diffuse; // ディフューズ色
    float4 specular; // スペキュラ
    float3 ambient; // アンビエント
};
