#include "BasicShaderHeader.hlsli"

float4 BasicPS(Output input ) : SV_TARGET
{
    float3 light = normalize(float3(1, -1, 1));

    // reflection vector
    float3 ref_light = normalize(reflect(light, input.normal.xyz));
    float specularB = pow(saturate(dot(ref_light, - input.ray)), specular.a);

    // 光が当たっている場合のみ考慮
    // surface (1.0), back-face (0.0)
    float is_surface = step(0.0f, dot(input.normal.xyz, - light));
    float4 specular_component = is_surface * specularB * float4(specular.rgb, 1);

    float2 sphereMapUV = (input.vnormal.xy + float2(1, -1)) * float2(0.5, -0.5);
    float4 texture_color_component = tex.Sample(smp, input.uv);

    float diffuseB = saturate(dot(- light, input.normal.xyz));
    float4 toon_diffuse = toon.Sample(smpToon, float2(0, 1.0 - diffuseB));

    float4 brightness = float4(max(toon_diffuse.rgb, ambient.rgb), 1);
    return brightness
        * diffuse.rgba
        * texture_color_component.rgba
        * sph.Sample(smp, sphereMapUV).rgba
        + spa.Sample(smp, sphereMapUV).rgba
        + specular_component.rgba;
}
