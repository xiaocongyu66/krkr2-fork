#pragma once

//---------------------------------------------------------------------------
// B3 SDL3 migration: presentation plumbing for the render module.
//
// This file owns every direct GL/SDL dependency of cpp/core/render/:
//  * KrkrGLFunctions: a table of GL entry-point typedefs, populated at B3
//    init through bindGL() from an already-loaded function-pointer loader
//    (SDL_GL_GetProcAddress in the SDL3 host; the engine renderer loads its
//    own pointers via wglGetProcAddress/eglGetProcAddress,
//    RenderManager_ogl.cpp:219-224 -- we never link a GL library here).
//  * KrkrRenderContext: thin SDL_GL_* lifecycle wrapper around a GL context
//    owned by SdlAppDelegate (which creates it and drives the frame swap),
//    replacing TVPForceSwapBuffer's role (AndroidUtils.cpp:90-92).
//  * compileProgram(): the module's only shader builder.
//
// GL semantic level is GLES2/ES3: pure client-side vertex arrays (no VAO),
// GLSL 100 ES shaders (no #version directives). GL enum values are
// #ifndef-guarded definitions matching the GL spec so they coexist with the
// engine's own guards (RenderManager.h, ogl_common.h) and with real GL
// headers when the host TU includes them.
//
// Threading: all GL entry points are only touched on the window/GL thread
// (the SDL3 frame loop in SdlAppDelegate::run()). The engine's GetPoint
// readback (glReadPixels, RenderManager_ogl.cpp:1091-1101) may be issued from
// a logic thread and only needs the *current* FBO readable -- the compositor
// guarantees the default framebuffer is current and flushed before swap
// (KrkrCompositor::flush); see the thread-constraint notes there.
//---------------------------------------------------------------------------

#include <SDL3/SDL.h>

#include <cstddef>

namespace krkr::render {

    // Minimal GL scalar types, identical to the GL spec typedefs (typedef
    // redeclaration with the same type is legal C++, so these are harmless when
    // the host TU also includes real GL headers).
    typedef unsigned int GLenum;
    typedef unsigned int GLuint;
    typedef int GLint;
    typedef int GLsizei;
    typedef float GLfloat;
    typedef unsigned char GLboolean;
    typedef unsigned char GLubyte;
    typedef char GLchar;
    typedef unsigned int GLbitfield;

// GL constants used by this module (guarded like RenderManager.h).
#ifndef GL_FALSE
#define GL_FALSE 0
#define GL_TRUE 1
#endif
#ifndef GL_BLEND
#define GL_BLEND 0x0BE2
#endif
#ifndef GL_TEXTURE_2D
#define GL_TEXTURE_2D 0x0DE1
#endif
#ifndef GL_TEXTURE_BINDING_2D
#define GL_TEXTURE_BINDING_2D 0x8069
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#endif
#ifndef GL_ACTIVE_TEXTURE
#define GL_ACTIVE_TEXTURE 0x84E0
#endif
#ifndef GL_TEXTURE_MAG_FILTER
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_LINEAR 0x2601
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_TEXTURE_WRAP_S
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#endif
#ifndef GL_LUMINANCE
#define GL_LUMINANCE 0x1909
#endif
#ifndef GL_RGB
#define GL_RGB 0x1907
#endif
#ifndef GL_RGBA
#define GL_RGBA 0x1908
#endif
#ifndef GL_UNSIGNED_BYTE
#define GL_UNSIGNED_BYTE 0x1401
#endif
#ifndef GL_FLOAT
#define GL_FLOAT 0x1406
#endif
#ifndef GL_UNPACK_ALIGNMENT
#define GL_UNPACK_ALIGNMENT 0x0CF5
#endif
#ifndef GL_UNPACK_ROW_LENGTH
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#endif
#ifndef GL_TEXTURE1
#define GL_TEXTURE1 0x84C1
#define GL_TEXTURE2 0x84C2
#endif
#ifndef GL_VIEWPORT
#define GL_VIEWPORT 0x0BA2
#endif
#ifndef GL_CURRENT_PROGRAM
#define GL_CURRENT_PROGRAM 0x8B8D
#endif
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#define GL_ARRAY_BUFFER_BINDING 0x8894
#endif
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#endif
#ifndef GL_COLOR_BUFFER_BIT
#define GL_COLOR_BUFFER_BIT 0x4000
#endif
#ifndef GL_TRIANGLE_STRIP
#define GL_TRIANGLE_STRIP 0x0005
#endif
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#endif
#ifndef GL_ONE
#define GL_ONE 1
#endif
#ifndef GL_ZERO
#define GL_ZERO 0
#endif

    //---------------------------------------------------------------------------
    // KrkrGLFunctions: GL function-pointer table.
    //
    // Entry points are resolved lazily at module init via bindGL(), which takes
    // an already-installed loader (B3 wiring passes SDL_GL_GetProcAddress from
    // SdlAppDelegate; a test harness can pass any provider). Typedefs use
    // Krkr-prefixed names so they never collide with real GL header typedefs
    // (which carry platform calling-convention macros on Win32).
    //---------------------------------------------------------------------------
    class KrkrGLFunctions {
    public:
        typedef void (*GenTexturesFunc)(GLsizei n, GLuint *textures);
        typedef void (*DeleteTexturesFunc)(GLsizei n, const GLuint *textures);
        typedef void (*BindTextureFunc)(GLenum target, GLuint texture);
        typedef void (*TexParameterFunc)(GLenum target, GLenum pname,
                                         GLint param);
        typedef void (*PixelStoreiFunc)(GLenum pname, GLint param);
        typedef void (*TexImage2DFunc)(GLenum target, GLint level,
                                       GLint internalformat, GLsizei width,
                                       GLsizei height, GLint border,
                                       GLenum format, GLenum type,
                                       const void *pixels);
        typedef void (*TexSubImage2DFunc)(GLenum target, GLint level,
                                          GLint xoffset, GLint yoffset,
                                          GLsizei width, GLsizei height,
                                          GLenum format, GLenum type,
                                          const void *pixels);
        typedef void (*CompressedTexImage2DFunc)(
            GLenum target, GLint level, GLenum internalformat, GLsizei width,
            GLsizei height, GLint border, GLsizei imageSize, const void *data);
        typedef void (*ActiveTextureFunc)(GLenum texture);
        typedef void (*BindBufferFunc)(GLenum target, GLuint buffer);
        typedef void (*BindFramebufferFunc)(GLenum target, GLuint framebuffer);
        typedef void (*BlendFuncFunc)(GLenum sfactor, GLenum dfactor);
        typedef void (*EnableFunc)(GLenum cap);
        typedef void (*DisableFunc)(GLenum cap);
        typedef GLboolean (*IsEnabledFunc)(GLenum cap);
        typedef void (*ViewportFunc)(GLint x, GLint y, GLsizei width,
                                     GLsizei height);
        typedef void (*ClearColorFunc)(GLfloat red, GLfloat green, GLfloat blue,
                                       GLfloat alpha);
        typedef void (*ClearFunc)(GLbitfield mask);
        typedef void (*FlushFunc)(void);
        typedef void (*GetIntegervFunc)(GLenum pname, GLint *data);
        typedef void (*UseProgramFunc)(GLuint program);
        typedef GLuint (*CreateShaderFunc)(GLenum type);
        typedef void (*ShaderSourceFunc)(GLuint shader, GLsizei count,
                                         const GLchar *const *string,
                                         const GLint *length);
        typedef void (*CompileShaderFunc)(GLuint shader);
        typedef void (*AttachShaderFunc)(GLuint program, GLuint shader);
        typedef void (*DetachShaderFunc)(GLuint program, GLuint shader);
        typedef GLuint (*CreateProgramFunc)(void);
        typedef void (*LinkProgramFunc)(GLuint program);
        typedef void (*DeleteShaderFunc)(GLuint shader);
        typedef void (*DeleteProgramFunc)(GLuint program);
        typedef void (*BindAttribLocationFunc)(GLuint program, GLuint index,
                                               const GLchar *name);
        typedef GLint (*GetUniformLocationFunc)(GLuint program,
                                                const GLchar *name);
        typedef void (*Uniform1iFunc)(GLint location, GLint v0);
        typedef void (*GetShaderivFunc)(GLuint shader, GLenum pname,
                                        GLint *params);
        typedef void (*GetProgramivFunc)(GLuint program, GLenum pname,
                                         GLint *params);
        typedef void (*GetShaderInfoLogFunc)(GLuint shader, GLsizei bufSize,
                                             GLsizei *length, GLchar *infoLog);
        typedef void (*GetProgramInfoLogFunc)(GLuint program, GLsizei bufSize,
                                              GLsizei *length, GLchar *infoLog);
        typedef void (*EnableVertexAttribArrayFunc)(GLuint index);
        typedef void (*DisableVertexAttribArrayFunc)(GLuint index);
        typedef void (*VertexAttribPointerFunc)(GLuint index, GLint size,
                                                GLenum type,
                                                GLboolean normalized,
                                                GLsizei stride,
                                                const void *pointer);
        typedef void (*DrawArraysFunc)(GLenum mode, GLint first, GLsizei count);

        GenTexturesFunc glGenTextures = nullptr;
        DeleteTexturesFunc glDeleteTextures = nullptr;
        BindTextureFunc glBindTexture = nullptr;
        TexParameterFunc glTexParameteri = nullptr;
        PixelStoreiFunc glPixelStorei = nullptr;
        TexImage2DFunc glTexImage2D = nullptr;
        TexSubImage2DFunc glTexSubImage2D = nullptr;
        CompressedTexImage2DFunc glCompressedTexImage2D = nullptr;
        ActiveTextureFunc glActiveTexture = nullptr;
        BindBufferFunc glBindBuffer = nullptr;
        BindFramebufferFunc glBindFramebuffer = nullptr;
        BlendFuncFunc glBlendFunc = nullptr;
        EnableFunc glEnable = nullptr;
        DisableFunc glDisable = nullptr;
        IsEnabledFunc glIsEnabled = nullptr;
        ViewportFunc glViewport = nullptr;
        ClearColorFunc glClearColor = nullptr;
        ClearFunc glClear = nullptr;
        FlushFunc glFlush = nullptr;
        GetIntegervFunc glGetIntegerv = nullptr;
        UseProgramFunc glUseProgram = nullptr;
        CreateShaderFunc glCreateShader = nullptr;
        ShaderSourceFunc glShaderSource = nullptr;
        CompileShaderFunc glCompileShader = nullptr;
        AttachShaderFunc glAttachShader = nullptr;
        DetachShaderFunc glDetachShader = nullptr;
        CreateProgramFunc glCreateProgram = nullptr;
        LinkProgramFunc glLinkProgram = nullptr;
        DeleteShaderFunc glDeleteShader = nullptr;
        DeleteProgramFunc glDeleteProgram = nullptr;
        BindAttribLocationFunc glBindAttribLocation = nullptr;
        GetUniformLocationFunc glGetUniformLocation = nullptr;
        Uniform1iFunc glUniform1i = nullptr;
        GetShaderivFunc glGetShaderiv = nullptr;
        GetProgramivFunc glGetProgramiv = nullptr;
        GetShaderInfoLogFunc glGetShaderInfoLog = nullptr;
        GetProgramInfoLogFunc glGetProgramInfoLog = nullptr;
        EnableVertexAttribArrayFunc glEnableVertexAttribArray = nullptr;
        DisableVertexAttribArrayFunc glDisableVertexAttribArray = nullptr;
        VertexAttribPointerFunc glVertexAttribPointer = nullptr;
        DrawArraysFunc glDrawArrays = nullptr;

        // True after a fully successful bindGL().
        bool isBound() const { return bound; }

    private:
        friend bool bindGL(SDL_FunctionPointer (*loader)(const char *));
        bool bound = false;
    };

    // Module GL accessor. Callers must run on the GL thread.
    KrkrGLFunctions &krkrGL();

    // Resolve every GL entry point through `loader`. Returns false when any
    // symbol is missing; partial results are left in place for diagnosis.
    // B3 wiring passes SDL_GL_GetProcAddress (SdlAppDelegate owns the context
    // and has already made it current).
    using GLProcLoader = SDL_FunctionPointer (*)(const char *);
    bool bindGL(GLProcLoader loader);

    //---------------------------------------------------------------------------
    // KrkrRenderContext: GL context lifecycle wrapper.
    //
    // SdlAppDelegate owns the GL context (created in init(),
    // SdlAppDelegate.h); this class only attaches a window/context pair and
    // forwards makeCurrent/swap. GL attributes match the engine host
    // (RGBA8/depth24/stencil8, AppDelegate.cpp:146-148;
    // SdlAppDelegate.cpp:55-61 sets the same set via SDL_GL_SetAttribute --
    // this attribute bundle lives here so engine-side forced-present paths can
    // re-assert it, while context creation stays with the shell).
    //---------------------------------------------------------------------------
    // AppDelegate.cpp:146-148: GLContextAttrs {8, 8, 8, 8, 24, 8}. Namespace
    // scope: the NSDMIs are needed in applyGLAttributes' default argument.
    struct GLContextAttrs {
        int red = 8;
        int green = 8;
        int blue = 8;
        int alpha = 8;
        int depth = 24;
        int stencil = 8;
        int doubleBuffer = 1;
    };

    class KrkrRenderContext {
    public:
        // SDL_GL_SetAttribute bundle for the attribute set above. Call before
        // SdlAppDelegate creates the context.
        static bool applyGLAttributes(const GLContextAttrs &attrs = {});

        KrkrRenderContext();
        ~KrkrRenderContext();

        KrkrRenderContext(const KrkrRenderContext &) = delete;
        KrkrRenderContext &operator=(const KrkrRenderContext &) = delete;

        // Attach an existing (shell-owned) window/context pair. Does not make
        // the context current; call makeCurrent() explicitly.
        bool attach(SDL_Window *window, SDL_GLContext context);
        void detach();

        // Activate the attached context on this thread (GL thread only).
        bool makeCurrent();

        // Present the attached window. This is the SDL3 counterpart of
        // TVPForceSwapBuffer (AndroidUtils.cpp:90-92). Note: the SDL3 host's
        // frame loop (SdlAppDelegate::run) performs the per-frame SwapWindow
        // itself -- this entry point exists for engine-driven forced present
        // paths.
        void swap();

        // True while the attached pair is current according to the last
        // makeCurrent()/detach() result. (Deliberately tracked locally rather
        // than querying SDL_GL_GetCurrentContext, which may be bound to another
        // thread's pair.)
        bool isCurrent() const;

        SDL_Window *window() const { return window_; }
        SDL_GLContext context() const { return context_; }

    private:
        SDL_Window *window_ = nullptr;
        SDL_GLContext context_ = nullptr;
        bool current_ = false;
    };

    // Build a GLSL ES program from vertex/fragment source. Binds the module
    // attribute convention (a_position -> 0, a_texCoord -> 1) before linking.
    // Returns 0 on compile/link failure (details go to stderr).
    GLuint compileProgram(const char *vertexShader, const char *fragmentShader);

    // Reset the engine renderer's GL extension info (TVPGLExtensionInfoInited,
    // RenderManager_ogl.cpp:123) so a recreated context re-queries
    // glGetString(GL_EXTENSIONS).
    // TODO(B3+): needs an engine-side export (e.g. TVPResetGLExtensionInfo in
    // core_visual); stub body until the engine symbol exists. B3 context-loss
    // wiring (SdlAppDelegate::resetGLExtensionInfoInited) calls this hook.
    void resetGLExtensionInfo();

} // namespace krkr::render
