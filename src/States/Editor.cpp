/*****************************************************************/ /**
 * @file   Editor.cpp
 * @brief  source file of Editor class
 * 
 * @author ichi-raven
 * @date   February 2024
 *********************************************************************/

#include "../include/States/Editor.hpp"
#include "../include/Mesh.hpp"
#include "../include/Material.hpp"
#include "../include/EntityInfo.hpp"
#include "../include/Transform.hpp"
#include "../include/Emitter.hpp"

#include <stb_image.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/string_cast.hpp>

#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <ImGuizmo.h>

#include <iostream>
#include <filesystem>

namespace palm
{
    // for casting path UTF-8 string to normal string
    std::string to_string(const std::filesystem::path& path)
    {
        const auto u8str = path.u8string();
        return std::string(reinterpret_cast<const char*>(u8str.data()), u8str.size());
    }

    void Editor::addEntity(const std::filesystem::path& path)
    {
        auto& device = common()->device;
        auto& window = common()->window;
        auto& scene  = common()->scene;

        vk2s::Scene model(to_string(path));

        const std::vector<vk2s::Mesh>& hostMeshes        = model.getMeshes();
        const std::vector<vk2s::Material>& hostMaterials = model.getMaterials();
        const std::vector<vk2s::Texture>& hostTextures   = model.getTextures();

        assert(hostMaterials.size() == hostMeshes.size() || !"The number of mesh is different from the number of material!");

        for (size_t instanceIndex = 0; instanceIndex < hostMeshes.size(); ++instanceIndex)
        {
            const auto entity = scene.create<Mesh, Material, EntityInfo, Transform>();
            auto& mesh        = scene.get<Mesh>(entity);
            auto& material    = scene.get<Material>(entity);
            auto& info        = scene.get<EntityInfo>(entity);
            auto& transform   = scene.get<Transform>(entity);

            const auto& hostMesh     = hostMeshes[instanceIndex];
            const auto& hostMaterial = hostMaterials[instanceIndex];

            mesh.hostMesh = hostMesh;

            {  // vertex buffer
                std::vector<Mesh::Vertex> vertices;
                vertices.resize(mesh.hostMesh.vertices.size());
                for (int i = 0; i < mesh.hostMesh.vertices.size(); ++i)
                {
                    vertices[i].pos    = mesh.hostMesh.vertices[i].pos;
                    vertices[i].normal = mesh.hostMesh.vertices[i].normal;
                    vertices[i].u      = mesh.hostMesh.vertices[i].uv.x;
                    vertices[i].v      = mesh.hostMesh.vertices[i].uv.y;
                }

                const auto vbSize  = vertices.size() * sizeof(Mesh::Vertex);
                const auto vbUsage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eStorageBuffer;
                vk::BufferCreateInfo ci({}, vbSize, vbUsage);
                vk::MemoryPropertyFlags fb = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;

                mesh.vertexBuffer = device.create<vk2s::Buffer>(ci, fb);
                mesh.vertexBuffer->write(vertices.data(), vbSize);
            }

            {  // index buffer

                const auto ibSize  = hostMesh.indices.size() * sizeof(uint32_t);
                const auto ibUsage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eStorageBuffer;

                vk::BufferCreateInfo ci({}, ibSize, ibUsage);
                vk::MemoryPropertyFlags fb = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;

                mesh.indexBuffer = device.create<vk2s::Buffer>(ci, fb);
                mesh.indexBuffer->write(hostMesh.indices.data(), ibSize);
            }

            {  // BLAS
                mesh.blas = device.create<vk2s::AccelerationStructure>(mesh.hostMesh.vertices.size(), sizeof(Mesh::Vertex), mesh.vertexBuffer.get(), mesh.hostMesh.indices.size() / 3, mesh.indexBuffer.get());
            }

            {  // materials
                // params initialize
                material.params.albedo    = hostMaterial.albedo;
                material.params.roughness = hostMaterial.roughness.x;
                material.params.IOR       = hostMaterial.eta.r;
                material.params.emissive  = glm::vec3(hostMaterial.emissive);

                // add emitter component if the material has emissive value
                if (glm::dot(material.params.emissive, material.params.emissive) > 0.0)
                {
                    scene.add<Emitter>(entity);

                    auto& emitter          = scene.get<Emitter>(entity);
                    emitter.attachedEntity = entity;

                    emitter.params.emissive = material.params.emissive;
                    emitter.params.type     = static_cast<std::underlying_type_t<Emitter::Type>>(Emitter::Type::eArea);
                    emitter.params.faceNum  = hostMesh.indices.size() / 3;
                }

                // texture loading
                // share create info
                vk::ImageCreateInfo ci;
                ci.arrayLayers   = 1;
                ci.imageType     = vk::ImageType::e2D;
                ci.mipLevels     = 1;
                ci.usage         = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
                ci.initialLayout = vk::ImageLayout::eUndefined;

                // albedo texture
                // TODO: other texture creating
                if (hostMaterial.albedoTex != -1)
                {
                    const auto& hostTex = hostTextures[hostMaterial.albedoTex];
                    const auto size     = hostTex.width * hostTex.height * static_cast<uint32_t>(STBI_rgb_alpha);

                    ci.format          = vk::Format::eR8G8B8A8Unorm;
                    ci.extent          = vk::Extent3D(hostTex.width, hostTex.height, 1);
                    material.albedoTex = device.create<vk2s::Image>(ci, vk::MemoryPropertyFlagBits::eDeviceLocal, size, vk::ImageAspectFlagBits::eColor);
                    material.albedoTex->write(hostTex.pData, size);
                    material.params.albedoTexIndex = 0;  // not Material::Params::kInvalidTexIndex

                    // transition from initial layout
                    UniqueHandle<vk2s::Command> cmd = device.create<vk2s::Command>();
                    cmd->begin(true);
                    cmd->transitionImageLayout(material.albedoTex.get(), vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal);
                    cmd->end();
                    cmd->execute();
                }

                {  // uniform buffer
                    const auto frameCount = window->getFrameCount();
                    const auto ubSize     = sizeof(Material::Params) * frameCount;
                    vk::BufferCreateInfo ci({}, ubSize, vk::BufferUsageFlagBits::eUniformBuffer);
                    vk::MemoryPropertyFlags fb = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;

                    material.uniformBuffer = device.create<vk2s::DynamicBuffer>(ci, fb, frameCount);
                    for (int i = 0; i < frameCount; ++i)
                    {
                        material.uniformBuffer->write(&material.params, sizeof(Material::Params), i * material.uniformBuffer->getBlockSize());
                    }
                }

                {  // bindgroup
                    material.bindGroup = device.create<vk2s::BindGroup>(mGeometryPass.bindLayouts[2].get());
                    material.bindGroup->bind(0, vk::DescriptorType::eUniformBufferDynamic, material.uniformBuffer.get());
                    if (material.albedoTex)
                    {
                        material.bindGroup->bind(1, vk::DescriptorType::eSampledImage, material.albedoTex);
                    }
                    else
                    {
                        material.bindGroup->bind(1, vk::DescriptorType::eSampledImage, mDummyTexture);
                    }
                    material.bindGroup->bind(2, mLinearSampler.get());
                }
            }

            {  // information
                info.entityName = mesh.hostMesh.nodeName;
                info.entityID   = entity;
                info.editable   = true;

                info.groupName      = path.filename().string();
                const size_t dotPos = info.groupName.find_last_of('.');
                if (dotPos != std::string_view::npos)
                {
                    info.groupName = info.groupName.substr(0, dotPos);
                }
            }

            {  // transform
                transform.params.world             = glm::identity<glm::mat4>();
                transform.params.worldInvTranspose = glm::identity<glm::mat4>();
                transform.params.vel               = glm::vec3(0.f);
                transform.params.entitySlot        = static_cast<uint32_t>(entity >> ec2s::kEntitySlotShiftWidth);
                transform.params.entityIndex       = static_cast<uint32_t>(entity & ec2s::kEntityIndexMask);

                const auto frameCount = window->getFrameCount();
                const auto size       = sizeof(Transform::Params) * frameCount;
                transform.uniformBuffer =
                    device.create<vk2s::DynamicBuffer>(vk::BufferCreateInfo({}, size, vk::BufferUsageFlagBits::eUniformBuffer), vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, frameCount);
                for (int i = 0; i < frameCount; ++i)
                {
                    transform.uniformBuffer->write(&transform.params, sizeof(Transform::Params), i * transform.uniformBuffer->getBlockSize());
                }

                transform.bindGroup = device.create<vk2s::BindGroup>(mGeometryPass.bindLayouts[1].get());
                transform.bindGroup->bind(0, vk::DescriptorType::eUniformBufferDynamic, transform.uniformBuffer.get());
            }

            // select added entity
            mPickedEntity = entity;
        }
    }

    void Editor::removeEntity(const ec2s::Entity entity)
    {
        if (mPickedEntity && *mPickedEntity == entity)
        {
            mPickedEntity.reset();
        }

        auto& device = common()->device;
        auto& scene  = common()->scene;

        device.waitIdle();

        if (scene.contains<Mesh>(entity))
        {
            auto& mesh = scene.get<Mesh>(entity);
            device.destroy(mesh.blas);
            device.destroy(mesh.vertexBuffer);
            device.destroy(mesh.indexBuffer);
            device.destroy(mesh.instanceBuffer);
        }

        if (scene.contains<Material>(entity))
        {
            auto& material = scene.get<Material>(entity);
            device.destroy(material.uniformBuffer);
            device.destroy(material.albedoTex);
            device.destroy(material.normalMapTex);
            device.destroy(material.metalnessTex);
            device.destroy(material.roughnessTex);
            device.destroy(material.bindGroup);
        }

        if (scene.contains<Transform>(entity))
        {
            auto& transform = scene.get<Transform>(entity);
            device.destroy(transform.uniformBuffer);
            device.destroy(transform.bindGroup);
        }

        if (scene.contains<Emitter>(entity))
        {
            if (mInfiniteEmitterEntity && mInfiniteEmitterEntity == entity)
            {
                mInfiniteEmitterEntity.reset();
            }

            auto& emitter = scene.get<Emitter>(entity);
            device.destroy(emitter.emissiveTex);
        }

        scene.destroy(entity);
    }

    void Editor::createGBuffer()
    {
        auto& device = getCommonRegion()->device;
        auto& window = getCommonRegion()->window;

        const auto [windowWidth, windowHeight] = window->getWindowSize();

        // create depth buffer
        {
            const auto format   = vk::Format::eD32Sfloat;
            const uint32_t size = windowWidth * windowHeight * vk2s::Compiler::getSizeOfFormat(format);

            vk::ImageCreateInfo ci;
            ci.arrayLayers   = 1;
            ci.extent        = vk::Extent3D(windowWidth, windowHeight, 1);
            ci.format        = format;
            ci.imageType     = vk::ImageType::e2D;
            ci.mipLevels     = 1;
            ci.usage         = vk::ImageUsageFlagBits::eDepthStencilAttachment;
            ci.initialLayout = vk::ImageLayout::eUndefined;

            mGBuffer.depthBuffer = device.create<vk2s::Image>(ci, vk::MemoryPropertyFlagBits::eDeviceLocal, size, vk::ImageAspectFlagBits::eDepth);
        }

        // create G-Buffer
        {
            const auto format   = vk::Format::eR32G32B32A32Sfloat;
            const uint32_t size = windowWidth * windowHeight * vk2s::Compiler::getSizeOfFormat(format);

            vk::ImageCreateInfo ci;
            ci.arrayLayers   = 1;
            ci.extent        = vk::Extent3D(windowWidth, windowHeight, 1);
            ci.format        = format;
            ci.imageType     = vk::ImageType::e2D;
            ci.mipLevels     = 1;
            ci.usage         = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment;
            ci.initialLayout = vk::ImageLayout::eUndefined;

            mGBuffer.albedoTex             = device.create<vk2s::Image>(ci, vk::MemoryPropertyFlagBits::eDeviceLocal, size, vk::ImageAspectFlagBits::eColor);
            mGBuffer.worldPosTex           = device.create<vk2s::Image>(ci, vk::MemoryPropertyFlagBits::eDeviceLocal, size, vk::ImageAspectFlagBits::eColor);
            mGBuffer.normalTex             = device.create<vk2s::Image>(ci, vk::MemoryPropertyFlagBits::eDeviceLocal, size, vk::ImageAspectFlagBits::eColor);
            mGBuffer.roughnessMetalnessTex = device.create<vk2s::Image>(ci, vk::MemoryPropertyFlagBits::eDeviceLocal, size, vk::ImageAspectFlagBits::eColor);

            UniqueHandle<vk2s::Command> cmd = device.create<vk2s::Command>();
            cmd->begin(true);
            cmd->transitionImageLayout(mGBuffer.albedoTex.get(), vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal);
            cmd->transitionImageLayout(mGBuffer.worldPosTex.get(), vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal);
            cmd->transitionImageLayout(mGBuffer.normalTex.get(), vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal);
            cmd->transitionImageLayout(mGBuffer.roughnessMetalnessTex.get(), vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal);
            cmd->end();
            cmd->execute();
        }
    }

    void Editor::initVulkan()
    {
        auto& device = getCommonRegion()->device;
        auto& window = getCommonRegion()->window;

        const auto [windowWidth, windowHeight] = window->getWindowSize();
        const auto frameCount                  = window->getFrameCount();

        try
        {
            // nearest sampler
            mNearestSampler = device.create<vk2s::Sampler>(vk::SamplerCreateInfo({}, vk::Filter::eNearest, vk::Filter::eNearest));
            // linear sampler
            mLinearSampler = device.create<vk2s::Sampler>(vk::SamplerCreateInfo({}, vk::Filter::eLinear, vk::Filter::eLinear));

            // create G-Buffer
            auto& device = getCommonRegion()->device;
            auto& window = getCommonRegion()->window;

            const auto [windowWidth, windowHeight] = window->getWindowSize();
            const auto frameCount                  = window->getFrameCount();

            // create dummy image
            {
// dummy texture
#ifndef NDEBUG
                constexpr uint8_t kDummyColor[] = { 255, 0, 255, 0 };  // Magenta
#else
                constexpr uint8_t kDummyColor[] = { 0, 0, 0, 0 };  // Black
#endif
                const auto format   = vk::Format::eR8G8B8A8Srgb;
                const uint32_t size = vk2s::Compiler::getSizeOfFormat(format);  // 1 * 1

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

            // to share implementation with swap chain recreation
            createGBuffer();

            // geometry pass
            {
                std::vector<Handle<vk2s::Image>> images = { mGBuffer.albedoTex, mGBuffer.worldPosTex, mGBuffer.normalTex, mGBuffer.roughnessMetalnessTex };

                mGeometryPass.renderpass = device.create<vk2s::RenderPass>(images, mGBuffer.depthBuffer, vk::AttachmentLoadOp::eClear);

                mGeometryPass.vs = device.create<vk2s::Shader>("../../shaders/Slang/Rasterize/Deferred/Geometry.slang", "vsmain");
                mGeometryPass.fs = device.create<vk2s::Shader>("../../shaders/Slang/Rasterize/Deferred/Geometry.slang", "fsmain");

                std::vector bindings0 = {
                    // Scene MVP information
                    // VP
                    vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBufferDynamic, 1, vk::ShaderStageFlagBits::eAll),
                };

                std::vector bindings1 = {
                    // Entity information
                    vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBufferDynamic, 1, vk::ShaderStageFlagBits::eAll),
                };

                std::vector bindings2 = {
                    // Material params
                    vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBufferDynamic, 1, vk::ShaderStageFlagBits::eAll),
                    // Material Textures
                    vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eSampledImage, 1, vk::ShaderStageFlagBits::eAll),
                    // Sampler
                    vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eSampler, 1, vk::ShaderStageFlagBits::eAll),

                };

                mGeometryPass.bindLayouts.emplace_back(device.create<vk2s::BindLayout>(bindings0));
                mGeometryPass.bindLayouts.emplace_back(device.create<vk2s::BindLayout>(bindings1));
                mGeometryPass.bindLayouts.emplace_back(device.create<vk2s::BindLayout>(bindings2));

                vk::VertexInputBindingDescription inputBinding(0, sizeof(Mesh::Vertex));
                const auto& inputAttributes = std::get<0>(mGeometryPass.vs->getReflection());
                vk::PipelineColorBlendAttachmentState colorBlendAttachment(VK_FALSE);
                colorBlendAttachment.setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);

                const auto dynamicStates = std::array{ vk::DynamicState::eViewport, vk::DynamicState::eScissor };

                // G-Buffer color
                std::array attachments = { colorBlendAttachment, colorBlendAttachment, colorBlendAttachment, colorBlendAttachment };

                vk2s::Pipeline::GraphicsPipelineInfo gpi{
                    .vs            = mGeometryPass.vs,
                    .fs            = mGeometryPass.fs,
                    .bindLayouts   = mGeometryPass.bindLayouts,
                    .renderPass    = mGeometryPass.renderpass,
                    .inputState    = vk::PipelineVertexInputStateCreateInfo({}, inputBinding, inputAttributes),
                    .inputAssembly = vk::PipelineInputAssemblyStateCreateInfo({}, vk::PrimitiveTopology::eTriangleList),
                    .viewportState = vk::PipelineViewportStateCreateInfo({}, 1, {}, 1, {}),
                    .rasterizer    = vk::PipelineRasterizationStateCreateInfo({}, VK_FALSE, VK_FALSE, vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eClockwise, VK_FALSE, 0.0f, 0.0f, 0.0f, 1.0f),
                    .multiSampling = vk::PipelineMultisampleStateCreateInfo({}, vk::SampleCountFlagBits::e1, VK_FALSE),
                    .depthStencil  = vk::PipelineDepthStencilStateCreateInfo({}, VK_TRUE, VK_TRUE, vk::CompareOp::eLess, VK_FALSE),
                    .colorBlending = vk::PipelineColorBlendStateCreateInfo({}, VK_FALSE, vk::LogicOp::eCopy, attachments),
                    .dynamicStates = vk::PipelineDynamicStateCreateInfo({}, dynamicStates),
                };

                mGeometryPass.pipeline = device.create<vk2s::Pipeline>(gpi);
            }

            {  // lighting pass
                mLightingPass.renderpass = device.create<vk2s::RenderPass>(window.get(), vk::AttachmentLoadOp::eClear);
                mLightingPass.vs         = device.create<vk2s::Shader>("../../shaders/Slang/Rasterize/Deferred/Lighting.slang", "vsmain");
                mLightingPass.fs         = device.create<vk2s::Shader>("../../shaders/Slang/Rasterize/Deferred/Lighting.slang", "fsmain");

                std::array bindings0 = {
                    vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eSampledImage, 1, vk::ShaderStageFlagBits::eAll),  //
                    vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eSampledImage, 1, vk::ShaderStageFlagBits::eAll),  //
                    vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eSampledImage, 1, vk::ShaderStageFlagBits::eAll),  //
                    vk::DescriptorSetLayoutBinding(3, vk::DescriptorType::eSampledImage, 1, vk::ShaderStageFlagBits::eAll),  //
                    vk::DescriptorSetLayoutBinding(4, vk::DescriptorType::eSampler, 1, vk::ShaderStageFlagBits::eAll),       //
                };

                std::vector bindings1 = {
                    vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBufferDynamic, 1, vk::ShaderStageFlagBits::eAll),  //
                    vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eAll),         //
                    vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eUniformBufferDynamic, 1, vk::ShaderStageFlagBits::eAll),  //
                    vk::DescriptorSetLayoutBinding(3, vk::DescriptorType::eSampledImage, 1, vk::ShaderStageFlagBits::eAll),          //
                    vk::DescriptorSetLayoutBinding(4, vk::DescriptorType::eSampler, 1, vk::ShaderStageFlagBits::eAll),               //
                };

                mLightingPass.bindLayouts.emplace_back(device.create<vk2s::BindLayout>(bindings0));
                mLightingPass.bindLayouts.emplace_back(device.create<vk2s::BindLayout>(bindings1));

                vk::PipelineColorBlendAttachmentState colorBlendAttachment(VK_FALSE);
                colorBlendAttachment.setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);

                const auto dynamicStates = std::array{ vk::DynamicState::eViewport, vk::DynamicState::eScissor };

                vk2s::Pipeline::GraphicsPipelineInfo gpi{
                    .vs            = mLightingPass.vs,
                    .fs            = mLightingPass.fs,
                    .bindLayouts   = mLightingPass.bindLayouts,
                    .renderPass    = mLightingPass.renderpass,
                    .inputState    = vk::PipelineVertexInputStateCreateInfo(),
                    .inputAssembly = vk::PipelineInputAssemblyStateCreateInfo({}, vk::PrimitiveTopology::eTriangleStrip),
                    .viewportState = vk::PipelineViewportStateCreateInfo({}, 1, {}, 1, {}),
                    .rasterizer    = vk::PipelineRasterizationStateCreateInfo({}, VK_FALSE, VK_FALSE, vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eClockwise, VK_FALSE, 0.0f, 0.0f, 0.0f, 1.0f),
                    .multiSampling = vk::PipelineMultisampleStateCreateInfo({}, vk::SampleCountFlagBits::e1, VK_FALSE),
                    .depthStencil  = vk::PipelineDepthStencilStateCreateInfo({}, VK_TRUE, VK_TRUE, vk::CompareOp::eLess, VK_FALSE),
                    .colorBlending = vk::PipelineColorBlendStateCreateInfo({}, VK_FALSE, vk::LogicOp::eCopy, 1, &colorBlendAttachment),
                    .dynamicStates = vk::PipelineDynamicStateCreateInfo({}, dynamicStates),
                };

                mLightingPass.pipeline = device.create<vk2s::Pipeline>(gpi);
            }

            // initialize ImGui
            device.initImGui(window.get(), mLightingPass.renderpass.get());

            // scene uniform buffer
            {
                const auto size = sizeof(SceneParams) * frameCount;
                mSceneBuffer    = device.create<vk2s::DynamicBuffer>(vk::BufferCreateInfo({}, size, vk::BufferUsageFlagBits::eUniformBuffer), vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, frameCount);
            }

            // storage buffer (for picked ID)
            {
                const auto size = sizeof(ec2s::Entity);
                mPickedIDBuffer = device.create<vk2s::Buffer>(vk::BufferCreateInfo({}, size, vk::BufferUsageFlagBits::eStorageBuffer), vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            }

            // emitter uniform buffer
            {
                const auto size = sizeof(Emitter::Params) * kMaxEmitterNum * frameCount;
                mEmitterBuffer  = device.create<vk2s::DynamicBuffer>(vk::BufferCreateInfo({}, size, vk::BufferUsageFlagBits::eUniformBuffer), vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, frameCount);
            }

            // create bindgroup
            mSceneBindGroup = device.create<vk2s::BindGroup>(mGeometryPass.bindLayouts[0].get());

            mSceneBindGroup->bind(0, vk::DescriptorType::eUniformBufferDynamic, mSceneBuffer.get());

            mGBuffer.bindGroup = device.create<vk2s::BindGroup>(mLightingPass.bindLayouts[0].get());
            mGBuffer.bindGroup->bind(0, vk::DescriptorType::eSampledImage, mGBuffer.albedoTex);
            mGBuffer.bindGroup->bind(1, vk::DescriptorType::eSampledImage, mGBuffer.worldPosTex);
            mGBuffer.bindGroup->bind(2, vk::DescriptorType::eSampledImage, mGBuffer.normalTex);
            mGBuffer.bindGroup->bind(3, vk::DescriptorType::eSampledImage, mGBuffer.roughnessMetalnessTex);
            mGBuffer.bindGroup->bind(4, mNearestSampler.get());

            mLightingBindGroup = device.create<vk2s::BindGroup>(mLightingPass.bindLayouts[1].get());
            mLightingBindGroup->bind(0, vk::DescriptorType::eUniformBufferDynamic, mSceneBuffer.get());
            mLightingBindGroup->bind(1, vk::DescriptorType::eStorageBuffer, mPickedIDBuffer.get());
            mLightingBindGroup->bind(2, vk::DescriptorType::eUniformBufferDynamic, mEmitterBuffer.get());
            mLightingBindGroup->bind(3, vk::DescriptorType::eSampledImage, mDummyTexture);
            mLightingBindGroup->bind(4, mLinearSampler.get());

            // create commands and sync objects

            mCommands.resize(frameCount);
            mImageAvailableSems.resize(frameCount);
            mRenderCompletedSems.resize(frameCount);
            mFences.resize(frameCount);
            for (int i = 0; i < frameCount; ++i)
            {
                mCommands[i]            = device.create<vk2s::Command>();
                mImageAvailableSems[i]  = device.create<vk2s::Semaphore>();
                mRenderCompletedSems[i] = device.create<vk2s::Semaphore>();
                mFences[i]              = device.create<vk2s::Fence>();
            }
        }
        catch (std::exception& e)
        {
            std::cerr << e.what() << "\n";
        }
    }

    void Editor::init()
    {
        initVulkan();

        auto& window = common()->window;
        auto& scene  = common()->scene;

        // search camera entity
        if (scene.size<vk2s::Camera>() == 0)
        {
            mCameraEntity = scene.create<vk2s::Camera, EntityInfo>();

            auto& camera                           = scene.get<vk2s::Camera>(mCameraEntity);
            const auto [windowWidth, windowHeight] = window->getWindowSize();

            camera = vk2s::Camera(60., 1. * windowWidth / windowHeight);
            camera.setPos(glm::vec3(0.0, 0.8, 3.0));
            camera.setLookAt(glm::vec3(0.0, 0.8, -2.0));

            auto& entityInfo      = scene.get<EntityInfo>(mCameraEntity);
            entityInfo.entityID   = mCameraEntity;
            entityInfo.entityName = "Main Camera";
            entityInfo.groupName  = "Camera";
            entityInfo.editable   = true;
        }
        else
        {
            scene.each<vk2s::Camera>([&](ec2s::Entity entity, vk2s::Camera& camera) { mCameraEntity = entity; });
        }

        // set envmap
        scene.each<Emitter>(
            [&](const ec2s::Entity entity, const Emitter& emitter)
            {
                if (emitter.params.type == static_cast<int32_t>(Emitter::Type::eInfinite))
                {
                    if (emitter.emissiveTex)
                    {
                        mInfiniteEmitterEntity = entity;
                        mLightingBindGroup->bind(3, vk::DescriptorType::eSampledImage, emitter.emissiveTex);
                    }
                }
            });

        // update material binding
        scene.each<Material>(
            [&](Material& material)
            {
                // bind dummy texture
                if (!material.albedoTex)
                {
                    material.bindGroup->bind(1, vk::DescriptorType::eSampledImage, mDummyTexture);
                }
                // bind default sampler
                material.bindGroup->bind(2, mNearestSampler.get());
            });

        // member variables initialization
        mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
        mCurrentPath           = std::filesystem::current_path();
        mLastTime              = glfwGetTime();
        mNow                   = 0;

        mEnvmapBrowser      = ImGui::FileBrowser(ImGuiFileBrowserFlags_EnterNewFilename | ImGuiFileBrowserFlags_CreateNewDir | ImGuiFileBrowserFlags_ConfirmOnEnter | ImGuiFileBrowserFlags_SkipItemsCausingError);
        mMaterialTexBrowser = ImGui::FileBrowser(ImGuiFileBrowserFlags_EnterNewFilename | ImGuiFileBrowserFlags_CreateNewDir | ImGuiFileBrowserFlags_ConfirmOnEnter | ImGuiFileBrowserFlags_SkipItemsCausingError);
    }

    void Editor::update()
    {
        constexpr auto colorClearValue   = vk::ClearValue(std::array{ 0.1f, 0.1f, 0.1f, 0.0f });
        constexpr auto gbufferClearValue = vk::ClearValue(std::array{ 0.2f, 0.2f, 0.2f, 0.0f });
        constexpr auto depthClearValue   = vk::ClearValue(vk::ClearDepthStencilValue(1.0f, 0));
        // HACK:
        constexpr std::array clearValues = { gbufferClearValue, gbufferClearValue, gbufferClearValue, gbufferClearValue, depthClearValue };

        auto& device = common()->device;
        auto& window = common()->window;
        auto& scene  = common()->scene;

        const auto [windowWidth, windowHeight] = window->getWindowSize();
        const auto frameCount                  = window->getFrameCount();

        // pre-render-----------------------------------------

        // update time
        const double currentTime = glfwGetTime();
        const float deltaTime    = static_cast<float>(currentTime - mLastTime);
        mLastTime                = currentTime;

        {  // key input
            if (!window->update() || window->getKey(GLFW_KEY_ESCAPE))
            {
                exitApplication();
            }

            if (window->getKey(GLFW_KEY_F5) && scene.size<Mesh>() > 0 && scene.size<Emitter>() > 0)
            {
                mChangeDst = AppState::eRenderer;
            }

            // update camera
            const double speed      = kCameraMoveSpeed * deltaTime;
            const double mouseSpeed = kCameraViewpointSpeed * deltaTime;
            scene.get<vk2s::Camera>(mCameraEntity).update(window->getpGLFWWindow(), speed, mouseSpeed);

            // remove picked entity with delete
            if (mPickedEntity && window->getKey(GLFW_KEY_DELETE))
            {
                removeEntity(*mPickedEntity);
                mPickedEntity.reset();
            }

            // change Guizmo operation
            if (window->getKey(GLFW_KEY_F1))
            {
                mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
            }
            if (window->getKey(GLFW_KEY_F2))
            {
                mCurrentGizmoOperation = ImGuizmo::ROTATE;
            }
            if (window->getKey(GLFW_KEY_F3))
            {
                mCurrentGizmoOperation = ImGuizmo::SCALE;
            }
        }

        // wait and reset fence
        mFences[mNow]->wait();

        // ImGui
        updateAndRenderImGui(deltaTime);

        // update shader resource buffers
        updateShaderResources();

        // acquire next image from swapchain(window)
        const auto [imageIndex, resized] = window->acquireNextImage(mImageAvailableSems[mNow].get());

        if (resized)
        {
            onResized();
            return;
        }

        // render-----------------------------------------

        mFences[mNow]->reset();

        auto& command = mCommands[mNow];
        // start writing command
        command->begin();
        // geometry pass
        {
            command->beginRenderPass(mGeometryPass.renderpass.get(), 0, vk::Rect2D({ 0, 0 }, { windowWidth, windowHeight }), clearValues);

            command->setPipeline(mGeometryPass.pipeline);

            const vk::Viewport viewport(0.f, 0.f, static_cast<float>(windowWidth), static_cast<float>(windowHeight), 0.f, 1.f);
            const vk::Rect2D scissor({ 0, 0 }, window->getVkSwapchainExtent());
            command->setViewport(0, viewport);
            command->setScissor(0, scissor);

            command->setBindGroup(0, mSceneBindGroup.get(), { mNow * static_cast<uint32_t>(mSceneBuffer->getBlockSize()) });
            // draw call
            scene.each<Mesh, Material, Transform>(
                [&](Mesh& mesh, Material& material, Transform& transform)
                {
                    command->setBindGroup(1, transform.bindGroup.get(), { mNow * static_cast<uint32_t>(transform.uniformBuffer->getBlockSize()) });
                    command->setBindGroup(2, material.bindGroup.get(), { mNow * static_cast<uint32_t>(material.uniformBuffer->getBlockSize()) });
                    command->bindVertexBuffer(mesh.vertexBuffer.get());
                    command->bindIndexBuffer(mesh.indexBuffer.get());

                    command->drawIndexed(mesh.hostMesh.indices.size(), 1, 0, 0, 1);
                });

            command->endRenderPass();
        }

        // barrier
        {
            command->transitionImageLayout(mGBuffer.albedoTex.get(), vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);
            command->transitionImageLayout(mGBuffer.worldPosTex.get(), vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);
            command->transitionImageLayout(mGBuffer.normalTex.get(), vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);
            command->transitionImageLayout(mGBuffer.roughnessMetalnessTex.get(), vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);
        }

        // lighting pass
        {
            command->beginRenderPass(mLightingPass.renderpass.get(), imageIndex, vk::Rect2D({ 0, 0 }, { windowWidth, windowHeight }), colorClearValue);

            command->setPipeline(mLightingPass.pipeline);

            const vk::Viewport viewport(0.f, 0.f, static_cast<float>(windowWidth) * kRenderArea.x, static_cast<float>(windowHeight) * kRenderArea.y, 0.f, 1.f);
            const vk::Rect2D scissor({ 0, 0 }, window->getVkSwapchainExtent());
            command->setViewport(0, viewport);
            command->setScissor(0, scissor);

            command->setBindGroup(0, mGBuffer.bindGroup.get());
            command->setBindGroup(1, mLightingBindGroup.get(), { mNow * static_cast<uint32_t>(mSceneBuffer->getBlockSize()), mNow * static_cast<uint32_t>(mEmitterBuffer->getBlockSize()) });
            command->draw(4, 1, 0, 0);
            command->drawImGui();

            command->endRenderPass();
        }

        // barrier
        {
            command->transitionImageLayout(mGBuffer.albedoTex.get(), vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eColorAttachmentOptimal);
            command->transitionImageLayout(mGBuffer.worldPosTex.get(), vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eColorAttachmentOptimal);
            command->transitionImageLayout(mGBuffer.normalTex.get(), vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eColorAttachmentOptimal);
            command->transitionImageLayout(mGBuffer.roughnessMetalnessTex.get(), vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eColorAttachmentOptimal);
        }

        // end writing commands
        command->end();

        // execute
        command->execute(mFences[mNow], mImageAvailableSems[mNow], mRenderCompletedSems[mNow]);
        // present swapchain(window) image
        if (window->present(imageIndex, mRenderCompletedSems[mNow].get()))
        {
            onResized();
        }

        // post-render-----------------------------------------

        // change state
        if (mChangeDst)
        {
            device.waitIdle();
            changeState(*mChangeDst);
        }

        // update key input state
        mDragging = window->getMouseKey(GLFW_MOUSE_BUTTON_LEFT);

        // update frame index
        mNow = (mNow + 1) % frameCount;
    }

    Editor::~Editor()
    {
        auto& device = getCommonRegion()->device;

        for (auto& fence : mFences)
        {
            fence->wait();
        }

        for (auto& fence : mFences)
        {
            device.destroy(fence);
        }

        for (auto& sem : mImageAvailableSems)
        {
            device.destroy(sem);
        }

        for (auto& sem : mRenderCompletedSems)
        {
            device.destroy(sem);
        }

        for (auto& command : mCommands)
        {
            device.destroy(command);
        }

        device.destroyImGui();
    }

    void Editor::updateShaderResources()
    {
        auto& window = common()->window;
        auto& scene  = common()->scene;
        auto& camera = scene.get<vk2s::Camera>(mCameraEntity);

        const auto [mx, my]        = window->getMousePos();
        const auto [width, height] = window->getWindowSize();

        // scene information
        {
            const auto& view = camera.getViewMatrix();
            const auto& proj = camera.getProjectionMatrix();

            const auto renderAreaWidth  = width * kRenderArea.x;
            const auto renderAreaHeight = height * kRenderArea.y;

            const auto x = static_cast<float>(mx / renderAreaWidth);
            const auto y = static_cast<float>(my / renderAreaHeight);

            SceneParams sceneParams{
                .view      = view,
                .proj      = proj,
                .viewInv   = glm::inverse(view),
                .projInv   = glm::inverse(proj),
                .camPos    = glm::vec4(camera.getPos(), 1.0),
                .mousePos  = glm::vec2(x, y),
                .frameSize = glm::uvec2(renderAreaWidth, renderAreaHeight),
            };

            mSceneBuffer->write(&sceneParams, sizeof(SceneParams), mNow * mSceneBuffer->getBlockSize());
        }

        // read clicked pixel's entity
        if (isPointerOnRenderArea() && window->getMouseKey(GLFW_MOUSE_BUTTON_LEFT) && !ImGuizmo::IsUsing() && !mDragging)
        {
            mPickedIDBuffer->read(
                [&](const void* p)
                {
                    const auto hovered = *(reinterpret_cast<const ec2s::Entity*>(p));
                    if (hovered != 0 && (!mPickedEntity || *mPickedEntity != hovered))
                    {
                        mPickedEntity = hovered;
                    }
                },
                sizeof(ec2s::Entity), 0);
        }

        // write entity transform
        scene.each<Transform>([&](Transform& transform) { transform.uniformBuffer->write(&transform.params, sizeof(Transform::Params), mNow * transform.uniformBuffer->getBlockSize()); });

        // write entity material
        scene.each<Material>([&](Material& material) { material.uniformBuffer->write(&material.params, sizeof(Material::Params), mNow * material.uniformBuffer->getBlockSize()); });

        // write emitters
        {
            constexpr size_t size = sizeof(Emitter::Params) * kMaxEmitterNum;
            std::byte zeros[size] = { static_cast<std::byte>(0) };
            mEmitterBuffer->write(zeros, size, mNow * mEmitterBuffer->getBlockSize());

            std::vector<Emitter::Params> emitterParams;
            emitterParams.reserve(kMaxEmitterNum);

            scene.each<Emitter>(
                [&](const ec2s::Entity entity, Emitter& emitter)
                {
                    if (emitterParams.size() >= kMaxEmitterNum)
                    {
                        return;
                    }

                    if (scene.contains<Transform>(entity))
					{
						emitter.params.pos = scene.get<Transform>(entity).pos;
					}

                    emitterParams.emplace_back(emitter.params);
                });

            if (!emitterParams.empty())
            {
                mEmitterBuffer->write(emitterParams.data(), sizeof(Emitter::Params) * emitterParams.size(), mNow * mEmitterBuffer->getBlockSize());
            }

            // update bind group
            mLightingBindGroup->bind(3, vk::DescriptorType::eSampledImage, mDummyTexture);
            if (mInfiniteEmitterEntity)
            {
                auto& emitter       = scene.get<Emitter>(*mInfiniteEmitterEntity);
                if (emitter.emissiveTex)
                {
                    mLightingBindGroup->bind(3, vk::DescriptorType::eSampledImage, emitter.emissiveTex);
                }
            }
        }
    }

    void Editor::updateAndRenderImGui(const double deltaTime)
    {
        auto& device = common()->device;
        auto& window = common()->window;
        auto& scene  = common()->scene;
        auto& camera = scene.get<vk2s::Camera>(mCameraEntity);

        const auto [windowWidth, windowHeight] = window->getWindowSize();

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();
        ImGuizmo::SetRect(0, 0, windowWidth * kRenderArea.x, windowHeight * kRenderArea.y);

        ImGui::SetNextWindowPos(ImVec2(0, 0));  // left
        ImGui::SetNextWindowSize(ImVec2(windowWidth, windowHeight * 0.03));
        ImGui::Begin("MenuBar", nullptr, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

        if (ImGui::BeginMenuBar())
        {
            // TODO: scene save/load, cut/copy/paste, undo/redo
            // the architecture of the GUI needs to be fundamentally reconstruct

            if (ImGui::BeginMenu("Add"))
            {
                if (ImGui::BeginMenu("Emitter"))
                {
                    if (ImGui::MenuItem("Point", nullptr))
                    {
                        // HACK: for unique Emitter name
                        static uint32_t pointEmitterNum = 0;

                        const auto added = scene.create<Emitter, Transform, EntityInfo>();
                        {
                            auto& emitter       = scene.get<Emitter>(added);
                            emitter.params.type = static_cast<std::underlying_type_t<Emitter::Type>>(Emitter::Type::ePoint);
                        }

                        {
                            auto& info      = scene.get<EntityInfo>(added);
                            info.entityID   = added;
                            info.entityName = std::string("point emitter ") + std::to_string(pointEmitterNum);
                            info.groupName  = "emitter";
                            info.editable   = true;
                        }

                        {
                            auto& transform = scene.get<Transform>(added);

                            transform.params.world             = glm::identity<glm::mat4>();
                            transform.params.worldInvTranspose = glm::identity<glm::mat4>();
                            transform.params.vel               = glm::vec3(0.f);
                            transform.params.entitySlot        = static_cast<uint32_t>((added & ec2s::kEntitySlotMask) >> ec2s::kEntitySlotShiftWidth);
                            transform.params.entityIndex       = static_cast<uint32_t>(added & ec2s::kEntityIndexMask);

                            const auto frameCount = window->getFrameCount();
                            const auto size       = sizeof(Transform::Params) * frameCount;
                            transform.uniformBuffer =
                                device.create<vk2s::DynamicBuffer>(vk::BufferCreateInfo({}, size, vk::BufferUsageFlagBits::eUniformBuffer), vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, frameCount);
                            for (int i = 0; i < frameCount; ++i)
                            {
                                transform.uniformBuffer->write(&transform.params, sizeof(Transform::Params), i * transform.uniformBuffer->getBlockSize());
                            }

                            transform.bindGroup = device.create<vk2s::BindGroup>(mGeometryPass.bindLayouts[1].get());
                            transform.bindGroup->bind(0, vk::DescriptorType::eUniformBufferDynamic, transform.uniformBuffer.get());
                        }

                        mPickedEntity = added;
                        ++pointEmitterNum;
                    }
                    else if (ImGui::MenuItem("Infinite", nullptr))
                    {
                        mEnvmapBrowser.SetTitle("load environment map image");
                        mEnvmapBrowser.SetTypeFilters({ ".png", ".jpg" });
                        mEnvmapBrowser.Open();

                        if (!mInfiniteEmitterEntity)
                        {
                            mInfiniteEmitterEntity = scene.create<Emitter, EntityInfo>();
                        }

                        {
                            auto& info      = scene.get<EntityInfo>(*mInfiniteEmitterEntity);
                            info.entityID   = *mInfiniteEmitterEntity;
                            info.entityName = std::string("Infinite emitter");
                            info.groupName  = "emitter";
                            info.editable   = true;
                        }
                    }

                    ImGui::EndMenu();
                }

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Mode"))
            {
                if (ImGui::MenuItem("Renderer", nullptr) && scene.size<Mesh>() != 0 && scene.size<Emitter>() != 0)
                {
                    mChangeDst = AppState::eRenderer;
                }

                ImGui::EndMenu();
            }

            ImGui::EndMenuBar();
        }
        ImGui::End();

        {
            ImGui::SetNextWindowPos(ImVec2(0, windowHeight * kRenderArea.y));  // bottom
            ImGui::SetNextWindowSize(ImVec2(windowWidth, windowHeight * (1.0f - kRenderArea.y)));
            ImGui::Begin("FileExplorer", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

            ImGui::Text(to_string(mCurrentPath).c_str());
            ImGui::SeparatorText("Model explorer");

            if (ImGui::Button("<="))
            {
                mCurrentPath = mCurrentPath.parent_path();
            }

            for (const auto& entry : std::filesystem::directory_iterator(mCurrentPath))
            {
                if (entry.is_directory())
                {
                    ImGui::SetNextItemOpen(false);

                    if (ImGui::TreeNode(to_string(entry.path().filename()).c_str()))
                    {
                        mCurrentPath = entry.path();

                        ImGui::TreePop();
                    }
                }
                else
                {
                    if (ImGui::Selectable(to_string(entry.path().filename()).c_str()))
                    {
                        addEntity(entry.path());
                    }
                }
            }

            ImGui::End();
        }

        {
            ImGui::SetNextWindowPos(ImVec2(windowWidth * kRenderArea.x, windowHeight * kMenuBarSize));  // right
            ImGui::SetNextWindowSize(ImVec2(windowWidth * (1.f - kRenderArea.x), windowHeight * (kRenderArea.y - kMenuBarSize)));
            ImGui::Begin("SceneEditor", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

            ImGui::Text("Scene Editor");

            scene.each<EntityInfo>(
                [&](ec2s::Entity entity, EntityInfo& info)
                {
                    std::string viewing = "[" + std::to_string(entity & ec2s::kEntityIndexMask) + "]: " + info.groupName + "/" + info.entityName;
                    const bool picked   = mPickedEntity && entity == *mPickedEntity;

                    if (ImGui::Selectable(viewing.c_str(), picked) && info.editable)
                    {
                        mPickedEntity = entity;
                    }
                });

            ImGui::SeparatorText("Information");
            ImGui::Text("device: %s", device.getPhysicalDeviceName().data());
            ImGui::Text("fps: %.3lf", 1. / deltaTime);
            const auto& pos    = camera.getPos();
            const auto& lookAt = camera.getLookAt();
            ImGui::Text("pos: (%.3lf, %.3lf, %.3lf)", pos.x, pos.y, pos.z);
            ImGui::Text("lookat: (%.3lf, %.3lf, %.3lf)", lookAt.x, lookAt.y, lookAt.z);

            // transform editing
            if (mPickedEntity && scene.contains<Transform>(*mPickedEntity))
            {
                ImGui::SeparatorText("Manipulation");
                ImGui::Text("Picked: %s", scene.get<EntityInfo>(*mPickedEntity).entityName.c_str());

                auto& transform = scene.get<Transform>(*mPickedEntity);

                const auto& viewMat = camera.getViewMatrix();
                // copy for modification
                glm::mat4 projectionMat = camera.getProjectionMatrix();
                // HACK: too adhoc (for Vulkan's inverse Y)
                projectionMat[1][1] *= -1.f;

                // position editor (translation)
                ImGui::InputFloat3("Translate", glm::value_ptr(transform.pos));
                // rotation editor (Euler angles)
                glm::vec3 rotInEuler = glm::degrees(glm::eulerAngles(transform.rot));
                ImGui::InputFloat3("Rotate", glm::value_ptr(rotInEuler));
                transform.rot = glm::quat(glm::radians(rotInEuler));
                // scale editor
                ImGui::InputFloat3("Scale", glm::value_ptr(transform.scale));

                // update buffer value
                transform.params.update(transform.pos, transform.rot, transform.scale);

                // manipulate (the entity must not be picked during the operation, so the state is preserved)
                ImGuizmo::Manipulate(glm::value_ptr(viewMat), glm::value_ptr(projectionMat), mCurrentGizmoOperation, ImGuizmo::WORLD, glm::value_ptr(transform.params.world));

                glm::vec3 translation, rotation, scale;
                ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(transform.params.world), glm::value_ptr(translation), glm::value_ptr(rotation), glm::value_ptr(scale));

                transform.pos   = translation;
                transform.rot   = glm::quat(glm::radians(rotation));
                transform.scale = scale;
                // re-calculate
                transform.params.update(transform.pos, transform.rot, transform.scale);
            }

            if (mPickedEntity && scene.contains<Material>(*mPickedEntity) && scene.contains<Transform>(*mPickedEntity))
            {  // material
                ImGui::SeparatorText("Material");

                auto& material      = scene.get<Material>(*mPickedEntity);
                auto& transform     = scene.get<Transform>(*mPickedEntity);
                bool enableEmissive = false;

                // show material editing UI
                material.updateAndDrawMaterialUI(enableEmissive);

                if (enableEmissive)
                {
                    // add emissive component
                    if (!scene.contains<Emitter>(*mPickedEntity))
                    {
                        scene.add<Emitter>(*mPickedEntity);
                    }

                    auto& emitter          = scene.get<Emitter>(*mPickedEntity);
                    emitter.attachedEntity = *mPickedEntity;

                    emitter.params.emissive = material.params.emissive;
                    emitter.params.type     = static_cast<std::underlying_type_t<Emitter::Type>>(Emitter::Type::eArea);
                    emitter.params.faceNum  = scene.get<Mesh>(*mPickedEntity).hostMesh.indices.size() / 3;
                }
                else if (glm::dot(material.params.emissive, material.params.emissive) == 0. && scene.contains<Emitter>(*mPickedEntity))
                {
                    // remove emissive component
                    scene.remove<Emitter>(*mPickedEntity);
                }
            }

            if (mPickedEntity && !scene.contains<Material>(*mPickedEntity) && scene.contains<Emitter>(*mPickedEntity))
            {
                auto& emitter = scene.get<Emitter>(*mPickedEntity);
                ImGui::ColorEdit3("Emissive", glm::value_ptr(emitter.params.emissive), ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
            }

            if (mPickedEntity && scene.contains<vk2s::Camera>(*mPickedEntity))
            {
                ImGui::SeparatorText("Camera");

                auto& camera = scene.get<vk2s::Camera>(*mPickedEntity);

                auto pos       = camera.getPos();
                auto lookAt    = camera.getLookAt();
                auto fov       = camera.getFOV();
                auto aspect    = camera.getAspect();
                auto nearPlane = camera.getNear();  // "near" is already defined by windows.h
                auto farPlane  = camera.getFar();   // "far" is already defined by windows.h

                if (ImGui::InputFloat3("Position", glm::value_ptr(pos)))
                {
                    camera.setPos(pos);
                }
                if (ImGui::InputFloat3("Look at", glm::value_ptr(lookAt)))
                {
                    camera.setLookAt(lookAt);
                }
                if (ImGui::InputDouble("Field of view", &fov))
                {
                    camera.setFOV(fov);
                }
                if (ImGui::InputDouble("Aspect ratio", &aspect))
                {
                    camera.setAspect(aspect);
                }
                if (ImGui::InputDouble("Near", &nearPlane))
                {
                    camera.setNear(nearPlane);
                }
                if (ImGui::InputDouble("Far", &farPlane))
                {
                    camera.setFar(farPlane);
                }
            }

            ImGui::End();
        }

        mEnvmapBrowser.Display();
        mMaterialTexBrowser.Display();

        if (mInfiniteEmitterEntity && mEnvmapBrowser.HasSelected())
        {
            const std::string& path = mEnvmapBrowser.GetSelected().string();
            mEnvmapBrowser.ClearSelected();

            auto& emitter       = scene.get<Emitter>(*mInfiniteEmitterEntity);
            emitter.params.type = static_cast<std::underlying_type_t<Emitter::Type>>(Emitter::Type::eInfinite);

            emitter.emissiveTex = device.create<vk2s::Image>(path);

            if (emitter.emissiveTex)
            {
                mLightingBindGroup->bind(3, vk::DescriptorType::eSampledImage, emitter.emissiveTex);
            }
            std::cout << "loaded envmap image: " << mEnvmapBrowser.GetSelected().string() << std::endl;
        }

        ImGui::Render();
    }

    void Editor::onResized()
    {
        auto& device = getCommonRegion()->device;
        auto& window = getCommonRegion()->window;

        window->resize();

        const auto [width, height] = window->getWindowSize();

        createGBuffer();

        std::vector<Handle<vk2s::Image>> images = { mGBuffer.albedoTex, mGBuffer.worldPosTex, mGBuffer.normalTex, mGBuffer.roughnessMetalnessTex };
        mGeometryPass.renderpass->recreateFrameBuffers(images, mGBuffer.depthBuffer);

        mLightingPass.renderpass->recreateFrameBuffers(window.get());

        // re-binding G-buffers
        mGBuffer.bindGroup->bind(0, vk::DescriptorType::eSampledImage, mGBuffer.albedoTex);
        mGBuffer.bindGroup->bind(1, vk::DescriptorType::eSampledImage, mGBuffer.worldPosTex);
        mGBuffer.bindGroup->bind(2, vk::DescriptorType::eSampledImage, mGBuffer.normalTex);
        mGBuffer.bindGroup->bind(3, vk::DescriptorType::eSampledImage, mGBuffer.roughnessMetalnessTex);
    }

    bool Editor::isPointerOnRenderArea() const
    {
        auto& window = common()->window;

        const auto [mx, my]        = window->getMousePos();
        const auto [width, height] = window->getWindowSize();

        return mx <= width * kRenderArea.x && my <= height * kRenderArea.y && mx > 0. && my > 0.;
    }

}  // namespace palm