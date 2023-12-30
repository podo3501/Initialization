#include "ComputeHeader.hlsli"

static const int gMaxBlurRadius = 5;

#define N 256
#define CacheSize (N + 2 * gMaxBlurRadius)
groupshared float4 gCache[CacheSize];

static const float PI = 3.14159265f;

float Gau(float4 v, float sigma)
{
    float x = length(v);
    return exp(-(pow(x, 2)) / (2 * pow(sigma, 2))) / (2 * PI * pow(sigma, 2));
}

[numthreads(N, 1, 1)]
void HorzBlurCS(
    int3 groupThreadID : SV_GroupThreadID, 
    int3 dispatchThreadID : SV_DispatchThreadID )
{       
    float weights[11] = { w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w10 };
    
    if (groupThreadID.x < gBlurRadius)
    {
        int x = max(dispatchThreadID.x - gBlurRadius, 0);
        gCache[groupThreadID.x] = gInput[int2(x, dispatchThreadID.y)];
    }
    if (groupThreadID.x >= N - gBlurRadius)
    {
        int x = min(dispatchThreadID.x + gBlurRadius, gInput.Length.x - 1);
        gCache[groupThreadID.x + 2 * gBlurRadius] = gInput[int2(x, dispatchThreadID.y)];
    }
    //마지막에 Radius만큼 저장되었던 값이 다른 쓰레드로 0로 값이 덮어버려 그런게 아닌가 싶은데 
    //if문으로 한번 해 보자. 
    gCache[groupThreadID.x + gBlurRadius] = gInput[min(dispatchThreadID.xy, gInput.Length.xy - 1)];

    GroupMemoryBarrierWithGroupSync();
    
    float4 blurColor = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float wP = 0;
    for (int i = -gBlurRadius; i <= gBlurRadius; ++i)
    {
        int k = groupThreadID.x + gBlurRadius + i;
        float gi = Gau(gCache[k] - gCache[groupThreadID.x + gBlurRadius], 0.05f);
        float w = weights[i + gBlurRadius] * gi;
        blurColor += w * gCache[k];
        wP = wP + w;
    }
    blurColor /= wP;
    gOutput[dispatchThreadID.xy] = blurColor;
}

[numthreads(1, N, 1)]
void VertBlurCS(
    int3 groupThreadID : SV_GroupThreadID,
    int3 dispatchThreadID : SV_DispatchThreadID)
{   
    float weights[11] = { w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w10 };

    if (groupThreadID.y < gBlurRadius)
    {
        int y = max(dispatchThreadID.y - gBlurRadius, 0);
        gCache[groupThreadID.y] = gInput[int2(dispatchThreadID.x, y)];
    }
    if (groupThreadID.y >= N - gBlurRadius)
    {
        int y = min(dispatchThreadID.y + gBlurRadius, gInput.Length.y - 1);
        gCache[groupThreadID.y + 2 * gBlurRadius] = gInput[int2(dispatchThreadID.x, y)];
    }
    gCache[groupThreadID.y + gBlurRadius] = gInput[min(dispatchThreadID.xy, gInput.Length.xy - 1)];
    
    GroupMemoryBarrierWithGroupSync();
    
    float4 blurColor = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float wP = 0;
    for (int i = -gBlurRadius; i <= gBlurRadius; ++i)
    {
        int k = groupThreadID.y + gBlurRadius + i;
        float gi = Gau(gCache[k] - gCache[groupThreadID.y + gBlurRadius], 0.05f);
        float w = weights[i + gBlurRadius] * gi;
        blurColor += w * gCache[k];
        wP = wP + w;
    }
    blurColor /= wP;
    gOutput[dispatchThreadID.xy] = blurColor;
}


[numthreads(1, 1, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{}