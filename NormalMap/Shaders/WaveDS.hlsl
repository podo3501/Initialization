#include "GraphicsHeader.hlsli"
#include "VertexInout_Wave.hlsli"

[domain("quad")]
DS_OUTPUT main(
	HS_CONSTANT_DATA_OUTPUT input,
	float2 uv : SV_DomainLocation,
	const OutputPatch<HS_CONTROL_POINT_OUTPUT, NUM_CONTROL_POINTS> quad)
{
    
    float3 v1 = lerp(quad[0].PosL, quad[1].PosL, uv.x);
    float3 v2 = lerp(quad[2].PosL, quad[3].PosL, uv.y);
    float3 p = lerp(v1, v2, uv.y);
    
	DS_OUTPUT Output;
	
    MaterialData matData = gMaterialData[gMaterialIndex];
    uint normalMapIndex = matData.NormalMapIndex;
    
    float4 texC = mul(float4(input.TexC, 0.0f, 1.0f), gTexTransform);
    Output.TexC = mul(texC, matData.MatTransform).xy;
    
    float4 normalMapSample = gTextureMaps[normalMapIndex].SampleLevel(gsamAnisotropicWrap, Output.TexC, 0);
    
    p.y = normalMapSample.y;
    
    float4 posW = mul(float4(p, 1.0f), gWorld);
    Output.PosW = posW.xyz;
    Output.PosH = mul(posW, gViewProj);
    Output.NormalW = mul(input.NormalL, (float3x3) gWorld);
    Output.TangentW = mul(input.TangentU, (float3x3) gWorld);
	
	return Output;
}
