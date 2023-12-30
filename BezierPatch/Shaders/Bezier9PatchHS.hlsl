#include "GraphicsHeader.hlsli"

HS_CONSTANT_DATA_OUTPUT_QUAD CalcHSPatchConstants(
	InputPatch<VertexOut, NUM_CONTROL_POINTS_TRI> ip,
	uint PatchID : SV_PrimitiveID)
{
    float3 centerL = (ip[0].PosL + ip[3].PosL + ip[6].PosL) * 0.333f;
    float3 centerW = mul(float4(centerL, 1.0f), gWorld).xyz;

    float dist = distance(centerW, gEyePosW);
	
    float lodStart = 5.0f;
    float lodEnd = 200.0f;
	
    float tess = 36.0f * saturate(1.0f - (dist - lodStart) / lodEnd);
    //float tess = 36.0f * saturate((lodEnd - dist) / (lodEnd - lodStart));
	
    HS_CONSTANT_DATA_OUTPUT_QUAD Output;

	Output.EdgeTessFactor[0] = 
		Output.EdgeTessFactor[1] = 
		Output.EdgeTessFactor[2] = 
		Output.EdgeTessFactor[3] = 
		Output.InsideTessFactor[0] = 
		Output.InsideTessFactor[1] = tess;

	return Output;
}

[domain("quad")]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(9)]
[patchconstantfunc("CalcHSPatchConstants")]
[maxtessfactor(64.0f)]
HS_CONTROL_POINT_OUTPUT main( 
	InputPatch<VertexOut, NUM_CONTROL_POINTS_TRI> ip,
	uint i : SV_OutputControlPointID,
	uint PatchID : SV_PrimitiveID )
{
	HS_CONTROL_POINT_OUTPUT Output;

    Output.PosL = ip[i].PosL;

	return Output;
}
