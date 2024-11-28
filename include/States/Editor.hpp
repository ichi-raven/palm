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

#include <ImGuizmo.h>
#include <imfilebrowser.h>

#include "../include/AppStates.hpp"
#include "../GraphicsPass.hpp"

#include <filesystem>

namespace palm
{
    /**
     * @brief  State to edit a scene
     */
    class Editor : public ec2s::State<palm::AppState, palm::CommonRegion>
    {
        //! Macro to generate required members
        GEN_STATE(Editor, palm::AppState, palm::CommonRegion);

    private:
        /**
         * @brief  Parameters shared across the Scene (passed to the GPU)
         */
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

        /**
         * @brief  Parameters per instance (passed to the GPU)
         */
        struct InstanceUB
        {
            glm::mat4 model;
            uint32_t matIndex;
            float padding[3];
        };

        /**
         * @brief  Summarized G-Buffer
         */
        struct GBuffer
        {
            UniqueHandle<vk2s::Image> depthBuffer;
            UniqueHandle<vk2s::Image> albedoTex;
            UniqueHandle<vk2s::Image> worldPosTex;
            UniqueHandle<vk2s::Image> normalTex;
            UniqueHandle<vk2s::Image> roughnessMetalnessTex;

            //! Binding G-Buffer for lighting pass
            UniqueHandle<vk2s::BindGroup> bindGroup;
        };

    private:
        /** 
         * @brief Vulkan(vk2s) initialization
         *  
         */
        void initVulkan();

        /** 
         * @brief  G-Buffer creation (separated to accommodate window resizing)
         *  
         */
        void createGBuffer();

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
         * @brief  Loads a 3D model from a specified path and adds it to the scene
         *  
         * @param path 3D model path
         */
        void addEntity(const std::filesystem::path& path);

        /** 
         * @brief  Delete the specified entity from the scene
         *  
         * @param entity Entity to be deleted
         */
        void removeEntity(const ec2s::Entity entity);

        /** 
         * @brief  Returns whether the mouse pointer is on the drawing area
         *  
         * @return 
         */
        bool isPointerOnRenderArea() const;

    private:
        //! Percentage of the rendering area in the window (outside of this is the GUI)
        inline const static glm::vec2 kRenderArea = glm::vec2(0.75f, 0.75f);
        //! Relative height of menu bar
        constexpr static double kMenuBarSize = 0.031;
        //! Maximum number of emitters that the editor can reflect in the drawing (must always sync with shader side)
        constexpr static size_t kMaxEmitterNum = 10;
        //! Camera movement speed
        constexpr static double kCameraMoveSpeed = 2.0;
        //! Camera viewpoint movement speed
        constexpr static double kCameraViewpointSpeed = 0.7;

    private:
        //! GPU commands (per frame)
        std::vector<Handle<vk2s::Command>> mCommands;
        //! Semaphore that waits until that frame is ready to be written (per frame)
        std::vector<Handle<vk2s::Semaphore>> mImageAvailableSems;
        //! Semaphore notifying that drawing is complete and presentable (per frame)
        std::vector<Handle<vk2s::Semaphore>> mRenderCompletedSems;
        //! Fence to wait for the CPU to write information until the frame is ready for processing (per frame)
        std::vector<Handle<vk2s::Fence>> mFences;

        //! G-Buffer
        GBuffer mGBuffer;

        //! Geometry path for deferred shading (draw all instances to G-Buffer)
        GraphicsPass mGeometryPass;
        //! Lighting path for deferred shading (apply shading to G-Buffer)
        GraphicsPass mLightingPass;

        //! Sampler with default settings for G-Buffer reading, etc.
        UniqueHandle<vk2s::Sampler> mDefaultSampler;

        //! Dummy image for pseudo binding
        UniqueHandle<vk2s::Image> mDummyTexture;

        //! Uniform buffer to write SceneParams
        UniqueHandle<vk2s::DynamicBuffer> mSceneBuffer;
        //! Buffer to which the GPU writes the entity that is mouse hovering
        UniqueHandle<vk2s::Buffer> mPickedIDBuffer;
        //! Uniform buffer to write emitter information
        UniqueHandle<vk2s::DynamicBuffer> mEmitterBuffer;
        //! BindGroup for scene information
        UniqueHandle<vk2s::BindGroup> mSceneBindGroup;
        //! BindGroup for Lighting pass informations
        UniqueHandle<vk2s::BindGroup> mLightingBindGroup;

        //! Currently Picked Entity
        std::optional<ec2s::Entity> mPickedEntity;
        //! Entity in scene with camera (always get it first, if not, create it)
        ec2s::Entity mCameraEntity = ec2s::kInvalidEntity;
        //! If State is to be changed, where to change it (to fix processing order)
        std::optional<AppState> mChangeDst;

        //! Current ImGuizmo operation mode (translate, rotate, scale)
        ImGuizmo::OPERATION mCurrentGizmoOperation;

        //! To detect only the first frame mouse clicked
        bool mDragging;

        //! For arbitrary texture loading
        ImGui::FileBrowser mFileBrowser;

        //! Current path (for explorer)
        std::filesystem::path mCurrentPath;
        //! Elapsed time to previous frame
        double mLastTime     = 0;
        //! Index of the frame buffer currently being processed
        uint32_t mNow        = 0;
    };

}  // namespace palm

#endif