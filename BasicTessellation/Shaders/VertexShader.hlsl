#include "GraphicsHeader.hlsli"

VertexOut main( VertexIn vin )
{
    VertexOut vout;
    
    vout.PosL = vin.PosL;
	
	return vout;
}