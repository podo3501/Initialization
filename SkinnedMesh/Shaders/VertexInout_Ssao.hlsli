static const int gSampleCount = 14;

Texture2D gRandomVecMap : register(t2);

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float3 PosV : POSITION;
    float2 TexC : TEXCOORD0;
};
