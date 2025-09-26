cmake_minimum_required(VERSION 3.25)
project(FiveMapper VERSION 0.1 LANGUAGES CXX)

include(FetchContent)
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.12.0
)
FetchContent_MakeAvailable(nlohmann_json)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(/W4 /permissive-)

add_executable(FiveMapper WIN32
  src/main.cpp
  src/platform/Window.h
  src/platform/Window.cpp
  src/platform/WindowConfig.h
  src/platform/WindowConfig.cpp
  src/dx12/DeviceResources.h
  src/dx12/DeviceResources.cpp
  src/dx12/GBufferTargets.h
  src/dx12/GBufferTargets.cpp
  src/dx12/GBufferRootSignature.h
  src/dx12/GBufferRootSignature.cpp
  src/dx12/GBufferPipeline.h
  src/dx12/GBufferPipeline.cpp
  src/dx12/ShaderUtils.h
  src/dx12/ShaderUtils.cpp
  src/dx12/LightingRootSignature.h
  src/dx12/LightingRootSignature.cpp
  src/dx12/LightingPipeline.h
  src/dx12/LightingPipeline.cpp
  src/dx12/ShadowMap.cpp
  src/dx12/ShadowMap.h
)

target_link_libraries(FiveMapper
    PRIVATE 
      nlohmann_json::nlohmann_json
      d3d12.lib
      dxgi.lib
      dxguid.lib
      d3dcompiler.lib
)

target_include_directories(FiveMapper
  PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/platform
    ${CMAKE_SOURCE_DIR}/src/dx12
)

add_custom_command(TARGET FiveMapper POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/shaders"
        "$<TARGET_FILE_DIR:FiveMapper>/shaders"
)
