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
#include "../HimeTracer.h"
#include "RenderPasses/Shared/PathTracer/PathTracer.h"

namespace Falcor
{
    /** Forward path tracer using a megakernel in DXR 1.0.

        The path tracer has a loop over the path vertices in the raygen shader.
        The kernel terminates when all paths have terminated.

        This pass implements a forward path tracer with next-event estimation,
        Russian roulette, and multiple importance sampling (MIS) with sampling
        of BRDFs and light sources.

        Extension: Support many-light sampling with provided emissive triangle
        texture. Currently the number of lights per pixel is controlled by
        "mMaxRaysPerPixel".
    */

    class himetracerdlldecl HimePathTracer : public PathTracer
    {
    public:
        using SharedPtr = std::shared_ptr<HimePathTracer>;

        static SharedPtr create(RenderContext* pRenderContext, const Dictionary& dict);

        virtual RenderPassReflection reflect(const CompileData& compileData) override;
        Dictionary getScriptingDictionary() override;
        virtual std::string getDesc() override { return sDesc; }
        virtual void setScene(RenderContext* pRenderContext, const Scene::SharedPtr& pScene) override;
        virtual void renderUI(Gui::Widgets& widget) override;
        virtual void execute(RenderContext* pRenderContext, const RenderData& renderData) override;

        // Scripting functions
        void setUseGroundTruthShadowRay(bool useGroundTruthShadowRay) { mTracerParams.useGroundTruthShadowRay = useGroundTruthShadowRay; }
        void setUseReflectionRay(bool useReflectionRay) { mTracerParams.useReflectionRay = useReflectionRay; }
        void setIgnoreShadowRayVisibility(bool ignoreShadowRayVisibility) { mTracerParams.ignoreShadowRayVisibility = ignoreShadowRayVisibility; }
        void setAccumulateShadowRay(bool accumulateShadowRay) { mTracerParams.accumulateShadowRay = accumulateShadowRay; }
        void setSampleWithProvidedUV(bool sampleWithProvidedUV) { mTracerParams.sampleWithProvidedUV = sampleWithProvidedUV; }
        bool getUseGroundTruthShadowRay() const { return mTracerParams.useGroundTruthShadowRay; }
        bool getUseReflectionRay() const { return mTracerParams.useReflectionRay; }
        bool getIgnoreShadowRayVisibility() const { return mTracerParams.ignoreShadowRayVisibility; }
        bool getAccumulateShadowRay() const { return mTracerParams.accumulateShadowRay; }
        bool getSampleWithProvidedUV() const { return mTracerParams.sampleWithProvidedUV; }

        static const char* sDesc;

    protected:
        struct Params
        {
            bool useGroundTruthShadowRay = true;
            bool useReflectionRay = true;
            bool ignoreShadowRayVisibility = false; ///< Whether all shadow rays are visible and ray tracing needed.
            bool accumulateShadowRay = false;       ///< All shadow ray in same sample(same spp) will be accumulated.
            bool sampleWithProvidedUV = false;      ///< Whether sample emissive triangle with provided uv.
            bool isLightsPerPixelChanged = false;   ///< Bool to notify listeners emissive triangle texture needs to update.
            bool enableDebugTexture = false;        ///< Bool to enable update debug texture.
            uint lightsPerPixel = 1;                ///< Lights per pixel.
            const uint kMaxLightsPerPixel = 8;      ///< Upper bound of lights per pixel.
        } mTracerParams;

        static void registerBindings(pybind11::module& m);
        friend void ::getPasses(Falcor::RenderPassLibrary& lib); // Do not remove namespace operator here.

        HimePathTracer(const Dictionary& dict);

        /** This function will update emissive if <b>mTracerParams.useGroundTruthShadowRay is false</b>.

            \note <b>mTracerParams.useGroundTruthShadowRay</b> has alread be bound to:\n
            Hime Path Tracer Params -> Ray Configurations -> Use ground truth shadow ray
         */
        virtual void updateEmissiveTriangleTexture(RenderContext* pRenderContext, const RenderData& renderData);

        /** This function will update debug texture if <b>mTracerParams.enableDebugTexture is true</b>.

            \note Remember to bind <b>mTracerParams.enableDebugTexture</b> to UI.
         */
        virtual void updateDebugTexture(RenderContext* pRenderContext, const RenderData& renderData);

        Texture::SharedPtr getEmissiveTriangleTexture(const RenderData& renderData);
        Texture::SharedPtr getDebugTexture(const RenderData& renderData);
        Texture::SharedPtr getPositionTexture(const RenderData& renderData);

        void recreateVars() override { mTracer.pVars = nullptr; }

    private:
        void prepareVars();
        void setTracerData(const RenderData& renderData);
        void setStaticParams(Program* pProgram) const override;
        void clearTextures(RenderContext* pRenderContext, const RenderData& renderData) const;

        // Ray tracing program.
        struct
        {
            RtProgram::SharedPtr pProgram;
            RtBindingTable::SharedPtr pBindingTable;
            RtProgramVars::SharedPtr pVars;
            ParameterBlock::SharedPtr pParameterBlock;      ///< ParameterBlock for all data.
        } mTracer;
    };
}
