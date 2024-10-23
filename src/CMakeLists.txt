
include_directories(
  ${CMAKE_SOURCE_DIR}/include
  ${VK2S_DIR}/include
  ${GLFW_DIR}/include
  ${STB_DIR}
  ${GLM_DIR}
  ${IMGUI_DIR}
  ${IMGUI_BACKEND_DIR}
  ${IMGUIZMO_DIR}
  ${SPIRV_REFLECT_DIR}
  ${ASSIMP_INCLUDE_DIR}
  ${Vulkan_INCLUDE_DIRS}
  ${EC2S_DIR}/include
  ${SLANG_DIR}/include)

# imgui 
set(IMGUI_BACKENDS_DIR ${IMGUI_DIR}/backends)
set(IMGUI_SOURCE_FILES
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_BACKENDS_DIR}/imgui_impl_vulkan.cpp
    ${IMGUI_BACKENDS_DIR}/imgui_impl_glfw.cpp
)

# imguizmo
set(IMGUIZMO_SOURCE_FILES
    ${IMGUIZMO_DIR}/GraphEditor.cpp
    ${IMGUIZMO_DIR}/ImCurveEdit.cpp
    ${IMGUIZMO_DIR}/ImGradient.cpp
    ${IMGUIZMO_DIR}/ImGuizmo.cpp
    ${IMGUIZMO_DIR}/ImSequencer.cpp
)

# Create executable
set (EXEC_SRCS 
main.cpp

States/Editor.cpp
States/Renderer.cpp
States/MaterialViewer.cpp

Integrators/PathIntegrator.cpp
${IMGUI_SOURCE_FILES}
${IMGUIZMO_SOURCE_FILES}
)

add_executable(${MAIN_EXE} ${EXEC_SRCS})
target_sources(${MAIN_EXE}
PUBLIC
${EXEC_SRCS}
# header files
../include/AppStates.hpp
../include/Film.hpp

../include/Integrators/Integrator.hpp
../include/Integrators/PathIntegrator.hpp

../include/Mesh.hpp
../include/Material.hpp
../include/Transform.hpp
../include/EntityInfo.hpp
../include/GraphicsPass.hpp

../include/States/Editor.hpp
../include/States/Renderer.hpp
../include/States/MaterialViewer.hpp
)

# link libraries
get_filename_component(VULKAN_LIB_DIR ${Vulkan_LIBRARIES} DIRECTORY)
target_link_directories(${MAIN_EXE} PRIVATE ${CMAKE_BINARY_DIR}/lib ${VULKAN_LIB_DIR} ${SLANG_DIR}/lib)

target_link_libraries(${MAIN_EXE} 
  PRIVATE
  Vulkan::Vulkan
  Vulkan::SPIRV-Tools
  Vulkan::shaderc_combined
  # HACK : CMake's FindVulkan can't find SPIRV-Tools-opt
  optimized SPIRV-Tools-opt debug SPIRV-Tools-optd
  optimized ${GLFW_LIB_NAME} debug ${GLFW_LIB_NAME}${CMAKE_DEBUG_POSTFIX}
  optimized ${ASSIMP_LIB_NAME} debug ${ASSIMP_LIB_NAME}${CMAKE_DEBUG_POSTFIX}
  optimized zlibstatic.lib debug zlibstaticd.lib
  optimized ${VK2S_LIB_NAME} debug ${VK2S_LIB_NAME}${CMAKE_DEBUG_POSTFIX}
  ${SLANG_LIB_NAME}
)

if(MSVC)
  set_property(TARGET ${MAIN_EXE} APPEND PROPERTY LINK_FLAGS "/DEBUG /PROFILE")
endif()
