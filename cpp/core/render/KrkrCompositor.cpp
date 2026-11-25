#include "KrkrCompositor.h"

#include "KrkrYUVRenderer.h"

#include <cstdio>
#include <cstdlib>
#include <algorithm>

namespace krkr::render {

    namespace {
        // Present shader: identity position + direct texture sample. Blend is
        // disabled while drawing (BlendFunc::DISABLE, TVPWindow.cpp:626).
        const char *kSurfaceVertexShader =
            "attribute vec2 a_position;\n"
            "attribute vec2 a_texCoord;\n"
            "varying vec2 v_texCoord;\n"
            "void main()\n"
            "{\n"
            "    gl_Position = vec4(a_position, 0.0, 1.0);\n"
            "    v_texCoord = a_texCoord;\n"
            "}\n";

        const char *kSurfaceFragmentShader =
            "#ifdef GL_ES\n"
            "precision mediump float;\n"
            "#endif\n"
            "varying vec2 v_texCoord;\n"
            "uniform sampler2D u_texture;\n"
            "void main()\n"
            "{\n"
            "    gl_FragColor = texture2D(u_texture, v_texCoord);\n"
            "}\n";

        // Debug: visualize the texcoord field (u -> red, v -> green) to
        // reveal whatever mapping the rasterizer is actually applying.
        const char *kSurfaceFragmentShaderTexcoordView =
            "#ifdef GL_ES\n"
            "precision mediump float;\n"
            "#endif\n"
            "varying vec2 v_texCoord;\n"
            "void main()\n"
            "{\n"
            "    gl_FragColor = vec4(v_texCoord, 0.0, 1.0);\n"
            "}\n";
    } // namespace

    //---------------------------------------------------------------------------
    // KrkrGLState
    //---------------------------------------------------------------------------
    KrkrGLState::KrkrGLState() {
        KrkrGLFunctions &gl = krkrGL();
        if(!gl.isBound())
            return;
        blendEnabled_ = gl.glIsEnabled(GL_BLEND);
        gl.glGetIntegerv(GL_VIEWPORT, viewport_);
        gl.glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture_);
        gl.glGetIntegerv(GL_TEXTURE_BINDING_2D, &textureBinding2D_);
        gl.glGetIntegerv(GL_CURRENT_PROGRAM, &currentProgram_);
        gl.glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBufferBinding_);
        gl.glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebufferBinding_);
        gl.glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpackAlignment_);
        gl.glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_ENABLED,
                               &vertexAttrib0Enabled_);
        gl.glGetVertexAttribiv(1, GL_VERTEX_ATTRIB_ARRAY_ENABLED,
                               &vertexAttrib1Enabled_);
        gl.glGetFloatv(GL_COLOR_CLEAR_VALUE, clearColor_);
    }

    KrkrGLState::~KrkrGLState() {
        KrkrGLFunctions &gl = krkrGL();
        if(!gl.isBound())
            return;
        // Restore in reverse-ish order; texture binding goes to the unit that
        // was active, then the active unit itself.
        gl.glPixelStorei(GL_UNPACK_ALIGNMENT, unpackAlignment_);
        gl.glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)framebufferBinding_);
        gl.glBindBuffer(GL_ARRAY_BUFFER, (GLuint)arrayBufferBinding_);
        gl.glUseProgram((GLuint)currentProgram_);
        gl.glActiveTexture((GLenum)activeTexture_);
        gl.glBindTexture(GL_TEXTURE_2D, (GLuint)textureBinding2D_);
        gl.glViewport(viewport_[0], viewport_[1], viewport_[2], viewport_[3]);
        gl.glClearColor(clearColor_[0], clearColor_[1], clearColor_[2],
                        clearColor_[3]);
        if(vertexAttrib0Enabled_ == GL_TRUE)
            gl.glEnableVertexAttribArray(0);
        else
            gl.glDisableVertexAttribArray(0);
        if(vertexAttrib1Enabled_ == GL_TRUE)
            gl.glEnableVertexAttribArray(1);
        else
            gl.glDisableVertexAttribArray(1);
        if(blendEnabled_ == GL_TRUE)
            gl.glEnable(GL_BLEND);
        else
            gl.glDisable(GL_BLEND);
    }

    //---------------------------------------------------------------------------
    // KrkrCompositor
    //---------------------------------------------------------------------------
    KrkrCompositor::KrkrCompositor() { overlayBounds_.set_size(0, 0); }

    KrkrCompositor::~KrkrCompositor() {
        if(program_ && krkrGL().isBound())
            krkrGL().glDeleteProgram(program_);
    }

    void KrkrCompositor::resize(int winW, int winH) {
        winW_ = winW;
        winH_ = winH;
    }

    void KrkrCompositor::setViewportPolicy(KrkrViewportPolicy *policy) {
        policy_ = policy;
    }

    void KrkrCompositor::setSurfaceTexture(KrkrTexture *tex, int w, int h,
                                           float scaleX, float scaleY) {
        surface_ = tex;
        surfaceW_ = w;
        surfaceH_ = h;
        surfaceScaleX_ = scaleX;
        surfaceScaleY_ = scaleY;
    }

    void KrkrCompositor::setFlipY(bool flip) { flipY_ = flip; }

    void KrkrCompositor::setOverlayYUV(KrkrYUVRenderer *renderer,
                                       const tTVPRect &bounds) {
        overlay_ = renderer;
        overlayBounds_ = bounds;
    }

    void KrkrCompositor::clearOverlayYUV() {
        overlay_ = nullptr;
        overlayBounds_.set_size(0, 0);
    }

    void KrkrCompositor::ensureProgram() {
        if(program_)
            return;
        const char *fs = kSurfaceFragmentShader;
        if(std::getenv("KRKR_SDL3_TEXCOORD_VIEW"))
            fs = kSurfaceFragmentShaderTexcoordView;
        program_ = compileProgram(kSurfaceVertexShader, fs);
        if(!program_) {
            std::fprintf(stderr, "krkr::render: surface shader build failed\n");
            return;
        }
        uniformTexture_ = krkrGL().glGetUniformLocation(program_, "u_texture");
    }

    void KrkrCompositor::draw() {
        if(!krkrGL().isBound())
            return;

        // Snapshot GL state: no reliance on the cocos2d::GL caches that powered
        // _RestoreGLStatues ( RenderManager_ogl.cpp:520-527 ).
        KrkrGLState state;

        KrkrGLFunctions &gl = krkrGL();

        // Presentation always targets the SDL window's default framebuffer,
        // regardless of an engine FBO left bound by the preceding frame.
        gl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
        // Letterbox clear (window background), then the policy rect.
        gl.glDisable(GL_BLEND); // BlendFunc::DISABLE (TVPWindow.cpp:626)
        gl.glViewport(0, 0, winW_, winH_);
        gl.glClearColor(0.f, 0.f, 0.f, 1.f);
        gl.glClear(GL_COLOR_BUFFER_BIT);

        Viewport vp;
        if(policy_) {
            vp = policy_->fit(winW_, winH_);
        } else {
            // No policy: 1:1 mapping, no letterbox.
            vp.w = winW_;
            vp.h = winH_;
        }

        if(surface_ && surface_->hasContent()) {
            drawSurface(vp);
        }
        if(overlay_) {
            drawOverlay(vp);
        }
    }

    void KrkrCompositor::drawSurface(const Viewport &vp) {
        KrkrGLFunctions &gl = krkrGL();
        GLuint name = surface_->glName();
        if(!name)
            return; // context lost / content invalidated -- nothing to show yet
        ensureProgram();
        if(!program_)
            return;

        // Full-viewport quad in NDC; the policy rect handles all fitting.
        // Texcoords cover the content region: UpdateDrawBuffer's texture-rect
        // arithmetic (TVPWindow.cpp:610-623) reduces to content-scale ratios --
        // (designW * scale / internalW) for the U axis, likewise V. For the
        // common scale==1, size==internal case this is 0..1.
        float texU = 1.f, texV = 1.f;
        if(surface_->internalWidth() > 0)
            texU = std::min(1.f,
                            (float)surfaceW_ * surfaceScaleX_ /
                                (float)surface_->internalWidth());
        if(surface_->internalHeight() > 0)
            texV = std::min(1.f,
                            (float)surfaceH_ * surfaceScaleY_ /
                                (float)surface_->internalHeight());
        // DrawSprite anchor(0,1) (TVPWindow.cpp:67-68): the engine content's
        // top row lands at the viewport's top edge, so V runs bottom-up over
        // the texture when flipY is on.
        float v0 = flipY_ ? texV : 0.f;
        float v1 = flipY_ ? 0.f : texV;

        // Diagnostic (env-gated): dump the mapping inputs once per process.
        if(std::getenv("KRKR_SDL3_DUMP_SURFACE")) {
            static bool logged = false;
            if(!logged) {
                logged = true;
                std::fprintf(
                    stderr,
                    "KRKR_MAP: win=%dx%d vp=%d,%d,%d,%d "
                    "surface=%dx%d scale=%.4fx%.4f internal=%dx%d "
                    "texU=%.4f texV=%.4f flipY=%d\n",
                    winW_, winH_, vp.x, vp.y, vp.w, vp.h, surfaceW_, surfaceH_,
                    surfaceScaleX_, surfaceScaleY_, surface_->internalWidth(),
                    surface_->internalHeight(), texU, texV, (int)flipY_);
                std::fflush(stderr);
            }
        }

        const float positions[4][2] = {
            { -1.f, -1.f },
            { 1.f, -1.f },
            { 1.f, 1.f },
            { -1.f, 1.f },
        };
        const float texcoords[4][2] = {
            { 0.f, v0 },
            { texU, v0 },
            { texU, v1 },
            { 0.f, v1 },
        };

        gl.glViewport(vp.x, vp.y, vp.w, vp.h);
        gl.glActiveTexture(GL_TEXTURE0);
        gl.glBindTexture(GL_TEXTURE_2D, name);
        gl.glUseProgram(program_);
        gl.glUniform1i(uniformTexture_, 0);
        gl.glBindBuffer(GL_ARRAY_BUFFER, 0);
        gl.glEnableVertexAttribArray(0);
        gl.glEnableVertexAttribArray(1);
        gl.glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, positions);
        gl.glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, texcoords);
        gl.glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        gl.glDisableVertexAttribArray(0);
        gl.glDisableVertexAttribArray(1);
    }

    void KrkrCompositor::drawOverlay(const Viewport &vp) {
        // Map design-space bounds to window pixels (PresentPicture's GetBounds
        // mapping, KRMoviePlayer.cpp:312-318): scale by the policy rect, keep
        // the top-left origin for the overlay's own GL flip.
        tTVPRect win = overlayBounds_;
        float sx = (surfaceW_ > 0) ? (float)vp.w / (float)surfaceW_ : vp.scaleX;
        float sy = (surfaceH_ > 0) ? (float)vp.h / (float)surfaceH_ : vp.scaleY;
        tjs_int left = (tjs_int)(vp.x + overlayBounds_.left * sx);
        tjs_int top = (tjs_int)(vp.y + overlayBounds_.top * sy);
        tjs_int right = (tjs_int)(vp.x + overlayBounds_.right * sx);
        tjs_int bottom = (tjs_int)(vp.y + overlayBounds_.bottom * sy);
        win.set_offsets(left, top);
        win.set_width(right - left);
        win.set_height(bottom - top);

        overlay_->setOutputSize(winW_, winH_);
        overlay_->draw(win);
    }

    void KrkrCompositor::flush() {
        // GetPoint contract (RenderManager_ogl.cpp:1091-1101): glReadPixels
        // needs the *current* FBO to hold the composited image. The compositor
        // binds no FBO of its own (the surface quad goes to the default
        // framebuffer) and KrkrGLState restores whatever was bound; a glFlush
        // here makes sure the draws are submitted before a readback or swap on
        // this thread. Note: a logic-thread readback must have its own context
        // binding; that is the caller's responsibility, not the compositor's.
        if(krkrGL().isBound())
            krkrGL().glFlush();
    }

    void KrkrCompositor::invalidate() {
        // No GL calls: the program belongs to the lost context and the driver
        // releases it with the context (or it is simply unreachable). Rebuilt
        // lazily by ensureProgram() on the new context.
        program_ = 0;
        uniformTexture_ = -1;
    }

} // namespace krkr::render
