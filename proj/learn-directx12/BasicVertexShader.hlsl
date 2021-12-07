#include "BasicShaderHeader.hlsli"

Output BasicVS(
    float4 pos : POSITION,
    float4 normal : NORMAL,
    float2 uv : TEXCOORD,
    min16uint2 boneno : BONE_NO,
    min16uint weight : WEIGHT
)
{
    Output output;
    output.svpos = mul(mul(mul(proj_matrix, view_matrix), world_matrix), pos); // column major
    output.pos = pos;
    normal.w = 0; // 平行移動成分を無効にする
    output.normal = mul(world_matrix, normal); // 法線にもワールド変換を行う
    output.vnormal = mul(view_matrix, output.normal); // 法線にもビュー変換を行う
    output.uv = uv;
    return output;
}
