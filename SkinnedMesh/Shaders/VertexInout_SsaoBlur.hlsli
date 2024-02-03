static const int gBlurRadius = 5;

Texture2D gInputMap : register(t2);

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};