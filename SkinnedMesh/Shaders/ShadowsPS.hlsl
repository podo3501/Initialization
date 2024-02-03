#include "GraphicsHeader.hlsli"
#include "VertexInout_Shadows.hlsli"

void main(VertexOut pin)
{
    MaterialData matData = gMaterialData[gMaterialIndex];
    
    float4 diffuseAlbedo = matData.DiffuseAlbedo;
    uint diffuseMapIndex = matData.DiffuseMapIndex;
    
    diffuseAlbedo *= gTextureMaps[diffuseMapIndex].Sample(gsamAnisotropicWrap, pin.TexC);
    
#ifdef ALPHA_TEST
    clip(diffuseAlbedo.a - 0.1f);
#endif

}