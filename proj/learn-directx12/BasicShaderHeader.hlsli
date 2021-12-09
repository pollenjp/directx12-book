// 頂点シェーダーからピクセルシェーダーへのやり取りに使用する構造体
struct Output
{
    float4 svpos : SV_POSITION; // システム用頂点座標
    float4 pos : POSITION;      // vertex position
    float4 normal: NORMAL0;     // normal
    float4 vnormal : NORMAL1;   // ビュー変換後の法線ベクトル
    float2 uv :TEXCOORD;        // uv 値
    float3 ray : VECTOR;        // ベクトル
};

Texture2D<float4> tex : register(t0); // 0 番スロットに設定されたテクスチャ
Texture2D<float4> sph : register(t1); // 1 番スロットに設定されたテクスチャ
Texture2D<float4> spa : register(t2); // 2 番スロットに設定されたテクスチャ
Texture2D<float4> toon : register(t3); // 3 番スロットに設定されたテクスチャ (トゥーン) 

SamplerState smp : register(s0); // 0 番スロットに設定されたサンプラー
SamplerState smpToon : register(s1); // 1 番スロットに設定されたサンプラー (トゥーン用) 

// 変換をまとめた構造体
cbuffer cbuff0 : register(b0) // 定数バッファー
{
    // matrix, float4x4, matrix<float, 4, 4> (same expression)
    float4x4 world_matrix;
    float4x4 view_matrix;
    float4x4 proj_matrix;
    float3 eye; // eye position
};

// 定数バッファー1
// マテリアル用
cbuffer Material : register(b1)
{
    float4 diffuse; // ディフューズ色
    float4 specular; // スペキュラ
    float3 ambient; // アンビエント
};
