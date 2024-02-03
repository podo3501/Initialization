#include "GraphicsHeader.hlsli"
#include "VertexInout_DrawNormals.hlsli"

float4 main(VertexOut pin) : SV_Target
{
    MaterialData matData = gMaterialData[gMaterialIndex];
    float4 diffuseAlbedo = matData.DiffuseAlbedo;
    uint diffuseMapIndex = matData.DiffuseMapIndex;
    uint normalMapIndex = matData.NormalMapIndex;
    
    diffuseAlbedo *= gTextureMaps[diffuseMapIndex].Sample(gsamAnisotropicWrap, pin.TexC);
    
    pin.NormalW = normalize(pin.NormalW);
    
    float3 normalV = mul(pin.NormalW, (float3x3) gView);
    return float4(normalV, 0.0f);
}