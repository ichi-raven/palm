#ifndef VKPT_INCLUDE_INTEGRATOR_HPP_
#define VKPT_INCLUDE_INTEGRATOR_HPP_

#include <vk2s/Device.hpp>
#include <vk2s/Camera.hpp>

#include "Film.hpp"
#include "MaterialTable.hpp"

namespace vkpt
{
    class Integrator
    {
    public:
        struct Sampling
        {
            uint32_t spp;
            uint32_t seed;
            Handle<vk2s::Fence> fence;
            Handle<vk2s::Image> sampleMap;
            vk2s::Camera camera;
        };

    public:
        Integrator(vk2s::Device& device, MaterialTable& matTable);

        void build();
        
        void setTLAS(Handle<vk2s::AccelerationStructure> tlas);

        void setInstanceMapping(Handle<vk2s::Buffer> instanceMapBuffer);

        void setFilm(Film film);

        void sample(Sampling& sampling);

    private:
        struct SamplingUB
        {
            glm::mat4 view;
            glm::mat4 proj;
            glm::mat4 viewInv;
            glm::mat4 projInv;
            uint32_t spp;
            uint32_t seed;
            uint32_t untilSpp;
            uint32_t padding;
        };

    private:

        vk2s::Device& mDevice;
        MaterialTable& mMatTable;

        std::pair<uint32_t, uint32_t> mBounds;

        Handle<vk2s::Shader> mRaygenShader;
        Handle<vk2s::Shader> mClosestHitShader;
        Handle<vk2s::Shader> mMissShader;

        Handle<vk2s::DynamicBuffer> mSampleBuffer;

        Handle<vk2s::BindLayout> mBindLayout;
        Handle<vk2s::BindGroup> mBindGroup;

        Handle<vk2s::Pipeline> mRaytracePipeline;
        Handle<vk2s::ShaderBindingTable> mShaderBindingTable;

        Handle<vk2s::Command> mCommand;
    };
}  // namespace vkpt

#endif