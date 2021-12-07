#include "BasicShaderHeader.hlsli"

float4 BasicPS(Output input ) : SV_TARGET
{
    float3 light = normalize(float3(1, -1, 1));
    float brightness = dot(-light, input.normal.xyz);
    float2 normalUV = (input.normal.xy + float2(1, -1)) * float2(0.5, -0.5);
    return float4(brightness, brightness, brightness, 1)
        * diffuse
        * tex.Sample(smp, input.uv)
        * sph.Sample(smp, normalUV);
}
