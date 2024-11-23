/*****************************************************************/ /**
 * @file   PathIntegrator.hpp
 * @brief  
 * 
 * @author ichi-raven
 * @date   October 2024
 *********************************************************************/
#ifndef PALM_INCLUDE_PATHINTEGRATOR_HPP_
#define PALM_INCLUDE_PATHINTEGRATOR_HPP_

#include "Integrator.hpp"

#include <vk2s/Device.hpp>
#include <vk2s/Camera.hpp>

#include <EC2S.hpp>

#include "Film.hpp"

namespace palm
{
    class PathIntegrator : public Integrator
    {
    public:
        PathIntegrator(vk2s::Device& device, ec2s::Registry& scene, Handle<vk2s::Image> output);

        virtual ~PathIntegrator() override;

        virtual void showConfigImGui() override;

        virtual void updateShaderResources() override;

        virtual void sample(Handle<vk2s::Fence> fence, Handle<vk2s::Command> command) override;

    private:
        struct SceneParams  // std140
        {
            glm::mat4 view;
            glm::mat4 proj;
            glm::mat4 viewInv;
            glm::mat4 projInv;
            glm::vec4 camPos;

            uint32_t sppPerFrame;
            uint32_t areaEmitterNum;
            float padding[2];
        };

        struct InstanceParams
        {
            glm::mat4 world;
            glm::mat4 worldInvTrans;
        };

        // can be modified from ImGui
        struct GUIParams
        {
            int spp            = 1;
            int accumulatedSpp = 0;
        };

        GUIParams mGUIParams;

        // TLAS
        UniqueHandle<vk2s::AccelerationStructure> mTLAS;

        // shader resources
        UniqueHandle<vk2s::Buffer> mSceneBuffer;
        UniqueHandle<vk2s::Buffer> mInstanceBuffer;
        UniqueHandle<vk2s::Buffer> mMaterialBuffer;
        UniqueHandle<vk2s::Buffer> mSampleBuffer;
        UniqueHandle<vk2s::Buffer> mEmittersBuffer;
        UniqueHandle<vk2s::Image> mPoolImage;
        UniqueHandle<vk2s::Sampler> mSampler;
        Handle<vk2s::Image> mDummyTexture;

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