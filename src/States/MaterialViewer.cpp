#include "../include/States/MaterialViewer.hpp"

#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <ImGuizmo.h>

namespace palm
{
    void MaterialViewer::init()
    {
        initVulkan();

        mLastTime = glfwGetTime();
        mNow      = 0;
    }

    void MaterialViewer::update()
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

        if (mChangeDst)
        {
            changeState(*mChangeDst);
        }

        // update frame index
        mNow = (mNow + 1) % frameCount;
    }

    MaterialViewer::~MaterialViewer()
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

    void MaterialViewer::initVulkan()
    {
        auto& device = getCommonRegion()->device;
        auto& window = getCommonRegion()->window;

        const auto [windowWidth, windowHeight] = window->getWindowSize();
        const auto frameCount                  = window->getFrameCount();

        try
        {
            // ImGui pass
            {
                mGuiPass.renderpass = device.create<vk2s::RenderPass>(window.get(), vk::AttachmentLoadOp::eClear);
            }

            // initialize ImGui
            device.initImGui(frameCount, window.get(), mGuiPass.renderpass.get());

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

    void MaterialViewer::updateAndRenderImGui(const double deltaTime)
    {
        auto& device = common()->device;
        auto& window = common()->window;
        auto& scene  = common()->scene;

        const auto [windowWidth, windowHeight] = window->getWindowSize();

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();
        ImGuizmo::SetRect(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, windowWidth, windowHeight);

        ImGui::Begin("Notice", NULL);
        ImGui::Text("Under construction!");
        if (ImGui::Button("go back to editor"))
        {
            mChangeDst = AppState::eEditor;
        }

        ImGui::End();

        ImGui::Render();
    }

    void MaterialViewer::updateShaderResources()
    {
    }

    void MaterialViewer::onResized()
    {
        auto& device = getCommonRegion()->device;
        auto& window = getCommonRegion()->window;

        window->resize();

        mGuiPass.renderpass->recreateFrameBuffers(window.get());
    }
}  // namespace palm