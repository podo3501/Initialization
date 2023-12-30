#include "GraphicsHeader.hlsli"

#ifndef NUM_DIR_LIGHTS
    #define NUM_DIR_LIGHTS 3
#endif

#ifndef NUM_POINT_LIGHTS
    #define NUM_POINT_LIGHTS 0
#endif 

#ifndef NUM_SPOT_LIGHTS
    #define NUM_SPOT_LIGHTS 0
#endif

#include "LightingUtil.hlsli"

float4 main(DS_OUTPUT pin) : SV_TARGET
{
    return gDiffuseAlbedo;
}

float4 main_normal(DS_OUTPUT_NORMAL pin) : SV_TARGET
{
    float4 diffuseAlbedo = gDiffuseAlbedo;
    
    float3 toEyeW = gEyePosW - pin.PosW;
    float distToEye = length(toEyeW);
    toEyeW /= distToEye;
    
    float shininess = 1.0f - gRoughness;
    float4 ambient = gAmbientLight * diffuseAlbedo;
    float3 shadowFactor = 1.0f;
    Material mat = { diffuseAlbedo, gFresnelR0, shininess };
    float4 directLight = ComputeLighting(gLights, mat, pin.PosW, pin.NormalW, toEyeW, shadowFactor);
    
    float4 litColor = ambient + directLight;
    
    litColor.a = diffuseAlbedo.a;
    return litColor;
}