#include "GraphicsHeader.hlsli"

float3 TriangleBezierSum(const OutputPatch<HS_CONTROL_POINT_OUTPUT, NUM_CONTROL_POINTS_TRIANGLE> patch,
	float3 uvw)
{
	float3 sum = float3(0.0f, 0.0f, 0.0f);
	
    float u = uvw.x;
    float v = uvw.y;
    float w = uvw.z;
	
    sum = u * u * u * patch[0].PosL + v * v * v * patch[9].PosL + w * w * w * patch[3].PosL +
				3 * u * u * v * patch[4].PosL + 3 * u * v * v * patch[7].PosL +
				3 * v * v * w * patch[8].PosL + 3 * v * w * w * patch[6].PosL +
				3 * w * w * v * patch[2].PosL + 3 * w * u * u * patch[1].PosL +
				6 * u * v * w * patch[5].PosL;
	
    return sum;
}

[domain("tri")]
DS_OUTPUT main(
	HS_CONSTANT_DATA_OUTPUT_TRI input,
	float3 uvw : SV_DomainLocation,
	const OutputPatch<HS_CONTROL_POINT_OUTPUT, NUM_CONTROL_POINTS_TRIANGLE> patch)
{
    DS_OUTPUT Output;
	
    float3 p = TriangleBezierSum(patch, uvw);
	
    float4 posW = mul(float4(p, 1.0f), gWorld);
    Output.PosH = mul(posW, gViewProj);
	
	return Output;
}
