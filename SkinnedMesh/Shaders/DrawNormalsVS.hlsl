#include "GraphicsHeader.hlsli"
#include "VertexInout_DrawNormals.hlsli"

VertexOut main(VertexIn vin)
{
    VertexOut vout = (VertexOut) 0.0f;
    
    MaterialData matData = gMaterialData[gMaterialIndex];
    
    vout.NormalW = mul(vin.NormalL, (float3x3) gWorld);
    vout.TangentW = mul(vin.TangentU, (float3x3) gWorld);
    
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosH = mul(posW, gViewProj);
    
    float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), gTexTransform);
    vout.TexC = mul(texC, matData.MatTransform).xy;
    
    return vout;
}