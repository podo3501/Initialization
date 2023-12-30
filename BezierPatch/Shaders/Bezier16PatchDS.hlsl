#include "GraphicsHeader.hlsli"

float4 BernsteinBasis(float t)
{
    float invT = 1.0f - t;
	
    return float4(
		invT * invT * invT,
		3.0f * t * invT * invT,
		3.0f * t * t * invT,
		t * t * t);
}

float4 dBernsteinBasis(float t)
{
    float invT = 1.0f - t;
	
    return float4(
		-3.0f * invT * invT,
		3.0f * invT * invT - 6.0f * t * invT,
		6.0f * t * invT - 3.0f * t * t,
		3 * t * t);
}

float3 CubicBezierSum(const OutputPatch<HS_CONTROL_POINT_OUTPUT, NUM_CONTROL_POINTS_QUAD> bezpatch,
	float4 basisU, float4 basisV)
{
    float3 sum = float3(0.0f, 0.0f, 0.0f);
    sum = basisV.x * (	basisU.x * bezpatch[0].PosL +
									basisU.y * bezpatch[1].PosL +
									basisU.z * bezpatch[2].PosL +
									basisU.w * bezpatch[3].PosL);
	
    sum += basisV.y * (basisU.x * bezpatch[4].PosL +
									basisU.y * bezpatch[5].PosL +
									basisU.z * bezpatch[6].PosL +
									basisU.w * bezpatch[7].PosL);
	
    sum += basisV.z * (basisU.x * bezpatch[8].PosL +
									basisU.y * bezpatch[9].PosL +
									basisU.z * bezpatch[10].PosL +
									basisU.w * bezpatch[11].PosL);
	
    sum += basisV.w * (basisU.x * bezpatch[12].PosL +
									basisU.y * bezpatch[13].PosL +
									basisU.z * bezpatch[14].PosL +
									basisU.w * bezpatch[15].PosL);

    return sum;
}

[domain("quad")]
DS_OUTPUT_NORMAL main(
	HS_CONSTANT_DATA_OUTPUT_QUAD input,
	float2 uv : SV_DomainLocation,
	const OutputPatch<HS_CONTROL_POINT_OUTPUT, NUM_CONTROL_POINTS_QUAD> patch)
{
    DS_OUTPUT_NORMAL Output;

    float4 basisU = BernsteinBasis(uv.x);
    float4 basisV = BernsteinBasis(uv.y);
	
    float3 p = CubicBezierSum(patch, basisU, basisV);
	
    float4 dBasisU = dBernsteinBasis(uv.x);
    float4 dBasisV = dBernsteinBasis(uv.y);
	
    float3 dpdu = CubicBezierSum(patch, dBasisU, basisV);
    float3 dpdv = CubicBezierSum(patch, basisU, dBasisV);
	
    float3 normal = normalize(cross(dpdu, dpdv));
	
    float4 posW = mul(float4(p, 1.0f), gWorld);
    Output.PosH = mul(posW, gViewProj);
    Output.PosW = posW.xyz;
    Output.NormalW = mul(float4(normal, 1.0f), gWorld).xyz;
	
	return Output;
}
