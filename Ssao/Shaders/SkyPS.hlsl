#include "GraphicsHeader.hlsli"
#include "VertexInout_Sky.hlsli"

float4 main(VertexOut pin) : SV_Target
{
    return gCubeMap.Sample(gsamLinearWrap, pin.PosL);
}