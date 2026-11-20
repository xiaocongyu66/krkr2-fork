#pragma once

//---------------------------------------------------------------------------
// B3: video overlay renderer (YUV420 Planar -> RGB), SDL3 presentation path.
//
// Drop-in for TVPYUVSprite (environ/cocos2d/YUVSprite.cpp): three I8 planes
// (Y plane = the sprite's own texture, U/V = _textureU/_textureV), converted
// by the same BT.601 fragment shader (YUVSprite.cpp:7-38). Plane upload keeps
// YUVSprite.cpp:114-148 semantics: glTexImage2D when a plane's size changes,
// glTexSubImage2D otherwise. Presentation is driven by the movie module
// ( VideoPresentOverlay::PresentPicture, KRMoviePlayer.cpp:241-297 ); audio/
// video sync and the m_picture ring (MAX_BUFFER_COUNT 4, KRMoviePlayer.h:21)
// stay there -- this class just draws whatever frame it is handed.
//
// Audio/video sync belongs to the movie module (KRMoviePlayer); this renderer
// only paints frames. Drawn above the game surface by KrkrCompositor, which
// replaces the MoviePlayerOverlay layering (KRMoviePlayer.cpp:320-328).
//---------------------------------------------------------------------------

#include "ComplexRect.h" // tTVPRect
#include "KrkrRenderContext.h"

namespace krkr::render {

    class KrkrYUVRenderer {
    public:
        KrkrYUVRenderer();
        ~KrkrYUVRenderer();

        KrkrYUVRenderer(const KrkrYUVRenderer &) = delete;
        KrkrYUVRenderer &operator=(const KrkrYUVRenderer &) = delete;

        // Plane update; I8 byte buffers (PixelFormat::I8, the texture format
        // the sprite path uses, YUVSprite.cpp:190-194). Chroma width is YW/2
        // (YV12/NV12 three-plane layout as fed by KRMoviePlayer.cpp:328-330),
        // heights are explicit so half-height chroma planes can shrink cleanly.
        // Size change re-allocates a plane, same size updates it in place
        // (YUVSprite.cpp:114-148). GL-thread only.
        void updatePlanes(const void *Y, int YW, int YH, const void *U,
                          const void *V, int UH, int VH);

        // Destination window size for the bounds' y flip. GL-thread only.
        void setOutputSize(int width, int height);

        // Paint the current frame into bounds (window pixels, top-left origin
        // -- the engine's tTVPRect convention; flipped to GL bottom-left here,
        // the sprite anchor(0,1) equivalent of PresentPicture's `top =
        // parentHeight - rc.top` KRMoviePlayer.cpp:313-318). GL-thread only.
        void draw(const tTVPRect &bounds);

        void setVisible(bool visible);
        bool visible() const { return visible_; }

        // Context loss: forget GL names/program without calling GL; content is
        // restored by the next updatePlanes() (the engine's redraw-after-loss
        // path; EVENT_RENDERER_RECREATED only rebuilds shaders, ogl:2968-2980).
        void invalidate();

    private:
        void ensureProgram();

        GLuint texY_ = 0;
        GLuint texU_ = 0;
        GLuint texV_ = 0;

        int planeYW_ = 0;
        int planeYH_ = 0;
        int planeUW_ = 0; // chroma width; planeUW_ == planeYW_ / 2
        int planeUH_ = 0;
        int planeVW_ = 0;
        int planeVH_ = 0;

        int outW_ = 0;
        int outH_ = 0;

        GLuint program_ = 0;
        GLint uniformY_ = -1;
        GLint uniformU_ = -1;
        GLint uniformV_ = -1;

        bool visible_ = true;
    };

} // namespace krkr::render
