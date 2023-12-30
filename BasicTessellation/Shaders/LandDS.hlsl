#include "GraphicsHeader.hlsli"

[domain("tri")]
DS_OUTPUT main(
	HS_CONSTANT_DATA_OUTPUT input,
	float3 domain : SV_DomainLocation,
	const OutputPatch<HS_CONTROL_POINT_OUTPUT, NUM_CONTROL_POINTS> patch)
{
	DS_OUTPUT Output;
	
    float3 p = patch[0].PosL * domain.x + patch[1].PosL * domain.y + patch[2].PosL * domain.z;
		
    p.y = 0.3f * (p.z * sin(0.1f * p.x) + p.x * cos(0.1f * p.z));
	
    float4 posW = mul(float4(p, 1.0f), gWorld);
    Output.PosH = mul(posW, gViewProj);
	
	return Output;
}
