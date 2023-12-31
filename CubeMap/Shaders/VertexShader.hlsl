#include "GraphicsHeader.hlsli"

VertexOut main( VertexIn vin )
{
    VertexOut vout;
    
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosW = posW.xyz;
    vout.PosH = mul(posW, gViewProj);
    vout.NormalW = mul(vin.NormalL, (float3x3)gWorld);
    
    MaterialData matData = gMaterialData[gMaterialIndex];
    float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), gTexTransform);
    vout.TexC = mul(texC, matData.MatTransform).xy;
	
	return vout;
}
