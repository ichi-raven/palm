/*****************************************************************/ /**
 * @file   Editor.cpp
 * @brief  source file of Editor class
 * 
 * @author ichi-raven
 * @date   February 2024
 *********************************************************************/

//#define VULKAN_HPP_NO_CONSTRUCTORS

#include "../include/States/Editor.hpp"
#include "../include/Mesh.hpp"
#include "../include/Material.hpp"
#include "../include/EntityInfo.hpp"
#include "../include/Transform.hpp"

#include <stb_image.h>
#include <glm/glm.hpp>

#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <ImGuizmo.h>

#include <iostream>
#include <filesystem>

namespace palm
{
    void Editor::addEntity(const std::filesystem::path& path)
    {
        auto& device = common()->device;
        auto& window = getCommonRegion()->window;
        auto& scene  = common()->scene;

        vk2s::Scene model(path.string());

        const std::vector<vk2s::Mesh>& hostMeshes        = model.getMeshes();
        const std::vector<vk2s::Material>& hostMaterials = model.getMaterials();

        assert(hostMaterials.size() == hostMeshes.size() || !"The number of mesh is different from the number of material!");

        for (const auto& hostMesh : hostMeshes)
        {
            const auto entity = scene.create<Mesh, Material, EntityInfo, Transform>();
            auto& mesh        = scene.get<Mesh>(entity);
            auto& material    = scene.get<Material>(entity);
            auto& info        = scene.get<EntityInfo>(entity);
            auto& transform   = scene.get<Transform>(entity);
            mesh.hostMesh     = hostMesh;

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
            // materials
            {
                const auto ubSize = sizeof(vk2s::Material) * hostMaterials.size();
                vk::BufferCreateInfo ci({}, ubSize, vk::BufferUsageFlagBits::eStorageBuffer);
                vk::MemoryPropertyFlags fb = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;

                material.uniformBuffer = device.create<vk2s::Buffer>(ci, fb);
                material.uniformBuffer->write(hostMaterials.data(), ubSize);
            }

            // information
            {
                info.entityName = mesh.hostMesh.nodeName;
                info.entityID   = entity;

                info.groupName       = path.filename().string();
                const size_t dot_pos = info.groupName.find_last_of('.');
                if (dot_pos != std::string_view::npos)
                {
                    info.groupName = info.groupName.substr(0, dot_pos);
                }
            }

            // transform
            {
                transform.params.world             = glm::identity<glm::mat4>();
                transform.params.worldInvTranspose = glm::transpose(glm::inverse(transform.params.world));
                transform.params.vel               = glm::vec3(0.f);
                transform.params.padding           = { 0.f };

                const auto frameCount = window->getFrameCount();
                const auto size       = sizeof(Transform::Params) * frameCount;
                transform.entityBuffer =
                    device.create<vk2s::DynamicBuffer>(vk::BufferCreateInfo({}, size, vk::BufferUsageFlagBits::eUniformBuffer), vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, frameCount);
                for (int i = 0; i < frameCount; ++i)
                {
                    transform.entityBuffer->write(&transform.params, sizeof(Transform::Params), i * transform.entityBuffer->getBlockSize());
                }

                transform.bindGroup = device.create<vk2s::BindGroup>(mGeometryPass.bindLayouts[1].get());
                transform.bindGroup->bind(0, vk::DescriptorType::eUniformBufferDynamic, transform.entityBuffer.get());
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

        auto& mesh = scene.get<Mesh>(entity);
        device.destroy(mesh.blas);
        device.destroy(mesh.vertexBuffer);
        device.destroy(mesh.indexBuffer);
        device.destroy(mesh.instanceBuffer);

        auto& material = scene.get<Material>(entity);
        device.destroy(material.uniformBuffer);
        device.destroy(material.albedoTex);
        device.destroy(material.normalMapTex);
        device.destroy(material.metalnessTex);
        device.destroy(material.roughnessTex);
        device.destroy(material.bindGroup);

        auto& transform = scene.get<Transform>(entity);
        device.destroy(transform.entityBuffer);

        scene.destroy(entity);
    }

    void Editor::createGBufferPass()
    {
        auto& device = getCommonRegion()->device;
        auto& window = getCommonRegion()->window;

        const auto [windowWidth, windowHeight] = window->getWindowSize();
        const auto frameCount                  = window->getFrameCount();

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

            mGBuffer.albedoTex   = device.create<vk2s::Image>(ci, vk::MemoryPropertyFlagBits::eDeviceLocal, size, vk::ImageAspectFlagBits::eColor);
            mGBuffer.worldPosTex = device.create<vk2s::Image>(ci, vk::MemoryPropertyFlagBits::eDeviceLocal, size, vk::ImageAspectFlagBits::eColor);
            mGBuffer.normalTex   = device.create<vk2s::Image>(ci, vk::MemoryPropertyFlagBits::eDeviceLocal, size, vk::ImageAspectFlagBits::eColor);

            UniqueHandle<vk2s::Command> cmd = device.create<vk2s::Command>();
            cmd->begin(true);
            cmd->transitionImageLayout(mGBuffer.albedoTex.get(), vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal);
            cmd->transitionImageLayout(mGBuffer.worldPosTex.get(), vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal);
            cmd->transitionImageLayout(mGBuffer.normalTex.get(), vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal);
            cmd->end();
            cmd->execute();
        }

        // geometry pass
        {
            std::vector<Handle<vk2s::Image>> images = { mGBuffer.albedoTex, mGBuffer.worldPosTex, mGBuffer.normalTex };

            mGeometryPass.renderpass = device.create<vk2s::RenderPass>(images, mGBuffer.depthBuffer, vk::AttachmentLoadOp::eClear);

            mGeometryPass.vs = device.create<vk2s::Shader>("../../shaders/Slang/rasterize/Geometry.slang", "vsmain");
            mGeometryPass.fs = device.create<vk2s::Shader>("../../shaders/Slang/rasterize/Geometry.slang", "fsmain");

            std::vector bindings0 = {
                // Scene MVP information
                // VP
                vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBufferDynamic, 1, vk::ShaderStageFlagBits::eAll),
            };

            std::vector bindings1 = {
                // Entity information
                vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBufferDynamic, 1, vk::ShaderStageFlagBits::eAll),
            };

            mGeometryPass.bindLayouts.emplace_back(device.create<vk2s::BindLayout>(bindings0));
            mGeometryPass.bindLayouts.emplace_back(device.create<vk2s::BindLayout>(bindings1));

            vk::VertexInputBindingDescription inputBinding(0, sizeof(Mesh::Vertex));
            const auto& inputAttributes = std::get<0>(mGeometryPass.vs->getReflection());
            vk::Viewport viewport(0.f, 0.f, static_cast<float>(windowWidth), static_cast<float>(windowHeight), 0.f, 1.f);
            vk::Rect2D scissor({ 0, 0 }, window->getVkSwapchainExtent());
            vk::PipelineColorBlendAttachmentState colorBlendAttachment(VK_FALSE);
            colorBlendAttachment.setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
            std::array attachments = { colorBlendAttachment, colorBlendAttachment, colorBlendAttachment };

            vk2s::Pipeline::GraphicsPipelineInfo gpi{
                .vs            = mGeometryPass.vs,
                .fs            = mGeometryPass.fs,
                .bindLayouts   = mGeometryPass.bindLayouts,
                .renderPass    = mGeometryPass.renderpass,
                .inputState    = vk::PipelineVertexInputStateCreateInfo({}, inputBinding, inputAttributes),
                .inputAssembly = vk::PipelineInputAssemblyStateCreateInfo({}, vk::PrimitiveTopology::eTriangleList),
                .viewportState = vk::PipelineViewportStateCreateInfo({}, 1, &viewport, 1, &scissor),
                .rasterizer    = vk::PipelineRasterizationStateCreateInfo({}, VK_FALSE, VK_FALSE, vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eClockwise, VK_FALSE, 0.0f, 0.0f, 0.0f, 1.0f),
                .multiSampling = vk::PipelineMultisampleStateCreateInfo({}, vk::SampleCountFlagBits::e1, VK_FALSE),
                .depthStencil  = vk::PipelineDepthStencilStateCreateInfo({}, VK_TRUE, VK_TRUE, vk::CompareOp::eLess, VK_FALSE),
                .colorBlending = vk::PipelineColorBlendStateCreateInfo({}, VK_FALSE, vk::LogicOp::eCopy, attachments),
            };

            mGeometryPass.pipeline = device.create<vk2s::Pipeline>(gpi);
        }

        mGBuffer.bindGroup = device.create<vk2s::BindGroup>(mLightingPass.bindLayouts[0].get());
        mGBuffer.bindGroup->bind(0, vk::DescriptorType::eSampledImage, mGBuffer.albedoTex);
        mGBuffer.bindGroup->bind(1, vk::DescriptorType::eSampledImage, mGBuffer.worldPosTex);
        mGBuffer.bindGroup->bind(2, vk::DescriptorType::eSampledImage, mGBuffer.normalTex);
        mGBuffer.bindGroup->bind(3, mDefaultSampler.get());
    }

    void Editor::initVulkan()
    {
        auto& device = getCommonRegion()->device;
        auto& window = getCommonRegion()->window;

        const auto [windowWidth, windowHeight] = window->getWindowSize();
        const auto frameCount                  = window->getFrameCount();

        try
        {
            // default sampler
            mDefaultSampler = device.create<vk2s::Sampler>(vk::SamplerCreateInfo());

            // create G-Buffer
            createGBufferPass();

            {  // lighting pass
                mLightingPass.renderpass = device.create<vk2s::RenderPass>(window.get(), vk::AttachmentLoadOp::eClear);
                mLightingPass.vs         = device.create<vk2s::Shader>("../../shaders/Slang/rasterize/Lighting.slang", "vsmain");
                mLightingPass.fs         = device.create<vk2s::Shader>("../../shaders/Slang/rasterize/Lighting.slang", "fsmain");

                std::array bindings0 = {
                    vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eSampledImage, 1, vk::ShaderStageFlagBits::eAll),
                    vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eSampledImage, 1, vk::ShaderStageFlagBits::eAll),
                    vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eSampledImage, 1, vk::ShaderStageFlagBits::eAll),
                    vk::DescriptorSetLayoutBinding(3, vk::DescriptorType::eSampler, 1, vk::ShaderStageFlagBits::eAll),
                };

                //std::vector bindings1 = {
                //    vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBufferDynamic, 1, vk::ShaderStageFlagBits::eAll),
                //    vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eAll),
                //    vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eCombinedImageSampler, Material::kDefaultTexNum, vk::ShaderStageFlagBits::eAll),
                //};

                mLightingPass.bindLayouts.emplace_back(device.create<vk2s::BindLayout>(bindings0));
                //mLightingPass.bindLayouts.emplace_back(device.create<vk2s::BindLayout>(bindings1));

                vk::Viewport viewport(0.f, 0.f, static_cast<float>(windowWidth), static_cast<float>(windowHeight), 0.f, 1.f);
                vk::Rect2D scissor({ 0, 0 }, window->getVkSwapchainExtent());
                vk::PipelineColorBlendAttachmentState colorBlendAttachment(VK_FALSE);
                colorBlendAttachment.setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);

                vk2s::Pipeline::GraphicsPipelineInfo gpi{
                    .vs            = mLightingPass.vs,
                    .fs            = mLightingPass.fs,
                    .bindLayouts   = mLightingPass.bindLayouts,
                    .renderPass    = mLightingPass.renderpass,
                    .inputState    = vk::PipelineVertexInputStateCreateInfo(),
                    .inputAssembly = vk::PipelineInputAssemblyStateCreateInfo({}, vk::PrimitiveTopology::eTriangleStrip),
                    .viewportState = vk::PipelineViewportStateCreateInfo({}, 1, &viewport, 1, &scissor),
                    .rasterizer    = vk::PipelineRasterizationStateCreateInfo({}, VK_FALSE, VK_FALSE, vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eClockwise, VK_FALSE, 0.0f, 0.0f, 0.0f, 1.0f),
                    .multiSampling = vk::PipelineMultisampleStateCreateInfo({}, vk::SampleCountFlagBits::e1, VK_FALSE),
                    .depthStencil  = vk::PipelineDepthStencilStateCreateInfo({}, VK_TRUE, VK_TRUE, vk::CompareOp::eLess, VK_FALSE),
                    .colorBlending = vk::PipelineColorBlendStateCreateInfo({}, VK_FALSE, vk::LogicOp::eCopy, 1, &colorBlendAttachment),
                };

                mLightingPass.pipeline = device.create<vk2s::Pipeline>(gpi);
            }

            // initialize ImGui
            device.initImGui(frameCount, window.get(), mLightingPass.renderpass.get());

            // uniform buffer
            {
                const auto size = sizeof(SceneParams) * frameCount;
                mSceneBuffer    = device.create<vk2s::DynamicBuffer>(vk::BufferCreateInfo({}, size, vk::BufferUsageFlagBits::eUniformBuffer), vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, frameCount);
            }

            // create bindgroup
            mSceneBindGroup = device.create<vk2s::BindGroup>(mGeometryPass.bindLayouts[0].get());

            mSceneBindGroup->bind(0, vk::DescriptorType::eUniformBufferDynamic, mSceneBuffer.get());

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

        if (scene.size<vk2s::Camera>() == 0)
        {
            mCameraEntity = scene.create<vk2s::Camera>();

            auto& camera                           = scene.get<vk2s::Camera>(mCameraEntity);
            const auto [windowWidth, windowHeight] = window->getWindowSize();

            camera = vk2s::Camera(60., 1. * windowWidth / windowHeight);
            camera.setPos(glm::vec3(0.0, 0.8, 3.0));
            camera.setLookAt(glm::vec3(0.0, 0.8, -2.0));
        }
        else
        {
            scene.each<vk2s::Camera>([&](ec2s::Entity entity, vk2s::Camera& camera) { mCameraEntity = entity; });
        }

        mLastTime = glfwGetTime();
        mNow      = 0;
    }

    void Editor::update()
    {
        constexpr auto colorClearValue   = vk::ClearValue(std::array{ 0.2f, 0.2f, 0.2f, 1.0f });
        constexpr auto gbufferClearValue = vk::ClearValue(std::array{ 0.1f, 0.1f, 0.1f, 1.0f });
        constexpr auto depthClearValue   = vk::ClearValue(vk::ClearDepthStencilValue(1.0f, 0));
        constexpr std::array clearValues = { gbufferClearValue, gbufferClearValue, gbufferClearValue, depthClearValue };

        auto& device = common()->device;
        auto& window = common()->window;
        auto& scene  = common()->scene;

        const auto [windowWidth, windowHeight] = window->getWindowSize();
        const auto frameCount                  = window->getFrameCount();

        static bool resizedWhenPresent = false;

        if (!window->update() || window->getKey(GLFW_KEY_ESCAPE))
        {
            exitApplication();
        }

        // update time
        const double currentTime = glfwGetTime();
        float deltaTime          = static_cast<float>(currentTime - mLastTime);
        mLastTime                = currentTime;

        // update camera
        const double speed      = 2.0f * deltaTime;
        const double mouseSpeed = 0.7f * deltaTime;
        scene.get<vk2s::Camera>(mCameraEntity).update(window->getpGLFWWindow(), speed, mouseSpeed);

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

        mFences[mNow]->reset();

        auto& command = mCommands[mNow];
        // start writing command
        command->begin();
        // geometry pass
        {
            command->beginRenderPass(mGeometryPass.renderpass.get(), 0, vk::Rect2D({ 0, 0 }, { windowWidth, windowHeight }), clearValues);

            command->setPipeline(mGeometryPass.pipeline);

            command->setBindGroup(0, mSceneBindGroup.get(), { mNow * static_cast<uint32_t>(mSceneBuffer->getBlockSize()) });
            // draw call
            scene.each<Mesh, Material, Transform>(
                [&](Mesh& mesh, Material& material, Transform& transform)
                {
                    command->setBindGroup(1, transform.bindGroup.get(), { mNow * static_cast<uint32_t>(transform.entityBuffer->getBlockSize()) });
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
        }

        // lighting pass
        {
            command->beginRenderPass(mLightingPass.renderpass.get(), imageIndex, vk::Rect2D({ 0, 0 }, { windowWidth, windowHeight }), colorClearValue);

            command->setPipeline(mLightingPass.pipeline);
            command->setBindGroup(0, mGBuffer.bindGroup.get());
            //command->setBindGroup(1, mSceneBindGroup.get(), { mNow * static_cast<uint32_t>(mSceneBuffer->getBlockSize()) });
            command->draw(4, 1, 0, 0);
            command->drawImGui();

            command->endRenderPass();
        }

        // barrier
        {
            command->transitionImageLayout(mGBuffer.albedoTex.get(), vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eColorAttachmentOptimal);
            command->transitionImageLayout(mGBuffer.worldPosTex.get(), vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eColorAttachmentOptimal);
            command->transitionImageLayout(mGBuffer.normalTex.get(), vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eColorAttachmentOptimal);
        }

        // end writing commands
        command->end();

        // execute
        command->execute(mFences[mNow], mImageAvailableSems[mNow], mRenderCompletedSems[mNow]);
        // present swapchain(window) image
        if (window->present(imageIndex, mRenderCompletedSems[mNow].get()))
        {
            onResized();
            return;
        }

        // change state
        if (mChangeDst)
        {
            device.waitIdle();
            changeState(*mChangeDst);
        }

        // update frame index
        mNow = (mNow + 1) % frameCount;
    }

    Editor::~Editor()
    {
        for (auto& fence : mFences)
        {
            fence->wait();
        }
        auto& device = getCommonRegion()->device;
        device.destroyImGui();
    }

    void Editor::updateShaderResources()
    {
        auto& scene  = common()->scene;
        auto& camera = scene.get<vk2s::Camera>(mCameraEntity);

        // scene information
        {
            //const auto& view = glm::transpose(mCamera.getViewMatrix()); // DEBUG!!
            //const auto& proj = glm::transpose(mCamera.getProjectionMatrix()); // DEBUG!!
            const auto& view = camera.getViewMatrix();
            const auto& proj = camera.getProjectionMatrix();

            SceneParams sceneParams{
                .view    = view,
                .proj    = proj,
                .viewInv = glm::inverse(view),
                .projInv = glm::inverse(proj),
                .camPos  = glm::vec4(camera.getPos(), 1.0),
            };

            mSceneBuffer->write(&sceneParams, sizeof(SceneParams), mNow * mSceneBuffer->getBlockSize());
        }

        // entity informations
        scene.each<Transform>([&](Transform& transform) { transform.entityBuffer->write(&transform.params, sizeof(Transform::Params), mNow * transform.entityBuffer->getBlockSize()); });
    }

    void Editor::updateAndRenderImGui(const double deltaTime)
    {
        auto& device = common()->device;
        auto& window = common()->window;
        auto& scene  = common()->scene;
        auto& camera = scene.get<vk2s::Camera>(mCameraEntity);

        const auto [windowWidth, windowHeight] = window->getWindowSize();

        constexpr auto kFontScale = 1.5f;

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();
        ImGuizmo::SetRect(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, windowWidth, windowHeight);

        ImGui::SetNextWindowPos(ImVec2(0, 0));  // left
        ImGui::SetNextWindowSize(ImVec2(windowWidth, windowHeight * 0.03));
        ImGui::Begin("MenuBar", NULL, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        ImGui::SetWindowFontScale(kFontScale);

        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                ImGui::MenuItem("New", NULL);
                ImGui::MenuItem("Open", NULL);
                ImGui::MenuItem("Save", NULL);
                ImGui::MenuItem("Save As", NULL);
                ImGui::MenuItem("Exit", NULL);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Edit"))
            {
                ImGui::MenuItem("Cut", NULL);
                ImGui::MenuItem("Copy", NULL);
                ImGui::MenuItem("Paste", NULL);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Mode"))
            {
                if (ImGui::MenuItem("Renderer", NULL) && scene.size<Mesh>() != 0)
                {
                    mChangeDst = AppState::eRenderer;
                }
                if (ImGui::MenuItem("MaterialViewer", NULL) && mPickedEntity)
                {
                    mChangeDst = AppState::eMaterialViewer;
                }
                ImGui::EndMenu();
            }

            ImGui::EndMenuBar();
        }
        ImGui::End();

        {
            ImGui::SetNextWindowPos(ImVec2(0, windowHeight * 0.80));  // bottom
            ImGui::SetNextWindowSize(ImVec2(windowWidth, windowHeight * 0.20));
            ImGui::Begin("FileExplorer", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
            ImGui::SetWindowFontScale(kFontScale);

            static std::filesystem::path current = std::filesystem::current_path();

            ImGui::Text(current.string().c_str());

            if (ImGui::Button("<-"))
            {
                current = current.parent_path();
            }

            for (const auto& entry : std::filesystem::directory_iterator(current))
            {
                if (entry.is_directory())
                {
                    ImGui::SetNextItemOpen(false);
                    if (ImGui::TreeNode(entry.path().filename().string().c_str()))
                    {
                        current = entry.path();

                        ImGui::TreePop();
                    }
                }
                else
                {
                    if (ImGui::Selectable(entry.path().filename().string().c_str()))
                    {
                        addEntity(entry.path());
                    }
                }
            }

            ImGui::End();
        }

        {
            ImGui::SetNextWindowPos(ImVec2(windowWidth * 0.70, windowHeight * 0.03));  // right
            ImGui::SetNextWindowSize(ImVec2(windowWidth * 0.30, windowHeight * 0.77));
            ImGui::Begin("SceneEditor", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
            ImGui::SetWindowFontScale(kFontScale);

            ImGui::Text("Scene Editor");

            scene.each<EntityInfo>(
                [&](ec2s::Entity entity, EntityInfo& info)
                {
                    std::string viewing = info.groupName + "/" + info.entityName + " entity[" + std::to_string(entity & ec2s::kEntityIndexMask) + "]";
                    const bool picked   = mPickedEntity && entity == *mPickedEntity;

                    if (ImGui::Selectable(viewing.c_str(), picked))
                    {
                        mPickedEntity = entity;
                    }

                    if (ImGui::IsItemHovered() && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
                    {
                        removeEntity(entity);
                    }
                });

            if (mPickedEntity && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
            {
                removeEntity(*mPickedEntity);
            }

            ImGui::Spacing();
            ImGui::Text("Information");
            ImGui::Text("device = %s", device.getPhysicalDeviceName().data());
            ImGui::Text("fps = %lf", 1. / deltaTime);
            const auto& pos    = camera.getPos();
            const auto& lookAt = camera.getLookAt();
            ImGui::Text("pos = (%lf, %lf, %lf)", pos.x, pos.y, pos.z);
            ImGui::Text("lookat = (%lf, %lf, %lf)", lookAt.x, lookAt.y, lookAt.z);

            ImGui::Spacing();
            // transform editing (experimental)
            if (mPickedEntity)
            {
                auto& transform = scene.get<Transform>(*mPickedEntity);

                static ImGuizmo::OPERATION currentGizmoOperation = ImGuizmo::TRANSLATE;
                const auto& viewMat                              = camera.getViewMatrix();
                glm::mat4 projectionMat                          = camera.getProjectionMatrix();
                projectionMat[1][1] *= -1.f;  // HACK: too adhoc

                ImGui::Text("Manipulation (Picked: %s)", scene.get<EntityInfo>(*mPickedEntity).entityName.c_str());
                // Position editor (Translation)
                ImGui::Text("Position");
                ImGui::InputFloat3("Translate", glm::value_ptr(transform.pos));

                // Rotation editor (Euler Angles)
                ImGui::Text("Rotation");
                ImGui::InputFloat3("Rotate", glm::value_ptr(transform.rot));

                // Scale editor
                ImGui::Text("Scale");
                ImGui::InputFloat3("Scale", glm::value_ptr(transform.scale));

                // update buffer value
                transform.params.update(transform.pos, glm::radians(transform.rot), transform.scale);

                if (ImGui::IsKeyPressed(ImGuiKey_1))
                {
                    currentGizmoOperation = ImGuizmo::TRANSLATE;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_2))
                {
                    currentGizmoOperation = ImGuizmo::ROTATE;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_3))
                {
                    currentGizmoOperation = ImGuizmo::SCALE;
                }

                ImGuizmo::Manipulate(glm::value_ptr(viewMat), glm::value_ptr(projectionMat), currentGizmoOperation, ImGuizmo::WORLD, glm::value_ptr(transform.params.world));

                glm::vec3 translation, rotation, objectScale;
                ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(transform.params.world), glm::value_ptr(translation), glm::value_ptr(rotation), glm::value_ptr(objectScale));

                transform.pos   = translation;
                transform.rot   = rotation;
                transform.scale = objectScale;
                // re calculate
                transform.params.update(transform.pos, glm::radians(transform.rot), transform.scale);
            }

            if (mPickedEntity)
            {  // material
                ImGui::Spacing();
                ImGui::Text("Material");
                Material::updateAndDrawMaterialUI(scene.get<Material>(*mPickedEntity).materialParams);
            }

            ImGui::End();
        }

        ImGui::Render();
    }

    void Editor::onResized()
    {
        auto& device = getCommonRegion()->device;
        auto& window = getCommonRegion()->window;

        window->resize();

        createGBufferPass();

        mLightingPass.renderpass->recreateFrameBuffers(window.get());
    }

}  // namespace palm