#pragma once

//---------------------------------------------------------------------------
// B3: final presentation compositor for the SDL3 host.
//
// Replaces the cocos2d half of the engine's present path
// (UpdateDrawBuffer + DrawSprite, TVPWindow.cpp:548-629): paints exactly one
// engine-rendered surface (the render manager's target texture) as a
// full-viewport quad, letterboxed by the viewport policy, then paints the
// video overlay on top (MoviePlayerOverlay always sits above the game --
// the overlay node is attached to PrimaryLayerArea after the draw sprite,
// KRMoviePlayer.cpp:320-328).
//
// GL state ownership: the engine renderer restored blend/FBO/pixel-store via
// cocos2d::GL caches in _RestoreGLStatues (RenderManager_ogl.cpp:520-527,
// registered :2981). The compositor must not depend on any external state
// cache -- KrkrGLState snapshots and restores everything it touches.
//
// Threading: draw()/flush() run on the GL thread inside SdlAppDelegate's
// frame callback; SdlAppDelegate owns SDL_GL_SwapWindow -- this class never
// swaps. The engine's GetPoint readback contract (glReadPixels on the
// current FBO, RenderManager_ogl.cpp:1091-1101) requires the default
// framebuffer to hold composited content when it runs -- flush() exists for
// that boundary; if GetPoint is issued from a logic thread, that thread must
// own the GL context for the read (binding is the caller's job).
//---------------------------------------------------------------------------

#include "ComplexRect.h" // tTVPRect
#include "KrkrRenderContext.h"
#include "KrkrTexture.h"
#include "KrkrViewportPolicy.h"

namespace krkr::render {

    class KrkrYUVRenderer;

    //---------------------------------------------------------------------------
    // KrkrGLState: RAII snapshot/restore of the GL state the compositor dirties
    // (blend, viewport, active texture + 2D binding, program, array buffer,
    // framebuffer, unpack alignment). No external state cache involved --
    // see _RestoreGLStatues (ogl:520-527) for the cocos-era dependency being
    // removed. GL-thread only.
    //---------------------------------------------------------------------------
    class KrkrGLState {
    public:
        KrkrGLState();
        ~KrkrGLState();

        KrkrGLState(const KrkrGLState &) = delete;
        KrkrGLState &operator=(const KrkrGLState &) = delete;

    private:
        GLboolean blendEnabled_ = GL_FALSE;
        GLint viewport_[4] = { 0, 0, 0, 0 };
        GLint activeTexture_ = 0;
        GLint textureBinding2D_ = 0;
        GLint currentProgram_ = 0;
        GLint arrayBufferBinding_ = 0;
        GLint framebufferBinding_ = 0;
        GLint unpackAlignment_ = 4;
        GLint vertexAttrib0Enabled_ = GL_FALSE;
        GLint vertexAttrib1Enabled_ = GL_FALSE;
        GLfloat clearColor_[4] = { 0.f, 0.f, 0.f, 0.f };
    };

    class KrkrCompositor {
    public:
        KrkrCompositor();
        ~KrkrCompositor();

        KrkrCompositor(const KrkrCompositor &) = delete;
        KrkrCompositor &operator=(const KrkrCompositor &) = delete;

        // Window size in pixels (SDL_GetWindowSizeInPixels equivalent). Drives
        // the viewport policy each frame.
        void resize(int winW, int winH);

        void setViewportPolicy(KrkrViewportPolicy *policy);

        // Adopt the engine's rendered surface. w/h is the design content size
        // (LayerWidth/Height in UpdateDrawBuffer, TVPWindow.cpp:604-622);
        // scaleX/scaleY the texture content scale (iTVPTexture2D::GetScale,
        // used to invert NPOT padding there).
        void setSurfaceTexture(KrkrTexture *tex, int w, int h, float scaleX,
                               float scaleY);

        // Engine content origin is top-left (tTVPRect rows run top->bottom);
        // the quad samples V bottom-up by default so the design top edge maps
        // to the viewport top edge -- the DrawSprite anchor(0,1) behavior
        // (TVPWindow.cpp:67-68, :548-560).
        void setFlipY(bool flip);
        bool flipY() const { return flipY_; }

        // Mount the video overlay (top layer). bounds is in design coordinates,
        // matching VideoPresentOverlay::PresentPicture's GetBounds mapping
        // (KRMoviePlayer.cpp:312-318): the compositor maps it through the
        // viewport policy. nullptr clears the overlay.
        void setOverlayYUV(KrkrYUVRenderer *renderer, const tTVPRect &bounds);
        void clearOverlayYUV();

        // Paint one frame: letterbox clear -> surface quad -> overlay. Restores
        // all touched GL state. GL-thread only. Does not swap.
        void draw();

        // Flush queued GL work so a subsequent glReadPixels / swap sees the
        // composited content (GetPoint contract,
        // RenderManager_ogl.cpp:1091-1101). GL-thread only.
        void flush();

        // Context loss: drop the owned shader program (no GL calls -- the
        // context is already gone; rebuilt lazily on the next draw on the new
        // context, the EVENT_RENDERER_RECREATED equivalent for this module).
        void invalidate();

    private:
        void ensureProgram();
        void drawSurface(const Viewport &vp);
        void drawOverlay(const Viewport &vp);

        KrkrViewportPolicy *policy_ = nullptr;
        KrkrTexture *surface_ = nullptr;
        int surfaceW_ = 0;
        int surfaceH_ = 0;
        float surfaceScaleX_ = 1.f;
        float surfaceScaleY_ = 1.f;
        bool flipY_ = true;

        KrkrYUVRenderer *overlay_ = nullptr;
        tTVPRect overlayBounds_;

        int winW_ = 0;
        int winH_ = 0;

        GLuint program_ = 0;
        GLint uniformTexture_ = -1;
    };

} // namespace krkr::render
