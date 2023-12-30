#include "GraphicsHeader.hlsli"

[domain("tri")]
DS_OUTPUT main(
	HS_CONSTANT_DATA_OUTPUT_TRI input,
	float3 domain : SV_DomainLocation,
	const OutputPatch<HS_CONTROL_POINT_OUTPUT, NUM_CONTROL_POINTS_SPHERE> patch)
{
	DS_OUTPUT Output;
	
    float3 p = patch[0].PosL * domain.x + patch[1].PosL * domain.y + patch[2].PosL * domain.z;
		
    float3 np = normalize(p);
    float3 radiusP = 4.0f * np;
	
    float4 posW = mul(float4(radiusP, 1.0f), gWorld);
    Output.PosH = mul(posW, gViewProj);
	
	return Output;
}
