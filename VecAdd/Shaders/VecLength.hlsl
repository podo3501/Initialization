#include "Header.hlsli"

[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    
    
    float a = gInputData.Consume();
    //OutputData outputData;
    //outputData.length = length(inputData.vec);
    //gOutputData.Append(outputData);
}