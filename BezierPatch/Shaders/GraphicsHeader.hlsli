#include "Common.hlsli"

Texture2D gDiffuseMap : register(t0);

SamplerState gsamPointWrap : register(s0);
SamplerState gsamPointClamp : register(s1);
SamplerState gsamLinearWrap : register(s2);
SamplerState gsamLinearClamp : register(s3);
SamplerState gsamAnisotropicWrap : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);

cbuffer ObjectCB : register(b0)
{
    float4x4 gWorld;
    float4x4 gTexTransform;
};

cbuffer PassCB : register(b1)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float3 gEyePosW;
    float gCbPerObjectPad1;
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSSize;
    float gNearZ;
    float gFarZ;
    float gTotalTime;
    float gDeltaTime;
    float4 gAmbientLight;
    float4 gFogColor;
    float gFogStart;
    float gFogRange;
    float2 gCbPerObjectPad2;
    Light gLights[MaxLights];
};

cbuffer Material : register(b2)
{
    float4 gDiffuseAlbedo;
    float3 gFresnelR0;
    float gRoughness;
    float4x4 gMatTranform;
};

struct VertexIn
{
    float3 PosL : POSITION;
};

struct VertexOut
{
    float3 PosL : POSITION;
};

struct HS_CONSTANT_DATA_OUTPUT_QUAD
{
    float EdgeTessFactor[4] : SV_TessFactor;
    float InsideTessFactor[2] : SV_InsideTessFactor;
};

struct HS_CONSTANT_DATA_OUTPUT_TRI
{
    float EdgeTessFactor[3] : SV_TessFactor;
    float InsideTessFactor[1] : SV_InsideTessFactor;
};

struct HS_CONTROL_POINT_OUTPUT
{
    float3 PosL : POSITION;
};

struct DS_OUTPUT
{
    float4 PosH : SV_POSITION;
};

struct DS_OUTPUT_NORMAL
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
};

#define NUM_CONTROL_POINTS_QUAD 16
#define NUM_CONTROL_POINTS_TRI 9
#define NUM_CONTROL_POINTS_TRIANGLE 10
#define NUM_CONTROL_POINTS_SPHERE 3