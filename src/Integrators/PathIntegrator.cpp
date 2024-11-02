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
                .view           = view,
                .proj           = proj,
                .viewInv        = glm::inverse(view),
                .projInv        = glm::inverse(proj),
                .camPos         = glm::vec4(camPos, 1.0f),
                .sppPerFrame    = 4,
                .padding        = { 0.f },
            };

            mSceneBuffer->write(&params, sizeof(SceneParams));
        }

        // create instance UB
        {
            std::vector<InstanceParams> params;
            mScene.each<Mesh, Transform>(
                [&](const Mesh& mesh, const Transform& transform)
                {
                    auto& p         = params.emplace_back();
                    p.world         = transform.params.world;
                    p.worldInvTrans = transform.params.worldInvTranspose;
                });

            const auto size = sizeof(InstanceParams) * params.size();
            mInstanceBuffer = device.create<vk2s::Buffer>(vk::BufferCreateInfo({}, size, vk::BufferUsageFlagBits::eStorageBuffer), vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            mInstanceBuffer->write(params.data(), size);
        }

        // create material UB
        {
            std::vector<Material::Params> params;
            mScene.each<Material>(
                [&](const Material& mat)
                {
                    params.emplace_back(mat.materialParams);
                    std::cout << params.back().albedo.r << params.back().albedo.g << params.back().albedo.b << "\n";
                });

            const auto size = sizeof(Material::Params) * params.size();
            mMaterialBuffer = device.create<vk2s::Buffer>(vk::BufferCreateInfo({}, size, vk::BufferUsageFlagBits::eStorageBuffer), vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            mMaterialBuffer->write(params.data(), size);
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
            // 2 : result image
            vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eAll),
            // 3 : scene parameters
            vk::DescriptorSetLayoutBinding(3, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eAll),
            // 4: vertex buffers
            vk::DescriptorSetLayoutBinding(4, vk::DescriptorType::eStorageBuffer, meshNum, vk::ShaderStageFlagBits::eAll),
            // 5: index buffers
            vk::DescriptorSetLayoutBinding(5, vk::DescriptorType::eStorageBuffer, meshNum, vk::ShaderStageFlagBits::eAll),
            // 6: instance buffers
            vk::DescriptorSetLayoutBinding(6, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eAll),
            // 7: material buffers
            vk::DescriptorSetLayoutBinding(7, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eAll),
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

        // create bindgroup
        {
            mVertexBuffers.reserve(meshNum);
            mIndexBuffers.reserve(meshNum);

            mScene.each<Mesh>(
                [&](const Mesh& mesh)
                {
                    mVertexBuffers.emplace_back(mesh.vertexBuffer);
                    mIndexBuffers.emplace_back(mesh.indexBuffer);
                });

            mBindGroup = device.create<vk2s::BindGroup>(mBindLayout.get());
            mBindGroup->bind(0, mTLAS.get());
            mBindGroup->bind(1, vk::DescriptorType::eStorageImage, mOutputImage);
            mBindGroup->bind(2, vk::DescriptorType::eStorageImage, mPoolImage);
            mBindGroup->bind(3, vk::DescriptorType::eUniformBuffer, mSceneBuffer.get());
            mBindGroup->bind(4, vk::DescriptorType::eStorageBuffer, mVertexBuffers);
            mBindGroup->bind(5, vk::DescriptorType::eStorageBuffer, mIndexBuffers);
            mBindGroup->bind(6, vk::DescriptorType::eStorageBuffer, mInstanceBuffer.get());
            mBindGroup->bind(7, vk::DescriptorType::eStorageBuffer, mMaterialBuffer.get());
        }
    }

    PathIntegrator::~PathIntegrator()
    {
    }

    void PathIntegrator::showConfigImGui()
    {
        ImGui::InputInt("spp", &mGUIParams.spp);
        ImGui::Text("total spp: %d", mGUIParams.accumulatedSpp);
    }

    void PathIntegrator::sample(Handle<vk2s::Fence> fence, Handle<vk2s::Command> command)
    {
        mGUIParams.accumulatedSpp = std::min(mGUIParams.accumulatedSpp + mGUIParams.spp, std::numeric_limits<int>::max());

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
            .view           = view,
            .proj           = proj,
            .viewInv        = glm::inverse(view),
            .projInv        = glm::inverse(proj),
            .camPos         = glm::vec4(camPos, 1.0f),
            .sppPerFrame    = static_cast<uint32_t>(mGUIParams.spp),
            .padding        = { 0.f },
        };

        mSceneBuffer->write(&params, sizeof(SceneParams));

        const auto extent = mOutputImage->getVkExtent();
        // trace ray
        command->setPipeline(mRaytracePipeline);
        command->setBindGroup(0, mBindGroup.get());
        command->traceRays(mShaderBindingTable.get(), extent.width, extent.height, 1);
    }
}  // namespace palm
