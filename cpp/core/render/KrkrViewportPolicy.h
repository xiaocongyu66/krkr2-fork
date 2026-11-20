#pragma once

//---------------------------------------------------------------------------
// B3: viewport fit policy for the SDL3 presentation path.
//
// Pure-math translation of TVPWindowLayer's layout
// (environ/impl/TVPWindow.cpp):
//  * fit()            -- RecalcPaintBox (:562-586): aspect-driven letterbox
//                        scale + centering offset, with the min/max/zoom scale
//                        triple (:581-584).
//  * Mode::Fill       -- toggleFillScale (:631-659): uniform fit vs.
//  anisotropic
//                        stretch that consumes the letterbox bars; toggling is
//                        a no-op when window and content ratios nearly match
//                        (:637-640).
// GL conventions: Viewport.x/y/w/h is a glViewport() rect in window pixels,
// bottom-left origin. offsetX/offsetY keep the engine's y-up letterbox
// centering and are expressed from the bottom-left as well.
//---------------------------------------------------------------------------

namespace krkr::render {

    struct Viewport {
        // Uniform fit scale after zoom, clamped to [minScale, maxScale]
        // (RecalcPaintBox's scale, setMinScale/setMaxScale/setZoomScale triple,
        // TVPWindow.cpp:581-584). Non-uniform in fill mode.
        float scale = 1.f;
        float scaleX = 1.f;
        float scaleY = 1.f;

        // Letterbox centering, window pixels (bottom-left origin).
        float offsetX = 0.f;
        float offsetY = 0.f;

        // glViewport() rect, window pixels.
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
    };

    class KrkrViewportPolicy {
    public:
        enum class Mode {
            Fit, // RecalcPaintBox product: uniform scale, centered letterbox
            Fill, // toggleFillScale product: stretch to remove the letterbox
                  // bars
        };

        void setDesignSize(int width, int height);
        void setZoomScale(float zoom); // setContentOffset/zoom role (:583)
        void setMinScale(float minScale); // setMinScale (:581)
        void setMaxScale(float maxScale); // setMaxScale (:582, default 2x)
        void setMode(Mode mode);
        Mode mode() const { return mode_; }

        // Aspect epsilon from toggleFillScale (:634-640): do not fill when the
        // window and content ratios almost match.
        static bool aspectRatiosDiffer(int designW, int designH, int winW,
                                       int winH);

        // Fit/Fill toggle with the toggleFillScale early-out (:637-641).
        Mode toggleMode(int winW, int winH);

        // RecalcPaintBox (:562-586) for the given window size. As
        // RecalcPaintBox does via setMinScale/setMaxScale/setZoomScale
        // (:581-583), this resets zoomScale to the fresh fit scale and min/max
        // scale to [scale, scale * 2] as a side effect.
        Viewport fit(int winW, int winH);

    private:
        int designW_ = 0;
        int designH_ = 0;
        float zoomScale_ = 1.f;
        float minScale_ = 0.f;
        float maxScale_ = 2.f;
        Mode mode_ = Mode::Fit;
    };

} // namespace krkr::render
