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
#include "ATrousWaveletFilter.h"
#include "RenderGraph/RenderPassHelpers.h"

namespace
{
    const char kDesc[] = "Implementation of \"Edge-Avoiding A-Trous Wavelet Transform for Fast Global Illumination Filtering\"";

    const char kATrousFile[] = "RenderPasses/Hime/ATrousWaveletFilter/ATrous.ps.slang";

    const char kColorInput[] = "color";
    const char kColorTexName[] = "gColorMap";
    const ChannelList kInputChannels = {
        { kColorInput, kColorTexName , "Ray tracing output."     , false, ResourceFormat::RGBA32Float },
        { "normal"   , "gNormalMap"  , "Normal in world space."  , false, ResourceFormat::RGBA32Float },
        { "position" , "gPosMap"     , "Position in world space.", false, ResourceFormat::RGBA32Float },
    };

    const ChannelDesc kOutput = {"filtered color", "gFilteredColor", "Filtered image.", false, ResourceFormat::RGBA16Float };

    const char kIterations[] = "iterations";
    const char kColorPhi[] = "color phi";
    const char kNormalPhi[] = "normal phi";
    const char kPositionPhi[] = "position phi";
}

// Don't remove this. it's required for hot-reload to function properly
extern "C" __declspec(dllexport) const char* getProjDir()
{
    return PROJECT_DIR;
}

extern "C" __declspec(dllexport) void getPasses(Falcor::RenderPassLibrary& lib)
{
    lib.registerClass("ATrousWaveletFilter", kDesc, ATrousWaveletFilter::create);

    ScriptBindings::registerBinding(&ATrousWaveletFilter::registerBindings);
}

ATrousWaveletFilter::SharedPtr ATrousWaveletFilter::create(RenderContext* pRenderContext, const Dictionary& dict)
{
    return SharedPtr(new ATrousWaveletFilter(dict));;
}

std::string ATrousWaveletFilter::getDesc() { return kDesc; }

Dictionary ATrousWaveletFilter::getScriptingDictionary()
{
    Dictionary d;
    d[kIterations] = mParams.iterations;
    d[kColorPhi] = mParams.cPhi;
    d[kNormalPhi] = mParams.nPhi;
    d[kPositionPhi] = mParams.pPhi;
    return d;
}

void ATrousWaveletFilter::registerBindings(pybind11::module& m)
{
    pybind11::class_<ATrousWaveletFilter, RenderPass, ATrousWaveletFilter::SharedPtr> pass(m, "ATrousWaveletFilter");
    pass.def_property(kIterations, &ATrousWaveletFilter::getIterations, &ATrousWaveletFilter::setIterations);
    pass.def_property(kColorPhi, &ATrousWaveletFilter::getCPhi, &ATrousWaveletFilter::setCPhi);
    pass.def_property(kNormalPhi, &ATrousWaveletFilter::getNPhi, &ATrousWaveletFilter::setNPhi);
    pass.def_property(kPositionPhi, &ATrousWaveletFilter::getPPhi, &ATrousWaveletFilter::setPPhi);
}

ATrousWaveletFilter::ATrousWaveletFilter(const Dictionary& dict)
{
    for (const auto& [key, value] : dict)
    {
        if (key == kIterations) mParams.iterations = value;
        else if (key == kColorPhi) mParams.cPhi = value;
        else if (key == kNormalPhi) mParams.nPhi = value;
        else if (key == kPositionPhi) mParams.pPhi = value;
    }

    mpATrousPass = FullScreenPass::create(kATrousFile);
}

RenderPassReflection ATrousWaveletFilter::reflect(const CompileData& compileData)
{
    RenderPassReflection reflector;

    addRenderPassInputs(reflector, kInputChannels);

    reflector.addOutput(kOutput.name, kOutput.desc)
        .format(kOutput.format)
        .bindFlags(Resource::BindFlags::ShaderResource | ResourceBindFlags::RenderTarget);

    return reflector;
}

void ATrousWaveletFilter::compile(RenderContext* context, const CompileData& compileData)
{
    uint2 dims = compileData.defaultTexDims;

    mParams.resolution = float2(dims.x, dims.y);

    Fbo::Desc desc;
    desc.setColorTarget(0, ResourceFormat::RGBA32Float);
    mpPingPongFbo[0] = Fbo::create2D(dims.x, dims.y, desc);
    mpPingPongFbo[1] = Fbo::create2D(dims.x, dims.y, desc);
}

void ATrousWaveletFilter::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    { // Clear Fbos.
        pRenderContext->clearFbo(mpPingPongFbo[0].get(), float4(0), 1.0f, 0, FboAttachmentType::All);
        pRenderContext->clearFbo(mpPingPongFbo[1].get(), float4(0), 1.0f, 0, FboAttachmentType::All);
    }

    Texture::SharedPtr pColorTexture = renderData[kColorInput]->asTexture();
    Texture::SharedPtr pOutputTexture = renderData[kOutput.name]->asTexture();

    if (mParams.useFiltering)
    {
        PROFILE("A-Trous Filtering");

        mpATrousPass.getRootVar()["PerFrameCB"]["gCPhi"] = mParams.cPhi;
        mpATrousPass.getRootVar()["PerFrameCB"]["gNPhi"] = mParams.nPhi;
        mpATrousPass.getRootVar()["PerFrameCB"]["gPPhi"] = mParams.pPhi;
        mpATrousPass.getRootVar()["PerFrameCB"]["gResolution"] = mParams.resolution;

        // Bind color, normal and position.
        for (const auto& input : kInputChannels)
        {
            mpATrousPass.getRootVar()[input.texname] = renderData[input.name]->asTexture();
        }

        pRenderContext->blit(pColorTexture->getSRV(), mpPingPongFbo[0]->getColorTexture(0)->getRTV());

        for (int i = 0; i < mParams.iterations; i++)
        {
            mpATrousPass.getRootVar()[kColorTexName] = mpPingPongFbo[0]->getColorTexture(0);
            mpATrousPass.getRootVar()["PerFrameCB"]["gStepSize"] = 1 << i;

            mpATrousPass->execute(pRenderContext, mpPingPongFbo[1]);

            // Notice that output value stores in ping poing fbo 0.
            std::swap(mpPingPongFbo[0], mpPingPongFbo[1]);
        }

        pRenderContext->blit(mpPingPongFbo[0]->getColorTexture(0)->getSRV(), pOutputTexture->getRTV());
    }
    else
    {
        pRenderContext->blit(pColorTexture->getSRV(), pOutputTexture->getRTV());
    }
}

void ATrousWaveletFilter::renderUI(Gui::Widgets& widget)
{
    widget.checkbox("Enable A-Trous filtering", mParams.useFiltering);

    widget.var("Iterations", mParams.iterations, 2, 10, 1);
    widget.var("Color Phi", mParams.cPhi, 0.0f, 10000.0f, 0.01f);
    widget.var("Normal Phi", mParams.nPhi, 0.0f, 10000.0f, 0.01f);
    widget.var("Position Phi", mParams.pPhi, 0.0f, 10000.0f, 0.01f);
}
