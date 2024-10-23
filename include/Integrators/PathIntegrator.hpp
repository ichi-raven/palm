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

        uint32_t mSpp;

        Handle<vk2s::Shader> mRaygenShader;
        Handle<vk2s::Shader> mClosestHitShader;
        Handle<vk2s::Shader> mMissShader;

        Handle<vk2s::DynamicBuffer> mSampleBuffer;

        Handle<vk2s::BindLayout> mBindLayout;
        Handle<vk2s::BindGroup> mBindGroup;

        Handle<vk2s::Pipeline> mRaytracePipeline;
        Handle<vk2s::ShaderBindingTable> mShaderBindingTable;

    };
}  // namespace palm

#endif