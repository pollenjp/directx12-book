// 頂点シェーダーからピクセルシェーダーへのやり取りに使用する構造体
struct Output
{
    float4 svpos : SV_POSITION; // システム用頂点座標
    float2 uv :TEXCOORD;        // uv 値
};

Texture2D<float4> tex : register(t0); // 0 番スロットに設定されたテクスチャ
SamplerState smp : register(s0); // 0 番スロットに設定されたサンプラー

// 変換をまとめた構造体
cbuffer cbuff0 : register(b0) // 定数バッファー
{
    // matrix, float4x4, matrix<float, 4, 4> (same expression)
    float4x4 mat; // 変換行列
    // matrix<float, 4, 4> mat; // 変換行列
};
