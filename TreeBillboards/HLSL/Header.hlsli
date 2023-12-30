#ifndef NUM_DIR_LIGHTS
#define NUM_DIR_LIGHTS 3
#endif

#ifndef NUM_POINT_LIGHTS
#define NUM_POINT_LIGHTS 0
#endif

#ifndef NUM_SPOT_LIGHTS
#define NUM_SPOT_LIGHTS 0
#endif

#include "LightingUtil.hlsl"

Texture2DArray gTreeMapArray : register(t0);

SamplerState gsamPointWrap : register(s0);
SamplerState gsamPointClamp : register(s1);
SamplerState gsamLinearWrap : register(s2);
SamplerState gsamLinearClamp : register(s3);
SamplerState gsamAnisotropicWrap : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);

cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gTexTransform;
};

cbuffer cbPass : register(b1)
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

cbuffer cbMaterial : register(b2)
{
    float4 gDiffuseAlbedo;
    float3 gFresnelR0;
    float gRoughness;
    float4x4 gMatTransform;
};

struct Vertex
{
    float3 PosH : POSITION;
    float2 NorH : NORMAL;
    float Height : HEIGHT;
};

//circle

struct VertexOut
{
    float3 PosH : POSITION;
    float2 NorH : NORMAL;
    float Height : HEIGHT;
};

struct GSOutput
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float3 NorW : NORMAL;
    uint PrimID : SV_PrimitiveID;
};

//sphere 
struct sVertex
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float Radius : RADIUS;
};

struct sVertexOut
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float Radius : RADIUS;
};

//vertex normal
struct nVertex
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
};

struct nVertexOut
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
};

struct VertexNormalGSOutput
{
    float4 PosH : SV_POSITION;
    uint PrimID : SV_PrimitiveID;
};

struct FaceNormalGSOutput
{
    float4 PosH : SV_POSITION;
};