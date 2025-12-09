/*****************************************************************/ /**
 * \file   ReSTIRIntegrator.cpp
 * \brief  source file of ReSTIRIntegrator class
 * 
 * \author ichi
 * \date   June 2025
 *********************************************************************/

#include "../include/Integrators/ReSTIRIntegrator.hpp"

#include "../include/Mesh.hpp"
#include "../include/Material.hpp"
#include "../include/EntityInfo.hpp"
#include "../include/Transform.hpp"
#include "../include/Emitter.hpp"

#include <iostream>

namespace palm
{

    ReSTIRIntegrator::ReSTIRIntegrator(vk2s::Device& device, ec2s::Registry& scene, Handle<vk2s::Image> output)
        : Integrator(device, scene, output)
        , mEmitterNum(0)
    {
        const auto extent = mOutputImage->getVkExtent();

        try
        {
            // create scene buffer
            {
                const auto size = sizeof(SceneParams);
                mSceneBuffer    = device.create<vk2s::Buffer>(vk::BufferCreateInfo({}, size, vk::BufferUsageFlagBits::eUniformBuffer), vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

                glm::mat4 view(1.0), proj(1.0);
                glm::vec3 camPos(0.0);
                mScene.each<vk2s::Camera>(
                    [&](const vk2s::Camera& camera)
                    {
                        view   = camera.getViewMatrix();
                        proj   = camera.getProjectionMatrix();
                        camPos = camera.getPos();
                    });

                mScene.each<Emitter>(
                    [&](const Emitter& emitter)
                    {
                        switch (emitter.params.type)
                        {
                        case static_cast<std::underlying_type_t<Emitter::Type>>(Emitter::Type::ePoint):
                            ++mEmitterNum;
                            break;
                        case static_cast<std::underlying_type_t<Emitter::Type>>(Emitter::Type::eArea):
                            mEmitterNum += emitter.params.faceNum;
                            break;
                        case static_cast<std::underlying_type_t<Emitter::Type>>(Emitter::Type::eInfinite):
                            ++mEmitterNum;
                            break;
                        }
                    });

                SceneParams params{
                    .view          = view,
                    .proj          = proj,
                    .viewInv       = glm::inverse(view),
                    .projInv       = glm::inverse(proj),
                    .camPos        = glm::vec4(camPos, 1.0f),
                    .sppPerFrame   = 1,
                    .accumulatedSpp = 0,
                    .allEmitterNum = mEmitterNum,
                    .reservoirSize  = 32,  // default size
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

                // WARN: ensures that the infinity light source is the first element of the emitterParams if exists
                mScene.each<Emitter>(
                    [&](const ec2s::Entity entity, Emitter& emitter)
                    {
                        if (emitter.params.type != static_cast<std::underlying_type_t<Emitter::Type>>(Emitter::Type::eInfinite))
                        {
                            return;
                        }

                        // register envmap texture
                        if (emitter.emissiveTex)
                        {
                            emitter.params.texIndex = mTextures.size();
                            mTextures.emplace_back(emitter.emissiveTex);
                        }

                        emitter.params.pos = glm::vec3(0.0);
                        params.emplace_back(emitter.params);
                    });

                // for emitter with transform
                mScene.each<Emitter, Transform>(
                    [&](const ec2s::Entity entity, Emitter& emitter, Transform& transform)
                    {
                        if (emitter.params.type == static_cast<std::underlying_type_t<Emitter::Type>>(Emitter::Type::eInfinite))
                        {
                            return;
                        }

                        emitter.params.pos = transform.pos;

                        if (scene.contains<Mesh>(entity))
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

                            Mesh& mesh = scene.get<Mesh>(entity);
                            for (int primitive = 0; primitive < mesh.hostMesh.indices.size() / 3; ++primitive)
                            {
                                emitter.params.primitiveIndex = primitive;
                                params.emplace_back(emitter.params);
                            }
                        }
                        else
                        {
                            params.emplace_back(emitter.params);
                        }
                    });

                const auto size = sizeof(Emitter::Params) * params.size();
                mEmittersBuffer = device.create<vk2s::Buffer>(vk::BufferCreateInfo({}, size, vk::BufferUsageFlagBits::eStorageBuffer), vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
                mEmittersBuffer->write(params.data(), size);
            }

            // create emitter reservoir
            {
                const auto size  = sizeof(EmitterReservoir) * extent.width * extent.height;
                mReservoirBuffer = device.create<vk2s::Buffer>(vk::BufferCreateInfo({}, size, vk::BufferUsageFlagBits::eStorageBuffer), vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            }

            // create sampler
            {
                mSampler = device.create<vk2s::Sampler>(vk::SamplerCreateInfo({}, vk::Filter::eLinear, vk::Filter::eLinear));
            }

            //create pool, DI, GI result image
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

                mPoolImage = device.create<vk2s::Image>(ci, vk::MemoryPropertyFlagBits::eDeviceLocal, size, vk::ImageAspectFlagBits::eColor);
                mDIImage   = device.create<vk2s::Image>(ci, vk::MemoryPropertyFlagBits::eDeviceLocal, size, vk::ImageAspectFlagBits::eColor);
                mGIImage   = device.create<vk2s::Image>(ci, vk::MemoryPropertyFlagBits::eDeviceLocal, size, vk::ImageAspectFlagBits::eColor);

                UniqueHandle<vk2s::Command> cmd = device.create<vk2s::Command>();
                cmd->begin(true);
                cmd->transitionImageLayout(mPoolImage.get(), vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral);
                cmd->transitionImageLayout(mDIImage.get(), vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral);
                cmd->transitionImageLayout(mGIImage.get(), vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral);
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
            const auto raygenShader = device.create<vk2s::Shader>("../../shaders/Slang/Integrators/ReSTIRIntegrator.slang", "rayGenShader");
            const auto missShader   = device.create<vk2s::Shader>("../../shaders/Slang/Integrators/ReSTIRIntegrator.slang", "missShader");
            const auto shadowShader = device.create<vk2s::Shader>("../../shaders/Slang/Integrators/ReSTIRIntegrator.slang", "shadowMissShader");
            const auto chitShader   = device.create<vk2s::Shader>("../../shaders/Slang/Integrators/ReSTIRIntegrator.slang", "closestHitShader");

            // create bind layout
            const auto meshNum  = mScene.size<Mesh>();
            std::array bindings = {
                // 0: TLAS
                vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eAccelerationStructureKHR, 1, vk::ShaderStageFlagBits::eAll),
                // 1: result image
                vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eAll),
                // 2: pool image
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
                // 11: emitter reservoir buffer
                vk::DescriptorSetLayoutBinding(11, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eAll),
                // 12: DI image
                vk::DescriptorSetLayoutBinding(12, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eAll),
                // 13: GI image
                vk::DescriptorSetLayoutBinding(13, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eAll),
            };

            mBindLayout = device.create<vk2s::BindLayout>(bindings);

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
                mBindGroup->bind(11, vk::DescriptorType::eStorageBuffer, mReservoirBuffer.get());
                mBindGroup->bind(12, vk::DescriptorType::eStorageImage, mDIImage);
                mBindGroup->bind(13, vk::DescriptorType::eStorageImage, mGIImage);
            }
        }
        catch (std::exception& e)
        {
            std::cerr << e.what() << "\n";
        }
    }

    ReSTIRIntegrator::~ReSTIRIntegrator()
    {
        mDevice.waitIdle();

        mDevice.destroy(mBindLayout);
        mDevice.destroy(mDummyTexture);
        // WARN: VB, IB and textures have no ownership
    }

    void ReSTIRIntegrator::showConfigImGui()
    {
        ImGui::InputInt("spp", &mGUIParams.spp);
        ImGui::Text("total spp: %d", mGUIParams.accumulatedSpp);
        ImGui::InputInt("reservoir size", &mGUIParams.reservoirSize);
    }

    void ReSTIRIntegrator::updateShaderResources()
    {
        bool cameraMoved = false;
        glm::mat4 view{}, proj{};
        glm::vec3 camPos = glm::vec3(0.0);

        mScene.each<vk2s::Camera>(
            [&](vk2s::Camera& camera)
            {
                cameraMoved = camera.moved();

                view   = camera.getViewMatrix();
                proj   = camera.getProjectionMatrix();
                camPos = camera.getPos();
            });

        mGUIParams.accumulatedSpp = std::min(mGUIParams.accumulatedSpp + mGUIParams.spp, std::numeric_limits<int>::max());
        if (cameraMoved)
        {
            mGUIParams.accumulatedSpp = 0;
        }

        SceneParams params{
            .view          = view,
            .proj          = proj,
            .viewInv       = glm::inverse(view),
            .projInv       = glm::inverse(proj),
            .camPos        = glm::vec4(camPos, 1.0f),
            .sppPerFrame   = static_cast<uint32_t>(mGUIParams.spp),
            .accumulatedSpp = static_cast<uint32_t>(mGUIParams.accumulatedSpp),
            .allEmitterNum = mEmitterNum,
            .reservoirSize = static_cast<uint32_t>(mGUIParams.reservoirSize),
        };

        mSceneBuffer->write(&params, sizeof(SceneParams));
    }

    void ReSTIRIntegrator::sample(Handle<vk2s::Command> command)
    {
        const auto extent = mOutputImage->getVkExtent();

        // trace ray
        command->setPipeline(mRaytracePipeline);
        command->setBindGroup(0, mBindGroup.get());
        command->traceRays(mShaderBindingTable.get(), extent.width, extent.height, 1);
    }

    ReSTIRIntegrator::GUIParams& ReSTIRIntegrator::getGUIParamsRef()
    {
        return mGUIParams;
    }

}  // namespace palm
