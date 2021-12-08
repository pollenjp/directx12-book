#include "BasicShaderHeader.hlsli"

float4 BasicPS(Output input ) : SV_TARGET
{
    float3 light = normalize(float3(1, -1, 1));

    // reflection vector
    float3 ref_light = normalize(reflect(light, input.normal.xyz));

    // 光が当たっている場合のみ考慮
    // surface (1.0), back-face (0.0)
    float is_surface = step(0.0f, dot(input.normal.xyz, - light));
    float4 specular_component = is_surface * specularB * float4(specular.rgb, 1);

    float brightness = dot(-light, input.normal.xyz);
    float2 sphereMapUV = (input.vnormal.xy + float2(1, -1)) * float2(0.5, -0.5);
    return specular_component;
    // return float4(brightness, brightness, brightness, 1)
    //     * diffuse
    //     * color
    //     * sph.Sample(smp, sphereMapUV)
    //     + spa.Sample(smp, sphereMapUV)
    //     + float4(color.rgb * ambient.rgb, 1);
}
