#include "GraphicsHeader.hlsli"
#include "VertexInout_Sky.hlsli"

VertexOut main( VertexIn vin )
{
    VertexOut vout;
    
    vout.PosL = vin.PosL;
    
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    posW.xyz += gEyePosW;
    
    vout.PosH = mul(posW, gViewProj).xyww;
	
	return vout;
}
