#include "GraphicsHeader.hlsli"

float3 BernsteinBasis(float t)
{
    float invT = 1.0f - t;
	
    return float3(
		invT * invT,
		2.0f * invT * t,
		t * t);
}

//float3 dBernsteinBasis(float t)
//{
//    float invT = 1.0f - t;
	
//    return float4(
//		-3.0f * invT * invT,
//		3.0f * invT * invT - 6.0f * t * invT,
//		6.0f * t * invT - 3.0f * t * t,
//		3 * t * t);
//}

float3 CubicBezierSum(const OutputPatch<HS_CONTROL_POINT_OUTPUT, NUM_CONTROL_POINTS_TRI> bezpatch,
	float3 basisU, float3 basisV)
{
    float3 sum = float3(0.0f, 0.0f, 0.0f);
    sum = basisV.x * (basisU.x * bezpatch[0].PosL +
									basisU.y * bezpatch[1].PosL +
									basisU.z * bezpatch[2].PosL);
	
    sum += basisV.y * (basisU.x * bezpatch[3].PosL +
									basisU.y * bezpatch[4].PosL +
									basisU.z * bezpatch[5].PosL);
	
    sum += basisV.z * (basisU.x * bezpatch[6].PosL +
									basisU.y * bezpatch[7].PosL +
									basisU.z * bezpatch[8].PosL);

    return sum;
}

[domain("quad")]
DS_OUTPUT main(
	HS_CONSTANT_DATA_OUTPUT_QUAD input,
	float2 uv : SV_DomainLocation,
	const OutputPatch<HS_CONTROL_POINT_OUTPUT, NUM_CONTROL_POINTS_TRI> patch)
{
	DS_OUTPUT Output;

    float3 p = CubicBezierSum(patch, BernsteinBasis(uv.x), BernsteinBasis(uv.y));
	
    float4 posW = mul(float4(p, 1.0f), gWorld);
    Output.PosH = mul(posW, gViewProj);
	
	return Output;
}
