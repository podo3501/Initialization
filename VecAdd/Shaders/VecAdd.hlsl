#include "Header.hlsli"

[numthreads(32, 1, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
    gOutput[DTid.x].v1 = gInputA[DTid.x].v1 + gInputB[DTid.x].v1;
    gOutput[DTid.x].v2 = gInputA[DTid.x].v2 + gInputB[DTid.x].v2;
}