/*****************************************************************//**
 * @file   MaterialViewer.hpp
 * @brief  
 * 
 * @author ichi-raven
 * @date   October 2024
 *********************************************************************/
#ifndef PALM_INCLUDE_STATES_MATERIALVIEWER_HPP_
#define PALM_INCLUDE_STATES_MATERIALVIEWER_HPP_

#include <EC2S.hpp>
#include <vk2s/Device.hpp>
#include <vk2s/Camera.hpp>
#include <vk2s/Scene.hpp>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "../include/AppStates.hpp"
#include "../include/GraphicsPass.hpp"

#include <filesystem>

namespace palm
{
    class MaterialViewer : public ec2s::State<palm::AppState, palm::CommonRegion>
    {
        GEN_STATE(MaterialViewer, palm::AppState, palm::CommonRegion);

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

    private:
        vk2s::Camera mCamera;

        std::vector<Handle<vk2s::Command>> mCommands;
        std::vector<Handle<vk2s::Semaphore>> mImageAvailableSems;
        std::vector<Handle<vk2s::Semaphore>> mRenderCompletedSems;
        std::vector<Handle<vk2s::Fence>> mFences;

        GraphicsPass mGuiPass;

        double mLastTime = 0;
        uint32_t mNow;
        uint32_t mFrameCount;

        std::optional<AppState> mChangeDst;
    };

}  // namespace palm

#endif
