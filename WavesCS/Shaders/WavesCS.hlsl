cbuffer wavesSetting
{
    float mK1;
    float mK2;
    float mK3;
    
    int row;
    int col;
    float magnitude;
};

RWTexture2D<float> gPrevSolInput : register(u0);
RWTexture2D<float> gCurrSolInput : register(u1);
RWTexture2D<float> gOutput : register(u2);

[numthreads(16, 16, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{  
    int x = DTid.x;
    int y = DTid.y;

    gOutput[int2(x, y)] =
	    mK1 * gPrevSolInput[int2(x, y)] +
	    mK2 * gCurrSolInput[int2(x, y)] +
	    mK3 * (gCurrSolInput[int2(x + 1, y)] +
		    gCurrSolInput[int2(x - 1, y)] +
		    gCurrSolInput[int2(x, y + 1)] +
		    gCurrSolInput[int2(x, y - 1)]);
}

[numthreads(1, 1, 1)]
void disturbCS(uint3 DTid : SV_DispatchThreadID)
{
    float halfMag = 0.5f * magnitude;

    gCurrSolInput[int2(row, col)] += magnitude;
    gCurrSolInput[int2(row, col + 1)] += halfMag;
    gCurrSolInput[int2(row, col - 1)] += halfMag;
    gCurrSolInput[int2(row + 1, col)] += halfMag;
    gCurrSolInput[int2(row - 1, col)] += halfMag;
}