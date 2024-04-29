/*****************************************************************/ /**
 * @file   Editor.cpp
 * @brief  source file of Editor class
 * 
 * @author ichi-raven
 * @date   February 2024
 *********************************************************************/

//#define VULKAN_HPP_NO_CONSTRUCTORS

#include "../include/States/Editor.hpp"

#include <stb_image.h>
#include <glm/glm.hpp>

#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <iostream>
#include <filesystem>

inline vk::TransformMatrixKHR convert(const glm::mat4x3& m)
{
    vk::TransformMatrixKHR mtx;
    auto mT = glm::transpose(m);
    memcpy(&mtx.matrix[0], &mT[0], sizeof(float) * 4);
    memcpy(&mtx.matrix[1], &mT[1], sizeof(float) * 4);
    memcpy(&mtx.matrix[2], &mT[2], sizeof(float) * 4);

    return mtx;
};

namespace palm
{
    inline void Editor::load(std::string_view path, vk2s::Device& device, vk2s::AssetLoader& loader, std::vector<MeshInstance>& meshInstances, Handle<vk2s::Buffer>& materialUB, std::vector<Handle<vk2s::Image>>& materialTextures)
    {
        std::vector<vk2s::AssetLoader::Mesh> hostMeshes;
        std::vector<vk2s::AssetLoader::Material> hostMaterials;
        loader.load(path.data(), hostMeshes, hostMaterials);

        meshInstances.resize(hostMeshes.size());
        for (size_t i = 0; i < meshInstances.size(); ++i)
        {
            auto& mesh           = meshInstances[i];
            mesh.hostMesh        = std::move(hostMeshes[i]);
            const auto& hostMesh = meshInstances[i].hostMesh;

            {  // vertex buffer
                const auto vbSize  = hostMesh.vertices.size() * sizeof(vk2s::AssetLoader::Vertex);
                const auto vbUsage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eStorageBuffer;
                vk::BufferCreateInfo ci({}, vbSize, vbUsage);
                vk::MemoryPropertyFlags fb = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;

                mesh.vertexBuffer = device.create<vk2s::Buffer>(ci, fb);
                mesh.vertexBuffer->write(hostMesh.vertices.data(), vbSize);
            }

            {  // index buffer

                const auto ibSize  = hostMesh.indices.size() * sizeof(uint32_t);
                const auto ibUsage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eStorageBuffer;

                vk::BufferCreateInfo ci({}, ibSize, ibUsage);
                vk::MemoryPropertyFlags fb = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;

                mesh.indexBuffer = device.create<vk2s::Buffer>(ci, fb);
                mesh.indexBuffer->write(hostMesh.indices.data(), ibSize);
            }
        }

        // materials
        constexpr double threshold = 1.5;
        std::vector<MaterialUB> materialData;
        materialData.reserve(hostMaterials.size());
        for (const auto& hostMat : hostMaterials)
        {
            auto& mat        = materialData.emplace_back();
            mat.materialType = static_cast<uint32_t>(MaterialType::eLambert);  // default
            mat.emissive     = glm::vec4(0.);
            mat.IOR          = 1.0;

            if (std::holds_alternative<glm::vec4>(hostMat.diffuse))
            {
                mat.albedo   = std::get<glm::vec4>(hostMat.diffuse);
                mat.texIndex = -1;
            }
            else
            {
                mat.albedo   = glm::vec4(0.3f, 0.3f, 0.3f, 1.f);  // DEBUG COLOR
                mat.texIndex = materialTextures.size();

                auto& texture           = materialTextures.emplace_back();
                const auto& hostTexture = std::get<vk2s::AssetLoader::Texture>(hostMat.diffuse);
                const auto width        = hostTexture.width;
                const auto height       = hostTexture.height;
                const auto size         = width * height * static_cast<uint32_t>(STBI_rgb_alpha);

                vk::ImageCreateInfo ci;
                ci.arrayLayers   = 1;
                ci.extent        = vk::Extent3D(width, height, 1);
                ci.format        = vk::Format::eR8G8B8A8Srgb;
                ci.imageType     = vk::ImageType::e2D;
                ci.mipLevels     = 1;
                ci.usage         = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
                ci.initialLayout = vk::ImageLayout::eUndefined;

                texture = device.create<vk2s::Image>(ci, vk::MemoryPropertyFlagBits::eDeviceLocal, size, vk::ImageAspectFlagBits::eColor);

                texture->write(hostTexture.pData, size);
            }

            if (hostMat.specular && hostMat.shininess && glm::length(*hostMat.specular) > threshold)
            {
                //mat.materialType = static_cast<uint32_t>(MaterialType::eLambert);  // default
                //mat.albedo       = glm::vec4(1.f);  // DEBUG COLOR

                mat.materialType = static_cast<uint32_t>(MaterialType::eConductor);
                mat.albedo       = *hostMat.specular;
                mat.alpha        = 1. - *hostMat.shininess / 1024.;
            }

            if (hostMat.IOR && *hostMat.IOR > 1.1)
            {
                //mat.materialType = static_cast<uint32_t>(MaterialType::eLambert);  // default
                //mat.albedo       = glm::vec4(1.f);  // DEBUG COLOR

                mat.materialType = static_cast<uint32_t>(MaterialType::eDielectric);
                mat.albedo       = glm::vec4(1.0);
                mat.IOR          = *hostMat.IOR;
            }

            if (hostMat.emissive && glm::length(*hostMat.emissive) > threshold)
            {
                mat.emissive = *hostMat.emissive;
            }
        }

        {
            const auto ubSize = sizeof(MaterialUB) * materialData.size();
            vk::BufferCreateInfo ci({}, ubSize, vk::BufferUsageFlagBits::eStorageBuffer);
            vk::MemoryPropertyFlags fb = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;

            materialUB = device.create<vk2s::Buffer>(ci, fb);
            materialUB->write(materialData.data(), ubSize);
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
            // load meshes and materials

            Handle<vk2s::Buffer> materialBuffer;
            std::vector<Handle<vk2s::Image>> materialTextures;
            auto sampler = device.create<vk2s::Sampler>(vk::SamplerCreateInfo());
            vk2s::AssetLoader loader;

            load("../../resources/model/CornellBox/CornellBox-Sphere.obj", device, loader, mMeshInstances, materialBuffer, materialTextures);


            // default sampler
            mDefaultSampler = device.create<vk2s::Sampler>(vk::SamplerCreateInfo());

            // create dummy image
            {
                const auto format   = vk::Format::eR8G8B8A8Unorm;
                const uint32_t size = vk2s::Compiler::getSizeOfFormat(format);  // 1pixel

                vk::ImageCreateInfo ci;
                ci.arrayLayers   = 1;
                ci.extent        = vk::Extent3D(1, 1, 1);
                ci.format        = format;
                ci.imageType     = vk::ImageType::e2D;
                ci.mipLevels     = 1;
                ci.usage         = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
                ci.initialLayout = vk::ImageLayout::eUndefined;

                mDummyImage = device.create<vk2s::Image>(ci, vk::MemoryPropertyFlagBits::eDeviceLocal, size, vk::ImageAspectFlagBits::eColor);

                uint8_t data[] = { 0, 0, 200, 0 };
                mDummyImage->write(data, size);
            }

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

                mGeometryPass.vs = device.create<vk2s::Shader>("../../shaders/raster/geometry.vert", "main");
                mGeometryPass.fs = device.create<vk2s::Shader>("../../shaders/raster/geometry.frag", "main");

                std::vector bindings0 = {
                    vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBufferDynamic, 1, vk::ShaderStageFlagBits::eAll),
                    vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eAll),
                    vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eCombinedImageSampler, std::max(1ull, materialTextures.size()), vk::ShaderStageFlagBits::eAll),
                };

                std::vector bindings1 = {
                    vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eAll),
                };

                mGeometryPass.bindLayouts.emplace_back(device.create<vk2s::BindLayout>(bindings0));
                mGeometryPass.bindLayouts.emplace_back(device.create<vk2s::BindLayout>(bindings1));

                vk::VertexInputBindingDescription inputBinding(0, sizeof(vk2s::AssetLoader::Vertex));
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

            {  // lighting pass
                mLightingPass.renderpass = device.create<vk2s::RenderPass>(window.get(), vk::AttachmentLoadOp::eClear);
                mLightingPass.vs         = device.create<vk2s::Shader>("../../shaders/raster/lighting.vert", "main");
                mLightingPass.fs         = device.create<vk2s::Shader>("../../shaders/raster/lighting.frag", "main");

                std::array bindings0 = {
                    vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eAll),
                    vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eAll),
                    vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eAll),
                };

                std::vector bindings1 = {
                    vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBufferDynamic, 1, vk::ShaderStageFlagBits::eAll),
                    vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eAll),
                    vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eCombinedImageSampler, std::max(1ull, materialTextures.size()), vk::ShaderStageFlagBits::eAll),
                };

                mLightingPass.bindLayouts.emplace_back(device.create<vk2s::BindLayout>(bindings0));
                mLightingPass.bindLayouts.emplace_back(device.create<vk2s::BindLayout>(bindings1));

                vk::VertexInputBindingDescription inputBinding(0, sizeof(vk2s::AssetLoader::Vertex));
                const auto& inputAttributes = std::get<0>(mGeometryPass.vs->getReflection());
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
                const auto size = sizeof(SceneUB) * frameCount;
                mSceneBuffer    = device.create<vk2s::DynamicBuffer>(vk::BufferCreateInfo({}, size, vk::BufferUsageFlagBits::eUniformBuffer), vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, frameCount);
            }

            for (uint32_t i = 0; auto& mesh : mMeshInstances)
            {
                const auto size     = sizeof(InstanceUB) * frameCount;
                mesh.instanceBuffer = device.create<vk2s::Buffer>(vk::BufferCreateInfo({}, size, vk::BufferUsageFlagBits::eUniformBuffer), vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

                InstanceUB data{
                    .model    = glm::mat4(1.0),
                    .matIndex = i++,
                    .padding  = { 0.0 },
                };

                mesh.instanceBuffer->write(&data, sizeof(InstanceUB));

                mesh.instanceBindGroup = device.create<vk2s::BindGroup>(mGeometryPass.bindLayouts[1].get());
                mesh.instanceBindGroup->bind(0, vk::DescriptorType::eUniformBuffer, mesh.instanceBuffer.get());
            }

            // create bindgroup
            mSceneBindGroup = device.create<vk2s::BindGroup>(mGeometryPass.bindLayouts[0].get());

            mSceneBindGroup->bind(0, vk::DescriptorType::eUniformBufferDynamic, mSceneBuffer.get());
            mSceneBindGroup->bind(1, vk::DescriptorType::eStorageBuffer, materialBuffer.get());
            if (materialTextures.empty())
            {
                mSceneBindGroup->bind(2, vk::DescriptorType::eCombinedImageSampler, mDummyImage, sampler);  // dummy
            }
            else
            {
                mSceneBindGroup->bind(2, vk::DescriptorType::eCombinedImageSampler, materialTextures, sampler);
            }

            mGBuffer.bindGroup = device.create<vk2s::BindGroup>(mLightingPass.bindLayouts[0].get());
            mGBuffer.bindGroup->bind(0, vk::DescriptorType::eCombinedImageSampler, mGBuffer.albedoTex, mDefaultSampler);
            mGBuffer.bindGroup->bind(1, vk::DescriptorType::eCombinedImageSampler, mGBuffer.worldPosTex, mDefaultSampler);
            mGBuffer.bindGroup->bind(2, vk::DescriptorType::eCombinedImageSampler, mGBuffer.normalTex, mDefaultSampler);

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

            mCamera = vk2s::Camera(60., 1. * windowWidth / windowHeight);
            mCamera.setPos(glm::vec3(0.0, 0.8, 3.0));
            mCamera.setLookAt(glm::vec3(0.0, 0.8, -2.0));
        }
        catch (std::exception& e)
        {
            std::cerr << e.what() << "\n";
        }
    }

    void Editor::init()
    {
        initVulkan();

        mLastTime = glfwGetTime();
        mNow      = 0;
    }

    void Editor::update()
    {
        constexpr auto colorClearValue   = vk::ClearValue(std::array{ 0.2f, 0.2f, 0.2f, 1.0f });
        constexpr auto gbufferClearValue = vk::ClearValue(std::array{ 0.f, 0.f, 0.f, 1.0f });
        constexpr auto depthClearValue   = vk::ClearValue(vk::ClearDepthStencilValue(1.0f, 0));
        constexpr std::array clearValues = { gbufferClearValue, gbufferClearValue, gbufferClearValue, depthClearValue };

        auto& device = getCommonRegion()->device;
        auto& window = getCommonRegion()->window;

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
        mCamera.update(window->getpGLFWWindow(), speed, mouseSpeed);

        // ImGui
        renderImGui();

        // wait and reset fence
        mFences[mNow]->wait();

        {  // write data
            SceneUB sceneUBO{
                .view   = mCamera.getViewMatrix(),
                .proj   = mCamera.getProjectionMatrix(),
                .camPos = glm::vec4(mCamera.getPos(), 1.0),
            };

            mSceneBuffer->write(&sceneUBO, sizeof(SceneUB), mNow * mSceneBuffer->getBlockSize());
        }

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
            for (auto& mesh : mMeshInstances)
            {
                command->setBindGroup(1, mesh.instanceBindGroup.get());
                command->bindVertexBuffer(mesh.vertexBuffer.get());
                command->bindIndexBuffer(mesh.indexBuffer.get());

                command->drawIndexed(mesh.hostMesh.indices.size(), 1, 0, 0, 1);
            }
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
            command->setBindGroup(1, mSceneBindGroup.get(), { mNow * static_cast<uint32_t>(mSceneBuffer->getBlockSize()) });
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

        // update frame index
        mNow = (mNow + 1) % frameCount;
    }

    Editor::~Editor()
    {
        for (auto& fence : mFences)
        {
            fence->wait();
        }
    }

    void Editor::renderImGui()
    {
        auto& device = getCommonRegion()->device;
        auto& window = getCommonRegion()->window;

        const auto [windowWidth, windowHeight] = window->getWindowSize();

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));// left
        ImGui::SetNextWindowSize(ImVec2(windowWidth, 20));  
        ImGui::Begin("MenuBar", NULL, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

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

            ImGui::EndMenuBar();
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(0, 20));                    //top
        ImGui::SetNextWindowSize(ImVec2(80, windowHeight - 180)); 
        ImGui::Begin("ToolBar", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

        ImGui::Text("Toolbar:");
        if (ImGui::Button("Play"))
        {
        }
        if (ImGui::Button("Stop"))
        {
        }

        ImGui::End();

        {
            ImGui::SetNextWindowPos(ImVec2(0, windowHeight - 180));  // bottom
            ImGui::SetNextWindowSize(ImVec2(windowWidth, 180));     
            ImGui::Begin("FileExplorer", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

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
                        ImGui::Text(entry.path().filename().string().c_str());
                    }
                }
            }

            ImGui::End();
        }

        {
            ImGui::SetNextWindowPos(ImVec2(windowWidth - 320, 20));     // right
            ImGui::SetNextWindowSize(ImVec2(320, windowHeight - 180));  
            ImGui::Begin("SceneEditor", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

            ImGui::Text("Scene Editor");

            ImGui::End();
        }

        //ImGui::Begin("configuration");
        //ImGui::Text("device = %s", device.getPhysicalDeviceName().data());
        ////ImGui::Text("fps = %lf", 1. / deltaTime);
        //const auto& pos    = mCamera.getPos();
        //const auto& lookAt = mCamera.getLookAt();
        //ImGui::Text("pos = (%lf, %lf, %lf)", pos.x, pos.y, pos.z);
        //ImGui::Text("lookat = (%lf, %lf, %lf)", lookAt.x, lookAt.y, lookAt.z);

        //ImGui::End();

        ImGui::Render();
    }

    void Editor::onResized()
    {
        auto& device = getCommonRegion()->device;
        auto& window = getCommonRegion()->window;

        window->resize();

        mLightingPass.renderpass->recreateFrameBuffers(window.get());
    }

}  // namespace palm