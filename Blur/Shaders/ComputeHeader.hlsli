texture2D                          gInput : register(t0);
RWTexture2D<float4>     gOutput : register(u1);

cbuffer cbSettings : register(b0)
{
    int gBlurRadius;
    
    float w0;
    float w1;
    float w2;
    float w3;
    float w4;
    float w5;
    float w6;
    float w7;
    float w8;
    float w9;
    float w10;
    float w11;
};