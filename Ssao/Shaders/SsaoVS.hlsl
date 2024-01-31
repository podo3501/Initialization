#include "SsaoHeader.hlsli"
#include "VertexInout_Ssao.hlsli"

VertexOut main(uint vid : SV_VertexID)
{
    VertexOut vout;
    
    vout.TexC = gTexCoords[vid];
    vout.PosH = float4(2.0f * vout.TexC.x - 1.0f, 1.0f - 2.0f * vout.TexC.y, 0.0f, 1.0f);
    
    float4 ph = mul(vout.PosH, gInvProj);
    vout.PosV = ph.xyz / ph.w;
    
    return vout;
}