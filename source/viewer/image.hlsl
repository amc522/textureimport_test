struct VsInput
{
    uint vertexId : SV_VertexID;
};

struct VsOutput
{
    float4 clipPosition : SV_Position;
    float2 uv : TEXCOORD;
};

cbuffer Constants : register(b0)
{
    float screenAspect;
    float textureAspect;
    uint arraySlice;
    uint face;
    uint mip;
    float volumeW;
    uint pointSample;
};

#ifdef TEXTURE_1D
    Texture1D<float4> gTexture : register(t0);
#elif defined(TEXTURE_1D_ARRAY)
    Texture1DArray<float4> gTexture : register(t0);
#elif defined(TEXTURE_2D)
    Texture2D<float4> gTexture : register(t0);
#elif defined(TEXTURE_2D_ARRAY)
    Texture2DArray<float4> gTexture : register(t0);
#elif defined(TEXTURE_CUBE)
    TextureCube<float4> gTexture : register(t0);
#elif defined(TEXTURE_CUBE_ARRAY)
    TextureCubeArray<float4> gTexture : register(t0);
#elif defined(TEXTURE_3D)
    Texture3D<float4> gTexture : register(t0);
#endif

SamplerState gPointSampler : register(s0);
SamplerState gTrilinearSampler : register(s1);

VsOutput vsMain(VsInput input)
{
    VsOutput output;
    output.clipPosition.zw = float2(0.5f, 1.0f);

    float2 topLeft;
    float2 bottomRight;
    
    float2 size = float2(textureAspect * 2.0f, 2.0f);
    size = (screenAspect > 1.0f) ? float2(size.x / screenAspect, size.y) : float2(size.x / screenAspect, size.y);
    size = (size.x > 2.0f) ? size * (2.0f / size.x) : size;
    size = (size.y > 2.0f) ? size * (2.0f / size.y) : size;

    topLeft = float2(-size.x * 0.5, size.y * 0.5);
    bottomRight = float2(size.x * 0.5, -size.y * 0.5);
    
    const float2 topRight = float2(bottomRight.x, topLeft.y);
    const float2 bottomLeft = float2(topLeft.x, bottomRight.y);

    switch(input.vertexId)
    {
    case 0:
        output.clipPosition.xy = topLeft;
        output.uv = float2(0.0f, 0.0f);
        break;
    case 1:
        output.clipPosition.xy = topRight;
        output.uv = float2(1.0f, 0.0f);
        break;
    case 2:
        output.clipPosition.xy = bottomRight;
        output.uv = float2(1.0f, 1.0f);
        break;
    case 3:
        output.clipPosition.xy = bottomLeft;
        output.uv = float2(0.0f, 1.0f);
        break;
    }

    return output;
}

float3 generateCubemapVector(float2 uv)
{
    //1 = u^2 + b^2;
    //sqrt(1 - u^2) = b;

    const float sin45 = sqrt(2.0f) / 2.0f;
    float2 unnormalizedUv = (uv * 2.0f - 1.0f) * sin45;
    unnormalizedUv.y = -unnormalizedUv.y;
    const float2 unnormalizedUvSqr = unnormalizedUv * unnormalizedUv;
    const float uvLength = sqrt(unnormalizedUvSqr.x + unnormalizedUvSqr.y);

    switch(face)
    {
    case 0: // +x
        return float3(sqrt(1.0f - uvLength), unnormalizedUv.y, -unnormalizedUv.x);
    case 1: // -x
        return float3(-sqrt(1.0f - uvLength), unnormalizedUv.y, unnormalizedUv.x);
    case 2: // +y
        return float3(unnormalizedUv.x, sqrt(1.0f - uvLength), -unnormalizedUv.y);
    case 3: // -y
        return float3(unnormalizedUv.x, -sqrt(1.0f - uvLength), unnormalizedUv.y);
    case 4: // +z
        return float3(unnormalizedUv.x, unnormalizedUv.y, sqrt(1.0f - uvLength));
    case 5: // -z
        return float3(-unnormalizedUv.x, unnormalizedUv.y, -sqrt(1.0f - uvLength));
    }

    return float3(0.0f, 0.0f, 0.0f);
}

float4 psMain(VsOutput input) : SV_Target
{
    if(pointSample)
    {
    #ifdef TEXTURE_1D
        return gTexture.SampleLevel(gPointSampler, input.uv.x, (float)mip);
    #elif defined(TEXTURE_1D_ARRAY)
        return gTexture.SampleLevel(gPointSampler, float2(input.uv.x, (float)arraySlice), (float)mip);
    #elif defined(TEXTURE_2D)
        return gTexture.SampleLevel(gPointSampler, float2(input.uv), (float)mip);
    #elif defined(TEXTURE_2D_ARRAY)
        return gTexture.SampleLevel(gPointSampler, float3(input.uv, (float)arraySlice), (float)mip);
    #elif defined(TEXTURE_CUBE)
        return gTexture.SampleLevel(gPointSampler, generateCubemapVector(input.uv), (float)mip);
    #elif defined(TEXTURE_CUBE_ARRAY)
        return gTexture.SampleLevel(gPointSampler, float4(generateCubemapVector(input.uv), (float)arraySlice), (float)mip);
    #elif defined(TEXTURE_3D)
        return gTexture.SampleLevel(gPointSampler, float3(input.uv, volumeW), (float)mip);
    #endif
    }
    else
    {
    #ifdef TEXTURE_1D
        return gTexture.SampleLevel(gTrilinearSampler, input.uv.x, (float)mip);
    #elif defined(TEXTURE_1D_ARRAY)
        return gTexture.SampleLevel(gTrilinearSampler, float2(input.uv.x, (float)arraySlice), (float)mip);
    #elif defined(TEXTURE_2D)
        return gTexture.SampleLevel(gTrilinearSampler, float2(input.uv), (float)mip);
    #elif defined(TEXTURE_2D_ARRAY)
        return gTexture.SampleLevel(gTrilinearSampler, float3(input.uv, (float)arraySlice), (float)mip);
    #elif defined(TEXTURE_CUBE)
        return gTexture.SampleLevel(gTrilinearSampler, generateCubemapVector(input.uv), (float)mip);
    #elif defined(TEXTURE_CUBE_ARRAY)
        return gTexture.SampleLevel(gTrilinearSampler, float4(generateCubemapVector(input.uv), (float)arraySlice), (float)mip);
    #elif defined(TEXTURE_3D)
        return gTexture.SampleLevel(gTrilinearSampler, float3(input.uv, volumeW), (float)mip);
    #endif
    }
}