/*****************************************************************/ /**
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

#define GLM_ENABLE_EXPERIMENTAL
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
    /**
     * @brief State to render a scene
     */
    class Renderer : public ec2s::State<palm::AppState, palm::CommonRegion>
    {
        //! Macro to generate required members
        GEN_STATE(Renderer, palm::AppState, palm::CommonRegion);

    private:
        /**
         * @brief  Parameters shared across the Scene (passed to the GPU)
         */
        struct SceneParams  // std430
        {
            glm::mat4 view;
            glm::mat4 proj;
            glm::mat4 viewInv;
            glm::mat4 projInv;
            glm::vec4 camPos;
        };

        /**
         * @brief  Parameters per instance (passed to the GPU)
         */
        struct InstanceUB
        {
            glm::mat4 model;
            uint32_t matIndex;
            float padding[3];
        };

    private:
        /** 
         * @brief Vulkan(vk2s) initialization
         *  
         */
        void initVulkan();

        /** 
         * @brief  ImGui update and drawing
         * @detail  Note that each information in the scene is updated
         * 
         * @param deltaTime Delta time elapsed in previous frame
         */
        void updateAndRenderImGui(const double deltaTime);

        /** 
         * @brief  Update resources to be bound to the shader
         *  
         */
        void updateShaderResources();

        /** 
         * @brief  Called when the window is resized
         *  
         */
        void onResized();

        /** 
         * @brief  Save the current outputImage to disk
         * @detail  Currently supports only png
         * 
         * @param saveDst  Destination path
         */
        void saveImage(const std::filesystem::path& saveDst);

    private:
        //! If State is to be changed, where to change it (to fix processing order)
        std::optional<AppState> mChangeDst;

        //! Image from which the Integrator outputs the current estimated luminance value
        UniqueHandle<vk2s::Image> mOutputImage;
        //! Staging buffer for storing output images
        UniqueHandle<vk2s::Buffer> mStagingBuffer;

        //! Selected Integrator
        std::unique_ptr<Integrator> mIntegrator;

        //! GPU commands (per frame)
        std::vector<Handle<vk2s::Command>> mCommands;
        //! Semaphore that waits until that frame is ready to be written (per frame)
        std::vector<Handle<vk2s::Semaphore>> mImageAvailableSems;
        //! Semaphore notifying that drawing is complete and presentable (per frame)
        std::vector<Handle<vk2s::Semaphore>> mRenderCompletedSems;
        //! Fence to wait for the CPU to write information until the frame is ready for processing (per frame)
        std::vector<Handle<vk2s::Fence>> mFences;

        //! Pass to draw ImGui
        GraphicsPass mGuiPass;

        //! File browser when saving images
        ImGui::FileBrowser mFileBrowser;

        //! Elapsed time to previous frame
        double mLastTime = 0;
        //! Index of the frame buffer currently being processed
        uint32_t mNow = 0;
    };

}  // namespace palm

#endif
