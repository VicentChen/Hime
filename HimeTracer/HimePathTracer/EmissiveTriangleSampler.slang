/***************************************************************************
 # Copyright (c) 2015-21, NVIDIA CORPORATION. All rights reserved.
 #
 # Redistribution and use in source and binary forms, with or without
 # modification, are permitted provided that the following conditions
 # are met:
 #  * Redistributions of source code must retain the above copyright
 #    notice, this list of conditions and the following disclaimer.
 #  * Redistributions in binary form must reproduce the above copyright
 #    notice, this list of conditions and the following disclaimer in the
 #    documentation and/or other materials provided with the distribution.
 #  * Neither the name of NVIDIA CORPORATION nor the names of its
 #    contributors may be used to endorse or promote products derived
 #    from this software without specific prior written permission.
 #
 # THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS "AS IS" AND ANY
 # EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 # IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 # PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 # CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 # EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 # PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 # PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 # OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 # (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 # OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **************************************************************************/
#include "Utils/Math/MathConstants.slangh"

import Scene.Scene;
import Utils.Sampling.SampleGeneratorInterface;
import Experimental.Scene.Lights.EmissiveLightSamplerHelpers;
import Experimental.Scene.Lights.EmissiveLightSamplerInterface;

/** Emissive light sampler which samples the provided emissive triangle.

    The struct wraps a LightCollection that stores the pre-processed lights.
    The program should instantiate the struct below. See EmissiveLightSampler.slang.
*/
struct EmissiveTriangleSampler
{
    uint4 _dummy;   ///< Dummy data necessary to have EmissiveUniformSampler as member on another struct.

    /** Draw a single light sample.
        \param[in] posW Shading point in world space.
        \param[in] normalW Normal at the shading point in world space.
        \param[in] triangleIndex Chosen triangle.
        \param[in] trianglePdf Pdf of chosen triangle.
        \param[in,out] sg Sample generator.
        \param[out] ls Light sample. Only valid if true is returned.
        \return True if a sample was generated, false otherwise.
    */
    bool sampleLight<S : ISampleGenerator>(const float3 posW, const float3 normalW, const uint triangleIndex, const float trianglePdf, inout S sg, out TriangleLightSample ls)
    {
        if (gScene.lightCollection.getActiveTriangleCount() == 0) return false;

        uint triangleCount = gScene.lightCollection.getTriangleCount();
        float triangleSelectionPdf = trianglePdf;

        // Sample the triangle uniformly.
        float2 uv = sampleNext2D(sg);
        if (!sampleTriangle(posW, triangleIndex, uv, ls)) return false;

        // The final probability density is the product of the sampling probabilities.
        ls.pdf *= triangleSelectionPdf;
        return true;
    }

    /** Draw a single light sample with provided uv.
        \param[in] posW Shading point in world space.
        \param[in] normalW Normal at the shading point in world space.
        \param[in] triangleIndex Chosen triangle.
        \param[in] trianglePdf Pdf of chosen triangle.
        \param[in] uv Random float2 for point selection.
        \param[out] ls Light sample. Only valid if true is returned.
        \return True if a sample was generated, false otherwise.
    */
    bool sampleLightWithProvidedUV(const float3 posW, const float3 normalW, const uint triangleIndex, const float trianglePdf, const float2 uv, out TriangleLightSample ls)
    {
        if (gScene.lightCollection.getActiveTriangleCount() == 0) return false;

        uint triangleCount = gScene.lightCollection.getTriangleCount();
        float triangleSelectionPdf = trianglePdf;

        // Sample the triangle uniformly.
        if (!sampleTriangle(posW, triangleIndex, uv, ls)) return false;

        // The final probability density is the product of the sampling probabilities.
        ls.pdf *= triangleSelectionPdf;
        return true;
    }
};
