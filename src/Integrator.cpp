#include "../include/Integrator.hpp"

namespace vkpt
{
    Integrator::Integrator(vk2s::Device& device, MaterialTable& matTable)
        : mDevice(device)
        , mMatTable(matTable)
    {
        {
            const auto ubSize = sizeof(SamplingUB);
            mSampleBuffer     = device.create<vk2s::DynamicBuffer>(vk::BufferCreateInfo({}, ubSize, vk::BufferUsageFlagBits::eUniformBuffer), vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, 1);
        }

        mRaygenShader     = device.create<vk2s::Shader>("../../shaders/pathtracing/raygen_mod.rgen", "main");
        mMissShader       = device.create<vk2s::Shader>("../../shaders/pathtracing/miss_mod.rmiss", "main");
        mClosestHitShader = device.create<vk2s::Shader>("../../shaders/pathtracing/closesthit_mod.rchit", "main");

        // create bind layout
        std::array bindings = {
            // 0 : TLAS
            vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eAccelerationStructureKHR, 1, vk::ShaderStageFlagBits::eAll),
            // 1 : result image
            vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eAll),
            // 2 : sample parameters
            vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eUniformBufferDynamic, 1, vk::ShaderStageFlagBits::eAll),
            // 3 : instance mappings
            vk::DescriptorSetLayoutBinding(3, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eAll),
            // 4 : material parameters
            vk::DescriptorSetLayoutBinding(4, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eAll),
            // 5 : material textures
            vk::DescriptorSetLayoutBinding(5, vk::DescriptorType::eCombinedImageSampler, std::max(1ull, mMatTable.getTextures().size()), vk::ShaderStageFlagBits::eAll),
            //// 6 : envmap texture
            //vk::DescriptorSetLayoutBinding(6, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eAll),
            //// 7 : pool image
            //vk::DescriptorSetLayoutBinding(7, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eAll)
        };

        mBindLayout = device.create<vk2s::BindLayout>(bindings);

        // shader groups
        constexpr int indexRaygen     = 0;
        constexpr int indexMiss       = 1;
        constexpr int indexClosestHit = 2;

        // create pipeline
        vk2s::Pipeline::VkRayTracingPipelineInfo rpi{
            .raygenShader = mRaygenShader,
            .missShader   = mMissShader,
            .chitShader   = mClosestHitShader,
            .bindLayout   = mBindLayout,
            .shaderGroups = { vk::RayTracingShaderGroupCreateInfoKHR(vk::RayTracingShaderGroupTypeKHR::eGeneral, indexRaygen, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR),
                              vk::RayTracingShaderGroupCreateInfoKHR(vk::RayTracingShaderGroupTypeKHR::eGeneral, indexMiss, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR),
                              vk::RayTracingShaderGroupCreateInfoKHR(vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup, VK_SHADER_UNUSED_KHR, indexClosestHit, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR) },
        };

        mRaytracePipeline = device.create<vk2s::Pipeline>(rpi);

        mShaderBindingTable = device.create<vk2s::ShaderBindingTable>(mRaytracePipeline.get(), 1, 1, 1, 0, rpi.shaderGroups);

        mCommand = device.create<vk2s::Command>();

        mBindGroup = device.create<vk2s::BindGroup>(mBindLayout.get());

        mMatTable.build(device);

        // set material and textures
        mBindGroup->bind(0, 2, vk::DescriptorType::eUniformBufferDynamic, mSampleBuffer.get());

        mBindGroup->bind(0, 4, vk::DescriptorType::eStorageBuffer, mMatTable.getBuffer().get());
        auto textures = mMatTable.getTextures();
        if (!textures.empty())
        {
            mBindGroup->bind(0, 5, vk::DescriptorType::eCombinedImageSampler, textures, mMatTable.getSampler());
        }
    }

    void Integrator::setTLAS(Handle<vk2s::AccelerationStructure> tlas)
    {
        mBindGroup->bind(0, 0, tlas.get());
    }

    void Integrator::setInstanceMapping(Handle<vk2s::Buffer> instanceMapBuffer)
    {
        mBindGroup->bind(0, 3, vk::DescriptorType::eStorageBuffer, instanceMapBuffer.get());
    }

    void Integrator::setFilm(Film film)
    {
        mBindGroup->bind(0, 1, vk::DescriptorType::eStorageImage, film.getImage());
        mBounds = film.getBounds();
    }

    void Integrator::sample(Sampling& sampling)
    {
        sampling.fence->wait();
        sampling.fence->reset();


        // write SampleUB
        SamplingUB writeData{
            .view     = sampling.camera.getViewMatrix(),
            .proj     = sampling.camera.getProjectionMatrix(),
            .viewInv  = glm::inverse(writeData.view),
            .projInv  = glm::inverse(writeData.proj),
            .spp      = sampling.spp,
            .seed     = sampling.seed,
            .untilSpp = 0,
        };

        mSampleBuffer->write(&writeData, sizeof(SamplingUB));

        // execute command
        // trace ray
        const auto [width, height] = mBounds;

        mCommand->reset();

        mCommand->begin();
        mCommand->setPipeline(mRaytracePipeline);
        mCommand->setBindGroup(mBindGroup.get(), { static_cast<uint32_t>(0) });
        mCommand->traceRays(mShaderBindingTable.get(), width, height, 1);
        mCommand->end();

        mCommand->execute(sampling.fence);
    }
}  // namespace vkpt
