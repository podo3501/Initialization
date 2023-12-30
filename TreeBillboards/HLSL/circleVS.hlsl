#include "header.hlsli"

VertexOut main( Vertex vin )
{
    VertexOut vout;
    
    vout.PosH = vin.PosH;
    vout.NorH = vin.NorH;
    vout.Height = vin.Height;
	
	return vout;
}

sVertexOut SphereVS(sVertex vin)
{
    sVertexOut vout;
    
    vout.PosL = vin.PosL;
    vout.NormalL = vin.NormalL;
    vout.Radius = vin.Radius;

    return vout;
}

nVertexOut NormalVS(nVertex vin)
{
    nVertexOut vout;
    
    vout.PosL = vin.PosL;
    vout.NormalL = vin.NormalL;
    
    return vout;
}