// B4: cocos2d.h 仅 cocos 壳编译（KRKR2_BUILD_APP 由 visual 模块 CMake
// 注入）。GetHeight 的历史实现从 Director 帧尺寸算宽高比、但最终仍返回
// GetWidth()（2048）；SDL3 分支保持同一固定逻辑尺寸。
#if KRKR2_BUILD_APP
#include "cocos2d.h"
#endif
#include "tjsCommHead.h"

#include "TVPScreen.h"
#include "Application.h"

int tTVPScreen::GetWidth() { return 2048; }
int tTVPScreen::GetHeight() {
    int w = GetWidth();
#if KRKR2_BUILD_APP
    const cocos2d::Size &size =
        cocos2d::Director::getInstance()->getOpenGLView()->getFrameSize();
    int h = w * (size.height / size.width); // NOTE: 历史实现最终返回 w，
    // 计算出的 h 未使用；保留原写法以维持可读一致性。
    (void)h;
#endif
    return w;
}

int tTVPScreen::GetDesktopLeft() { return 0; }
int tTVPScreen::GetDesktopTop() { return 0; }
int tTVPScreen::GetDesktopWidth() { return GetWidth(); }
int tTVPScreen::GetDesktopHeight() { return GetHeight(); }
