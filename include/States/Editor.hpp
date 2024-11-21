/*****************************************************************//**
/*****************************************************************//**
 * @file   Editor.hpp
 * @brief  header file of editor state
 * 
 * @author ichi-raven
 * @date   February 2024
 *********************************************************************/
#ifndef PALM_INCLUDE_STATES_EDITOR_HPP_
#define PALM_INCLUDE_STATES_EDITOR_HPP_

#include <EC2S.hpp>
#include <vk2s/Device.hpp>
#include <vk2s/Camera.hpp>
#include <vk2s/Scene.hpp>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "../include/AppStates.hpp"
#include "../GraphicsPass.hpp"

#include <filesystem>

namespace palm
{
    class Editor : public ec2s::State<palm::AppState, palm::CommonRegion>
    {
        GEN_STATE(Editor, palm::AppState, palm::CommonRegion);

    private:
        struct SceneParams  // std140
        {
            glm::mat4 view;
            glm::mat4 proj;
            glm::mat4 viewInv;
            glm::mat4 projInv;
            glm::vec4 camPos;
            glm::vec2 mousePos;
            glm::uvec2 frameSize;
        };

        struct InstanceUB
        {
            glm::mat4 model;
            uint32_t matIndex;
            float padding[3];
        };

        struct InstanceMappingUB  // std140
        {
            uint64_t VBAddress;
            uint64_t IBAddress;
            uint32_t materialIndex;
            uint32_t padding[3];
        };

        struct GBuffer
        {
            UniqueHandle<vk2s::Image> depthBuffer;
            UniqueHandle<vk2s::Image> albedoTex;
            UniqueHandle<vk2s::Image> worldPosTex;
            UniqueHandle<vk2s::Image> normalTex;

            UniqueHandle<vk2s::BindGroup> bindGroup;
        };

    private:
        void initVulkan();

        void createGBuffer();

        void updateAndRenderImGui(const double deltaTime);

        void updateShaderResources();

        void onResized();

        void addEntity(const std::filesystem::path& path);

        void removeEntity(const ec2s::Entity entity);

    private:
        std::vector<Handle<vk2s::Command>> mCommands;
        std::vector<Handle<vk2s::Semaphore>> mImageAvailableSems;
        std::vector<Handle<vk2s::Semaphore>> mRenderCompletedSems;
        std::vector<Handle<vk2s::Fence>> mFences;

        GBuffer mGBuffer;

        GraphicsPass mGeometryPass;
        GraphicsPass mLightingPass;

        UniqueHandle<vk2s::Sampler> mDefaultSampler;

        UniqueHandle<vk2s::DynamicBuffer> mSceneBuffer;
        UniqueHandle<vk2s::Buffer> mPickedIDBuffer;
        UniqueHandle<vk2s::BindGroup> mSceneBindGroup;
        UniqueHandle<vk2s::BindGroup> mLightingBindGroup;

        std::optional<ec2s::Entity> mPickedEntity;
        ec2s::Entity mCameraEntity = ec2s::kInvalidEntity;

        std::optional<AppState> mChangeDst;

        double mLastTime     = 0;
        uint32_t mNow        = 0;
        uint32_t mFrameCount = 0;
        bool mManipulating   = false;

        inline const static glm::vec2 kRenderArea = glm::vec2(0.75f, 0.75f);
    };

}  // namespace palm

#endif