/*****************************************************************/ /**
 * @file   Renderer.cpp
 * @brief  
 * 
 * @author ichi-raven
 * @date   October 2024
 *********************************************************************/
#include "../include/States/Renderer.hpp"

#include "../include/Integrators/PathIntegrator.hpp"
#include "../include/Integrators/ReSTIRIntegrator.hpp"

#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <ImGuizmo.h>
#include <imfilebrowser.h>

#include <stb_image_write.h>

#include <filesystem>
#include <iostream>

namespace palm
{
    void Renderer::init()
    {
        initVulkan();
        mFileBrowser = ImGui::FileBrowser(ImGuiFileBrowserFlags_EnterNewFilename | ImGuiFileBrowserFlags_CreateNewDir | ImGuiFileBrowserFlags_ConfirmOnEnter | ImGuiFileBrowserFlags_SkipItemsCausingError);

        mLastTime = glfwGetTime();
        mNow      = 0;
    }

    void Renderer::update()
    {
        constexpr auto colorClearValue = vk::ClearValue(std::array{ 0.2f, 0.2f, 0.2f, 1.0f });
        constexpr auto depthClearValue = vk::ClearValue(vk::ClearDepthStencilValue(1.0f, 0));

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
        const float deltaTime    = static_cast<float>(currentTime - mLastTime);
        mLastTime                = currentTime;

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

        {  // clear output image
            const vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
            command->clearImage(mOutputImage.get(), vk::ImageLayout::eGeneral, colorClearValue, range);
        }

        // sample if integrator is valid
        if (mIntegrator)
        {
            mIntegrator->sample(command);
        }

        {  // copy output image

            const auto region = vk::ImageCopy()
                                    .setExtent({ windowWidth, windowHeight, 1 })
                                    .setSrcSubresource({ vk::ImageAspectFlagBits::eColor, 0, 0, 1 })
                                    .setSrcOffset({ 0, 0, 0 })
                                    .setDstSubresource({ vk::ImageAspectFlagBits::eColor, 0, 0, 1 })
                                    .setDstOffset({ 0, 0, 0 });

            command->transitionImageLayout(mOutputImage.get(), vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferSrcOptimal);
            command->copyImageToSwapchain(mOutputImage.get(), window.get(), region, imageIndex);
            command->transitionImageLayout(mOutputImage.get(), vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eGeneral);
        }

        // GUI pass
        command->beginRenderPass(mGuiPass.renderpass.get(), imageIndex, vk::Rect2D({ 0, 0 }, { windowWidth, windowHeight }), colorClearValue);
        command->drawImGui();
        command->endRenderPass();

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

    Renderer::~Renderer()
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

    void Renderer::initVulkan()
    {
        auto& device = getCommonRegion()->device;
        auto& window = getCommonRegion()->window;

        const auto [windowWidth, windowHeight] = window->getWindowSize();
        const auto frameCount                  = window->getFrameCount();

        try
        {
            // ImGui pass (initialized by clear op)
            mGuiPass.renderpass = device.create<vk2s::RenderPass>(window.get(), vk::AttachmentLoadOp::eLoad);

            // initialize ImGui
            device.initImGui(window.get(), mGuiPass.renderpass.get());

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

            // create output image
            {
                const auto format   = window->getVkSwapchainImageFormat();
                const uint32_t size = windowWidth * windowHeight * vk2s::Compiler::getSizeOfFormat(format);

                vk::ImageCreateInfo ci;
                ci.arrayLayers   = 1;
                ci.extent        = vk::Extent3D(windowWidth, windowHeight, 1);
                ci.format        = format;
                ci.imageType     = vk::ImageType::e2D;
                ci.mipLevels     = 1;
                ci.usage         = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eStorage;
                ci.initialLayout = vk::ImageLayout::eUndefined;

                mOutputImage = device.create<vk2s::Image>(ci, vk::MemoryPropertyFlagBits::eDeviceLocal, size, vk::ImageAspectFlagBits::eColor);

                UniqueHandle<vk2s::Command> cmd = device.create<vk2s::Command>();
                cmd->begin(true);
                cmd->transitionImageLayout(mOutputImage.get(), vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral);
                cmd->end();
                cmd->execute();
            }

            // create staging buffer
            {
                constexpr vk::Format outputFormat = vk::Format::eR8G8B8A8Unorm;
                const uint32_t channelSize        = vk2s::Compiler::getSizeOfFormat(outputFormat);
                const uint32_t size               = windowWidth * windowHeight * channelSize;
                mStagingBuffer                    = device.create<vk2s::Buffer>(vk::BufferCreateInfo({}, size, vk::BufferUsageFlagBits::eTransferDst), vk::MemoryPropertyFlagBits::eHostVisible);
            }
        }
        catch (std::exception& e)
        {
            std::cerr << e.what() << "\n";
        }
    }

    void Renderer::updateAndRenderImGui(const double deltaTime)
    {
        auto& device = common()->device;
        auto& window = common()->window;
        auto& scene  = common()->scene;

        const auto [windowWidth, windowHeight] = window->getWindowSize();

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));  // left
        ImGui::SetNextWindowSize(ImVec2(windowWidth, windowHeight * 0.03));
        ImGui::Begin("MenuBar", nullptr, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Save Rendered Image", nullptr))
                {
                    std::time_t now      = std::time(nullptr);
                    std::string fileName = "rendered_" + std::string(std::ctime(&now)) + ".png";
                    saveImage(std::filesystem::path(fileName));
                }

                if (ImGui::MenuItem("Save As", nullptr))
                {
                    mFileBrowser.SetTitle("save rendered image");
                    mFileBrowser.Open();
                }

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Mode"))
            {
                if (ImGui::MenuItem("Editor", nullptr))
                {
                    mChangeDst = AppState::eEditor;
                }
                ImGui::EndMenu();
            }

            ImGui::EndMenuBar();
        }
        ImGui::End();

        ImGui::Begin("Select Integrator");
        if (ImGui::Selectable("path"))
        {
            // set integrator
            mIntegrator = std::make_unique<PathIntegrator>(device, scene, mOutputImage);
        }
        if (ImGui::Selectable("ReSTIR"))
        {
            // set integrator
            mIntegrator = std::make_unique<ReSTIRIntegrator>(device, scene, mOutputImage);
        }

        if (mIntegrator)
        {
            ImGui::SeparatorText("Integrator Config");
            mIntegrator->showConfigImGui();
        }

        ImGui::End();

        mFileBrowser.Display();

        if (mFileBrowser.HasSelected())
        {
            const std::string& path = mFileBrowser.GetSelected().string();
            std::cout << "saved current estimates (image) to: " << mFileBrowser.GetSelected().string() << std::endl;
            mFileBrowser.ClearSelected();
            saveImage(std::filesystem::path(path));
        }

        ImGui::Render();
    }

    void Renderer::updateShaderResources()
    {
        if (mIntegrator)
        {
            mIntegrator->updateShaderResources();
        }
    }

    void Renderer::onResized()
    {
        auto& device = getCommonRegion()->device;
        auto& window = getCommonRegion()->window;

        window->resize();

        mGuiPass.renderpass->recreateFrameBuffers(window.get());
    }

    void Renderer::saveImage(const std::filesystem::path& saveDst)
    {
        auto& device = getCommonRegion()->device;

        const auto extent                 = mOutputImage->getVkExtent();
        constexpr vk::Format outputFormat = vk::Format::eR8G8B8A8Unorm;
        const uint32_t channelSize        = vk2s::Compiler::getSizeOfFormat(outputFormat);
        const uint32_t size               = extent.width * extent.height * channelSize;

        const auto copyRegion = vk::BufferImageCopy().setBufferOffset(0).setBufferRowLength(0).setBufferImageHeight(0).setImageSubresource({ vk::ImageAspectFlagBits::eColor, 0, 0, 1 }).setImageOffset({ 0, 0, 0 }).setImageExtent(extent);

        UniqueHandle<vk2s::Command> cmd = device.create<vk2s::Command>();
        cmd->begin(true);
        cmd->transitionImageLayout(mOutputImage.get(), vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferSrcOptimal);
        cmd->copyImageToBuffer(mOutputImage.get(), mStagingBuffer.get(), copyRegion);
        cmd->transitionImageLayout(mOutputImage.get(), vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eGeneral);
        cmd->end();
        cmd->execute();

        device.getVkDevice()->waitIdle();

        std::vector<uint8_t> output(extent.width * extent.height * 3);
        {
            std::uint8_t* p = reinterpret_cast<std::uint8_t*>(device.getVkDevice()->mapMemory(mStagingBuffer->getVkDeviceMemory().get(), 0, size));
            for (size_t h = 0; h < extent.height; ++h)
            {
                for (size_t w = 0; w < extent.width; ++w)
                {
                    const size_t index    = h * extent.width + w;
                    output[index * 3 + 0] = p[index * 4 + 0];
                    output[index * 3 + 1] = p[index * 4 + 1];
                    output[index * 3 + 2] = p[index * 4 + 2];
                }
            }

            device.getVkDevice()->unmapMemory(mStagingBuffer->getVkDeviceMemory().get());
        }

        const int res = stbi_write_png(saveDst.string<char>().c_str(), extent.width, extent.height, 3, output.data(), extent.width * 3);
        if (res == 0)
        {
            std::cerr << "failed to output!\n";
        }
    }

}  // namespace palm