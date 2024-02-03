#include "SsaoHeader.hlsli"
#include "VertexInout_Ssao.hlsli"

float OcclusionFunction(float distZ)
{
    float occlusion = 0.0f;
    if (distZ > gSurfaceEpsilon)
    {
        float fadeLength = gOcclusionFadeEnd - gOcclusionFadeStart;
        occlusion = saturate((gOcclusionFadeEnd - distZ) / fadeLength);
    }
    return occlusion;
}

float4 main(VertexOut pin) : SV_Target
{
    float3 n = normalize(gNormalMap.SampleLevel(gsamPointClamp, pin.TexC, 0.0f).xyz);
    float pz = gDepthMap.SampleLevel(gsamDepthMap, pin.TexC, 0.0f).r;
    pz = NdcDepthToViewDepth(pz);
    
    float3 p = (pz / pin.PosV.z) * pin.PosV;
    float3 randVec = 2.0f * gRandomVecMap.SampleLevel(gsamLinearWrap, 4.0f * pin.TexC, 0.0f).rbg - 1.0f;
    
    float occlusionSum = 0.0f;
    
    for (int i = 0; i < gSampleCount; ++i)
    {
        float3 offset = reflect(gOffsetVectors[i].xyz, randVec);
        float flip = sign(dot(offset, n));
        float3 q = p + flip * gOcclusionRadius * offset;
        float4 projQ = mul(float4(q, 1.0f), gProjTex);
        projQ /= projQ.w;
        
        float rz = gDepthMap.SampleLevel(gsamDepthMap, projQ.xy, 0.0f).r;
        rz = NdcDepthToViewDepth(rz);
        float3 r = (rz / q.z) * q;

        float distZ = p.z - r.z;
        float dp = max(dot(n, normalize(r - p)), 0.0f);
        float occlusion = dp * OcclusionFunction(distZ);
        //float occlusion = OcclusionFunction(distZ); 
        
        occlusionSum += occlusion;
    }
    occlusionSum /= gSampleCount;
    
    float access = 1.0f - occlusionSum;
    return saturate(pow(access, 6.0f));
}