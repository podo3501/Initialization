#include "GraphicsHeader.hlsli"
#include "VertexInout_ShadowDebug.hlsli"

float4 main(VertexOut pin) : SV_Target
{
    return float4(gShadowMap.Sample(gsamLinearWrap, pin.TexC).rrr, 1.0f);
}