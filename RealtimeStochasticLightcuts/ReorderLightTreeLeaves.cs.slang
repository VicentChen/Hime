#include "LightTreeData.slangh"

#ifndef GROUP_SIZE
    // Compile-time error if GROUP_SIZE is not defined.
    #error GROUP_SIZE is not defined. Add define in cpp file.
#endif

cbuffer PerFrameCB
{
    uint lightCount = 0;
    uint levelCount = 0;
}

RWStructuredBuffer<LightTreeNode> gLightTree;
StructuredBuffer<LightTreeNode> gSortingHelper;
StructuredBuffer<uint2> gSortingKeyIndex;

[numthreads(GROUP_SIZE, 1, 1)]
void reorderLightTreeLeaves(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint threadId = dispatchThreadId.x;
    if (threadId >= lightCount) return;

    uint nodeOffset = getCurrentLevelNodeStartIdx(levelCount - 1);
    uint nodeIdx = nodeOffset + threadId;

    int index = gSortingKeyIndex[threadId].x;
    gLightTree[nodeIdx] = gSortingHelper[index];
}
