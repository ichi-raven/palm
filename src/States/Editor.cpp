/*****************************************************************//**
 * @file   Editor.cpp
 * @brief  source file of Editor class
 * 
 * @author ichi-raven
 * @date   February 2024
 *********************************************************************/

#include "../include/States/Editor.hpp"

#include <stb_image.h>
#include <glm/glm.hpp>

#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <iostream>





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

        //hostMeshes.erase(hostMeshes.begin());
        //hostMeshes.erase(hostMeshes.begin());
        //hostMaterials.erase(hostMaterials.begin());
        //hostMaterials.erase(hostMaterials.begin());

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
            // create depth buffer
            Handle<vk2s::Image> depthBuffer;
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

                depthBuffer = device.create<vk2s::Image>(ci, vk::MemoryPropertyFlagBits::eDeviceLocal, size, vk::ImageAspectFlagBits::eDepth);
            }

            mRenderpass = device.create<vk2s::RenderPass>(window.get(), vk::AttachmentLoadOp::eClear, depthBuffer);

            device.initImGui(frameCount, window.get(), mRenderpass.get());

            auto vertexShader   = device.create<vk2s::Shader>("../../shaders/rasterize/vertex.vert", "main");
            auto fragmentShader = device.create<vk2s::Shader>("../../shaders/rasterize/fragment.frag", "main");

            std::array bindings = {
                vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBufferDynamic, 1, vk::ShaderStageFlagBits::eAll),
            };
            auto bindLayout = device.create<vk2s::BindLayout>(bindings);

            vk::VertexInputBindingDescription inputBinding(0, sizeof(vk2s::AssetLoader::Vertex));
            vk::Viewport viewport(0.0f, 0.0f, static_cast<float>(windowWidth), static_cast<float>(windowHeight), 0.0f, 1.0f);
            vk::Rect2D scissor({ 0, 0 }, window->getVkSwapchainExtent());
            vk::PipelineColorBlendAttachmentState colorBlendAttachment(VK_FALSE);
            colorBlendAttachment.setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);

            vk2s::Pipeline::VkGraphicsPipelineInfo gpi{
                .vs            = vertexShader,
                .fs            = fragmentShader,
                .bindLayout    = bindLayout,
                .renderPass    = mRenderpass,
                .inputState    = vk::PipelineVertexInputStateCreateInfo({}, inputBinding, std::get<0>(vertexShader->getReflection())),
                .inputAssembly = vk::PipelineInputAssemblyStateCreateInfo({}, vk::PrimitiveTopology::eTriangleList),
                .viewportState = vk::PipelineViewportStateCreateInfo({}, 1, &viewport, 1, &scissor),
                .rasterizer    = vk::PipelineRasterizationStateCreateInfo({}, VK_FALSE, VK_FALSE, vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eClockwise, VK_FALSE, 0.0f, 0.0f, 0.0f, 1.0f),
                .multiSampling = vk::PipelineMultisampleStateCreateInfo({}, vk::SampleCountFlagBits::e1, VK_FALSE),
                .depthStencil  = vk::PipelineDepthStencilStateCreateInfo({}, VK_TRUE, VK_TRUE, vk::CompareOp::eLess, VK_FALSE),
                .colorBlending = vk::PipelineColorBlendStateCreateInfo({}, VK_FALSE, vk::LogicOp::eCopy, 1, &colorBlendAttachment),
            };

            mGraphicsPipeline = device.create<vk2s::Pipeline>(gpi);

            // load meshes and materials

            Handle<vk2s::Buffer> materialBuffer;
            std::vector<Handle<vk2s::Image>> materialTextures;
            auto sampler = device.create<vk2s::Sampler>(vk::SamplerCreateInfo());
            vk2s::AssetLoader loader;

            load("../../resources/model/CornellBox/CornellBox-Sphere.obj", device, loader, mMeshInstances, materialBuffer, materialTextures);

            // uniform buffer
            {
                const auto size = sizeof(SceneUB) * frameCount;
                mSceneBuffer    = device.create<vk2s::DynamicBuffer>(vk::BufferCreateInfo({}, size, vk::BufferUsageFlagBits::eUniformBuffer), vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, frameCount);
            }

            // create bindgroup
            mBindGroup = device.create<vk2s::BindGroup>(bindLayout.get());

            mBindGroup->bind(0, 0, vk::DescriptorType::eUniformBufferDynamic, mSceneBuffer.get());

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
        mNow = 0;
    }

    void Editor::update()
    {
        constexpr auto colorClearValue   = vk::ClearValue(std::array{ 0.2f, 0.2f, 0.2f, 1.0f });
        constexpr auto depthClearValue   = vk::ClearValue(vk::ClearDepthStencilValue(1.0f, 0));
        constexpr std::array clearValues = { colorClearValue, depthClearValue };
        
        auto& device = getCommonRegion()->device;
        auto& window = getCommonRegion()->window;
        
        const auto [windowWidth, windowHeight] = window->getWindowSize();
        const auto frameCount                  = window->getFrameCount();


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
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGui::Begin("configuration");
        ImGui::Text("device = %s", device.getPhysicalDeviceName().data());
        ImGui::Text("fps = %lf", 1. / deltaTime);
        const auto& pos    = mCamera.getPos();
        const auto& lookAt = mCamera.getLookAt();
        ImGui::Text("pos = (%lf, %lf, %lf)", pos.x, pos.y, pos.z);
        ImGui::Text("lookat = (%lf, %lf, %lf)", lookAt.x, lookAt.y, lookAt.z);

        ImGui::End();

        ImGui::Render();

        // wait and reset fence
        mFences[mNow]->wait();
        mFences[mNow]->reset();

        {  // write data
            SceneUB sceneUBO{
                .model = glm::mat4(1.0),
                .view  = mCamera.getViewMatrix(),
                .proj  = mCamera.getProjectionMatrix(),
            };

            mSceneBuffer->write(&sceneUBO, sizeof(SceneUB), mNow * mSceneBuffer->getBlockSize());
        }

        // acquire next image from swapchain(window)
        const auto [imageIndex, resized] = window->acquireNextImage(mImageAvailableSems[mNow].get());

        auto& command = mCommands[mNow];
        // start writing command
        command->begin();
        command->beginRenderPass(mRenderpass.get(), imageIndex, vk::Rect2D({ 0, 0 }, { windowWidth, windowHeight }), clearValues);

        command->setPipeline(mGraphicsPipeline);

        command->setBindGroup(mBindGroup.get(), { mNow * static_cast<uint32_t>(mSceneBuffer->getBlockSize()) });
        for (auto& mesh : mMeshInstances)
        {
            command->bindVertexBuffer(mesh.vertexBuffer.get());
            command->bindIndexBuffer(mesh.indexBuffer.get());

            command->drawIndexed(mesh.hostMesh.indices.size(), 1, 0, 0, 1);
        }

        command->drawImGui();
        command->endRenderPass();

        // end writing commands
        command->end();

        // execute
        command->execute(mFences[mNow], mImageAvailableSems[mNow], mRenderCompletedSems[mNow]);
        // present swapchain(window) image
        window->present(imageIndex, mRenderCompletedSems[mNow].get());
    
        // update frame index
        mNow = (mNow + 1) % frameCount;
    }

    Editor::~Editor()
    {

    }

}  // namespace palm