
include_directories(
  ${CMAKE_SOURCE_DIR}/include
  ${VK2S_DIR}/include
  ${GLFW_DIR}/include
  ${STB_DIR}
  ${GLM_DIR}
  ${IMGUI_DIR}
  ${IMGUI_BACKEND_DIR}
  ${SPIRV_REFLECT_DIR}
  ${ASSIMP_INCLUDE_DIR}
  ${Vulkan_INCLUDE_DIRS}
  ${EC2S_DIR}/include)

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


# Create executable
add_executable(${MAIN_EXE})
target_sources(${MAIN_EXE}
PUBLIC
main.cpp
Scene.cpp
Integrator.cpp
MaterialTable.cpp
Film.cpp
)

# link libraries
get_filename_component(VULKAN_LIB_DIR ${Vulkan_LIBRARIES} DIRECTORY)
target_link_directories(${MAIN_EXE} PRIVATE ${CMAKE_BINARY_DIR}/lib ${VULKAN_LIB_DIR})

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
)

if(MSVC)
  set_property(TARGET ${MAIN_EXE} APPEND PROPERTY LINK_FLAGS "/DEBUG /PROFILE")
endif()
