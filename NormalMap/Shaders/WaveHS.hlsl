#include "GraphicsHeader.hlsli"
#include "VertexInout_Wave.hlsli"

HS_CONSTANT_DATA_OUTPUT CalcHSPatchConstants(
	InputPatch<VertexOut, NUM_CONTROL_POINTS> ip,
	uint PatchID : SV_PrimitiveID)
{
    //float3 centerL = (ip[0].PosL + ip[1].PosL + ip[2].PosL) * 0.333f;
    //float3 centerW = mul(float4(centerL, 1.0f), gWorld).xyz;

    //float dist = distance(centerW, gEyePosW);
	
    //float lodStart = 20.0f;
    //float lodEnd = 900.0f;
	
    //float tess = 64.0f * saturate(1.0f - (dist - lodStart) / lodEnd);
    float tess = 64.0f;// * saturate((lodEnd - dist) / (lodEnd - lodStart));
	
	HS_CONSTANT_DATA_OUTPUT Output;
    Output.NormalL = ip[0].NormalL;
	Output.TexC = 

	Output.EdgeTessFactor[0] = tess;
    Output.EdgeTessFactor[1] = tess;
    Output.EdgeTessFactor[2] = tess;
    Output.EdgeTessFactor[3] = tess;
	
	Output.InsideTessFactor[0] = tess;
    Output.InsideTessFactor[1] = tess;

	return Output;
}

[domain("quad")]
[partitioning("integer")]
[outputtopology("triangle_cw")] 
[outputcontrolpoints(4)]
[patchconstantfunc("CalcHSPatchConstants")]
[maxtessfactor(64.0f)]
HS_CONTROL_POINT_OUTPUT main( 
	InputPatch<VertexOut, NUM_CONTROL_POINTS> ip,
	uint i : SV_OutputControlPointID,
	uint PatchID : SV_PrimitiveID )
{
	HS_CONTROL_POINT_OUTPUT Output;

    Output = ip[i];

	return Output;
}
