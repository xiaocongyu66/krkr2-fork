#include "KrkrViewportPolicy.h"

#include <algorithm>
#include <cmath>

namespace krkr::render {

    void KrkrViewportPolicy::setDesignSize(int width, int height) {
        designW_ = width;
        designH_ = height;
    }

    void KrkrViewportPolicy::setZoomScale(float zoom) { zoomScale_ = zoom; }

    void KrkrViewportPolicy::setMinScale(float minScale) {
        minScale_ = minScale;
    }

    void KrkrViewportPolicy::setMaxScale(float maxScale) {
        maxScale_ = maxScale;
    }

    void KrkrViewportPolicy::setMode(Mode mode) { mode_ = mode; }

    bool KrkrViewportPolicy::aspectRatiosDiffer(int designW, int designH,
                                                int winW, int winH) {
        if(!designW || !designH || !winW || !winH)
            return false;
        // toggleFillScale names R = view aspect, r = content aspect and
        // early-outs when |R - r| < 0.01 (TVPWindow.cpp:634-641).
        float R = (float)winW / (float)winH;
        float r = (float)designW / (float)designH;
        return std::fabs(R - r) >= 0.01f;
    }

    KrkrViewportPolicy::Mode KrkrViewportPolicy::toggleMode(int winW,
                                                            int winH) {
        if(!aspectRatiosDiffer(designW_, designH_, winW, winH))
            return mode_;
        mode_ = (mode_ == Mode::Fit) ? Mode::Fill : Mode::Fit;
        return mode_;
    }

    Viewport KrkrViewportPolicy::fit(int winW, int winH) {
        Viewport vp;
        if(!designW_ || !designH_ || !winW || !winH) {
            // Degenerate: present nothing (RecalcPaintBox bails without sizes,
            // TVPWindow.cpp:563-564).
            vp.w = winW;
            vp.h = winH;
            return vp;
        }

        if(mode_ == Mode::Fill) {
            // toggleFillScale's stretch factors (TVPWindow.cpp:642-653):
            // scaleX = R/r (borders left/right) or scaleY = r/R (borders
            // top/bottom) -- together the per-axis window/design ratios.
            vp.scaleX = (float)winW / (float)designW_;
            vp.scaleY = (float)winH / (float)designH_;
            vp.scale = std::min(vp.scaleX, vp.scaleY);
            vp.offsetX = 0.f;
            vp.offsetY = 0.f;
            vp.x = 0;
            vp.y = 0;
            vp.w = winW;
            vp.h = winH;
            return vp;
        }

        // RecalcPaintBox body (TVPWindow.cpp:566-580). R = content aspect
        // (design), r = view aspect (window).
        float r = (float)winW / (float)winH;
        float R = (float)designW_ / (float)designH_;
        float baseScale;
        float baseOffsetX, baseOffsetY;
        if(R > r) {
            baseScale = (float)winW / (float)designW_;
            baseOffsetX = 0.f;
            baseOffsetY = ((float)winH - (float)designH_ * baseScale) * 0.5f;
        } else {
            baseScale = (float)winH / (float)designH_;
            baseOffsetX = ((float)winW - (float)designW_ * baseScale) * 0.5f;
            baseOffsetY = 0.f;
        }

        // RecalcPaintBox's scale triple (TVPWindow.cpp:581-583):
        // setMinScale(scale); setMaxScale(scale * 2); setZoomScale(scale).
        minScale_ = baseScale;
        maxScale_ = baseScale * 2.f;
        zoomScale_ = baseScale;

        // Clamp/zoom role (setMinScale/setMaxScale/setZoomScale, :581-584):
        // zoom applied afterwards grows the base rect from its center.
        float scale = std::clamp(zoomScale_, minScale_, maxScale_);
        float baseW = (float)designW_ * baseScale;
        float baseH = (float)designH_ * baseScale;
        float drawW = (float)designW_ * scale;
        float drawH = (float)designH_ * scale;
        float offsetX = baseOffsetX + (baseW - drawW) * 0.5f;
        float offsetY = baseOffsetY + (baseH - drawH) * 0.5f;

        vp.scale = scale;
        vp.scaleX = scale;
        vp.scaleY = scale;
        vp.offsetX = offsetX;
        vp.offsetY = offsetY;
        vp.x = (int)offsetX;
        vp.y = (int)offsetY;
        vp.w = (int)(offsetX + drawW) - vp.x;
        vp.h = (int)(offsetY + drawH) - vp.y;
        return vp;
    }

} // namespace krkr::render
