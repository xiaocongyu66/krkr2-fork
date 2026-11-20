cmake_minimum_required(VERSION 3.19)
project(core_movie_module LANGUAGES CXX)

set(MOVIE_PATH ${CMAKE_CURRENT_LIST_DIR})
set(MOVIE_SOURCE_FILES

    ${MOVIE_PATH}/ffmpeg/AEChannelInfo.cpp
    ${MOVIE_PATH}/ffmpeg/AEFactory.cpp
    ${MOVIE_PATH}/ffmpeg/AEStreamInfo.cpp
    ${MOVIE_PATH}/ffmpeg/AEUtil.cpp
    ${MOVIE_PATH}/ffmpeg/AudioCodecFFmpeg.cpp
    ${MOVIE_PATH}/ffmpeg/AudioCodecPassthrough.cpp
    ${MOVIE_PATH}/ffmpeg/AudioDevice.cpp
    ${MOVIE_PATH}/ffmpeg/BaseRenderer.cpp
    ${MOVIE_PATH}/ffmpeg/BitstreamStats.cpp
    ${MOVIE_PATH}/ffmpeg/Clock.cpp
    ${MOVIE_PATH}/ffmpeg/CodecUtils.cpp
    ${MOVIE_PATH}/ffmpeg/Demux.cpp
    ${MOVIE_PATH}/ffmpeg/DemuxFFmpeg.cpp
    ${MOVIE_PATH}/ffmpeg/DemuxPacket.cpp
    ${MOVIE_PATH}/ffmpeg/FactoryCodec.cpp
    ${MOVIE_PATH}/ffmpeg/FFmpegInit.cpp
    ${MOVIE_PATH}/ffmpeg/InputStream.cpp
    ${MOVIE_PATH}/ffmpeg/KRMovieLayer.cpp
    ${MOVIE_PATH}/ffmpeg/Message.cpp
    ${MOVIE_PATH}/ffmpeg/MessageQueue.cpp
    ${MOVIE_PATH}/ffmpeg/ProcessInfo.cpp
    ${MOVIE_PATH}/ffmpeg/RenderFlags.cpp
    ${MOVIE_PATH}/ffmpeg/StreamInfo.cpp
    ${MOVIE_PATH}/ffmpeg/Thread.cpp
    ${MOVIE_PATH}/ffmpeg/Timer.cpp
    ${MOVIE_PATH}/ffmpeg/TimeUtils.cpp
    ${MOVIE_PATH}/ffmpeg/VideoCodec.cpp
    ${MOVIE_PATH}/ffmpeg/VideoCodecFFmpeg.cpp
    ${MOVIE_PATH}/ffmpeg/VideoPlayer.cpp
    ${MOVIE_PATH}/ffmpeg/VideoPlayerAudio.cpp
    ${MOVIE_PATH}/ffmpeg/VideoPlayerVideo.cpp
    ${MOVIE_PATH}/ffmpeg/VideoReferenceClock.cpp
    ${MOVIE_PATH}/ffmpeg/VideoRenderer.cpp
)

set(MOVIE_HEADERS_DIR
    ${MOVIE_PATH}/
    ${MOVIE_PATH}/ffmpeg
)

add_library(${PROJECT_NAME} STATIC ${MOVIE_SOURCE_FILES})
target_include_directories(${PROJECT_NAME} PUBLIC ${MOVIE_HEADERS_DIR})
krkr2_target_link_foundation(${PROJECT_NAME} PUBLIC)
target_link_libraries(${PROJECT_NAME} PRIVATE
    core_sound_module
    core_visual_module
    core_utils_module
)
krkr2_target_include_base_headers(${PROJECT_NAME})
krkr2_target_include_environ_headers(${PROJECT_NAME})

find_package(FFMPEG COMPONENTS
        avutil
        avformat
        avfilter
        avcodec
        swscale
        swresample REQUIRED
)

target_include_directories(${PROJECT_NAME} PUBLIC ${FFMPEG_INCLUDE_DIRS})

target_link_libraries(${PROJECT_NAME} PRIVATE ${FFMPEG_LIBRARIES})

# ---------------------------------------------------------------------------
# cocos2d 后端源码（B4）。KRMoviePlayer.cpp 经 Cocos Node/Sprite 场景图呈现
# 视频，krffmpeg.cpp 构造两侧 player 对象，二者都需要 cocos2dx 头与库；
# 引擎接缝符号（GetVideoOverlayObject 等）的 SDL3 壳替代实现在
# core_environ_sdl3。配置 KRKR2_BUILD_APP=OFF（krkr2_sdl3 专用配置）时整块
# 跳过，movie 模块不引入 cocos2dx 依赖。
# ---------------------------------------------------------------------------
if(KRKR2_BUILD_APP)
set(MOVIE_COCOS_SOURCE_FILES
    ${MOVIE_PATH}/ffmpeg/KRMoviePlayer.cpp
    ${MOVIE_PATH}/ffmpeg/krffmpeg.cpp
)

add_library(core_movie_cocos STATIC ${MOVIE_COCOS_SOURCE_FILES})
# core_visual_module/core_sound_module 直接链接：KRMoviePlayer.cpp 经
# flat-name 头引用 visual impl（WindowImpl/VideoOvlImpl）与
# sound/win32（WaveMixer.h），PRIVATE 依赖不向上传递。
target_link_libraries(core_movie_cocos PRIVATE core_movie_module core_visual_module core_sound_module)
# KRMoviePlayer.cpp 经 flat-name 头引用 visual impl（WindowImpl/VideoOvlImpl）
# 与 environ cocos2d 目录（CocosWindowAccess/YUVSprite）。
krkr2_target_include_base_headers(core_movie_cocos)
krkr2_target_include_environ_headers(core_movie_cocos)
# Timer.h -> ObjectList.h/TickCount.h live in utils; CocosWindowAccess/
# YUVSprite live under environ/cocos2d. Direct includes, so the dirs must
# be on this target's path (PRIVATE deps of core_movie_module do not
# propagate to it).
target_include_directories(core_movie_cocos PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../utils
    ${CMAKE_CURRENT_LIST_DIR}/../environ/cocos2d
)

find_package(cocos2dx CONFIG REQUIRED)
target_link_libraries(core_movie_cocos PRIVATE cocos2dx::cocos2d)
endif() # KRKR2_BUILD_APP
