cmake_minimum_required(VERSION 3.20)
project(voxel_game LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(VOXEL_BUILD_TESTS "Build tests" OFF)

# -------- Dependencies --------
include(FetchContent)

# Vulkan FIRST (needed before linking any target against Vulkan::Vulkan)
find_package(Vulkan REQUIRED)

# GLFW
FetchContent_Declare(glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4
)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# glm
FetchContent_Declare(glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG 1.0.1
)
FetchContent_MakeAvailable(glm)

# ImGui
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        v1.91.0
)
FetchContent_MakeAvailable(imgui)

add_library(imgui_glfw_vulkan STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui_glfw_vulkan PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_glfw_vulkan PUBLIC Vulkan::Vulkan glfw)

# -------- Paths --------
set(SRC_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(SHADER_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/shaders)   # GLSL input
set(ASSETS_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/assets)    # assets input
set(SHADERS_BIN_DIR ${CMAKE_BINARY_DIR}/shaders)       # SPIR-V output

# -------- Sources for the app (so VS shows headers/shaders nicely) --------
file(GLOB_RECURSE SRC_FILES    CONFIGURE_DEPENDS ${SRC_DIR}/*.cpp)
file(GLOB_RECURSE HDR_FILES    CONFIGURE_DEPENDS ${INCLUDE_DIR}/*.h ${INCLUDE_DIR}/*.hpp)
file(GLOB_RECURSE SHADER_FILES CONFIGURE_DEPENDS
     ${SHADER_DIR}/*.vert ${SHADER_DIR}/*.frag ${SHADER_DIR}/*.comp ${SHADER_DIR}/*.geom)

add_executable(voxel_game
  ${SRC_FILES}
  ${HDR_FILES}
  ${SHADER_FILES}  # NOTE: these are *inputs*; build rules below produce .spv
)
target_include_directories(voxel_game PRIVATE ${INCLUDE_DIR})
target_link_libraries(voxel_game PRIVATE glfw Vulkan::Vulkan glm::glm imgui_glfw_vulkan)

# CMakeLists.txt – after you define your target
add_custom_target(gen_atlas ALL
    COMMAND ${CMAKE_COMMAND} -E echo "Generating atlas..."
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/make_atlas.py
    BYPRODUCTS ${CMAKE_BINARY_DIR}/build_assets/atlas_32px_8x8.png
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_dependencies(${PROJECT_NAME} gen_atlas)

# Copy to run dir
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/build_assets/atlas_32px_8x8.png
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets/atlas.png)

# VS filters
source_group(TREE ${SRC_DIR}     PREFIX "Source Files" FILES ${SRC_FILES})
source_group(TREE ${INCLUDE_DIR} PREFIX "Header Files" FILES ${HDR_FILES})
source_group(TREE ${SHADER_DIR}  PREFIX "Shaders"      FILES ${SHADER_FILES})

# -------- Shader compilation: GLSL -> SPIR-V with glslc --------
# Find glslc
if (NOT GLSLC)
  find_program(GLSLC glslc HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/Bin32")
endif()

set(SPV_OUTPUTS "")
if (GLSLC)
  file(GLOB_RECURSE SHADERS_SRC CONFIGURE_DEPENDS
       "${SHADER_DIR}/*.vert" "${SHADER_DIR}/*.frag" "${SHADER_DIR}/*.comp" "${SHADER_DIR}/*.geom")

  foreach(SH ${SHADERS_SRC})
    get_filename_component(SH_NAME ${SH} NAME)                     # e.g., triangle.vert
    set(SPV "${SHADERS_BIN_DIR}/${SH_NAME}.spv")                   # build/shaders/triangle.vert.spv

    add_custom_command(
      OUTPUT  ${SPV}
      COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADERS_BIN_DIR}"
      COMMAND ${GLSLC} -O "${SH}" -o "${SPV}"
      DEPENDS ${SH}
      COMMENT "glslc ${SH_NAME}"
      VERBATIM
    )
    list(APPEND SPV_OUTPUTS ${SPV})
  endforeach()

  add_custom_target(ShadersSPV ALL DEPENDS ${SPV_OUTPUTS})
  add_dependencies(voxel_game ShadersSPV)
else()
  message(WARNING "glslc not found — shaders will NOT be compiled. "
                  "Install the Vulkan SDK or add glslc to PATH.")
endif()

# -------- Safe post-build copy (assets + compiled shaders) --------
# Tiny script that only copies if the source exists (avoids MSB3073)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/cmake")

function(add_safe_copy_dir FROM_DIR SUBFOLDER_NAME)
  set(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/cmake/copy_if_exists_${SUBFOLDER_NAME}.cmake")
  file(WRITE "${SCRIPT}" "
    if (EXISTS \"\${src}\")
      file(MAKE_DIRECTORY \"\${dst}\")
      execute_process(COMMAND \"${CMAKE_COMMAND}\" -E copy_directory \"\${src}\" \"\${dst}\")
    endif()
  ")
  add_custom_command(TARGET voxel_game POST_BUILD
    COMMAND ${CMAKE_COMMAND} -Dsrc="${FROM_DIR}"
                              -Ddst="$<TARGET_FILE_DIR:voxel_game>/${SUBFOLDER_NAME}"
                              -P "${SCRIPT}"
    COMMENT "Copy ${SUBFOLDER_NAME} -> $<TARGET_FILE_DIR:voxel_game>/${SUBFOLDER_NAME}"
    VERBATIM
  )
endfunction()

add_safe_copy_dir("${ASSETS_DIR}"      "assets")
add_safe_copy_dir("${SHADERS_BIN_DIR}" "shaders")