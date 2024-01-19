struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
    float3 TangentU : TANGENT;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float3 TangentW : TANGENT;
    float2 TexC0 : TEXCOORD0;
    float2 TexC1 : TEXCOORD1;
};

//struct HS_CONSTANT_DATA_OUTPUT
//{
//    float EdgeTessFactor[4] : SV_TessFactor;
//    float InsideTessFactor[2] : SV_InsideTessFactor;
//    float3 PosL : POSITION;
//    float3 NormalL : NORMAL;
//    float2 TexC : TEXCOORD;
//    float3 TangentU : TANGENT;
//};

//struct HS_CONTROL_POINT_OUTPUT
//{
//    float3 PosL : POSITION;
//    float3 NormalL : NORMAL;
//    float2 TexC : TEXCOORD;
//    float3 TangentU : TANGENT;
//};

//struct DS_OUTPUT
//{
//    float4 PosH : SV_POSITION;
//    float3 PosW : POSITION;
//    float3 NormalW : NORMAL;
//    float3 TangentW : TANGENT;
//    float2 TexC : TEXCOORD;
//};

#define NUM_CONTROL_POINTS 4