/*****************************************************************/ /**
 * \file   ReSTIRIntegrator.hpp
 * \brief  header file of ReSTIRIntegrator class
 * 
 * \author ichi
 * \date   June 2025
 *********************************************************************/
#ifndef PALM_INCLUDE_RESTIRINTEGRATOR_HPP_
#define PALM_INCLUDE_RESTIRINTEGRATOR_HPP_

#include "Integrator.hpp"
#include "Emitter.hpp"

#include <vk2s/Device.hpp>
#include <vk2s/Camera.hpp>

#include <EC2S.hpp>

namespace palm
{
    class ReSTIRIntegrator : public Integrator
    {
    public:
        // can be modified from ImGui
        struct GUIParams
        {
            int spp            = 1;
            int accumulatedSpp = 0;
            int reservoirSize = 32;  // maximum size of reservoir
        };

    public:
        ReSTIRIntegrator(vk2s::Device& device, ec2s::Registry& scene, Handle<vk2s::Image> output);

        virtual ~ReSTIRIntegrator() override;

        virtual void showConfigImGui() override;

        virtual void updateShaderResources() override;

        virtual void sample(Handle<vk2s::Command> command) override;

        GUIParams& getGUIParamsRef();

    private:
        // shader groups
        constexpr static int kIndexRaygen     = 0;
        constexpr static int kIndexMiss       = 1;
        constexpr static int kIndexShadow     = 2;
        constexpr static int kIndexClosestHit = 3;

    private:
        struct SceneParams  // std140
        {
            glm::mat4 view;
            glm::mat4 proj;
            glm::mat4 viewInv;
            glm::mat4 projInv;
            glm::vec4 camPos;

            uint32_t sppPerFrame;
            uint32_t allEmitterNum;
            uint32_t reservoirSize;
            uint32_t padding;
        };

        struct InstanceParams
        {
            glm::mat4 world;
            glm::mat4 worldInvTrans;
        };

        struct EmitterReservoir
        {
            glm::vec3 pos;
            int32_t type = static_cast<std::underlying_type_t<Emitter::Type>>(Emitter::Type::ePoint);

            int32_t faceNum        = 0;  // for area emitter, the number of faces
            int32_t meshIndex      = -1;
            int32_t primitiveIndex = -1;  // for area emitter, the primitive index of the face
            int32_t padding        = 0;

            glm::vec3 emissive;
            int32_t texIndex = -1;

            float wSum;
            float p;
            uint32_t streamLength;
            uint32_t padding1;
        };

        GUIParams mGUIParams;
        uint32_t mEmitterNum;

        // TLAS
        UniqueHandle<vk2s::AccelerationStructure> mTLAS;

        UniqueHandle<vk2s::Image> mEnvmapPDFImage;

        // shader resources
        UniqueHandle<vk2s::Buffer> mSceneBuffer;
        UniqueHandle<vk2s::Buffer> mInstanceBuffer;
        UniqueHandle<vk2s::Buffer> mMaterialBuffer;
        UniqueHandle<vk2s::Buffer> mSampleBuffer;
        UniqueHandle<vk2s::Buffer> mEmittersBuffer;
        UniqueHandle<vk2s::Buffer> mReservoirBuffer;
        UniqueHandle<vk2s::Image> mPoolImage;
        UniqueHandle<vk2s::Image> mDIImage;
        UniqueHandle<vk2s::Image> mGIImage;
        UniqueHandle<vk2s::Sampler> mSampler;

        // WARN: VB, IB and textures have no ownership
        std::vector<Handle<vk2s::Buffer>> mVertexBuffers;
        std::vector<Handle<vk2s::Buffer>> mIndexBuffers;
        std::vector<Handle<vk2s::Image>> mTextures;

        // binding
        Handle<vk2s::BindLayout> mBindLayout;
        UniqueHandle<vk2s::BindGroup> mBindGroup;

        // pipeline and SBT
        UniqueHandle<vk2s::Pipeline> mRaytracePipeline;
        UniqueHandle<vk2s::ShaderBindingTable> mShaderBindingTable;
    };
}  // namespace palm

#endif