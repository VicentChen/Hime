import ReservoirData;
import ReSTIRHelpers;

#ifndef CHUNK_SIZE
    // Compile-time error if CHUNK_SIZE is not defined.
    #error CHUNK_SIZE is not defined. Add define in cpp file.
#endif

RWStructuredBuffer<PackedReservoirData> gReservoirBuffer;
RWTexture2DArray<float4> gLightTexture;

cbuffer PerFrameCB
{
    // compute shader parameters
    uint2 dispatchDim;
    uint frameCount;
}

[numthreads(CHUNK_SIZE, CHUNK_SIZE, 1)]
void generateLightTexture(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 launchIdx = dispatchThreadId.xy;
    uint2 launchDim = dispatchDim;
    if (any(launchIdx >= launchDim)) return;

    ReservoirData reservoir = loadReservoir(gReservoirBuffer, launchIdx, launchDim);

    uint lightIndex = reservoir.getLightIndex();
    float lightPdf = rcp(reservoir.getInvPdf());
    float2 lightUV = reservoir.getSampleUV();
    gLightTexture[uint3(launchIdx, 0)] = float4((float)lightIndex, lightPdf, lightUV);
}
