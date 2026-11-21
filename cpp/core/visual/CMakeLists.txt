cmake_minimum_required(VERSION 3.19)
project(core_visual_module LANGUAGES CXX)

set(VISUAL_PATH ${CMAKE_CURRENT_LIST_DIR})
set(VISUAL_SOURCE_FILES
    ${VISUAL_PATH}/LoadWEBP.cpp
    ${VISUAL_PATH}/TransIntf.cpp
    ${VISUAL_PATH}/MenuItemIntf.cpp
    ${VISUAL_PATH}/SaveTLG5.cpp
    ${VISUAL_PATH}/FontSystem.cpp
    ${VISUAL_PATH}/tvpps.inc
    ${VISUAL_PATH}/LoadPVRv3.cpp
    ${VISUAL_PATH}/RectItf.cpp
    ${VISUAL_PATH}/VideoOvlIntf.cpp
    ${VISUAL_PATH}/LayerBitmapIntf.cpp
    ${VISUAL_PATH}/FontImpl.cpp
    ${VISUAL_PATH}/RenderManager.cpp
    ${VISUAL_PATH}/LoadJPEG.cpp
    ${VISUAL_PATH}/GraphicsLoaderIntf.cpp
    ${VISUAL_PATH}/SaveTLG6.cpp
    ${VISUAL_PATH}/FreeType.cpp
    ${VISUAL_PATH}/LoadJXR.cpp
    ${VISUAL_PATH}/ImageFunction.cpp
    ${VISUAL_PATH}/tvpgl.cpp
    ${VISUAL_PATH}/LoadTLG.cpp
    ${VISUAL_PATH}/LayerTreeOwnerImpl.cpp
    ${VISUAL_PATH}/GraphicsLoadThread.cpp
    ${VISUAL_PATH}/LayerManager.cpp
    ${VISUAL_PATH}/PrerenderedFont.cpp
    ${VISUAL_PATH}/LoadBPG.cpp
    ${VISUAL_PATH}/WindowIntf.cpp
    ${VISUAL_PATH}/FreeTypeFontRasterizer.cpp
    ${VISUAL_PATH}/LoadPNG.cpp
    ${VISUAL_PATH}/LayerIntf.cpp
    ${VISUAL_PATH}/ComplexRect.cpp
    ${VISUAL_PATH}/CharacterData.cpp
    ${VISUAL_PATH}/BitmapLayerTreeOwner.cpp
    ${VISUAL_PATH}/BitmapIntf.cpp
    ${VISUAL_PATH}/argb.cpp

    ${VISUAL_PATH}/gl/blend_function.cpp
    ${VISUAL_PATH}/gl/ResampleImage.cpp
    ${VISUAL_PATH}/gl/WeightFunctor.cpp

    ${VISUAL_PATH}/ogl/astcrt.cpp
    ${VISUAL_PATH}/ogl/etcpak.cpp
    ${VISUAL_PATH}/ogl/imagepacker.cpp
    ${VISUAL_PATH}/ogl/pvrtc.cpp
    ${VISUAL_PATH}/ogl/RenderManager_ogl.cpp

    ${VISUAL_PATH}/impl/BasicDrawDevice.cpp
    ${VISUAL_PATH}/impl/BitmapBitsAlloc.cpp
    ${VISUAL_PATH}/impl/BitmapInfomation.cpp
    ${VISUAL_PATH}/impl/DInputMgn.cpp
    ${VISUAL_PATH}/impl/DrawDevice.cpp
#    ${VISUAL_PATH}/impl/GDIFontRasterizer.cpp
    ${VISUAL_PATH}/impl/GraphicsLoaderImpl.cpp
    ${VISUAL_PATH}/impl/LayerBitmapImpl.cpp
    ${VISUAL_PATH}/impl/LayerImpl.cpp
    ${VISUAL_PATH}/impl/MenuItemImpl.cpp
#    ${VISUAL_PATH}/impl/NativeFreeTypeFace.cpp
    ${VISUAL_PATH}/impl/PassThroughDrawDevice.cpp
    ${VISUAL_PATH}/impl/TVPScreen.cpp
#    ${VISUAL_PATH}/impl/TVPSysFont.cpp
    ${VISUAL_PATH}/impl/VideoOvlImpl.cpp
#    ${VISUAL_PATH}/impl/VSyncTimingThread.cpp
    ${VISUAL_PATH}/impl/WindowImpl.cpp
)

set(VISUAL_HEADERS_DIR
    ${VISUAL_PATH}/
    ${VISUAL_PATH}/gl
    ${VISUAL_PATH}/ogl
    ${VISUAL_PATH}/impl
)

add_library(${PROJECT_NAME} STATIC ${VISUAL_SOURCE_FILES})
target_include_directories(${PROJECT_NAME} PUBLIC ${VISUAL_HEADERS_DIR})
krkr2_target_link_foundation(${PROJECT_NAME} PUBLIC)
target_link_libraries(${PROJECT_NAME} PRIVATE core_sound_module core_utils_module)
krkr2_target_include_base_headers(${PROJECT_NAME})
krkr2_target_include_environ_headers(${PROJECT_NAME})
# B4: cocos 侧源码段（RenderManager/RenderManager_ogl/TVPScreen 内的
# KRKR2_BUILD_APP 门控）在默认 full 构建开启，SDL3 壳配置（KRKR2_BUILD_APP=OFF）
# 下编译为等价的非 cocos 路径。
target_compile_definitions(${PROJECT_NAME} PRIVATE
    KRKR2_BUILD_APP=$<BOOL:${KRKR2_BUILD_APP}>
)
target_include_directories(${PROJECT_NAME} PRIVATE
    ${KRKR2_CORE_DIR}/plugin
)

target_link_libraries(${PROJECT_NAME} PRIVATE libbpg)

find_package(WebP CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} INTERFACE WebP::webp WebP::webpdecoder WebP::webpdemux)

# cocos2dx：仅 cocos 壳配置依赖（B4）。visual 的 cocos 引用面（RenderManager
# 软件 renderer 的 Texture2D 适配、ogl renderer 的 AdapterTexture2D、
# TVPScreen 的 Director 帧尺寸）经 KRKR2_BUILD_APP 编译门控留在模块内，
# 非 cocos 配置不链接 cocos2dx。
if(KRKR2_BUILD_APP)
    find_package(cocos2dx CONFIG REQUIRED)
    target_link_libraries(${PROJECT_NAME} PRIVATE
            cocos2dx::cocos2d
    )
else()
    # RenderManager.cpp includes xxhash/xxhash.h; the app build gets it via
    # the copy cocos2d-x bundles internally, so the overlay xxhash port
    # (same include layout) is only needed for host configurations. The
    # upstream port exports no usable CMake config, so resolve the header
    # and library the plain way (vcpkg puts them on the default search
    # path -- same pattern as JXR/LibArchive below).
    find_path(XXHASH_INCLUDE_DIR NAMES xxhash/xxhash.h)
    find_library(XXHASH_LIBRARY NAMES xxhash)
    target_include_directories(${PROJECT_NAME} PRIVATE ${XXHASH_INCLUDE_DIR})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${XXHASH_LIBRARY})
endif()

find_package(JXR REQUIRED)
target_compile_definitions(${PROJECT_NAME} PRIVATE -D__ANSI__)
target_include_directories(${PROJECT_NAME} PRIVATE ${JXR_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${JXR_LIBRARIES})

find_package(libjpeg-turbo CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PUBLIC
    $<IF:$<TARGET_EXISTS:libjpeg-turbo::turbojpeg>,libjpeg-turbo::turbojpeg,libjpeg-turbo::turbojpeg-static>
)

target_link_libraries(${PROJECT_NAME} PRIVATE core_imageops)

find_package(lz4 CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PUBLIC
    lz4::lz4
)
