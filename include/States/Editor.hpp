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
#include <vk2s/AssetLoader.hpp>

#include "../include/AppStates.hpp"

namespace palm
{
    class Editor : public ec2s::State<palm::AppState, palm::CommonRegion>
    {
        GEN_STATE(Editor, palm::AppState, palm::CommonRegion);

    private:
        struct SceneUB  // std430
        {
            glm::mat4 model;
            glm::mat4 view;
            glm::mat4 proj;
        };

        enum class MaterialType : int32_t
        {
            eLambert    = 0,
            eConductor  = 1,
            eDielectric = 2,
            eMaterialNum
        };

        struct MaterialUB  // std430
        {
            glm::vec4 albedo;
            glm::vec4 emissive;
            int32_t texIndex;
            int32_t materialType;
            float alpha;
            float IOR;
        };

        struct InstanceMappingUB  // std430
        {
            uint64_t VBAddress;
            uint64_t IBAddress;
            uint32_t materialIndex;
            uint32_t padding[3];
        };

        struct MeshInstance
        {
            vk2s::AssetLoader::Mesh hostMesh;
            Handle<vk2s::Buffer> vertexBuffer;
            Handle<vk2s::Buffer> indexBuffer;

            Handle<vk2s::AccelerationStructure> blas;
        };

        struct FrameBuffer
        {
            UniqueHandle<vk2s::Image> depthBuffer;
            UniqueHandle<vk2s::Image> worldPosTex;
            UniqueHandle<vk2s::Image> normalTex;
        };

        struct GraphicsPipeline
        {
            UniqueHandle<vk2s::RenderPass> renderpass;
            UniqueHandle<vk2s::Pipeline> pipeline;
            UniqueHandle<vk2s::Shader> vs;
            UniqueHandle<vk2s::Shader> fs;

        };

    private:
        inline static void load(std::string_view path, vk2s::Device& device, vk2s::AssetLoader& loader, std::vector<MeshInstance>& meshInstances, Handle<vk2s::Buffer>& materialUB, std::vector<Handle<vk2s::Image>>& materialTextures);

        void initVulkan();

        void renderImGui();

        void onResized();

    private:
        vk2s::Camera mCamera;

        std::vector<Handle<vk2s::Command>> mCommands;
        std::vector<Handle<vk2s::Semaphore>> mImageAvailableSems;
        std::vector<Handle<vk2s::Semaphore>> mRenderCompletedSems;
        std::vector<Handle<vk2s::Fence>> mFences;

        FrameBuffer mFrameBuffer;

        GraphicsPipeline mGeometryPass;
        GraphicsPipeline mLightingPass;
        UniqueHandle<vk2s::Pipeline> mRayTracePipeline;
        UniqueHandle<vk2s::DynamicBuffer> mSceneBuffer;
        UniqueHandle<vk2s::BindGroup> mBindGroup;

        std::vector<MeshInstance> mMeshInstances;

        double mLastTime = 0;
        uint32_t mNow;
    };

}  // namespace palm

#endif