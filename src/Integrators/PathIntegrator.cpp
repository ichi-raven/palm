/*****************************************************************/ /**
 * @file   PathIntegrator.cpp
 * @brief  
 * 
 * @author ichi-raven
 * @date   October 2024
 *********************************************************************/

#include "../include/Integrators/PathIntegrator.hpp"

#include "../include/Mesh.hpp"
#include "../include/Material.hpp"
#include "../include/EntityInfo.hpp"
#include "../include/Transform.hpp"

namespace palm
{

    PathIntegrator::PathIntegrator(vk2s::Device& device, ec2s::Registry& scene, Handle<vk2s::Image> output)
        : Integrator(device, scene, output)
    {
        // scene loading

        Handle<vk2s::Buffer> materialBuffer;
        Handle<vk2s::Buffer> emitterBuffer;
        Handle<vk2s::Buffer> triEmitterBuffer;
        Handle<vk2s::Buffer> infiniteEmitterBuffer;
        std::vector<Handle<vk2s::Image>> materialTextures;
        
        auto sampler = device.create<vk2s::Sampler>(vk::SamplerCreateInfo());

        // create scene UB
        Handle<vk2s::DynamicBuffer> sceneBuffer;
        {
            const auto size = sizeof(SceneUB) * frameCount;
            sceneBuffer     = device.create<vk2s::DynamicBuffer>(vk::BufferCreateInfo({}, size, vk::BufferUsageFlagBits::eUniformBuffer), vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, frameCount);
        }

        //create pool image
        {
            const auto format   = vk::Format::eR32G32B32A32Sfloat;
            const auto extent   = mOutputImage->getVkExtent();
            const uint32_t size = extent.width * extent.height * vk2s::Compiler::getSizeOfFormat(format);

            vk::ImageCreateInfo ci;
            ci.arrayLayers   = 1;
            ci.extent        = extent;
            ci.format        = format;
            ci.imageType     = vk::ImageType::e2D;
            ci.mipLevels     = 1;
            ci.usage         = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eStorage;
            ci.initialLayout = vk::ImageLayout::eUndefined;

            // change format to pooling
            mPoolImage = device.create<vk2s::Image>(ci, vk::MemoryPropertyFlagBits::eDeviceLocal, size, vk::ImageAspectFlagBits::eColor);

            UniqueHandle<vk2s::Command> cmd = device.create<vk2s::Command>();
            cmd->begin(true);
            cmd->transitionImageLayout(mPoolImage.get(), vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral);
            cmd->end();
            cmd->execute();
        }

        // deploy instances
        vk::AccelerationStructureInstanceKHR templateDesc{};
        templateDesc.instanceCustomIndex = 0;
        templateDesc.mask                = 0xFF;
        templateDesc.flags               = 0;

        std::vector<vk::AccelerationStructureInstanceKHR> asInstances;
        asInstances.reserve(mScene.size<Mesh>());
        mScene.each<Mesh, Transform>(
            [&](const Mesh& mesh, const Transform& transform)
            {
                const auto& blas                                  = mesh.blas;
                const auto transform                              = glm::mat4(1.f);
                vk::TransformMatrixKHR mtxTransform               = transform.convert(transform);
                vk::AccelerationStructureInstanceKHR asInstance   = templateDesc;
                asInstance.transform                              = mtxTransform;
                asInstance.accelerationStructureReference         = blas->getVkDeviceAddress();
                asInstance.instanceShaderBindingTableRecordOffset = 0;
                asInstances.emplace_back(asInstance);
            });

        // create TLAS
        mTLAS = device.create<vk2s::AccelerationStructure>(asInstances);

        // load shaders
        const auto raygenShader = device.create<vk2s::Shader>("../../shaders/Slang/Integrators/PathIntegrator.slang", "raygenMain");
        const auto missShader   = device.create<vk2s::Shader>("../../shaders/Slang/Integrators/PathIntegrator.slang", "missMain");
        const auto shadowShader = device.create<vk2s::Shader>("../../shaders/Slang/Integrators/PathIntegrator.slang", "shadowMain");
        const auto chitShader   = device.create<vk2s::Shader>("../../shaders/Slang/Integrators/PathIntegrator.slang", "chitMain");

        // create bind layout
        std::array bindings = {
            // 0 : TLAS
            vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eAccelerationStructureKHR, 1, vk::ShaderStageFlagBits::eAll),
            // 1 : result image
            vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eAll),
            // 2 : scene parameters
            vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eUniformBufferDynamic, 1, vk::ShaderStageFlagBits::eAll),
            // 3 : instance mappings
            vk::DescriptorSetLayoutBinding(3, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eAll),
            // 4 : material parameters
            vk::DescriptorSetLayoutBinding(4, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eAll),
            // 5 : material textures
            vk::DescriptorSetLayoutBinding(5, vk::DescriptorType::eCombinedImageSampler, std::max(1ull, materialTextures.size()), vk::ShaderStageFlagBits::eAll),
            // 6 : emitters information buffer
            vk::DescriptorSetLayoutBinding(6, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eAll),
            // 7 : triangle emitters buffer
            vk::DescriptorSetLayoutBinding(7, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eAll),
            // 8 : infinite emitter buffer
            vk::DescriptorSetLayoutBinding(8, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eAll),
            // 9: sampling pool image
            vk::DescriptorSetLayoutBinding(9, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eAll),
        };

        auto bindLayout = device.create<vk2s::BindLayout>(bindings);

        // shader groups
        constexpr int kIndexRaygen     = 0;
        constexpr int kIndexMiss       = 1;
        constexpr int kIndexShadow     = 2;
        constexpr int kIndexClosestHit = 3;

        // create ray tracing pipeline
        vk2s::Pipeline::RayTracingPipelineInfo rpi{
            .raygenShaders = { raygenShader },
            .missShaders   = { missShader, shadowShader },
            .chitShaders   = { chitShader },
            .bindLayouts   = bindLayout,
            .shaderGroups  = { vk::RayTracingShaderGroupCreateInfoKHR(vk::RayTracingShaderGroupTypeKHR::eGeneral, kIndexRaygen, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR),
                               vk::RayTracingShaderGroupCreateInfoKHR(vk::RayTracingShaderGroupTypeKHR::eGeneral, kIndexMiss, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR),
                               vk::RayTracingShaderGroupCreateInfoKHR(vk::RayTracingShaderGroupTypeKHR::eGeneral, kIndexShadow, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR),
                               vk::RayTracingShaderGroupCreateInfoKHR(vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup, VK_SHADER_UNUSED_KHR, kIndexClosestHit, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR) },
        };

        auto raytracePipeline = device.create<vk2s::Pipeline>(rpi);

        // create shader binding table
        
        //auto shaderBindingTable = device.create<vk2s::ShaderBindingTable>(raytracePipeline.get(), 1, 2, 1, 0, rpi.shaderGroups);

        // create bindgroup
        auto bindGroup = device.create<vk2s::BindGroup>(bindLayout.get());
        bindGroup->bind(0, mTLAS.get());
        bindGroup->bind(1, vk::DescriptorType::eStorageImage, resultImage);
        bindGroup->bind(2, vk::DescriptorType::eUniformBufferDynamic, sceneBuffer.get());
        bindGroup->bind(3, vk::DescriptorType::eStorageBuffer, instanceMapBuffer.get());
        bindGroup->bind(4, vk::DescriptorType::eStorageBuffer, materialBuffer.get());
        if (!materialTextures.empty())
        {
            bindGroup->bind(5, vk::DescriptorType::eCombinedImageSampler, materialTextures, sampler);
        }
        bindGroup->bind(6, vk::DescriptorType::eStorageBuffer, emitterBuffer.get());
        bindGroup->bind(7, vk::DescriptorType::eStorageBuffer, triEmitterBuffer.get());
        bindGroup->bind(8, vk::DescriptorType::eStorageBuffer, infiniteEmitterBuffer.get());
        bindGroup->bind(9, vk::DescriptorType::eStorageImage, poolImage);
    }

    PathIntegrator::~PathIntegrator()
    {
    }

    void PathIntegrator::showConfigImGui()
    {
        int spp = 0;
        ImGui::InputInt("spp", &spp);
        mGUIParams.spp = spp;
    }

    void PathIntegrator::sample(Handle<vk2s::Fence> fence, Handle<vk2s::Command> command)
    {
        // trace ray
        command->setPipeline(mRaytracePipeline);
        command->setBindGroup(0, mBindGroup.get(), { static_cast<uint32_t>(now * sceneBuffer->getBlockSize()) });
        command->traceRays(mShaderBindingTable.get(), windowWidth, windowHeight, 1);
    }
}  // namespace palm
