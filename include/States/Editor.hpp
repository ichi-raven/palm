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

#include <filesystem>

namespace palm
{
    class Editor : public ec2s::State<palm::AppState, palm::CommonRegion>
    {
        GEN_STATE(Editor, palm::AppState, palm::CommonRegion);

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

        struct Mesh
        {
            vk2s::Mesh hostMesh;
            Handle<vk2s::Buffer> vertexBuffer;
            Handle<vk2s::Buffer> indexBuffer;

            Handle<vk2s::AccelerationStructure> blas;
            Handle<vk2s::Buffer> instanceBuffer;
        };

        struct Material
        {
            enum class Type : int32_t
            {
                eLambert    = 0,
                eConductor  = 1,
                eDielectric = 2,
                eMaterialNum
            };

            struct Params  // std430
            {
                glm::vec4 albedo;
                glm::vec4 emissive;
                int32_t texIndex;
                int32_t materialType;
                float alpha;
                float IOR;
            };

            vk2s::Material materialParam;
            Handle<vk2s::Buffer> uniformBuffer;

            constexpr static uint32_t kDefaultTexNum = 4;
            Handle<vk2s::Image> albedoTex;
            Handle<vk2s::Image> roughnessTex;
            Handle<vk2s::Image> metalnessTex;
            Handle<vk2s::Image> normalMapTex;

            Handle<vk2s::BindGroup> bindGroup;
        };

        struct EntityInfo
        {
            std::string groupName;
            std::string entityName;
            ec2s::Entity entityID;
        };

        struct Transform
        {
            struct Params
            {
                glm::mat4 model;
                glm::mat4 modelInvTranspose;
                glm::vec3 vel;
                float padding;

                void update(glm::vec3 translate, const glm::vec3& rotation, const glm::vec3& scaling)
                {
                    vel = translate - glm::vec3(model[0][3], model[1][3], model[2][3]);

                    model             = glm::translate(glm::mat4(1.f), translate) * glm::toMat4(glm::quat(rotation)) * glm::scale(glm::mat4(1.f), scaling);
                    modelInvTranspose = glm::transpose(glm::inverse(model));
                }
            };

            Params params;
            glm::vec3 pos = { 0.f, 0.f, 0.f };
            glm::vec3 rot = { 0.f, 0.f, 0.f };
            glm::vec3 scale = { 1.f, 1.f, 1.f };
            Handle<vk2s::DynamicBuffer> entityBuffer;
            Handle<vk2s::BindGroup> bindGroup;
        };

        struct GBuffer
        {
            UniqueHandle<vk2s::Image> depthBuffer;
            UniqueHandle<vk2s::Image> albedoTex;
            UniqueHandle<vk2s::Image> worldPosTex;
            UniqueHandle<vk2s::Image> normalTex;

            UniqueHandle<vk2s::BindGroup> bindGroup;
        };

        struct GraphicsPass
        {
            UniqueHandle<vk2s::RenderPass> renderpass;
            UniqueHandle<vk2s::Pipeline> pipeline;
            UniqueHandle<vk2s::Shader> vs;
            UniqueHandle<vk2s::Shader> fs;
            std::vector<Handle<vk2s::BindLayout>> bindLayouts;
        };

    private:
        //inline static void load(std::string_view path, vk2s::Device& device, std::vector<MeshInstance>& meshInstances, Handle<vk2s::Buffer>& materialUB, std::vector<Handle<vk2s::Image>>& materialTextures);

        void initVulkan();

        void updateAndRenderImGui(const double deltaTime);

        void updateShaderResources();

        void onResized();

        void addEntity(const std::filesystem::path& path);

        void removeEntity(ec2s::Entity entity);

    private:
        vk2s::Camera mCamera;

        std::vector<Handle<vk2s::Command>> mCommands;
        std::vector<Handle<vk2s::Semaphore>> mImageAvailableSems;
        std::vector<Handle<vk2s::Semaphore>> mRenderCompletedSems;
        std::vector<Handle<vk2s::Fence>> mFences;

        GBuffer mGBuffer;

        GraphicsPass mGeometryPass;
        GraphicsPass mLightingPass;

        UniqueHandle<vk2s::Image> mDummyImage;
        UniqueHandle<vk2s::Sampler> mDefaultSampler;

        UniqueHandle<vk2s::DynamicBuffer> mSceneBuffer;
        UniqueHandle<vk2s::BindGroup> mSceneBindGroup;

        std::vector<ec2s::Entity> mActiveEntities;
        std::optional<ec2s::Entity> mPickedEntity;

        double mLastTime = 0;
        uint32_t mNow;
        uint32_t mFrameCount;
    };

}  // namespace palm

#endif