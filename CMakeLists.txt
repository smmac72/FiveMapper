cmake_minimum_required(VERSION 3.25)
project(FiveMapper VERSION 0.1 LANGUAGES CXX)

include(FetchContent)

# nlohmann_json (header-only)
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.12.0
)
FetchContent_MakeAvailable(nlohmann_json)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(/W4 /permissive-)

# try to find assimp via package manager (vcpkg, etc). if not found, fetch from source.
set(ASSIMP_FOUND_LOCAL FALSE)
find_package(assimp CONFIG QUIET)
if(NOT assimp_FOUND)
  message(STATUS "assimp config not found -> fetching assimp from source")
  # slim build: only what we need
  FetchContent_Declare(
    assimp_src
    GIT_REPOSITORY https://github.com/assimp/assimp.git
    GIT_TAG        v5.3.1
  )
  # reduce build time
  set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
  set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
  set(ASSIMP_BUILD_OBJ_IMPORTER ON CACHE BOOL "" FORCE)
  set(ASSIMP_BUILD_FBX_IMPORTER ON CACHE BOOL "" FORCE)
  set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
  set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(assimp_src)
  # assimp source tree defines target "assimp"
  set(ASSIMP_FOUND_LOCAL TRUE)
endif()

# try to find directxtex; if not found, fetch and create alias Microsoft::DirectXTex
find_package(directxtex CONFIG QUIET)
if(NOT directxtex_FOUND)
  message(STATUS "directxtex config not found -> fetching DirectXTex from source")
  FetchContent_Declare(
    directxtex_src
    GIT_REPOSITORY https://github.com/microsoft/DirectXTex.git
    GIT_TAG        jun2024
  )
  # keep it minimal
  set(BUILD_TOOLS OFF CACHE BOOL "" FORCE)
  set(BUILD_DX11  OFF CACHE BOOL "" FORCE)
  set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(directxtex_src)
  # the upstream project defines target "DirectXTex"; we alias to what our code expects
  add_library(Microsoft::DirectXTex ALIAS DirectXTex)
endif()

add_executable(FiveMapper WIN32
  src/main.cpp

  # platform
  src/platform/Window.h
  src/platform/Window.cpp
  src/platform/WindowConfig.h
  src/platform/WindowConfig.cpp

  # dx12 core
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
  src/dx12/ShadowPipeline.h
  src/dx12/ShadowPipeline.cpp
  src/dx12/ShadowRootSignature.h
  src/dx12/ShadowRootSignature.cpp

  # assets
  src/assets/TextureLoader.h
  src/assets/TextureLoader.cpp
  src/assets/Material.h
  src/assets/Material.cpp
  src/assets/Mesh.h
  src/assets/Mesh.cpp
)

# link libs (prefer config packages when found)
if(assimp_FOUND)
  target_link_libraries(FiveMapper PRIVATE assimp::assimp)
else()
  # when built from source, target name is "assimp"
  target_link_libraries(FiveMapper PRIVATE assimp)
endif()

if(directxtex_FOUND)
  target_link_libraries(FiveMapper PRIVATE Microsoft::DirectXTex)
else()
  # alias defined above maps to the source-built DirectXTex
  target_link_libraries(FiveMapper PRIVATE Microsoft::DirectXTex)
endif()

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
    ${CMAKE_SOURCE_DIR}/src/assets
)

add_custom_command(TARGET FiveMapper POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_SOURCE_DIR}/shaders"
          "$<TARGET_FILE_DIR:FiveMapper>/shaders"
)
