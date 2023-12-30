struct Data
{
    float3 v1;
    float2 v2;
};

struct InputData
{
    float3 vec;
};

struct OutputData
{
    float length;
};

StructuredBuffer<Data> gInputA : register(t0);
StructuredBuffer<Data> gInputB : register(t1);
RWStructuredBuffer<Data> gOutput : register(u0);
ConsumeStructuredBuffer<float> gInputData : register(u0);
AppendStructuredBuffer<OutputData> gOutputData : register(u1);
