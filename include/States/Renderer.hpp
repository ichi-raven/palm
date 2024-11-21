/*****************************************************************//**
/*****************************************************************//**
 * @file   Renderer.hpp
 * @brief  
 * 
 * @author ichi-raven
 * @date   October 2024
 *********************************************************************/
#ifndef PALM_INCLUDE_STATES_RENDERER_HPP_
#define PALM_INCLUDE_STATES_RENDERER_HPP_

#include <EC2S.hpp>
#include <vk2s/Device.hpp>
#include <vk2s/Camera.hpp>
#include <vk2s/Scene.hpp>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <imfilebrowser.h>

#include "../include/AppStates.hpp"
#include "../include/GraphicsPass.hpp"
#include "../include/Integrators/Integrator.hpp"

#include <filesystem>

namespace palm
{
    class Renderer : public ec2s::State<palm::AppState, palm::CommonRegion>
    {
        GEN_STATE(Renderer, palm::AppState, palm::CommonRegion);

    private:
        struct SceneParams  // std430
        {
            glm::mat4 view;
            glm::mat4 proj;
            glm::mat4 viewInv;
            glm::mat4 projInv;
            glm::vec4 camPos;
        };

        struct InstanceUB
        {
            glm::mat4 model;
            uint32_t matIndex;
            float padding[3];
        };

        struct InstanceMappingUB  // std430
        {
            uint64_t VBAddress;
            uint64_t IBAddress;
            uint32_t materialIndex;
            uint32_t padding[3];
        };

    private:

        void initVulkan();

        void updateAndRenderImGui(const double deltaTime);

        void updateShaderResources();

        void onResized();

        void saveImage(const std::filesystem::path& saveDst);

    private:

        std::optional<AppState> mChangeDst;

        UniqueHandle<vk2s::Image> mOutputImage;
        UniqueHandle<vk2s::Buffer> mStagingBuffer;

        std::unique_ptr<Integrator> mIntegrator;

        std::vector<Handle<vk2s::Command>> mCommands;
        std::vector<Handle<vk2s::Semaphore>> mImageAvailableSems;
        std::vector<Handle<vk2s::Semaphore>> mRenderCompletedSems;
        std::vector<Handle<vk2s::Fence>> mFences;

        GraphicsPass mGuiPass;

        ImGui::FileBrowser mFileBrowser;

        double mLastTime     = 0;
        uint32_t mNow        = 0;
        uint32_t mFrameCount = 0;
    };

}  // namespace palm

#endif
