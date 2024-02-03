cbuffer cbSsao : register(b0)
{
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gProjTex;
    float4 gOffsetVectors[14];
    
    float4 gBlurWeights[3];
    float2 gInvRenderTargetSize;
    
    float gOcclusionRadius;
    float gOcclusionFadeStart;
    float gOcclusionFadeEnd;
    float gSurfaceEpsilon;
};

cbuffer cbRootConstants : register(b1)
{
    bool gHorizontalBlur;
};

Texture2D gNormalMap : register(t0);
Texture2D gDepthMap : register(t1);

SamplerState gsamPointClamp : register(s0);
SamplerState gsamLinearClamp : register(s1);
SamplerState gsamDepthMap : register(s2);
SamplerState gsamLinearWrap : register(s3);

static const float2 gTexCoords[6] =
{
    float2(0.0f, 1.0f),
    float2(0.0f, 0.0f),
    float2(1.0f, 0.0f),
    float2(0.0f, 1.0f),
    float2(1.0f, 0.0f),
    float2(1.0f, 1.0f)
};

float NdcDepthToViewDepth(float z_ndc)
{
    float viewZ = gProj[3][2] / (z_ndc - gProj[2][2]);
    return viewZ;
}