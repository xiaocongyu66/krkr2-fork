#include "SdlWindowLayer.h"

#include "SdlKeyCodeConv.h" // TVPConvertSDLMouseButtonToVKCode
#include "EventIntf.h" // TVPPostInputEvent, TVPDeliverWindowUpdateEvents
#include "WindowIntf.h" // input event classes
#include "InputState.h" // krkr::input::sharedState()
#include "InputCancellation.h" // krkr::input::mouseKeyButton, planInputCancellation
#include "InputModifiers.h" // krkr::input::key::*, legacyShiftFlags
#include "PointerCoordinates.h" // krkr::input::pointerPixel
#include "Random.h" // TVPPushEnvironNoise
#include "TickCount.h" // TVPGetRoughTickCount32
#include "tjsVariant.h" // tTJSVariant (OnCloseQuery immediate path)
#include "tjsString.h" // ttstr (onCloseQuery event name)
#include "tvpinputdefs.h" // ss* modifier flags
#include "vkdefine.h" // VK_LBUTTON family

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace krkr::sdl3 {

    SdlWindowLayer *SdlWindowLayer::lastWindowLayer = nullptr;
    SdlWindowLayer *SdlWindowLayer::currentWindowLayer = nullptr;
    tjs_int SdlWindowLayer::sharedViewWidth = 0;
    tjs_int SdlWindowLayer::sharedViewHeight = 0;
    SdlWindowLayer::ImeModeListener SdlWindowLayer::imeModeListener;
    std::function<float()> SdlWindowLayer::DpiProvider;
    std::function<void(iTVPTexture2D *)> SdlWindowLayer::DrawBufferCallback;
    std::function<void(SdlWindowLayer *)> SdlWindowLayer::CurrentWindowCallback;
    std::function<bool()> SdlWindowLayer::ModalPump;

    SdlWindowLayer::SdlWindowLayer(tTJSNI_Window *window) :
        nativeWindow(window) {
        // Chain registration, same as TVPWindowLayer (TVPWindow.cpp:84-93).
        prevWindow = lastWindowLayer;
        lastWindowLayer = this;
        if(prevWindow)
            prevWindow->nextWindow = this;
    }

    SdlWindowLayer::~SdlWindowLayer() {
        // Unlink (TVPWindow.cpp:44-52).
        if(lastWindowLayer == this)
            lastWindowLayer = prevWindow;
        if(nextWindow)
            nextWindow->prevWindow = prevWindow;
        if(prevWindow)
            prevWindow->nextWindow = nextWindow;
        leaveCurrentWindow();
    }

    void SdlWindowLayer::leaveCurrentWindow() {
        if(currentWindowLayer != this)
            return;
        // No script callbacks: invalidation can precede destruction.
        cancelPointerInput();
        auto *another = lastWindowLayer;
        while(another &&
              (another == this || !another->Visible || !another->nativeWindow))
            another = another->prevWindow;
        SetCurrentWindow(another); // notifyImeMode() runs inside (B2-2).
    }

    void SdlWindowLayer::SetCurrentWindow(SdlWindowLayer *window) {
        if(currentWindowLayer && currentWindowLayer != window &&
           currentWindowLayer->nativeWindow)
            currentWindowLayer->nativeWindow->OnReleaseCapture();
        currentWindowLayer = window;
        if(CurrentWindowCallback)
            CurrentWindowCallback(window);
        // TVPWindow.cpp:482: the current window owns the shared IME state.
        notifyImeMode();
    }

    void SdlWindowLayer::SetImeModeListener(ImeModeListener listener) {
        imeModeListener = std::move(listener);
    }

    void SdlWindowLayer::SetDpiProvider(std::function<float()> provider) {
        DpiProvider = std::move(provider);
    }

    void SdlWindowLayer::SetDrawBufferCallback(
        std::function<void(iTVPTexture2D *)> callback) {
        DrawBufferCallback = std::move(callback);
    }

    void SdlWindowLayer::SetCurrentWindowCallback(
        std::function<void(SdlWindowLayer *)> callback) {
        CurrentWindowCallback = std::move(callback);
    }

    void SdlWindowLayer::SetModalPump(std::function<bool()> pump) {
        ModalPump = std::move(pump);
    }

    void SdlWindowLayer::notifyImeMode() {
        // TVPWindow.cpp:423-452 UpdateImeMode: the current window's mode
        // decides the host text-input state; imDisable/imClose detach.
        const auto *cur = currentWindowLayer;
        const bool enabled = cur &&
            cur->LastSetImeMode != tTVPImeMode::imDisable &&
            cur->LastSetImeMode != tTVPImeMode::imClose;
        if(imeModeListener)
            imeModeListener(enabled);
    }

    void SdlWindowLayer::SetSharedViewSize(tjs_int width, tjs_int height) {
        sharedViewWidth = width;
        sharedViewHeight = height;
        for(auto *layer = lastWindowLayer; layer; layer = layer->prevWindow)
            layer->RecalcPaintBox();
    }

    tjs_int SdlWindowLayer::GetSharedViewWidth() { return sharedViewWidth; }

    tjs_int SdlWindowLayer::GetSharedViewHeight() { return sharedViewHeight; }

    //---------------------------------------------------------------------------
    // Paint box: TVPWindow.cpp:562-586 port. Compare the view aspect (r) with
    // the content aspect (R) and fit with centered letterbox offsets; min scale
    // is the fit scale and max scale is twice it; zoom scale starts at the fit.
    //---------------------------------------------------------------------------
    void SdlWindowLayer::getPaintBox(float &fillX, float &fillY) const {
        fillX = fillScaleX;
        fillY = fillScaleY;
    }

    void SdlWindowLayer::RecalcPaintBox() {
        if(!LayerWidth || !LayerHeight)
            return;
        const float viewW = static_cast<float>(sharedViewWidth);
        const float viewH = static_cast<float>(sharedViewHeight);
        const float contW = static_cast<float>(ContentWidth);
        const float contH = static_cast<float>(ContentHeight);
        if(viewW <= 0 || viewH <= 0 || contW <= 0 || contH <= 0)
            return;

        const float r = viewW / viewH; // viewSize aspect (original: size)
        const float R =
            contW / contH; // contentSize aspect (original: contSize)
        float scale, offsetX, offsetY;
        if(R > r) {
            scale = viewW / contW;
            offsetX = 0;
            offsetY = (viewH - contH * scale) / 2;
        } else {
            scale = viewH / contH;
            offsetX = (viewW - contW * scale) / 2;
            offsetY = 0;
        }
        // setMinScale(scale) / setMaxScale(scale * 2) from
        // TVPWindow.cpp:581-582.
        paintBox.minScale = scale;
        paintBox.maxScale = scale * 2;
        // setZoomScale(scale) / setContentOffset(offset) from
        // TVPWindow.cpp:583-584.
        paintBox.scale = scale;
        paintBox.offsetX = offsetX;
        paintBox.offsetY = offsetY;
        // TVPWindow.cpp:585 updateInset() is layout work for the compositor
        // (B3).

        // Zoom (SetZoom) is a separate multiplicative factor in the SDL host:
        // the paint-box fit scale above sizes the letterbox, and user zoom
        // scales the layer area inside it. Input conversion below divides by
        // the product.
    }

    void SdlWindowLayer::layerPosition(float windowX, float windowY, tjs_int &x,
                                       tjs_int &y) const {
        // Prototype equivalent: SDL_RenderCoordinatesFromWindow +
        // krkr::input::pointerPixel (SdlPointerInput.cpp:33-39). SDL window
        // coordinates already have a top-left origin, so no flipPointerY is
        // needed (the cocos host flipped with CocosPointerCoordinates.h:10).
        const float zoom = ActualZoomDenom
            ? static_cast<float>(ActualZoomNumer) /
                static_cast<float>(ActualZoomDenom)
            : 1.f;
        const float scale = (paintBox.scale > 0 ? paintBox.scale : 1.f) * zoom;
        const krkr::input::PointerPosition local{
            (windowX - paintBox.offsetX) / scale,
            (windowY - paintBox.offsetY) / scale,
        };
        const auto pixel = krkr::input::pointerPixel(local);
        x = pixel.x;
        y = pixel.y;
    }

    tjs_uint32 SdlWindowLayer::currentShiftState() const {
        // Legacy engine shift state: keyboard modifiers + held mouse buttons.
        tjs_uint32 shift =
            krkr::input::legacyShiftFlags(krkr::input::sharedState());
        shift |= GetMouseButtonState();
        return shift;
    }

    tjs_uint32 SdlWindowLayer::GetMouseButtonState() const {
        // TVPWindow.cpp:594-602, but sourced from the shared state instead of
        // TVPGetAsyncKeyState (no Win32 dependency in the SDL shell).
        tjs_uint32 s = 0;
        auto &state = krkr::input::sharedState();
        if(state.isDown(krkr::input::key::leftMouse))
            s |= ssLeft;
        if(state.isDown(krkr::input::key::rightMouse))
            s |= ssRight;
        if(state.isDown(krkr::input::key::middleMouse))
            s |= ssMiddle;
        return s;
    }

    tTVPMouseButton SdlWindowLayer::MouseButtonForKey(std::uint32_t key) {
        // TVPWindow.cpp:462-473.
        switch(key) {
            case krkr::input::key::leftMouse:
                return mbLeft;
            case krkr::input::key::rightMouse:
                return mbRight;
            case krkr::input::key::middleMouse:
                return mbMiddle;
            default:
                throw std::invalid_argument("Unsupported touch mouse button");
        }
    }

    std::uint32_t SdlWindowLayer::touchMoveThresholdSq() const {
        // TVPMainScene::create set TVPWindowLayer::_touchMoveThresholdSq from
        // Device::getDPI()/10 (MainScene.cpp:539-542). The shell supplies the
        // SDL display DPI (SdlAppDelegate::getDPI); 96dpi keeps the default
        // geometry until the runner installs the provider.
        const float dpi = DpiProvider ? DpiProvider() : 96.f;
        const float threshold = dpi / 10.f;
        return static_cast<std::uint32_t>(threshold * threshold);
    }

    //---------------------------------------------------------------------------
    // Mouse input. TVPWindow.cpp:285-318 port; SDL supplies the button/index
    // and window coordinates, the rest mirrors the cocos path.
    //---------------------------------------------------------------------------
    void SdlWindowLayer::onPointerDown(std::uint32_t sdlButton, float windowX,
                                       float windowY) {
        if(!nativeWindow || currentWindowLayer != this)
            return; // TVPWindow checks AcceptsGameInput() && current==this.
        const auto vk =
            TVPConvertSDLMouseButtonToVKCode(static_cast<Uint8>(sdlButton));
        if(!vk)
            return;
        tjs_int x = 0, y = 0;
        layerPosition(windowX, windowY, x, y);
        LastMouseX = x;
        LastMouseY = y;
        mouseVelocityTracker.addMovement(TVPGetRoughTickCount32(),
                                         static_cast<float>(x),
                                         static_cast<float>(y));
        krkr::input::sharedState().press(vk);
        TVPPostInputEvent(new tTVPOnMouseDownInputEvent(
            nativeWindow, x, y, MouseButtonForKey(vk), currentShiftState()));
    }

    void SdlWindowLayer::onPointerUp(std::uint32_t sdlButton, float windowX,
                                     float windowY) {
        if(!nativeWindow)
            return;
        const auto vk =
            TVPConvertSDLMouseButtonToVKCode(static_cast<Uint8>(sdlButton));
        if(!vk)
            return;
        // Native ups can arrive after focus-loss cancellation; drop the
        // redundant release exactly like SdlPointerInput.cpp:92-95.
        if(!krkr::input::sharedState().isDown(vk))
            return;
        tjs_int x = 0, y = 0;
        layerPosition(windowX, windowY, x, y);
        LastMouseX = x;
        LastMouseY = y;
        mouseVelocityTracker.addMovement(TVPGetRoughTickCount32(),
                                         static_cast<float>(x),
                                         static_cast<float>(y));
        krkr::input::sharedState().release(vk);
        TVPPostInputEvent(new tTVPOnMouseUpInputEvent(
            nativeWindow, x, y, MouseButtonForKey(vk), currentShiftState()));
    }

    void SdlWindowLayer::onPointerMove(float windowX, float windowY) {
        if(!nativeWindow || currentWindowLayer != this)
            return;
        tjs_int x = 0, y = 0;
        layerPosition(windowX, windowY, x, y);
        LastMouseX = x;
        LastMouseY = y;
        TVPPostInputEvent(new tTVPOnMouseMoveInputEvent(nativeWindow, x, y,
                                                        currentShiftState()),
                          TVP_EPT_DISCARDABLE);
        // TVPWindow.cpp:313-318 feeds the RNG with pointer positions.
        const std::uint32_t pos = (static_cast<std::uint32_t>(y) << 16) +
            static_cast<std::uint32_t>(x);
        TVPPushEnvironNoise(&pos, sizeof(pos));
    }

    void SdlWindowLayer::onPointerWheel(float deltaX, float deltaY,
                                        float windowX, float windowY) {
        if(!nativeWindow || currentWindowLayer != this)
            return;
        tjs_int x = 0, y = 0;
        layerPosition(windowX, windowY, x, y);
        LastMouseX = x;
        LastMouseY = y;
        // The engine consumes WHEEL_DELTA (120) units; the cocos host inverts
        // the OS sign here (TVPWindow.cpp:139). Horizontal deltas are ignored
        // until the engine grows a horizontal wheel event.
        (void)deltaX;
        const tjs_int delta = deltaY > 0 ? -120 : 120;
        TVPPostInputEvent(new tTVPOnMouseWheelInputEvent(
            nativeWindow, currentShiftState(), delta, x, y));
    }

    //---------------------------------------------------------------------------
    // Touch -> mouse synthesis, TVPWindow.cpp:155-285 port:
    //  - first contact selects the button by contact count
    //  (TouchButton.h:16-21:
    //    1/2/3 fingers -> left/right/middle);
    //  - movement that exceeds a time or distance threshold promotes the
    //  gesture
    //    to a real press: mouse-move + mouse-down;
    //  - release before any movement is a tap: mouse-move + mouse-down + click
    //  +
    //    mouse-up at the release point;
    //  - the gesture owns its button until the last finger lifts.
    //---------------------------------------------------------------------------
    void SdlWindowLayer::onFingerDown(std::uint32_t fingerId, float windowX,
                                      float windowY) {
        (void)windowX;
        (void)windowY;
        if(std::find(touchFingers.begin(), touchFingers.end(), fingerId) !=
           touchFingers.end())
            return;
        touchFingers.push_back(fingerId);
        touchButton.selectForContacts(touchFingers.size());
        touchVelocityTracker.start(static_cast<tjs_int>(fingerId));
        if(touchFingers.size() == 1) {
            touchStartX = windowX;
            touchStartY = windowY;
            touchMoved = false;
            touchBeginTick = TVPGetRoughTickCount32();
        }
        // Two-finger midpoint bookkeeping (TVPWindow.cpp:172-175) drives
        // display pan/scale gestures; those are compositor-side (B3).
    }

    void SdlWindowLayer::onFingerMove(std::uint32_t fingerId, float windowX,
                                      float windowY) {
        if(!nativeWindow)
            return;
        if(std::find(touchFingers.begin(), touchFingers.end(), fingerId) ==
           touchFingers.end())
            return;
        if(touchFingers.size() != 1)
            return; // multi-finger gestures are not synthesized (B3).

        if(!touchMoved) {
            const float dx = windowX - touchStartX;
            const float dy = windowY - touchStartY;
            if(TVPGetRoughTickCount32() - touchBeginTick > 150 ||
               static_cast<std::uint32_t>(dx * dx + dy * dy) >
                   touchMoveThresholdSq()) {
                // First accepted movement: deliver the move at the press point,
                // then the synthesized button down (TVPWindow.cpp:184-204).
                tjs_int x = 0, y = 0;
                layerPosition(touchStartX, touchStartY, x, y);
                LastMouseX = x;
                LastMouseY = y;
                TVPPostInputEvent(new tTVPOnMouseMoveInputEvent(
                    nativeWindow, x, y, currentShiftState()));
                if(auto button = touchButton.press()) {
                    krkr::input::sharedState().press(*button);
                    TVPPostInputEvent(new tTVPOnMouseDownInputEvent(
                        nativeWindow, x, y, MouseButtonForKey(*button),
                        currentShiftState()));
                }
                touchMoved = true;
            }
        } else {
            tjs_int x = 0, y = 0;
            layerPosition(windowX, windowY, x, y);
            LastMouseX = x;
            LastMouseY = y;
            touchVelocityTracker.update(
                static_cast<tjs_int>(fingerId), TVPGetRoughTickCount32(),
                static_cast<float>(x), static_cast<float>(y));
            TVPPostInputEvent(new tTVPOnMouseMoveInputEvent(
                                  nativeWindow, x, y, currentShiftState()),
                              TVP_EPT_DISCARDABLE);
            const std::uint32_t pos = (static_cast<std::uint32_t>(y) << 16) +
                static_cast<std::uint32_t>(x);
            TVPPushEnvironNoise(&pos, sizeof(pos));
        }
    }

    void SdlWindowLayer::onFingerUp(std::uint32_t fingerId, float windowX,
                                    float windowY) {
        const auto it =
            std::find(touchFingers.begin(), touchFingers.end(), fingerId);
        if(it == touchFingers.end())
            return; // ignore a late release from a discarded gesture.

        const auto button = touchButton.value();
        if(touchFingers.size() == 1 && nativeWindow) {
            tjs_int x = 0, y = 0;
            layerPosition(windowX, windowY, x, y);
            LastMouseX = x;
            LastMouseY = y;
            if(!touchMoved) {
                // Tap: move + down at the press point, click, up at release
                // (TVPWindow.cpp:234-253).
                tjs_int sx = 0, sy = 0;
                layerPosition(touchStartX, touchStartY, sx, sy);
                TVPPostInputEvent(new tTVPOnMouseMoveInputEvent(
                    nativeWindow, sx, sy, currentShiftState()));
                TVPPostInputEvent(new tTVPOnMouseDownInputEvent(
                    nativeWindow, sx, sy, MouseButtonForKey(button),
                    currentShiftState()));
                TVPPostInputEvent(
                    new tTVPOnClickInputEvent(nativeWindow, x, y));
            }
            krkr::input::sharedState().latchReleasedPress(button);
            TVPPostInputEvent(new tTVPOnMouseUpInputEvent(
                nativeWindow, x, y, MouseButtonForKey(button),
                currentShiftState()));
        }
        touchFingers.erase(it);

        if(touchFingers.empty()) {
            touchMoved = false;
            touchButton.finish(); // resets the selected button
            krkr::input::sharedState().release(button);
        }
    }

    void
    SdlWindowLayer::cancelInput(const std::vector<std::uint32_t> &released) {
        // TVPWindow.cpp:346-389 port. Deduplicate the released keys through
        // planInputCancellation so one held key generates exactly one up,
        // emulated mouse-key presses collapse into pointer ups, and
        // untranslated mouse-key keys never receive a bogus up.
        auto keys = released;
        if(touchButton.pressed())
            keys.push_back(touchButton.value());
        const auto plan = krkr::input::planInputCancellation(
            keys,
            { UseMouseKey, MouseLeftButtonEmulatedPushed,
              MouseRightButtonEmulatedPushed });

        for(auto fingerId : touchFingers)
            touchVelocityTracker.end(static_cast<tjs_int>(fingerId));
        touchFingers.clear();
        touchButton.reset();
        touchMoved = false;
        mouseVelocityTracker.clear();
        MouseLeftButtonEmulatedPushed = false;
        MouseRightButtonEmulatedPushed = false;
        MouseKeyXAccel = 0;
        MouseKeyYAccel = 0;
        LastMouseMoved = false;

        if(!nativeWindow)
            return;

        // Do not route through OnKeyUp: mouse-key release can generate a click
        // there. Direct posts also avoid consuming unread press markers via
        // GetMouseButtonState(). Pointer-only cancellation keeps keyboard
        // modifiers.
        const auto shift =
            krkr::input::legacyShiftFlags(krkr::input::sharedState());
        for(auto button : plan.pointerUps) {
            // The emulated button is released state-side here; the cocos host
            // never pressed it into the shared state (TVPWindow.cpp:675-688),
            // but this host derives shift state from krkr::input::sharedState.
            krkr::input::sharedState().release(button);
            TVPPostInputEvent(new tTVPOnMouseUpInputEvent(
                nativeWindow, LastMouseX, LastMouseY, MouseButtonForKey(button),
                shift, /*cancellation=*/true));
        }
        for(auto key : plan.keyUps)
            TVPPostInputEvent(
                new tTVPOnKeyUpInputEvent(nativeWindow, key, shift));
    }

    void SdlWindowLayer::cancelPointerInput() {
        // Gesture-only reset, no events (mirrors
        // TVPMainScene::CancelPointerInput with notifyWindow=false;
        // SdlAppDelegate posts the ups on focus loss).
        for(auto fingerId : touchFingers)
            touchVelocityTracker.end(static_cast<tjs_int>(fingerId));
        touchFingers.clear();
        touchButton.reset();
        touchMoved = false;
        mouseVelocityTracker.clear();
    }

    //---------------------------------------------------------------------------
    // UseMouseKey emulation, TVPWindow.cpp:698-837 port. The emulated cursor
    // moves in the same primary-layer space as LastMouseX/Y; press emulation
    // additionally tracks the button through the shared key state so shift
    // flags (this host derives them from krkr::input::sharedState) and
    // cancellation dedupe cover the emulated presses.
    //---------------------------------------------------------------------------
    void SdlWindowLayer::emulatedMouseDown(std::uint32_t button) {
        if(!nativeWindow)
            return;
        krkr::input::sharedState().press(button);
        TVPPostInputEvent(new tTVPOnMouseDownInputEvent(
            nativeWindow, LastMouseX, LastMouseY, MouseButtonForKey(button),
            currentShiftState()));
    }

    void SdlWindowLayer::emulatedMouseUp(std::uint32_t button) {
        krkr::input::sharedState().release(button);
        if(!nativeWindow)
            return;
        TVPPostInputEvent(new tTVPOnMouseUpInputEvent(
            nativeWindow, LastMouseX, LastMouseY, MouseButtonForKey(button),
            currentShiftState()));
    }

    void SdlWindowLayer::GenerateMouseEvent(bool fl, bool fr, bool fu,
                                            bool fd) {
        // TVPWindow.cpp:698-777: idle ticks are throttled to 45ms, movement
        // uses an accelerating ramp driven by the shared key state.
        if(!fl && !fr && !fu && !fd) {
            if(TVPGetRoughTickCount32() - 45 < LastMouseKeyTick)
                return;
        }

        const bool shift =
            0 != (krkr::input::sharedState().query(VK_SHIFT, true));
        const bool left = fl ||
            krkr::input::sharedState().query(VK_LEFT, true) ||
            krkr::input::sharedState().query(VK_PADLEFT, true);
        const bool right = fr ||
            krkr::input::sharedState().query(VK_RIGHT, true) ||
            krkr::input::sharedState().query(VK_PADRIGHT, true);
        const bool up = fu || krkr::input::sharedState().query(VK_UP, true) ||
            krkr::input::sharedState().query(VK_PADUP, true);
        const bool down = fd ||
            krkr::input::sharedState().query(VK_DOWN, true) ||
            krkr::input::sharedState().query(VK_PADDOWN, true);

        const bool flags = left || right || up || down;

        if(!right && !left && !up && !down) {
            LastMouseMoved = false;
            MouseKeyXAccel = 0;
            MouseKeyYAccel = 0;
        }

        if(!shift) {
            if(!right && left && MouseKeyXAccel > 0)
                MouseKeyXAccel = 0;
            if(!left && right && MouseKeyXAccel < 0)
                MouseKeyXAccel = 0;
            if(!down && up && MouseKeyYAccel > 0)
                MouseKeyYAccel = 0;
            if(!up && down && MouseKeyYAccel < 0)
                MouseKeyYAccel = 0;
        } else {
            if(left)
                MouseKeyXAccel = -TVP_MOUSE_SHIFT_ACCEL;
            if(right)
                MouseKeyXAccel = TVP_MOUSE_SHIFT_ACCEL;
            if(up)
                MouseKeyYAccel = -TVP_MOUSE_SHIFT_ACCEL;
            if(down)
                MouseKeyYAccel = TVP_MOUSE_SHIFT_ACCEL;
        }

        if(right || left || up || down) {
            if(left)
                if(MouseKeyXAccel > -TVP_MOUSE_MAX_ACCEL)
                    MouseKeyXAccel = MouseKeyXAccel ? MouseKeyXAccel - 2 : -2;
            if(right)
                if(MouseKeyXAccel < TVP_MOUSE_MAX_ACCEL)
                    MouseKeyXAccel = MouseKeyXAccel ? MouseKeyXAccel + 2 : +2;
            if(!left && !right) {
                if(MouseKeyXAccel > 0)
                    MouseKeyXAccel--;
                else if(MouseKeyXAccel < 0)
                    MouseKeyXAccel++;
            }

            if(up)
                if(MouseKeyYAccel > -TVP_MOUSE_MAX_ACCEL)
                    MouseKeyYAccel = MouseKeyYAccel ? MouseKeyYAccel - 2 : -2;
            if(down)
                if(MouseKeyYAccel < TVP_MOUSE_MAX_ACCEL)
                    MouseKeyYAccel = MouseKeyYAccel ? MouseKeyYAccel + 2 : +2;
            if(!up && !down) {
                if(MouseKeyYAccel > 0)
                    MouseKeyYAccel--;
                else if(MouseKeyYAccel < 0)
                    MouseKeyYAccel++;
            }
        }

        if(flags) {
            LastMouseX += MouseKeyXAccel >> 1;
            LastMouseY += MouseKeyYAccel >> 1;
            LastMouseMoved = true;
        }
        LastMouseKeyTick = TVPGetRoughTickCount32();
    }

    //---------------------------------------------------------------------------
    // iWindowLayer implementation
    //---------------------------------------------------------------------------
    void SdlWindowLayer::SetPaintBoxSize(tjs_int w, tjs_int h) {
        LayerWidth = w;
        LayerHeight = h;
        RecalcPaintBox(); // TVPWindow.cpp:142-146
    }

    bool SdlWindowLayer::GetFormEnabled() { return Visible; }

    void SdlWindowLayer::SetDefaultMouseCursor() { SDL_ShowCursor(true); }

    void SdlWindowLayer::GetCursorPos(tjs_int &x, tjs_int &y) {
        x = LastMouseX;
        y = LastMouseY;
    }

    void SdlWindowLayer::SetCursorPos(tjs_int x, tjs_int y) {
        // Cache-only like TVPWindow.cpp:397-411 without the cursor node (B3
        // owns cursor rendering).
        LastMouseX = x;
        LastMouseY = y;
    }

    void SdlWindowLayer::SetHintText(const ttstr &text) {
        hintText = text;
        // SDL shell has no per-layer tooltip UI yet.
    }

    void SdlWindowLayer::SetAttentionPoint(tjs_int left, tjs_int top,
                                           const struct tTVPFont *font) {
        // TVPWindow.h:161-166 keeps the attention row for the on-screen
        // keyboard.
        textInputPosY = top;
    }

    void SdlWindowLayer::ZoomRectangle(tjs_int &left, tjs_int &top,
                                       tjs_int &right, tjs_int &bottom) {
        // TVPWindow.cpp:454-460.
        left = static_cast<tjs_int64>(left) * ActualZoomNumer / ActualZoomDenom;
        top = static_cast<tjs_int64>(top) * ActualZoomNumer / ActualZoomDenom;
        right =
            static_cast<tjs_int64>(right) * ActualZoomNumer / ActualZoomDenom;
        bottom =
            static_cast<tjs_int64>(bottom) * ActualZoomNumer / ActualZoomDenom;
    }

    void SdlWindowLayer::BringToFront() {
        if(!nativeWindow)
            return;
        if(currentWindowLayer != this)
            SetCurrentWindow(this); // TVPWindow.cpp:485-494 without positions.
    }

    void SdlWindowLayer::ShowWindowAsModal() {
        inMode = true;
        SetVisible(true);
        BringToFront();
        modalResult = 0;
        while(this == currentWindowLayer && !modalResult && nativeWindow) {
            if(!ModalPump || !ModalPump()) {
                modalResult = 2; // mrCancel when the engine or SDL exits.
                break;
            }
        }
        inMode = false;
    }

    bool SdlWindowLayer::GetVisible() { return Visible; }

    void SdlWindowLayer::SetVisible(bool bVisible) {
        // TVPWindow.cpp:526-538: revoke the current-window role before hiding
        // so queued ups are not delivered to an inactive layer.
        const bool wasCurrent = currentWindowLayer == this;
        if(!bVisible && wasCurrent) {
            auto *next = prevWindow;
            while(next && (!next->Visible || !next->nativeWindow))
                next = next->prevWindow;
            if(!next) {
                next = nextWindow;
                while(next && (!next->Visible || !next->nativeWindow))
                    next = next->nextWindow;
            }
            SetCurrentWindow(next);
        }
        Visible = bVisible;
        if(bVisible)
            BringToFront();
        else if(!bVisible && wasCurrent && DrawBufferCallback)
            DrawBufferCallback(nullptr);
    }

    const char *SdlWindowLayer::GetCaption() { return caption.c_str(); }

    void SdlWindowLayer::SetCaption(const std::string &caption_) {
        caption = caption_;
    }

    void SdlWindowLayer::SetWidth(tjs_int w) {
        ContentWidth = w;
        RecalcPaintBox();
    }

    void SdlWindowLayer::SetHeight(tjs_int h) {
        ContentHeight = h;
        RecalcPaintBox();
    }

    void SdlWindowLayer::SetSize(tjs_int w, tjs_int h) {
        ContentWidth = w;
        ContentHeight = h;
        RecalcPaintBox();
    }

    void SdlWindowLayer::GetSize(tjs_int &w, tjs_int &h) {
        w = ContentWidth;
        h = ContentHeight;
    }

    tjs_int SdlWindowLayer::GetWidth() const { return ContentWidth; }

    tjs_int SdlWindowLayer::GetHeight() const { return ContentHeight; }

    void SdlWindowLayer::GetWinSize(tjs_int &w, tjs_int &h) {
        // getViewSize(): the shared SDL window client area.
        w = sharedViewWidth;
        h = sharedViewHeight;
    }

    void SdlWindowLayer::SetZoom(tjs_int numer, tjs_int denom) {
        // TVPWindow.h:214-229: reduce, store both setting and actual pair.
        tjs_int a = numer;
        tjs_int b = denom;
        while(b) {
            tjs_int t = b;
            b = a % b;
            a = t;
        }
        numer = numer / a;
        denom = denom / a;
        ZoomNumer = numer;
        ZoomDenom = denom;
        ActualZoomNumer = numer;
        ActualZoomDenom = denom;
        RecalcPaintBox();
    }

    void SdlWindowLayer::UpdateDrawBuffer(iTVPTexture2D *tex) {
        if(DrawBufferCallback && currentWindowLayer == this)
            DrawBufferCallback(tex);
    }

    void SdlWindowLayer::InvalidateClose() {
        // TVPWindow.cpp:58-61: detach the native instance before destruction.
        nativeWindow = nullptr;
        leaveCurrentWindow();
        delete this;
    }

    bool SdlWindowLayer::GetWindowActive() {
        return currentWindowLayer == this;
    }

    void SdlWindowLayer::Close() {
        // TVPWindow.cpp:949-984 minus the scenegraph removal.
        if(Closing)
            return; // already waiting on the script's close query

        ProgramClosing = true;
        try {
            if(inMode) {
                modalResult = 2; // mrCancel (Application.h:21-24)
            } else if(OnCloseQuery()) {
                // caFree path: the cocos host removed the scene node here; the
                // SDL shell performs teardown through the native-instance
                // invalidation that OnCloseQueryCalled drives below, and the
                // engine releases this layer (B3 life-cycle).
                Closing = true;
            }
        } catch(...) {
            ProgramClosing = false;
            throw;
        }
        ProgramClosing = false;
    }

    void SdlWindowLayer::OnCloseQueryCalled(bool b) {
        // TVPWindow.cpp:987-1013.
        if(!ProgramClosing) {
            // close action by the user
            if(b) {
                if(inMode)
                    modalResult = 1; // modal result
                else
                    SetVisible(false); // just hide
                Closing = false;
                if(nativeWindow) {
                    if(nativeWindow->IsMainWindow()) {
                        // this is the main window
                        if(iTJSDispatch2 *obj =
                               nativeWindow->GetOwnerNoAddRef()) {
                            obj->Invalidate(0, nullptr, nullptr, obj);
                        }
                    }
                }
                // else: the cocos host deleted the orphan layer; SDL layer
                // lifetime is native/engine-owned (B3).
            } else {
                Closing = false; // vetoed
            }
        } else {
            // close action by the program
            CanCloseWork = b;
        }
    }

    bool SdlWindowLayer::OnCloseQuery() {
        // TVPWindow.cpp:905-947, simplified to the non-scenegraph paths.
        if(TVPGetBreathing())
            return false;

        if(nativeWindow &&
           (modalResult == 0 || modalResult == 2 /* mrCancel */)) {
            if(iTJSDispatch2 *obj = nativeWindow->GetOwnerNoAddRef()) {
                if(!ProgramClosing) {
                    // close action does not happen immediately: let the
                    // script's onCloseQuery decide through the queued close
                    // event.
                    TVPPostInputEvent(new tTVPOnCloseInputEvent(nativeWindow));
                    Closing = true; // waiting closing...
                    return false;
                }
                CanCloseWork = true;
                tTJSVariant arg[1] = { true };
                ttstr eventname(TJS_W("onCloseQuery"));
                TVPPostEvent(obj, obj, eventname, 0, TVP_EPT_IMMEDIATE, 1, arg);
                // TVPDrawSceneOnce(0) (immediate delivery on the cocos host)
                // has no SDL shell equivalent yet; TVPInvokeEvents runs on the
                // next pump.
                return CanCloseWork;
            }
        }
        return true;
    }

    void SdlWindowLayer::InternalKeyDown(tjs_uint16 key, tjs_uint32 shift) {
        // TVPWindow.cpp:779-837 port.
        tjs_uint32 tick = TVPGetRoughTickCount32();
        TVPPushEnvironNoise(&tick, sizeof(tick));
        TVPPushEnvironNoise(&key, sizeof(key));
        TVPPushEnvironNoise(&shift, sizeof(shift));

        if(UseMouseKey) {
            // Return/Space/Pad1 and Escape/Pad2 emulate pointer buttons
            // (krkr::input::mouseKeyButton), but only while the emulated
            // cursor stays inside the layer (TVPWindow.cpp:785-801).
            if(const auto button = krkr::input::mouseKeyButton(key)) {
                // Repeats are intentional (TVPWindow.cpp:790-798 sets the flag
                // and posts unconditionally on every keydown), matching the
                // Win32 host's repeated emulated presses on a held key.
                if(LastMouseX >= 0 && LastMouseY >= 0 &&
                   LastMouseX < LayerWidth && LastMouseY < LayerHeight) {
                    if(button == krkr::input::key::leftMouse) {
                        MouseLeftButtonEmulatedPushed = true;
                        emulatedMouseDown(button);
                    }
                    if(button == krkr::input::key::rightMouse) {
                        MouseRightButtonEmulatedPushed = true;
                        emulatedMouseDown(button);
                    }
                }
                return;
            }

            // Arrows and the D-pad move the emulated cursor; the initial kick
            // also arms the 100ms repeat delay (TVPWindow.cpp:803-833).
            switch(key) {
                case VK_LEFT:
                case VK_PADLEFT:
                    if(MouseKeyXAccel == 0 && MouseKeyYAccel == 0) {
                        GenerateMouseEvent(true, false, false, false);
                        LastMouseKeyTick = TVPGetRoughTickCount32() + 100;
                    }
                    return;
                case VK_RIGHT:
                case VK_PADRIGHT:
                    if(MouseKeyXAccel == 0 && MouseKeyYAccel == 0) {
                        GenerateMouseEvent(false, true, false, false);
                        LastMouseKeyTick = TVPGetRoughTickCount32() + 100;
                    }
                    return;
                case VK_UP:
                case VK_PADUP:
                    if(MouseKeyXAccel == 0 && MouseKeyYAccel == 0) {
                        GenerateMouseEvent(false, false, true, false);
                        LastMouseKeyTick = TVPGetRoughTickCount32() + 100;
                    }
                    return;
                case VK_DOWN:
                case VK_PADDOWN:
                    if(MouseKeyXAccel == 0 && MouseKeyYAccel == 0) {
                        GenerateMouseEvent(false, false, false, true);
                        LastMouseKeyTick = TVPGetRoughTickCount32() + 100;
                    }
                    return;
                default:
                    break;
            }
        }

        if(nativeWindow)
            TVPPostInputEvent(
                new tTVPOnKeyDownInputEvent(nativeWindow, key, shift));
    }

    void SdlWindowLayer::OnKeyUp(tjs_uint16 vk, int shift) {
        // TVPWindow.h:253-257 + TVPWindow.cpp:839-875 (mouse-key release).
        tjs_uint32 s = shift;
        s |= GetMouseButtonState();
        tjs_uint32 tick = TVPGetRoughTickCount32();
        TVPPushEnvironNoise(&tick, sizeof(tick));
        TVPPushEnvironNoise(&vk, sizeof(vk));
        TVPPushEnvironNoise(&s, sizeof(s));
        if(!nativeWindow)
            return;

        if(UseMouseKey) {
            if(const auto button = krkr::input::mouseKeyButton(vk)) {
                // Pressed in another window, or cancelled -- no release here
                // (TVPWindow.cpp:846-852).
                if((button == krkr::input::key::leftMouse &&
                    !MouseLeftButtonEmulatedPushed) ||
                   (button == krkr::input::key::rightMouse &&
                    !MouseRightButtonEmulatedPushed))
                    return;
                if(LastMouseX >= 0 && LastMouseY >= 0 &&
                   LastMouseX < LayerWidth && LastMouseY < LayerHeight) {
                    if(button == krkr::input::key::leftMouse) {
                        // Emulated left release fires a click pair
                        // (TVPWindow.cpp:858-866); the click lands at the
                        // cursor because no motion separates press and
                        // release.
                        MouseLeftButtonEmulatedPushed = false;
                        TVPPostInputEvent(new tTVPOnClickInputEvent(
                            nativeWindow, LastMouseX, LastMouseY));
                        emulatedMouseUp(button);
                    } else {
                        MouseRightButtonEmulatedPushed = false;
                        emulatedMouseUp(button);
                    }
                }
                return;
            }
        }

        TVPPostInputEvent(new tTVPOnKeyUpInputEvent(nativeWindow, vk, s));
    }

    void SdlWindowLayer::OnKeyPress(tjs_uint16 vk, int repeat,
                                    bool prevkeystate, bool convertkey) {
        // TVPWindow.cpp:1048-1055.
        (void)repeat;
        (void)prevkeystate;
        (void)convertkey;
        if(nativeWindow && vk) {
            if(UseMouseKey && (vk == 0x1b || vk == 13 || vk == 32))
                return; // mouse-key mode swallows Escape/Return/Space
            TVPPostInputEvent(new tTVPOnKeyPressInputEvent(nativeWindow, vk));
        }
    }

    tTVPImeMode SdlWindowLayer::GetDefaultImeMode() const {
        return DefaultImeMode;
    }

    void SdlWindowLayer::SetDefaultImeMode(tTVPImeMode mode, bool reset) {
        DefaultImeMode = mode;
        if(reset)
            ResetImeMode();
    }

    void SdlWindowLayer::SetImeMode(tTVPImeMode mode) {
        // TVPWindow.cpp:413-421: imDontCare keeps the last concrete mode, and
        // only the current window reaches the host IME state.
        if(mode == tTVPImeMode::imDontCare)
            return;
        LastSetImeMode = mode;
        if(currentWindowLayer == this)
            notifyImeMode();
    }

    void SdlWindowLayer::ResetImeMode() { SetImeMode(DefaultImeMode); }

    void SdlWindowLayer::UpdateWindow(tTVPUpdateType type) {
        // TVPWindow.cpp:280-290.
        (void)type;
        if(nativeWindow) {
            tTVPRect r;
            r.left = 0;
            r.top = 0;
            r.right = LayerWidth;
            r.bottom = LayerHeight;
            nativeWindow->NotifyWindowExposureToLayer(r);
            TVPDeliverWindowUpdateEvents();
        }
    }

    void SdlWindowLayer::SetVisibleFromScript(bool b) { SetVisible(b); }

    void SdlWindowLayer::SetUseMouseKey(bool b) {
        // TVPWindow.cpp:1018-1036.
        UseMouseKey = b;
        if(b) {
            MouseLeftButtonEmulatedPushed = false;
            MouseRightButtonEmulatedPushed = false;
            LastMouseKeyTick = TVPGetRoughTickCount32();
        } else {
            // Release emulated buttons so a stuck press cannot survive the
            // mode switch.
            if(MouseLeftButtonEmulatedPushed) {
                MouseLeftButtonEmulatedPushed = false;
                emulatedMouseUp(krkr::input::key::leftMouse);
            }
            if(MouseRightButtonEmulatedPushed) {
                MouseRightButtonEmulatedPushed = false;
                emulatedMouseUp(krkr::input::key::rightMouse);
            }
        }
    }

    bool SdlWindowLayer::GetUseMouseKey() const { return UseMouseKey; }

    void SdlWindowLayer::ResetMouseVelocity() { mouseVelocityTracker.clear(); }

    void SdlWindowLayer::ResetTouchVelocity(tjs_int id) {
        touchVelocityTracker.end(id);
    }

    bool SdlWindowLayer::GetMouseVelocity(float &x, float &y,
                                          float &speed) const {
        // TVPWindow.h:306-312.
        if(mouseVelocityTracker.getVelocity(x, y)) {
            speed = hypotf(x, y);
            return true;
        }
        return false;
    }

    void SdlWindowLayer::TickBeat() {
        // TVPWindow.cpp:391-395: pump the emulated cursor while arrows are
        // held. The engine drives this from SystemWatchTimerTimer
        // (Application->Run() inside the SDL frame callback).
        if(UseMouseKey && currentWindowLayer == this)
            GenerateMouseEvent(false, false, false, false);
    }

    void SdlWindowLayer::toggleFillScale() {
        // TVPWindow.cpp:631-659 port. The compositor (B3) consumes
        // fillScaleX/Y.
        if(!LayerWidth || !LayerHeight || !ContentWidth || !ContentHeight)
            return;
        const float r =
            static_cast<float>(LayerWidth) / static_cast<float>(LayerHeight);
        const float R = static_cast<float>(sharedViewWidth) /
            static_cast<float>(sharedViewHeight);
        if(std::fabs(R - r) < 0.01f)
            return; // do not fill border if screen ratio is almost the same

        const float scaleX = paintBox.scale * fillScaleX;
        const float scaleY = paintBox.scale * fillScaleY;
        if(scaleX == scaleY) {
            if(R > r) { // border at left/right
                fillScaleX = R / r;
                fillScaleY = 1.f;
            } else { // border at top/bottom
                fillScaleY = r / R;
                fillScaleX = 1.f;
            }
        } else {
            fillScaleX = 1.f;
            fillScaleY = 1.f;
        }
        // setZoomScale(std::min(scaleX, scaleY)) + setContentOffset(ZERO) +
        // relocateContainer(false) (TVPWindow.cpp:654-658) are draw/layout-side
        // and land with the compositor (B3).
    }

} // namespace krkr::sdl3
