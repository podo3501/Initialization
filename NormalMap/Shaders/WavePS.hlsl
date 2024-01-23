#ifndef NUM_DIR_LIGHTS
    #define NUM_DIR_LIGHTS 3
#endif

#ifndef NUM_POINT_LIGHTS
    #define NUM_POINT_LIGHTS 0
#endif 

#ifndef NUM_SPOT_LIGHTS
    #define NUM_SPOT_LIGHTS 0
#endif

#include "GraphicsHeader.hlsli"
#include "LightingUtil.hlsli"
#include "VertexInout_Wave.hlsli"

float3 NormalSampleToWorldSpace(float3 normalMapSample, float3 unitNormalW, float3 tangentW)
{
    float3 normalT = 2.0f * normalMapSample - 1.0f;
    
    float3 N = unitNormalW;
    float3 T = normalize(tangentW - dot(tangentW, N) * N);
    float3 B = cross(N, T);
    
    float3x3 TBN = float3x3(T, B, N);
    
    float3 bumpedNormalW = mul(normalT, TBN);

    return bumpedNormalW;
}

float4 main(VertexOut pin) : SV_Target
{
    MaterialData matData = gMaterialData[gMaterialIndex];
    float4 diffuseAlbedo = matData.DiffuseAlbedo;
    float3 fresnelR0 = matData.FresnelR0;
    float roughness = matData.Roughness;
    uint diffuseTexIndex = matData.DiffuseMapIndex;
    uint normalMapIndex = matData.NormalMapIndex;
    
    pin.NormalW = normalize(pin.NormalW);
    
    float3 normalMapSample0 = gTextureMaps[diffuseTexIndex].Sample(gsamAnisotropicWrap, pin.TexC0).rgb;
    float3 bumpedNormalW0 = NormalSampleToWorldSpace(normalMapSample0, pin.NormalW, pin.TangentW);
    
    float3 normalMapSample1 = gTextureMaps[normalMapIndex].Sample(gsamAnisotropicWrap, pin.TexC1).rgb;
    float3 bumpedNormalW1 = NormalSampleToWorldSpace(normalMapSample1, pin.NormalW, pin.TangentW);
    
    float3 bumpedNormalW = normalize(bumpedNormalW0 + bumpedNormalW1);
    
    //bumpedNormalW = pin.NormalW;
    
    //diffuseAlbedo *= gTextureMaps[diffuseTexIndex].Sample(gsamAnisotropicWrap, pin.TexC);
    
    
#ifdef ALPHA_TEST
    clip(diffuseAlbedo.a - 0.1f);
#endif
    
    float3 toEyeW = normalize(gEyePosW - pin.PosW);
    
    float4 ambient = gAmbientLight * diffuseAlbedo;
    const float shininess = (1.0f - roughness);// * ((normalMapSample0.a + normalMapSample1.a) / 2.0f);
    
    float3 shadowFactor = 1.0f;
    Material mat = { diffuseAlbedo, fresnelR0, shininess };
    float4 directLight = ComputeLighting(gLights, mat, pin.PosW, bumpedNormalW, toEyeW, shadowFactor);
    
    float4 litColor = ambient+directLight;
    
    float3 r = reflect(-toEyeW, bumpedNormalW);
    float4 reflectionColor = gCubeMap.Sample(gsamLinearWrap, r);
    float3 fresnelFactor = SchlickFresnel(fresnelR0, bumpedNormalW, r);
    litColor.rgb += shininess * fresnelFactor * reflectionColor.rgb;
    
#ifdef FOG
    float fogAmount = saturate((distToEye - gFogStart) / gFogRange);
    litColor = lerp(litColor, gFogColor, fogAmount);
#endif    
    
    litColor.a = diffuseAlbedo.a;
    return litColor;
}