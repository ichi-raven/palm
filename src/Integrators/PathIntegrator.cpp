/*****************************************************************/ /**
 * @file   PathIntegrator.cpp
 * @brief  source file of PathIntegrator class
 * 
 * @author ichi-raven
 * @date   October 2024
 *********************************************************************/

#include "../include/Integrators/PathIntegrator.hpp"

#include "../include/Mesh.hpp"
#include "../include/Material.hpp"
#include "../include/EntityInfo.hpp"
#include "../include/Transform.hpp"
#include "../include/Emitter.hpp"

#include <iostream>

namespace palm
{

    PathIntegrator::PathIntegrator(vk2s::Device& device, ec2s::Registry& scene, Handle<vk2s::Image> output)
        : Integrator(device, scene, output)
    {
        const auto extent = mOutputImage->getVkExtent();

        try
        {
            // create dummy image
            {
                // dummy texture
                constexpr uint8_t kDummyColor[] = { 255, 0, 255, 255 }; // Magenta
                const auto format               = vk::Format::eR8G8B8A8Srgb;
                const uint32_t size             = vk2s::Compiler::getSizeOfFormat(format);  // 1 * 1

                vk::ImageCreateInfo ci;
                ci.arrayLayers   = 1;
                ci.extent        = vk::Extent3D(1, 1, 1);  // 1 * 1
                ci.format        = format;
                ci.imageType     = vk::ImageType::e2D;
                ci.mipLevels     = 1;
                ci.usage         = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
                ci.initialLayout = vk::ImageLayout::eUndefined;

                // change format to pooling
                mDummyTexture = device.create<vk2s::Image>(ci, vk::MemoryPropertyFlagBits::eDeviceLocal, size, vk::ImageAspectFlagBits::eColor);
                mDummyTexture->write(kDummyColor, size);

                UniqueHandle<vk2s::Command> cmd = device.create<vk2s::Command>();
                cmd->begin(true);
                cmd->transitionImageLayout(mDummyTexture.get(), vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal);
                cmd->end();
                cmd->execute();
            }

            // create scene buffer
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

                uint32_t areaEmitterNum = 0;
                mScene.each<Emitter>(
                    [&](const Emitter& emitter)
                    {
                        if (emitter.params.type == static_cast<std::underlying_type_t<Emitter::Type>>(Emitter::Type::eArea))
                        {
                            ++areaEmitterNum;
                        }
                    });

                SceneParams params{
                    .view           = view,
                    .proj           = proj,
                    .viewInv        = glm::inverse(view),
                    .projInv        = glm::inverse(proj),
                    .camPos         = glm::vec4(camPos, 1.0f),
                    .sppPerFrame    = 4,
                    .areaEmitterNum = areaEmitterNum,
                    .padding        = { 0.f },
                };

                mSceneBuffer->write(&params, sizeof(SceneParams));
            }

            // create instance buffer
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

            // create material buffer and load texture
            {
                const auto select = [&](Handle<vk2s::Image> img) -> Handle<vk2s::Image>
                {
                    if (img)
                    {
                        return img;
                    }

                    return mDummyTexture;
                };

                std::vector<Material::Params> params;
                int32_t texIndex = 0;
                mScene.each<Material>(
                    [&](const Material& mat)
                    {
                        Material::Params texIndexModified = mat.params;
                        if (mat.albedoTex)
                        {
                            texIndexModified.albedoTexIndex = texIndex + 0;
                        }
                        if (mat.roughnessTex)
                        {
                            texIndexModified.roughnessTexIndex = texIndex + 1;
                        }
                        if (mat.metalnessTex)
                        {
                            texIndexModified.metalnessTexIndex = texIndex + 2;
                        }
                        if (mat.normalMapTex)
                        {
                            texIndexModified.normalMapTexIndex = texIndex + 3;
                        }
                        texIndex += Material::kDefaultTexNum;

                        params.emplace_back(texIndexModified);

                        mTextures.emplace_back(select(mat.albedoTex));
                        mTextures.emplace_back(select(mat.roughnessTex));
                        mTextures.emplace_back(select(mat.metalnessTex));
                        mTextures.emplace_back(select(mat.normalMapTex));
                    });

                const auto size = sizeof(Material::Params) * params.size();
                mMaterialBuffer = device.create<vk2s::Buffer>(vk::BufferCreateInfo({}, size, vk::BufferUsageFlagBits::eStorageBuffer), vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
                mMaterialBuffer->write(params.data(), size);

                // if all empty, set dummy image
                if (mTextures.empty())
                {
                    mTextures.emplace_back(mDummyTexture);
                }
            }

            // create emitter buffer
            {
                std::vector<Emitter::Params> params;
                mScene.each<Emitter, Transform>(
                    [&](const ec2s::Entity entity, Emitter& emitter, Transform& transform)
                    {
                        if (emitter.attachedEntity)
                        {
                            int32_t idx = 0;
                            scene.each<Mesh>(
                                [&](const ec2s::Entity entity, const Mesh& mesh)
                                {
                                    if (entity == *emitter.attachedEntity)
                                    {
                                        emitter.params.meshIndex = idx;
                                    }
                                    ++idx;
                                });
                        }

                        emitter.params.pos = transform.pos;
                        params.emplace_back(emitter.params);
                    });

                const auto size = sizeof(Emitter::Params) * params.size();
                mEmittersBuffer = device.create<vk2s::Buffer>(vk::BufferCreateInfo({}, size, vk::BufferUsageFlagBits::eStorageBuffer), vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
                mEmittersBuffer->write(params.data(), size);
            }

            {  // create sampler
                mSampler = device.create<vk2s::Sampler>(vk::SamplerCreateInfo());
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
                // 0: TLAS
                vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eAccelerationStructureKHR, 1, vk::ShaderStageFlagBits::eAll),
                // 1: result image
                vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eAll),
                // 2: result image
                vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eAll),
                // 3: scene parameters
                vk::DescriptorSetLayoutBinding(3, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eAll),
                // 4: vertex buffers
                vk::DescriptorSetLayoutBinding(4, vk::DescriptorType::eStorageBuffer, meshNum, vk::ShaderStageFlagBits::eAll),
                // 5: index buffers
                vk::DescriptorSetLayoutBinding(5, vk::DescriptorType::eStorageBuffer, meshNum, vk::ShaderStageFlagBits::eAll),
                // 6: instance buffers
                vk::DescriptorSetLayoutBinding(6, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eAll),
                // 7: material buffers
                vk::DescriptorSetLayoutBinding(7, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eAll),
                // 8: emissive buffers
                vk::DescriptorSetLayoutBinding(8, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eAll),
                // 9: textures
                vk::DescriptorSetLayoutBinding(9, vk::DescriptorType::eSampledImage, std::max((size_t)1, mTextures.size()), vk::ShaderStageFlagBits::eAll),
                // 10: sampler
                vk::DescriptorSetLayoutBinding(10, vk::DescriptorType::eSampler, 1, vk::ShaderStageFlagBits::eAll),
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
                mBindGroup->bind(8, vk::DescriptorType::eStorageBuffer, mEmittersBuffer.get());
                mBindGroup->bind(9, vk::DescriptorType::eSampledImage, mTextures);
                mBindGroup->bind(10, mSampler.get());
            }
        }
        catch (std::exception& e)
        {
            std::cerr << e.what() << "\n";
        }
    }

    PathIntegrator::~PathIntegrator()
    {
        mDevice.waitIdle();

        mDevice.destroy(mBindLayout);
        mDevice.destroy(mDummyTexture);
        // WARN: VB, IB and textures have no ownership
    }

    void PathIntegrator::showConfigImGui()
    {
        ImGui::InputInt("spp", &mGUIParams.spp);
        ImGui::Text("total spp: %d", mGUIParams.accumulatedSpp);
    }

    void PathIntegrator::updateShaderResources()
    {
        mGUIParams.accumulatedSpp = std::min(mGUIParams.accumulatedSpp + mGUIParams.spp, std::numeric_limits<int>::max());

        glm::mat4 view{}, proj{};
        glm::vec3 camPos = glm::vec3(0.0);

        mScene.each<vk2s::Camera>(
            [&](const vk2s::Camera& camera)
            {
                view   = camera.getViewMatrix();
                proj   = camera.getProjectionMatrix();
                camPos = camera.getPos();
            });

        SceneParams params{
            .view        = view,
            .proj        = proj,
            .viewInv     = glm::inverse(view),
            .projInv     = glm::inverse(proj),
            .camPos      = glm::vec4(camPos, 1.0f),
            .sppPerFrame = static_cast<uint32_t>(mGUIParams.spp),
            .padding     = { 0.f },
        };

        mSceneBuffer->write(&params, sizeof(SceneParams));
    }

    void PathIntegrator::sample(Handle<vk2s::Command> command)
    {
        const auto extent = mOutputImage->getVkExtent();

        // trace ray
        command->setPipeline(mRaytracePipeline);
        command->setBindGroup(0, mBindGroup.get());
        command->traceRays(mShaderBindingTable.get(), extent.width, extent.height, 1);
    }

}  // namespace palm
