cmake_minimum_required(VERSION 3.19)
project(core_render_module LANGUAGES CXX)

# B3 SDL3 migration: the presentation half of visual/ogl rendering.
#
# What lives here (and what does not):
#  * No cocos2d headers anywhere -- iTVPTexture2D is used through its
#    RenderManager.h interface (visual/), and the engine texture's GL name
#    is handed over via the KrkrTextureNameResolver hook.
#  * GL entry points are bound at runtime through bindGL() (KrkrRenderContext),
#    with the loader supplied by the host (SDL_GL_GetProcAddress). KrkrRender
#    Context.h includes <SDL3/SDL.h>; SDL is linked here so the module only
#    builds in the SDL3 shell configuration.
#  * The module never swaps buffers itself -- SdlAppDelegate owns the frame
#    loop; KrkrRenderContext::swap only exists for engine-driven forced
#    present paths.

set(RENDER_PATH ${CMAKE_CURRENT_LIST_DIR})
set(RENDER_SOURCE_FILES
    ${RENDER_PATH}/KrkrRenderContext.cpp
    ${RENDER_PATH}/KrkrTexture.cpp
    ${RENDER_PATH}/KrkrCompositor.cpp
    ${RENDER_PATH}/KrkrYUVRenderer.cpp
    ${RENDER_PATH}/KrkrViewportPolicy.cpp
)

find_package(SDL3 3.4 CONFIG REQUIRED)

add_library(${PROJECT_NAME} STATIC ${RENDER_SOURCE_FILES})
target_link_libraries(${PROJECT_NAME} PRIVATE SDL3::SDL3)

# Flat-name includes per repo convention: KrkrTexture.h includes
# "RenderManager.h" from visual/.
target_include_directories(${PROJECT_NAME}
    PUBLIC ${CMAKE_CURRENT_LIST_DIR}
           ${CMAKE_CURRENT_LIST_DIR}/../visual
)
krkr2_target_link_foundation(${PROJECT_NAME} PUBLIC)
krkr2_target_include_base_headers(${PROJECT_NAME})
krkr2_target_include_environ_headers(${PROJECT_NAME})
