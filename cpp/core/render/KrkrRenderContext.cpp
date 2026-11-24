#include "KrkrRenderContext.h"

#include <cstdio>

namespace krkr::render {

    namespace {
        KrkrGLFunctions gKrkrGL;

        GLuint compileShader(GLenum type, const char *src) {
            KrkrGLFunctions &gl = krkrGL();
            GLuint shader = gl.glCreateShader(type);
            if(!shader)
                return 0;
            const char *srcs[1] = { src };
            GLint lengths[1] = { -1 }; // null-terminated
            gl.glShaderSource(shader, 1, srcs, lengths);
            gl.glCompileShader(shader);
            GLint status = GL_FALSE;
            gl.glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
            if(status != GL_TRUE) {
                GLchar log[2048];
                GLsizei len = 0;
                gl.glGetShaderInfoLog(shader, sizeof(log), &len, log);
                std::fprintf(stderr,
                             "krkr::render: shader compile failed (%d): %.*s\n",
                             (int)type, (int)len, log);
                gl.glDeleteShader(shader);
                return 0;
            }
            return shader;
        }
    } // namespace

    KrkrGLFunctions &krkrGL() { return gKrkrGL; }

    bool bindGL(GLProcLoader loader) {
        if(!loader)
            return false;
        KrkrGLFunctions &gl = gKrkrGL;
#define KRKR_BIND(name)                                                        \
    do {                                                                       \
        gl.name = reinterpret_cast<decltype(gl.name)>(loader(#name));          \
    } while(false)
        KRKR_BIND(glGenTextures);
        KRKR_BIND(glDeleteTextures);
        KRKR_BIND(glBindTexture);
        KRKR_BIND(glTexParameteri);
        KRKR_BIND(glPixelStorei);
        KRKR_BIND(glTexImage2D);
        KRKR_BIND(glTexSubImage2D);
        KRKR_BIND(glCompressedTexImage2D);
        KRKR_BIND(glActiveTexture);
        KRKR_BIND(glBindBuffer);
        KRKR_BIND(glBindFramebuffer);
        KRKR_BIND(glBlendFunc);
        KRKR_BIND(glEnable);
        KRKR_BIND(glDisable);
        KRKR_BIND(glIsEnabled);
        KRKR_BIND(glViewport);
        KRKR_BIND(glClearColor);
        KRKR_BIND(glClear);
        KRKR_BIND(glFlush);
        KRKR_BIND(glGetIntegerv);
        KRKR_BIND(glGetVertexAttribiv);
        KRKR_BIND(glGetFloatv);
        KRKR_BIND(glUseProgram);
        KRKR_BIND(glCreateShader);
        KRKR_BIND(glShaderSource);
        KRKR_BIND(glCompileShader);
        KRKR_BIND(glAttachShader);
        KRKR_BIND(glDetachShader);
        KRKR_BIND(glCreateProgram);
        KRKR_BIND(glLinkProgram);
        KRKR_BIND(glDeleteShader);
        KRKR_BIND(glDeleteProgram);
        KRKR_BIND(glBindAttribLocation);
        KRKR_BIND(glGetUniformLocation);
        KRKR_BIND(glUniform1i);
        KRKR_BIND(glGetShaderiv);
        KRKR_BIND(glGetProgramiv);
        KRKR_BIND(glGetShaderInfoLog);
        KRKR_BIND(glGetProgramInfoLog);
        KRKR_BIND(glEnableVertexAttribArray);
        KRKR_BIND(glDisableVertexAttribArray);
        KRKR_BIND(glVertexAttribPointer);
        KRKR_BIND(glDrawArrays);
#undef KRKR_BIND
        gl.bound = gl.glGenTextures && gl.glDeleteTextures &&
            gl.glBindTexture && gl.glTexParameteri && gl.glPixelStorei &&
            gl.glTexImage2D && gl.glTexSubImage2D &&
            gl.glCompressedTexImage2D && gl.glActiveTexture &&
            gl.glBindBuffer && gl.glBindFramebuffer && gl.glBlendFunc &&
            gl.glEnable && gl.glDisable && gl.glIsEnabled && gl.glViewport &&
            gl.glClearColor && gl.glClear && gl.glFlush && gl.glGetIntegerv &&
            gl.glGetVertexAttribiv && gl.glGetFloatv && gl.glUseProgram &&
            gl.glCreateShader && gl.glCreateProgram && gl.glShaderSource &&
            gl.glCompileShader && gl.glAttachShader && gl.glDetachShader &&
            gl.glLinkProgram && gl.glDeleteShader && gl.glDeleteProgram &&
            gl.glBindAttribLocation && gl.glGetUniformLocation &&
            gl.glUniform1i && gl.glGetShaderiv && gl.glGetProgramiv &&
            gl.glGetShaderInfoLog && gl.glGetProgramInfoLog &&
            gl.glEnableVertexAttribArray && gl.glDisableVertexAttribArray &&
            gl.glVertexAttribPointer && gl.glDrawArrays;
        return gl.bound;
    }

    bool KrkrRenderContext::applyGLAttributes(const GLContextAttrs &attrs) {
        // Matches SdlAppDelegate.cpp:55-61 and AppDelegate.cpp:146-148
        // (RGBA8 / depth24 / stencil8 / double-buffered).
        bool ok = true;
        ok = SDL_GL_SetAttribute(SDL_GL_RED_SIZE, attrs.red) && ok;
        ok = SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, attrs.green) && ok;
        ok = SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, attrs.blue) && ok;
        ok = SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, attrs.alpha) && ok;
        ok = SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, attrs.depth) && ok;
        ok = SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, attrs.stencil) && ok;
        ok = SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, attrs.doubleBuffer) && ok;
        return ok;
    }

    KrkrRenderContext::KrkrRenderContext() = default;

    KrkrRenderContext::~KrkrRenderContext() { detach(); }

    bool KrkrRenderContext::attach(SDL_Window *window, SDL_GLContext context) {
        if(!window || !context)
            return false;
        window_ = window;
        context_ = context;
        current_ = false;
        return true;
    }

    void KrkrRenderContext::detach() {
        if(!window_)
            return;
        // Release current-thread binding before letting go of the pair (no-op
        // if the context was never current here).
        if(current_ && context_) {
            SDL_GL_MakeCurrent(window_, nullptr);
        }
        window_ = nullptr;
        context_ = nullptr;
        current_ = false;
    }

    bool KrkrRenderContext::makeCurrent() {
        if(!window_ || !context_)
            return false;
        current_ = SDL_GL_MakeCurrent(window_, context_);
        return current_;
    }

    void KrkrRenderContext::swap() {
        if(window_)
            SDL_GL_SwapWindow(window_);
    }

    bool KrkrRenderContext::isCurrent() const { return current_; }

    GLuint compileProgram(const char *vertexShader,
                          const char *fragmentShader) {
        KrkrGLFunctions &gl = krkrGL();
        if(!gl.isBound() || !vertexShader || !fragmentShader)
            return 0;

        GLuint vs = compileShader(GL_VERTEX_SHADER, vertexShader);
        if(!vs)
            return 0;
        GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragmentShader);
        if(!fs) {
            gl.glDeleteShader(vs);
            return 0;
        }

        GLuint program = gl.glCreateProgram();
        if(!program) {
            gl.glDeleteShader(fs);
            gl.glDeleteShader(vs);
            return 0;
        }
        // Module attribute convention (KrkrCompositor / KrkrYUVRenderer):
        // fixed indices so client-array draw calls do not depend on driver
        // attribute assignment.
        gl.glBindAttribLocation(program, 0, "a_position");
        gl.glBindAttribLocation(program, 1, "a_texCoord");
        gl.glAttachShader(program, vs);
        gl.glAttachShader(program, fs);
        gl.glLinkProgram(program);

        GLint status = GL_FALSE;
        gl.glGetProgramiv(program, GL_LINK_STATUS, &status);
        if(status != GL_TRUE) {
            GLchar log[2048];
            GLsizei len = 0;
            gl.glGetProgramInfoLog(program, sizeof(log), &len, log);
            std::fprintf(stderr, "krkr::render: program link failed: %.*s\n",
                         (int)len, log);
            gl.glDeleteProgram(program);
            program = 0;
        }
        // Shaders are detached + deleted; the compiled program keeps them alive
        // until link in every conformant implementation.
        if(program) {
            gl.glDetachShader(program, vs);
            gl.glDetachShader(program, fs);
        }
        gl.glDeleteShader(vs);
        gl.glDeleteShader(fs);
        return program;
    }

    void resetGLExtensionInfo() {
        // Extension information belongs to a GL context and must be queried
        // again after recreation.
        extern void TVPResetGLExtensionInfo();
        TVPResetGLExtensionInfo();
    }

} // namespace krkr::render
