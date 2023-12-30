#include "GraphicsHeader.hlsli"

#ifndef NUM_DIR_LIGHTS
    #define NUM_DIR_LIGHTS 3
#endif

#ifndef NUM_POINT_LIGHTS
    #define NUM_POINT_LIGHTS 0
#endif 

#ifndef NUM_SPOT_LIGHTS
    #define NUM_SPOT_LIGHTS 0
#endif

#include "LightingUtil.hlsli"

float4 main(DS_OUTPUT pin) : SV_TARGET
{
    return gDiffuseAlbedo;
}