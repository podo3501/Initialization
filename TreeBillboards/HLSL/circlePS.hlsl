#include "header.hlsli"

float4 main(GSOutput pin) : SV_TARGET
{
    float4 diffuseAlbedo = gDiffuseAlbedo;
	
#ifdef ALPHA_TEST
    clip(diffuseAlbedo.a - 0.1f);
#endif
    
    pin.NorW = normalize(pin.NorW);
    
    float3 toEyeW = gEyePosW - pin.PosW;
    float distToEye = length(toEyeW);
    toEyeW /= distToEye;
    
    float4 ambient = gAmbientLight * diffuseAlbedo;
    float shininess = 1.0f - gRoughness;
    Material mat = { diffuseAlbedo, gFresnelR0, shininess };
    float3 shadowFactor = 1.0f;
    float4 directLight = ComputeLighting(gLights, mat, pin.PosW, pin.NorW, toEyeW, shadowFactor);
    
    float4 litColor = ambient + directLight;
#ifdef FOG
    float fogAmount = saturate((distToEye - gFogStart) / gFogRange );
    litColor = lerp(litColor, gFogColor, fogAmount);
#endif
    litColor.a = diffuseAlbedo.a;

    return litColor;
}

float4 VertexNormalPS(VertexNormalGSOutput pin) : SV_TARGET
{
    float4 litColor = gDiffuseAlbedo;

    return litColor;
}

float4 FaceNormalPS(FaceNormalGSOutput pin) : SV_TARGET
{
    float4 litColor = gDiffuseAlbedo;
    litColor.r = 0.0f;
    litColor.g = 1.0f;
    litColor.b = 0.0f;
    return litColor;
}