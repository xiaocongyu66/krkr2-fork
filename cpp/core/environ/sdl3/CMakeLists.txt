cmake_minimum_required(VERSION 3.28)
project(core_environ_sdl3 LANGUAGES CXX)

find_package(SDL3 3.4 CONFIG REQUIRED)

add_library(${PROJECT_NAME} STATIC
    SdlAppDelegate.cpp
    SdlEngineRunner.cpp
    SdlKeyCodeConv.cpp
    SdlWindowLayer.cpp
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_LIST_DIR})

# core_environ_module is the engine composition root: it PUBLIC-links the
# foundation modules and the domain stack the shell sources compile against
# (visual/event/input headers), so this module stays header-complete even when
# it is built standalone under a cocos KRKR2_BUILD_APP=ON configuration.
target_link_libraries(${PROJECT_NAME}
    PUBLIC core_environ_module SDL3::SDL3
)
