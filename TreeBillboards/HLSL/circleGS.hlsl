#include "Header.hlsli"

[maxvertexcount(4)]
void main(
	line VertexOut gin[2],
	uint primID : SV_PrimitiveID,	
	inout TriangleStream< GSOutput > triStream)
{
    float4 v[4];
    v[0] = float4(gin[0].PosH, 1.0f);
    v[1] = float4(gin[0].PosH, 1.0f);
    v[1].y += gin[0].Height;
    v[2] = float4(gin[1].PosH, 1.0f);
    v[3] = float4(gin[1].PosH, 1.0f);
    v[3].y += gin[1].Height;
    
    float3 n[4];
    n[0] = float3(gin[0].NorH.x, 0.0f, gin[0].NorH.y);
    n[1] = float3(gin[1].NorH.x, 0.0f, gin[1].NorH.y);
    n[2] = float3(gin[0].NorH.x, 0.0f, gin[0].NorH.y);
    n[3] = float3(gin[1].NorH.x, 0.0f, gin[1].NorH.y);
	
	
    GSOutput gout;
    [unroll]
    for (uint i = 0; i < 4; ++i)
    {
        gout.PosH = mul(v[i], gViewProj);
        gout.PosW = v[i].xyz;
        gout.NorW = n[i];
        gout.PrimID = primID;
        
        triStream.Append(gout);
    }
}

float3 MidPoint(float3 v0, float3 v1)
{
    return mul(0.5f, v0 + v1);
}

void SubVertex(float3 v[3], out float3 m[3])
{
    m[0] = MidPoint(v[0], v[1]); //m0
    m[1] = MidPoint(v[1], v[2]); //m1
    m[2] = MidPoint(v[0], v[2]); //m2
}

void OutTriStream(float3 vm, float radius, 
    uint primID : SV_PrimitiveID, inout TriangleStream<GSOutput> triStream)
{
    float3 vn = normalize(vm);
        
    float3 PosL;
    PosL = mul(vn, radius);
        
    float4 PosW;
    PosW = mul(float4(PosL, 1.0f), gWorld);
        
    GSOutput gout;
    gout.PosH = mul(PosW, gViewProj);
    gout.PosW = PosW.xyz;
    gout.NorW = vn;
    gout.PrimID = primID;
        
    triStream.Append(gout);
}

void MakeTriStream(float3 vm[8], float radius, 
    uint primID : SV_PrimitiveID, inout TriangleStream<GSOutput> triStream)
{
    for (int i = 0; i < 8; ++i)
    {
        OutTriStream(vm[i], radius, primID, triStream);
    }
}

void MakeTriStream(float3 vm[3], float radius,
    uint primID : SV_PrimitiveID, inout TriangleStream<GSOutput> triStream)
{
    for (int i = 0; i < 3; ++i)
    {
        OutTriStream(vm[i], radius, primID, triStream);
    }
}

void Subdivide(float3 p[3], float radius, 
    uint primID : SV_PrimitiveID, inout TriangleStream<GSOutput> triStream)
{
    float3 m[3];
    SubVertex(p, m);
    
    float3 vm[8];
    vm[0] = m[0]; //m0
    vm[1] = p[1];
    vm[2] = m[1]; //m1
    vm[3] = p[0];
    vm[4] = m[0]; //m0
    vm[5] = m[2]; //m2
    vm[6] = m[1]; //m1
    vm[7] = p[2];
    
    MakeTriStream(vm, radius, primID, triStream);
}

[maxvertexcount(32)]
void SphereGS(
    triangle sVertexOut gin[3],
    uint primID : SV_PrimitiveID,
	inout TriangleStream<GSOutput> triStream)
{
    float3 toEyeW = gEyePosW - (float1x3) gWorld;
    float distToEye = length(toEyeW);
    
    float3 p[3] = { gin[0].PosL, gin[1].PosL, gin[2].PosL };
    
    if( distToEye > 70.0f )
    {
        //step 1
        MakeTriStream(p, gin[0].Radius, primID, triStream);
    }
    else if( distToEye > 25.0f && distToEye <= 70.0f )
    {
        //step 2
        Subdivide(p, gin[0].Radius, primID, triStream);
    }
    else
    {
        //step 3
        float3 m[3];
        SubVertex(p, m);
        float3 vtxSet[4][3] =
        {
            { p[0], m[0], m[2] },
            { m[0], m[2], m[1] },
            { m[2], m[1], p[2] },
            { m[0], p[1], m[1] },
        };
    
        for (int i = 0; i < 4; ++i)
        {
            Subdivide(vtxSet[i], gin[0].Radius, primID, triStream);
        }
    }
}

[maxvertexcount(3)]
void SphereExplosionGS(
    triangle sVertexOut gin[3],
    uint primID : SV_PrimitiveID,
	inout TriangleStream<GSOutput> triStream)
{
    float radius = gin[0].Radius;
    float3 p[3] = { gin[0].PosL, gin[1].PosL, gin[2].PosL };
    
    float3 triCross = cross(gin[0].PosL - gin[1].PosL, gin[0].PosL - gin[2].PosL);
    float3 nCross = normalize(triCross);
    
    float speed = ((primID % 4) + 2) * 2.5f;
    float3 move = mul(nCross, gTotalTime * speed);
    p[0] += move;
    p[1] += move;
    p[2] += move;
    
    for (int i = 0; i < 3; ++i)
    {
        float3 vn = normalize(p[i]);
        
        float3 PosL;
        PosL = mul(p[i], radius);
        
        float4 PosW;
        PosW = mul(float4(PosL, 1.0f), gWorld);
        
        GSOutput gout;
        gout.PosH = mul(PosW, gViewProj);
        gout.PosW = PosW.xyz;
        gout.NorW = vn;
        gout.PrimID = primID;
        
        triStream.Append(gout);
    }
}

//vertex normal
[maxvertexcount(2)]
void VertexNormalGS(
	point nVertexOut gin[1],
	uint primID : SV_PrimitiveID,
	inout LineStream<VertexNormalGSOutput> lineStream)
{    
    float3 p[2];
    float3 normal = normalize(gin[0].NormalL);
    p[0] = gin[0].PosL;
    p[1] = gin[0].PosL + mul(normal, 0.5f);
    
    for (int i = 0; i < 2; ++i)
    {
        float4 PosW;
        PosW = mul(float4(p[i], 1.0f), gWorld);
        
        VertexNormalGSOutput gout;
        gout.PosH = mul(PosW, gViewProj);
        gout.PrimID = primID;

        lineStream.Append(gout);
    }
}

//face normal
[maxvertexcount(2)]
void FaceNormalGS(
    triangle nVertexOut gin[3],
    inout LineStream<FaceNormalGSOutput> lineStream)
{  
    float3 triCross = cross(gin[0].PosL - gin[1].PosL, gin[0].PosL - gin[2].PosL);
    float3 normal = normalize(triCross);
    
    float3 p[2];
    p[0] = (gin[0].PosL + gin[1].PosL + gin[2].PosL) / 3.0f;
    p[1] = p[0] + mul(normal, 0.5f);

    for (int i = 0; i < 2; ++i)
    {
        float4 PosW;
        PosW = mul(float4(p[i], 1.0f), gWorld);
        
        FaceNormalGSOutput gout;
        gout.PosH = mul(PosW, gViewProj);

        lineStream.Append(gout);
    }
}
