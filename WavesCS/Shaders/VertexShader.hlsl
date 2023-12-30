#include "GraphicsHeader.hlsli"

VertexOut main( VertexIn vin )
{
    VertexOut vout;
    
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    
    vout.PosH = mul(posW, gViewProj);
    vout.PosW = posW.xyz;
    vout.NormalW = mul(vin.NormalL, (float3x3)gWorld);
    float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), gTexTransform);
    vout.TexC = mul(texC, gMatTranform).xy;
	
	return vout;
}

Texture2D gDisplacementMap : register(t1);

VertexOut mainCS(VertexIn vin)
{
    VertexOut vout;
    
    vin.PosL.y += gDisplacementMap.SampleLevel(gsamLinearWrap, vin.TexC, 1.0f).r;
    float du = 250.0f;
    float dv = 250.0f;
    float l = gDisplacementMap.SampleLevel(gsamPointClamp, vin.TexC - float2(du, 0.0f), 0.0f).r;
    float r = gDisplacementMap.SampleLevel(gsamPointClamp, vin.TexC + float2(du, 0.0f), 0.0f).r;
    float t = gDisplacementMap.SampleLevel(gsamPointClamp, vin.TexC - float2(0.0f, dv), 0.0f).r;
    float b = gDisplacementMap.SampleLevel(gsamPointClamp, vin.TexC + float2(0.0f, dv), 0.0f).r;
    
    vin.NormalL = normalize(float3(-r + l, 2.0f * 1.0f, b - t));
       
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosW = posW.xyz;  
    vout.PosH = mul(posW, gViewProj);
    vout.NormalW = mul(vin.NormalL, (float3x3) gWorld);
    float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), gTexTransform);
    vout.TexC = mul(texC, gMatTranform).xy;

    return vout;
}