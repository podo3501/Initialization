#include "GraphicsHeader.hlsli"
#include "VertexInout.hlsli"

VertexOut main( VertexIn vin )
{
    VertexOut vout = (VertexOut) 0.0f;
    
    MaterialData matData = gMaterialData[gMaterialIndex];
    
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosW = posW.xyz;
    vout.NormalW = mul(vin.NormalL, (float3x3) gWorld);
    
    vout.PosH = mul(posW, gViewProj);
    
    float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), gTexTransform);
    vout.TexC = mul(texC, matData.MatTransform).xy;
    
    vout.ProjTex = mul(float4(vin.PosL, 1.0f), gProjectiveProj);
	
    return vout;
}
