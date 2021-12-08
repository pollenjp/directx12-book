#include "BasicShaderHeader.hlsli"

float4 BasicPS(Output input ) : SV_TARGET
{
    float3 light = normalize(float3(1, -1, 1));
    float brightness = dot(-light, input.normal.xyz);
    float2 sphereMapUV = (input.vnormal.xy + float2(1, -1)) * float2(0.5, -0.5);
    float4 color = tex.Sample(smp, input.uv);
    return float4(brightness, brightness, brightness, 1)
        * diffuse
        * color
        * sph.Sample(smp, sphereMapUV)
        + spa.Sample(smp, sphereMapUV)
        + float4(color.rgb * ambient.rgb, 1);
}
