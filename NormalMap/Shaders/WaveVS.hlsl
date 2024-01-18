#include "GraphicsHeader.hlsli"
#include "VertexInout_Wave.hlsli"

VertexOut main( VertexIn vin )
{
    VertexOut vout;
    
    MaterialData matData = gMaterialData[gMaterialIndex];
    uint normalMapIndex0 = matData.DiffuseMapIndex;
    uint normalMapIndex1 = matData.NormalMapIndex;
    
    float4x4 moveTexTransform0 = gTexTransform;
    float4x4 moveTexTransform1 = gTexTransform;
    moveTexTransform0._41 += gTotalTime * -0.04f;
    moveTexTransform1._42 += gTotalTime * -0.04f;
    
    float2 texC0 = mul(mul(float4(vin.TexC, 0.0f, 1.0f), moveTexTransform0), matData.MatTransform).xy;
    float2 texC1 = mul(mul(float4(vin.TexC, 0.0f, 1.0f), moveTexTransform1), matData.MatTransform).xy;
    
    float4 normalMapSample0 = gTextureMaps[normalMapIndex0].SampleLevel(gsamAnisotropicWrap, texC0, 0);
    float4 normalMapSample1 = gTextureMaps[normalMapIndex1].SampleLevel(gsamAnisotropicWrap, texC1, 0);
    
    float3 posL = vin.PosL;
    posL.y = normalMapSample0.y + normalMapSample1.y;
    
    //vout.TexC = mul(texC, matData.MatTransform).xy;
    vout.TexC = texC0;
    
    float4 posW = mul(float4(posL, 1.0f), gWorld);
    vout.PosW = posW.xyz;
    vout.PosH = mul(posW, gViewProj);
    vout.NormalW = mul(vin.NormalL, (float3x3) gWorld);
    vout.TangentW = mul(vin.TangentU, (float3x3) gWorld);
	
    return vout;
}
