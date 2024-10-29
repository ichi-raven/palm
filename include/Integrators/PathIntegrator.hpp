/*****************************************************************//**
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

        virtual void sample(Handle<vk2s::Fence> fence, Handle<vk2s::Command> command) override;

    private:

        struct SceneParams  // std430
        {
            glm::mat4 view;
            glm::mat4 proj;
            glm::mat4 viewInv;
            glm::mat4 projInv;
            glm::vec4 camPos;        
            
            uint32_t sppPerFrame;
            float padding[3];
        };

        struct InstanceParams
        {
            glm::mat4 world;
            glm::mat4 worldInvTrans;
        };

        // can be modified from ImGui
        struct GUIParams
        {
            int spp = 4;
            int accumulatedSpp = 0;
        };

        GUIParams mGUIParams;

        // TLAS
        Handle<vk2s::AccelerationStructure> mTLAS;

        // shader resources
        Handle<vk2s::Buffer> mSceneBuffer;
        Handle<vk2s::Buffer> mInstanceBuffer;
        Handle<vk2s::Buffer> mSampleBuffer;
        Handle<vk2s::Image> mPoolImage;
        std::vector<Handle<vk2s::Buffer>> mVertexBuffers;
        std::vector<Handle<vk2s::Buffer>> mIndexBuffers;

        // binding 
        Handle<vk2s::BindLayout> mBindLayout;
        Handle<vk2s::BindGroup> mBindGroup;

        // pipeline and SBT
        Handle<vk2s::Pipeline> mRaytracePipeline;
        Handle<vk2s::ShaderBindingTable> mShaderBindingTable;
        
        
    };
}  // namespace palm

#endif