cmake_minimum_required(VERSION 3.20)
project(hello-triangle LANGUAGES CXX)

add_executable(hello-triangle WIN32
    src/main.cpp
    src/D3DApp.cpp
    src/Mesh.cpp
    src/Shader.cpp
)

target_include_directories(hello-triangle PRIVATE src)

target_link_libraries(hello-triangle PRIVATE
    d3d11
    dxgi
    dxguid
)

target_compile_definitions(hello-triangle PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    $<$<CONFIG:Debug>:D3D_DEBUG_LAYER>
)

# Treat all source files as UTF-8 (suppresses MSVC C4819 encoding warning).
target_compile_options(hello-triangle PRIVATE /utf-8)

# ---------------------------------------------------------------------------
# Shader compilation (fxc, Shader Model 5.0)
# Output .cso files to the build tree; POST_BUILD copies them next to the exe.
# ---------------------------------------------------------------------------
set(SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")

set(CSO_VERTEX "${SHADER_OUTPUT_DIR}/vertex.cso")
set(CSO_PIXEL  "${SHADER_OUTPUT_DIR}/pixel.cso")

add_custom_command(
    OUTPUT  "${CSO_VERTEX}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_OUTPUT_DIR}"
    COMMAND fxc /nologo /T vs_5_0 /E VSMain
                /Fo "${CSO_VERTEX}"
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/vertex.hlsl"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/vertex.hlsl"
    COMMENT "fxc: vertex.hlsl -> vertex.cso"
    VERBATIM
)

add_custom_command(
    OUTPUT  "${CSO_PIXEL}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_OUTPUT_DIR}"
    COMMAND fxc /nologo /T ps_5_0 /E PSMain
                /Fo "${CSO_PIXEL}"
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/pixel.hlsl"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/pixel.hlsl"
    COMMENT "fxc: pixel.hlsl -> pixel.cso"
    VERBATIM
)

add_custom_target(hello-triangle-shaders DEPENDS
    "${CSO_VERTEX}"
    "${CSO_PIXEL}"
)
add_dependencies(hello-triangle hello-triangle-shaders)

# Copy compiled shaders to <exe_dir>/shaders/ after each build.
add_custom_command(TARGET hello-triangle POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:hello-triangle>/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CSO_VERTEX}"
        "$<TARGET_FILE_DIR:hello-triangle>/shaders/vertex.cso"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CSO_PIXEL}"
        "$<TARGET_FILE_DIR:hello-triangle>/shaders/pixel.cso"
    VERBATIM
)

# ---------------------------------------------------------------------------
# Phase 1-7: D3D12 port — hello-triangle-d3d12
# Same rotating RGB triangle, implemented with Direct3D 12.
# Shaders remain SM 5.0 / fxc; dxc + SM 6.0 is introduced in Phase 3.
# ---------------------------------------------------------------------------
add_executable(hello-triangle-d3d12 WIN32
    src/main12.cpp
    src/D3D12App.cpp
)

target_include_directories(hello-triangle-d3d12 PRIVATE src)

target_link_libraries(hello-triangle-d3d12 PRIVATE
    d3d12
    dxgi
    dxguid
    d3dcompiler
)

target_compile_definitions(hello-triangle-d3d12 PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    $<$<CONFIG:Debug>:D3D_DEBUG_LAYER>
)

target_compile_options(hello-triangle-d3d12 PRIVATE /utf-8)

# Compile D3D12-specific shaders (fxc, SM 5.0).
set(CSO12_VERTEX "${SHADER_OUTPUT_DIR}/vertex12.cso")
set(CSO12_PIXEL  "${SHADER_OUTPUT_DIR}/pixel12.cso")

add_custom_command(
    OUTPUT  "${CSO12_VERTEX}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_OUTPUT_DIR}"
    COMMAND fxc /nologo /T vs_5_0 /E VSMain
                /Fo "${CSO12_VERTEX}"
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/vertex12.hlsl"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/vertex12.hlsl"
    COMMENT "fxc: vertex12.hlsl -> vertex12.cso"
    VERBATIM
)

add_custom_command(
    OUTPUT  "${CSO12_PIXEL}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_OUTPUT_DIR}"
    COMMAND fxc /nologo /T ps_5_0 /E PSMain
                /Fo "${CSO12_PIXEL}"
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/pixel12.hlsl"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/pixel12.hlsl"
    COMMENT "fxc: pixel12.hlsl -> pixel12.cso"
    VERBATIM
)

add_custom_target(hello-triangle-d3d12-shaders DEPENDS
    "${CSO12_VERTEX}"
    "${CSO12_PIXEL}"
)
add_dependencies(hello-triangle-d3d12 hello-triangle-d3d12-shaders)

# Copy D3D12 shaders to <exe_dir>/shaders/ after build.
add_custom_command(TARGET hello-triangle-d3d12 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:hello-triangle-d3d12>/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CSO12_VERTEX}"
        "$<TARGET_FILE_DIR:hello-triangle-d3d12>/shaders/vertex12.cso"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CSO12_PIXEL}"
        "$<TARGET_FILE_DIR:hello-triangle-d3d12>/shaders/pixel12.cso"
    VERBATIM
)
