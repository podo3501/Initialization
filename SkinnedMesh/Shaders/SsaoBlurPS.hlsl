#include "SsaoHeader.hlsli"
#include "VertexInout_SsaoBlur.hlsli"

float4 main(VertexOut pin) : SV_Target
{
    float blurWeights[12] =
    {
        gBlurWeights[0].x, gBlurWeights[0].y, gBlurWeights[0].z, gBlurWeights[0].w,
        gBlurWeights[1].x, gBlurWeights[1].y, gBlurWeights[1].z, gBlurWeights[1].w,
        gBlurWeights[2].x, gBlurWeights[2].y, gBlurWeights[2].z, gBlurWeights[2].w,
    };
    
    float2 texOffset;
    if (gHorizontalBlur)
    {
        texOffset = float2(gInvRenderTargetSize.x, 0.0f);
    }
    else
    {
        texOffset = float2(0.0f, gInvRenderTargetSize.y);
    }

    float4 color = blurWeights[gBlurRadius] * gInputMap.SampleLevel(gsamPointClamp, pin.TexC, 0.0f);
    float totalWeight = blurWeights[gBlurRadius];
    
    float3 centerNormal = gNormalMap.SampleLevel(gsamPointClamp, pin.TexC, 0.0f).xyz;
    float centerDepth = NdcDepthToViewDepth(gDepthMap.SampleLevel(gsamDepthMap, pin.TexC, 0.0f).r);
    
    for (float i = -gBlurRadius; i <= gBlurRadius; ++i)
    {
        if (i == 0)
            continue;

        float2 tex = pin.TexC + i * texOffset;
        
        float3 neighborNormal = gNormalMap.SampleLevel(gsamPointClamp, tex, 0.0f).xyz;
        float neighborDepth = NdcDepthToViewDepth(gDepthMap.SampleLevel(gsamDepthMap, tex, 0.0f).r);
        
        //if (dot(neighborNormal, centerNormal) >= 0.8f &&
           // abs(neighborDepth - centerDepth) <= 0.2f)
        if (dot(neighborNormal, centerNormal) >= 0.8f &&
            abs(neighborDepth - centerDepth) <= 0.2f)
        {
            float weight = blurWeights[i + gBlurRadius];
            color += weight * gInputMap.SampleLevel(gsamPointClamp, tex, 0.0f);

            totalWeight += weight;
        }
    }

    return color / totalWeight;
}