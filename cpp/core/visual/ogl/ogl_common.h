#pragma once

#if defined(_WIN32) || defined(LINUX)
#if defined(_M_X64)
#define GLEW_STATIC
#endif
#include "GL/glew.h"
#endif

#ifndef GL_UNPACK_ROW_LENGTH
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#endif

#if TARGET_OS_MAC
#include <OpenGL/gl.h>
#endif

#if TARGET_OS_IPHONE
#include <OpenGLES/ES2/gl.h>
#include <OpenGLES/ES2/glext.h>
#endif

#ifdef __ANDROID__
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#endif

#include <string>

// B4: CHECK_GL_ERROR_DEBUG 之前由 cocos2dx 头链提供；非 cocos 配置
// （KRKR2_BUILD_APP 关闭）没有该定义，这里给一个空操作兜底。cocos 配置
// 下 cocos2d 的同名宏优先生效（#ifndef 守护，行为不变）。
#ifndef CHECK_GL_ERROR_DEBUG
#define CHECK_GL_ERROR_DEBUG()                                                 \
    do {                                                                       \
    } while(false)
#endif

bool TVPCheckGLExtension(const std::string &extname);
