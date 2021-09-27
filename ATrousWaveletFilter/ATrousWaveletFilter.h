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
#pragma once
#include "Falcor.h"
#include "FalcorExperimental.h"

using namespace Falcor;

extern "C" __declspec(dllexport) void getPasses(Falcor::RenderPassLibrary& lib);

/** Implementation of HPG 2010 paper: Edge-Avoiding A-Trous Wavelet Transform for Fast Global Illumination.

    This paper focuses on denoising ray tracing results.
 */
class ATrousWaveletFilter : public RenderPass
{
public:
    using SharedPtr = std::shared_ptr<ATrousWaveletFilter>;

    static SharedPtr create(RenderContext* pRenderContext = nullptr, const Dictionary& dict = {});

    virtual std::string getDesc() override;
    virtual Dictionary getScriptingDictionary() override;
    virtual RenderPassReflection reflect(const CompileData& compileData) override;
    virtual void compile(RenderContext* pContext, const CompileData& compileData) override;
    virtual void execute(RenderContext* pRenderContext, const RenderData& renderData) override;
    virtual void renderUI(Gui::Widgets& widget) override;

    // Scripting functions
    void setIterations(int iterations) { mParams.iterations = iterations; }
    void setCPhi(float cPhi) { mParams.cPhi = cPhi; }
    void setNPhi(float nPhi) { mParams.nPhi = nPhi; }
    void setPPhi(float pPhi) { mParams.pPhi = pPhi; }
    int getIterations() const { return mParams.iterations; }
    float getCPhi() const { return mParams.cPhi; }
    float getNPhi() const { return mParams.nPhi; }
    float getPPhi() const { return mParams.pPhi; }    

protected:
    static void registerBindings(pybind11::module& m);
    friend void getPasses(Falcor::RenderPassLibrary& lib);

private:
    ATrousWaveletFilter(const Dictionary& dict);

    struct
    {
        bool useFiltering = true;
        int iterations = 4;

        float2 resolution;

        float cPhi = 10.0f;  ///< Color Phi.
        float nPhi = 128.0f; ///< Normal Phi.
        float pPhi = 10.0f;  ///< Position Phi.
    } mParams;

    Fbo::SharedPtr mpPingPongFbo[2];

    FullScreenPass::SharedPtr mpATrousPass;
};
