cmake_minimum_required(VERSION 3.20)
project(hello-triangle LANGUAGES CXX)

add_executable(hello-triangle WIN32
    src/main.cpp
    src/D3DApp.cpp
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
)

# Enable D3D debug layer in Debug builds
target_compile_definitions(hello-triangle PRIVATE
    $<$<CONFIG:Debug>:D3D_DEBUG_LAYER>
)
