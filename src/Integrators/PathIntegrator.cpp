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

        //Handle<vk2s::Buffer> materialBuffer;
        //Handle<vk2s::Buffer> emitterBuffer;
        //Handle<vk2s::Buffer> triEmitterBuffer;
        //Handle<vk2s::Buffer> infiniteEmitterBuffer;
        //std::vector<Handle<vk2s::Image>> materialTextures;

        const auto extent = mOutputImage->getVkExtent();
        auto sampler      = device.create<vk2s::Sampler>(vk::SamplerCreateInfo());

        // create scene UB
        {
            const auto size = sizeof(SceneParams);
            mSceneBuffer    = device.create<vk2s::Buffer>(vk::BufferCreateInfo({}, size, vk::BufferUsageFlagBits::eUniformBuffer), vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

            glm::mat4 view, proj;
            glm::vec3 camPos;
            mScene.each<vk2s::Camera>(
                [&](const vk2s::Camera& camera)
                {
                    view   = camera.getViewMatrix();
                    proj   = camera.getProjectionMatrix();
                    camPos = camera.getPos();
                });

            SceneParams params{
                .view       = view,
                .proj       = proj,
                .viewInv    = glm::inverse(view),
                .projInv    = glm::inverse(proj),
                .camPos     = glm::vec4(camPos, 1.0f),
                .lightDir   = glm::normalize(glm::vec4(1.f, 1.f, 1.f, 0.f)),
            };

            mSceneBuffer->write(&params, sizeof(SceneParams));
        }

        //create pool image
        {
            const auto format   = vk::Format::eR32G32B32A32Sfloat;
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
        {
            mScene.each<Mesh, Transform>(
                [&](const Mesh& mesh, const Transform& transform)
                {
                    const auto& blas                                  = mesh.blas;
                    vk::AccelerationStructureInstanceKHR asInstance   = templateDesc;
                    asInstance.transform                              = transform.params.convert();
                    asInstance.accelerationStructureReference         = blas->getVkDeviceAddress();
                    asInstance.instanceShaderBindingTableRecordOffset = 0;
                    asInstances.emplace_back(asInstance);
                });
        }

        // create TLAS
        mTLAS = device.create<vk2s::AccelerationStructure>(asInstances);

        // load shaders
        const auto raygenShader = device.create<vk2s::Shader>("../../shaders/Slang/Integrators/PathIntegrator.slang", "rayGenShader");
        const auto missShader   = device.create<vk2s::Shader>("../../shaders/Slang/Integrators/PathIntegrator.slang", "missShader");
        const auto shadowShader = device.create<vk2s::Shader>("../../shaders/Slang/Integrators/PathIntegrator.slang", "shadowMissShader");
        const auto chitShader   = device.create<vk2s::Shader>("../../shaders/Slang/Integrators/PathIntegrator.slang", "closestHitShader");

        // create bind layout
        const auto meshNum  = mScene.size<Mesh>();
        std::array bindings = {
            // 0 : TLAS
            vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eAccelerationStructureKHR, 1, vk::ShaderStageFlagBits::eAll),
            // 1 : result image
            vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eAll),
            // 2 : scene parameters
            vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eAll),
            // 3: vertex buffers
            vk::DescriptorSetLayoutBinding(3, vk::DescriptorType::eStorageBuffer, meshNum, vk::ShaderStageFlagBits::eAll),
            // 4: index buffers
            vk::DescriptorSetLayoutBinding(4, vk::DescriptorType::eStorageBuffer, meshNum, vk::ShaderStageFlagBits::eAll),
        };

        mBindLayout = device.create<vk2s::BindLayout>(bindings);

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
            .bindLayouts   = mBindLayout,
            .shaderGroups  = { vk::RayTracingShaderGroupCreateInfoKHR(vk::RayTracingShaderGroupTypeKHR::eGeneral, kIndexRaygen, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR),
                               vk::RayTracingShaderGroupCreateInfoKHR(vk::RayTracingShaderGroupTypeKHR::eGeneral, kIndexMiss, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR),
                               vk::RayTracingShaderGroupCreateInfoKHR(vk::RayTracingShaderGroupTypeKHR::eGeneral, kIndexShadow, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR),
                               vk::RayTracingShaderGroupCreateInfoKHR(vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup, VK_SHADER_UNUSED_KHR, kIndexClosestHit, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR) },
        };

        mRaytracePipeline = device.create<vk2s::Pipeline>(rpi);

        // create shader binding table

        mShaderBindingTable = device.create<vk2s::ShaderBindingTable>(mRaytracePipeline.get(), 1, 2, 1, 0, rpi.shaderGroups);
        //mShaderBindingTable = [&]()
        //{
        //    vk2s::ShaderBindingTable::RegionInfo raygenInfo{
        //        .shaderTypeNum       = 1,
        //        .entryNum            = 1,
        //        .additionalEntrySize = 0,
        //        .entryWriter         = [](std::byte* pDst, std::byte* pSrc, const uint32_t handleSize, const uint32_t alignedHandleSize) { std::memcpy(pDst, pSrc, handleSize); },
        //    };
        //    vk2s::ShaderBindingTable::RegionInfo missInfo{
        //        .shaderTypeNum       = 2,
        //        .entryNum            = 2,
        //        .additionalEntrySize = 0,
        //        .entryWriter =
        //            [](std::byte* pDst, std::byte* pSrc, const uint32_t handleSize, const uint32_t alignedHandleSize)
        //        {
        //            for (int i = 0; i < 2; ++i)
        //            {
        //                std::memcpy(pDst, pSrc, handleSize);
        //                pDst += handleSize;
        //                // Since each shader uses a different shader, the address on the handle side is also advanced simply
        //                pSrc += alignedHandleSize;
        //            }
        //        },
        //    };
        //    vk2s::ShaderBindingTable::RegionInfo hitInfo{
        //        .shaderTypeNum       = 1,
        //        .entryNum            = static_cast<uint32_t>(mScene.size<Mesh>()),
        //        .additionalEntrySize = sizeof(vk::DeviceAddress) * 2,
        //        .entryWriter =
        //            [&](std::byte* pDst, std::byte* pSrc, const uint32_t handleSize, const uint32_t alignedHandleSize)
        //        {
        //            mScene.each<Mesh>(
        //                [&](const Mesh& mesh)
        //                {
        //                    std::memcpy(pDst, pSrc, handleSize);
        //                    pDst += handleSize;

        //                    auto deviceAddress = mesh.indexBuffer->getVkDeviceAddress();
        //                    std::memcpy(pDst, &deviceAddress, sizeof(deviceAddress));
        //                    pDst += sizeof(deviceAddress);

        //                    deviceAddress = mesh.vertexBuffer->getVkDeviceAddress();
        //                    std::memcpy(pDst, &deviceAddress, sizeof(deviceAddress));
        //                    pDst += sizeof(deviceAddress);
        //                });
        //        },
        //    };
        //    vk2s::ShaderBindingTable::RegionInfo callableInfo{ .shaderTypeNum = 0, .additionalEntrySize = 0 };

        //    return device.create<vk2s::ShaderBindingTable>(mRaytracePipeline.get(), raygenInfo, missInfo, hitInfo, callableInfo, rpi.shaderGroups);
        //}();

        // create bindgroup
        {
            mVertexBuffers.reserve(meshNum);
            mIndexBuffers.reserve(meshNum);

            mScene.each<Mesh>(
                [&](Mesh& mesh)
                {
                    mVertexBuffers.emplace_back(mesh.vertexBuffer);
                    mIndexBuffers.emplace_back(mesh.indexBuffer);
                });

            mBindGroup = device.create<vk2s::BindGroup>(mBindLayout.get());
            mBindGroup->bind(0, mTLAS.get());
            mBindGroup->bind(1, vk::DescriptorType::eStorageImage, mOutputImage);
            mBindGroup->bind(2, vk::DescriptorType::eUniformBuffer, mSceneBuffer.get());
            mBindGroup->bind(3, vk::DescriptorType::eStorageBuffer, mVertexBuffers);
            mBindGroup->bind(4, vk::DescriptorType::eStorageBuffer, mIndexBuffers);
        }
    }

    PathIntegrator::~PathIntegrator()
    {
    }

    void PathIntegrator::showConfigImGui()
    {
        ImGui::InputInt("spp", &mGUIParams.spp);
    }

    void PathIntegrator::sample(Handle<vk2s::Fence> fence, Handle<vk2s::Command> command)
    {
        const auto extent = mOutputImage->getVkExtent();
        // trace ray
        command->setPipeline(mRaytracePipeline);
        command->setBindGroup(0, mBindGroup.get());
        command->traceRays(mShaderBindingTable.get(), extent.width, extent.height, 1);
    }
}  // namespace palm
