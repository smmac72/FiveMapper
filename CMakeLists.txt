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
add_compile_options(/W4 /permissive- /WX)

set(SOURCES
    src/main.cpp
    src/platform/Window.cpp
    src/dx12/DeviceResources.cpp
    src/dx12/GBufferPipeline.cpp
    src/dx12/GBufferRootSignature.cpp
)

set(HEADERS
    src/platform/Window.h
    src/dx12/DeviceResources.h
    src/dx12/GBufferPipeline.h
    src/dx12/GBufferRootSignature.h
)

add_executable(FiveMapper WIN32 ${SOURCES} ${HEADERS})

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
