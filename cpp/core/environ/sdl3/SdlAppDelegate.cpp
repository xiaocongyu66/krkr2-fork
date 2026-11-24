#include "SdlAppDelegate.h"

#include "SdlKeyCodeConv.h"
#include "SdlWindowLayer.h"
#include "KrkrRenderContext.h"

#include "HostLifecycle.h" // TVPOnHostForeground/TVPOnHostBackground
#include "WindowIntf.h" // tTVPOnKeyDown/KeyUp/CloseInputEvent
#include "WindowImpl.h" // tTJSNI_BaseWindow, TVPGetCurrentShiftKeyState
#include "EventIntf.h" // TVPPostInputEvent
#include "tvpinputdefs.h" // TVP_SS_REPEAT, VK_PAD range
#include "InputState.h" // krkr::input::sharedState()
#include "InputModifiers.h" // krkr::input::key::*

#include <algorithm>
#include <cstring>

namespace krkr::sdl3 {
    namespace {
        // SDL reports touch coordinates normalized to [0,1] regardless of the
        // window scale; rescale into SDL window units for the layer adapter.
        void fingerUnits(SDL_Window *window, float x, float y, float &outX,
                         float &outY) {
            int width = 0, height = 0;
            if(window)
                SDL_GetWindowSize(window, &width, &height);
            outX = x * static_cast<float>(width);
            outY = y * static_cast<float>(height);
        }
    } // namespace

    SdlAppDelegate::SdlAppDelegate() = default;

    SdlAppDelegate::SdlAppDelegate(const Config &config) : config(config) {}

    SdlAppDelegate::~SdlAppDelegate() { releaseWindow(); }

    bool SdlAppDelegate::init() {
        if(window)
            return true; // already initialized

        // GAMEPAD is included for SDL_EVENT_GAMEPAD_*; the cocos host discovers
        // controllers through its own Controller subsystem (MainScene).
        if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD))
            return false;
        ownsSDL = true;

        // Prototype main.cpp:237-241: without this SDL emits QUIT after the
        // last window's close request, which would defeat a script's first
        // onCloseQuery veto. Genuine QUIT (system menu, signal) still arrives
        // as SDL_EVENT_QUIT.
        SDL_SetHintWithPriority(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, "0",
                                SDL_HINT_OVERRIDE);

        // GL context attributes, aligned with AppDelegate.cpp:146-148
        // (RGBA8 + depth24 + stencil8).
        if(!render::KrkrRenderContext::applyGLAttributes()) {
            SDL_Quit();
            ownsSDL = false;
            return false;
        }

        const SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
            SDL_WINDOW_HIGH_PIXEL_DENSITY |
            (config.fullscreen ? SDL_WINDOW_FULLSCREEN : 0);
        window = SDL_CreateWindow(config.title.c_str(), config.width,
                                  config.height, flags);
        if(!window) {
            SDL_Quit();
            ownsSDL = false;
            return false;
        }
        windowID = SDL_GetWindowID(window);

        glContext = SDL_GL_CreateContext(window);
        if(!glContext) {
            releaseWindow();
            return false;
        }
        if(!SDL_GL_MakeCurrent(window, glContext)) {
            releaseWindow();
            return false;
        }
        vsync = SDL_GL_SetSwapInterval(1); // match the 60fps Director pacing

        int width = 0, height = 0;
        SDL_GetWindowSize(window, &width, &height);
        SdlWindowLayer::SetSharedViewSize(width, height);

        // Interactive use starts with the window's real focus state
        // (prototype main.cpp:313-314).
        focused = (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
        hostForeground = focused;
        wasFullscreen = config.fullscreen;

        openInitialGamepads();
        return true;
    }

    void SdlAppDelegate::releaseWindow() {
        closeGamepads();
        if(glContext) {
            SDL_GL_DestroyContext(glContext);
            glContext = nullptr;
        }
        if(window) {
            SDL_DestroyWindow(window);
            window = nullptr;
        }
        if(ownsSDL) {
            SDL_Quit();
            ownsSDL = false;
        }
    }

    void SdlAppDelegate::run() {
        if(!window)
            return; // init() not called or failed
        running = true;
        while(running) {
            pollEvents();
            if(!running)
                break;
            if(frameCallback)
                frameCallback(); // engine frame: Application->Run() (B2-2 wires
                                 // it)
            SDL_GL_SwapWindow(window);
            if(!vsync)
                SDL_Delay(16); // prototype main.cpp:448 fallback pacing
        }
    }

    void SdlAppDelegate::stop() { running = false; }

    bool SdlAppDelegate::pumpModalFrame() {
        if(!running || !window)
            return false;
        pollEvents();
        if(!running)
            return false;
        if(frameCallback)
            frameCallback();
        SDL_GL_SwapWindow(window);
        return running;
    }

    void SdlAppDelegate::setFullscreen(bool fullscreen) {
        if(!window)
            return;
        if(SDL_SetWindowFullscreen(window, fullscreen))
            wasFullscreen = fullscreen;
    }

    bool SdlAppDelegate::isFullscreen() const {
        return window &&
            (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
    }

    void SdlAppDelegate::setFrameCallback(FrameCallback callback) {
        frameCallback = std::move(callback);
    }

    void SdlAppDelegate::setCloseQueryCallback(CloseQueryCallback callback) {
        closeQueryCallback = std::move(callback);
    }

    void
    SdlAppDelegate::setDrawableSizeCallback(DrawableSizeCallback callback) {
        drawableSizeCallback = std::move(callback);
    }

    void SdlAppDelegate::setContextLostCallback(ContextLostCallback callback) {
        contextLostCallback = std::move(callback);
    }

    SdlAppDelegate::ContextLostCallback
    SdlAppDelegate::getContextLostCallback() const {
        return contextLostCallback;
    }

    void SdlAppDelegate::setTextInputEnabled(bool enabled) {
        textInputEnabled = enabled;
        if(!window)
            return;
        if(enabled)
            SDL_StartTextInput(window);
        else
            SDL_StopTextInput(window);
    }

    void SdlAppDelegate::setIMEDelegate(SdlIMEDelegate *delegate) {
        imeDelegate = delegate;
    }

    SdlIMEDelegate *SdlAppDelegate::getIMEDelegate() const {
        return imeDelegate;
    }

    float SdlAppDelegate::getDisplayScale() const {
        if(!window)
            return 1.f;
        const float scale = SDL_GetWindowDisplayScale(window);
        return scale > 0.f ? scale : 1.f;
    }

    float SdlAppDelegate::getDPI() const {
        // Device::getDPI() replacement. SDL reports a content scale; the legacy
        // baseline is 96dpi. MainScene.cpp:540 (touch threshold: DPI/10) and
        // MainScene.cpp:832 (UI scale factor) consume this via B2-2.
        return 96.f * getDisplayScale();
    }

    void SdlAppDelegate::getWindowSize(int &width, int &height) const {
        if(window)
            SDL_GetWindowSize(window, &width, &height);
        else {
            width = 0;
            height = 0;
        }
    }

    void SdlAppDelegate::getWindowSizeInPixels(int &width, int &height) const {
        if(window)
            SDL_GetWindowSizeInPixels(window, &width, &height);
        else {
            width = 0;
            height = 0;
        }
    }

    void SdlAppDelegate::pollEvents() {
        SDL_Event event;
        while(SDL_PollEvent(&event)) {
            switch(event.type) {
                case SDL_EVENT_KEY_DOWN:
                case SDL_EVENT_KEY_UP:
                    handleKeyboard(event.key);
                    break;

                case SDL_EVENT_TEXT_INPUT:
                    // MainScene.cpp:1291 equivalent: committed UTF-8 text.
                    if(imeDelegate)
                        imeDelegate->insertText(event.text.text,
                                                std::strlen(event.text.text));
                    break;

                case SDL_EVENT_TEXT_EDITING:
                    // Preedit: the desktop engine never had this; B2-2 wires
                    // the composing-string feedback into the engine.
                    if(imeDelegate)
                        imeDelegate->setPreedit(event.edit.text,
                                                event.edit.start,
                                                event.edit.length);
                    break;

                case SDL_EVENT_MOUSE_MOTION:
                    if(event.motion.windowID != windowID)
                        break;
                    if(auto *layer = SdlWindowLayer::GetCurrentWindowLayer())
                        layer->onPointerMove(event.motion.x, event.motion.y);
                    break;

                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    if(event.button.windowID != windowID || !focused)
                        break;
                    if(auto *layer = SdlWindowLayer::GetCurrentWindowLayer())
                        layer->onPointerDown(event.button.button,
                                             event.button.x, event.button.y);
                    break;

                case SDL_EVENT_MOUSE_BUTTON_UP:
                    if(event.button.windowID != windowID)
                        break;
                    if(auto *layer = SdlWindowLayer::GetCurrentWindowLayer())
                        layer->onPointerUp(event.button.button, event.button.x,
                                           event.button.y);
                    break;

                case SDL_EVENT_MOUSE_WHEEL: {
                    if(event.wheel.windowID != windowID || !focused)
                        break;
                    // SdlPointerInput.cpp:76-84: flip direction for "natural"
                    // scrolling so sign matches the OS convention.
                    const float direction =
                        event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.f
                                                                        : 1.f;
                    if(auto *layer = SdlWindowLayer::GetCurrentWindowLayer())
                        layer->onPointerWheel(direction * event.wheel.x,
                                              direction * event.wheel.y,
                                              event.wheel.mouse_x,
                                              event.wheel.mouse_y);
                    break;
                }

                case SDL_EVENT_FINGER_DOWN: {
                    float x = 0, y = 0;
                    fingerUnits(window, event.tfinger.x, event.tfinger.y, x, y);
                    if(auto *layer = SdlWindowLayer::GetCurrentWindowLayer())
                        layer->onFingerDown(
                            static_cast<std::uint32_t>(event.tfinger.fingerID),
                            x, y);
                    break;
                }

                case SDL_EVENT_FINGER_MOTION: {
                    float x = 0, y = 0;
                    fingerUnits(window, event.tfinger.x, event.tfinger.y, x, y);
                    if(auto *layer = SdlWindowLayer::GetCurrentWindowLayer())
                        layer->onFingerMove(
                            static_cast<std::uint32_t>(event.tfinger.fingerID),
                            x, y);
                    break;
                }

                case SDL_EVENT_FINGER_UP: {
                    float x = 0, y = 0;
                    fingerUnits(window, event.tfinger.x, event.tfinger.y, x, y);
                    if(auto *layer = SdlWindowLayer::GetCurrentWindowLayer())
                        layer->onFingerUp(
                            static_cast<std::uint32_t>(event.tfinger.fingerID),
                            x, y);
                    break;
                }

                case SDL_EVENT_WINDOW_FOCUS_GAINED:
                case SDL_EVENT_WINDOW_FOCUS_LOST:
                case SDL_EVENT_WINDOW_MINIMIZED:
                case SDL_EVENT_WINDOW_RESTORED:
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                case SDL_EVENT_WINDOW_RESIZED:
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                    handleWindowEvent(event.window);
                    break;

                case SDL_EVENT_QUIT:
                    if(allowClose())
                        running = false;
                    // A vetoed QUIT keeps the loop alive;
                    // SDL_HINT_QUIT_ON_LAST_ WINDOW_CLOSE=0 ensures SDL does
                    // not re-inject it.
                    break;

                case SDL_EVENT_GAMEPAD_ADDED:
                    if(!gamepads.count(event.gdevice.which)) {
                        if(SDL_Gamepad *pad =
                               SDL_OpenGamepad(event.gdevice.which))
                            gamepads.emplace(event.gdevice.which, pad);
                    }
                    break;

                case SDL_EVENT_GAMEPAD_REMOVED: {
                    auto keys = gamepadAxisKeys.find(event.gdevice.which);
                    if(keys != gamepadAxisKeys.end()) {
                        for(const auto &axis : keys->second) {
                            const auto vk = axis.second;
                            const bool alsoHeld = std::any_of(
                                keys->second.begin(), keys->second.end(),
                                [vk, &axis](const auto &other) {
                                    return other.first != axis.first &&
                                        other.second == vk;
                                });
                            if(!alsoHeld)
                                postVirtualKey(vk, false, false);
                        }
                        gamepadAxisKeys.erase(keys);
                    }
                    auto it = gamepads.find(event.gdevice.which);
                    if(it != gamepads.end()) {
                        SDL_CloseGamepad(it->second);
                        gamepads.erase(it);
                    }
                    break;
                }

                case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                case SDL_EVENT_GAMEPAD_BUTTON_UP: {
                    const auto vk = TVPConvertSDLGamepadToVK(
                        static_cast<SDL_GamepadButton>(event.gbutton.button));
                    if(!vk)
                        break;
                    // Gamepad button events do not auto-repeat in SDL.
                    postVirtualKey(
                        vk, event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN, false);
                    break;
                }

                case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
                    if(!focused)
                        break;
                    constexpr Sint16 stickThreshold = 8000;
                    const auto axis =
                        static_cast<SDL_GamepadAxis>(event.gaxis.axis);
                    const Sint16 value = event.gaxis.value;
                    std::uint32_t vk = TVPConvertSDLGamepadAxisToVK(axis);
                    bool down = false;
                    if(vk) {
                        // SDL trigger values are unsigned pressure mapped to
                        // [0, 32767]; any positive value is a press.
                        down = value > 0;
                    } else {
                        switch(axis) {
                            case SDL_GAMEPAD_AXIS_LEFTX:
                            case SDL_GAMEPAD_AXIS_RIGHTX:
                                if(value <= -stickThreshold) {
                                    vk = VK_PADLEFT;
                                    down = true;
                                } else if(value >= stickThreshold) {
                                    vk = VK_PADRIGHT;
                                    down = true;
                                }
                                break;
                            case SDL_GAMEPAD_AXIS_LEFTY:
                            case SDL_GAMEPAD_AXIS_RIGHTY:
                                if(value <= -stickThreshold) {
                                    vk = VK_PADUP;
                                    down = true;
                                } else if(value >= stickThreshold) {
                                    vk = VK_PADDOWN;
                                    down = true;
                                }
                                break;
                            default:
                                break;
                        }
                    }
                    const auto id = event.gaxis.which;
                    auto &held = gamepadAxisKeys[id];
                    const auto axisIndex = static_cast<std::size_t>(axis);
                    if(axisIndex >= held.size())
                        break;
                    const std::uint32_t previous = held[axisIndex];
                    if(previous == vk && down)
                        break;
                    if(previous) {
                        const bool stillHeld = std::any_of(
                            held.begin(), held.end(),
                            [previous, axisIndex](const auto &entry) {
                                return entry.first != axisIndex &&
                                    entry.second == previous;
                            });
                        if(!stillHeld)
                            postVirtualKey(previous, false, false);
                    }
                    held[axisIndex] = vk && down ? vk : 0;
                    if(vk && down && previous != vk) {
                        const bool alreadyHeld =
                            std::any_of(held.begin(), held.end(),
                                        [vk, axisIndex](const auto &entry) {
                                            return entry.first != axisIndex &&
                                                entry.second == vk;
                                        });
                        if(!alreadyHeld)
                            postVirtualKey(vk, true, false);
                    }
                    break;
                }

                default:
                    break;
            }
        }
    }

    void SdlAppDelegate::handleWindowEvent(const SDL_WindowEvent &event) {
        if(event.windowID != windowID)
            return;
        switch(event.type) {
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
                if(!focused) {
                    focused = true;
                    onHostForeground();
                }
                break;

            case SDL_EVENT_WINDOW_FOCUS_LOST:
                if(focused) {
                    focused = false;
                    // Release held input BEFORE telling the engine it went to
                    // the background (InputCancellation semantics).
                    cancelInputOnFocusLoss();
                    onHostBackground();
                }
                break;

            case SDL_EVENT_WINDOW_MINIMIZED:
                // Windows also loses focus alongside minimize; the focus
                // handlers above own the input release, this only moves the
                // engine state.
                onHostBackground();
                break;

            case SDL_EVENT_WINDOW_RESTORED:
                if(!hostForeground)
                    onHostForeground();
                break;

            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                // The script's onCloseQuery veto: refusing simply keeps the
                // window, matching the twice-to-close behavior of the old host.
                if(allowClose())
                    running = false;
                break;

            case SDL_EVENT_WINDOW_RESIZED: {
                // Window units (not pixels) match the SDL mouse coordinate
                // space.
                SdlWindowLayer::SetSharedViewSize(event.data1, event.data2);
                break;
            }

            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                // Drawable pixels drive glViewport; a resize does not recreate
                // SDL's GL context.
                if(drawableSizeCallback)
                    drawableSizeCallback();
                break;

            default:
                break;
        }
    }

    void SdlAppDelegate::handleKeyboard(const SDL_KeyboardEvent &event) {
        if(event.windowID != windowID)
            return; // foreign window (prototype main.cpp:342-345)
        const auto vk = TVPConvertScancodeToVKCode(event.scancode);
        if(!vk || vk >= 0x200)
            return; // unmapped, or beyond sharedState()'s 0x200-entry table
        if(!focused)
            return; // drop keys while unfocused (prototype main.cpp:352-357)
        // SDL3 has no TEXTEDITING_DEL event. While text input is active,
        // forward Backspace through the IME delegate's cocos-compatible
        // deleteBackward path instead of also posting it as a regular key.
        if(textInputEnabled && imeDelegate && vk == VK_BACK && event.down) {
            imeDelegate->deleteBackward();
            return;
        }
        postVirtualKey(vk, event.down, event.repeat);
    }

    void SdlAppDelegate::postVirtualKey(std::uint32_t vk, bool down,
                                        bool repeat) {
        tjs_uint32 shift = TVPGetCurrentShiftKeyState();
        if(repeat)
            shift |= TVP_SS_REPEAT;
        auto &state = krkr::input::sharedState();
        // MainScene.cpp:908-915 / 977-982: the current window layer owns the
        // script-visible key pair. InternalKeyDown additionally hosts the
        // UseMouseKey emulation (TVPWindow.cpp:779-837), which the direct
        // input-event posts used to bypass.
        auto *layer = SdlWindowLayer::GetCurrentWindowLayer();
        if(down) {
            state.press(vk);
            if(layer)
                layer->InternalKeyDown(vk, shift);
        } else {
            if(!state.release(vk))
                return; // late/duplicate release (prototype main.cpp:362-365)
            if(layer)
                layer->OnKeyUp(vk, shift);
        }
    }

    void SdlAppDelegate::onHostForeground() {
        if(hostForeground)
            return;
        hostForeground = true;
        TVPOnHostForeground(); // engine: Application->OnActivate()
    }

    void SdlAppDelegate::onHostBackground() {
        if(!hostForeground)
            return;
        hostForeground = false;
        TVPOnHostBackground(); // engine: Application->OnDeactivate()
    }

    void SdlAppDelegate::cancelInputOnFocusLoss() {
        // Full cocos equivalent: TVPMainScene::cancelGameInput ->
        // TVPWindowLayer::CancelInput, which deduplicates the released keys
        // through krkr::input::planInputCancellation before posting the
        // cancellation ups. Without a live layer only the plain key ups keep
        // script-visible state consistent.
        auto &state = krkr::input::sharedState();
        const std::vector<std::uint32_t> released = state.releaseAll();
        if(auto *layer = SdlWindowLayer::GetCurrentWindowLayer()) {
            layer->cancelInput(released); // posts pointer/key ups internally
        } else {
            for(auto key : released) {
                if(auto *win = currentNativeWindow())
                    TVPPostInputEvent(new tTVPOnKeyUpInputEvent(
                        win, key, TVPGetCurrentShiftKeyState()));
            }
        }
    }

    void SdlAppDelegate::notifyContextLost() {
        dispatchRendererRecreated();
        resetGLExtensionInfoInited();
        if(contextLostCallback)
            contextLostCallback();
    }

    void SdlAppDelegate::dispatchRendererRecreated() {
        // The renderer-recreated event requires the cocos Director; SDL
        // invalidates its render resources through the context-lost callback.
    }

    void SdlAppDelegate::resetGLExtensionInfoInited() {
        // A new context requires extension information to be queried again.
        render::resetGLExtensionInfo();
    }

    bool SdlAppDelegate::allowClose() const {
        return !closeQueryCallback || closeQueryCallback();
    }

    tTJSNI_BaseWindow *SdlAppDelegate::currentNativeWindow() const {
        if(auto *layer = SdlWindowLayer::GetCurrentWindowLayer())
            return layer->getNativeWindow();
        return nullptr;
    }

    void SdlAppDelegate::openInitialGamepads() {
        // Gamepads attached before the window existed never emit ADDED events;
        // SDL_GetGamepads snapshots them (cocos: startDiscoveryController).
        int count = 0;
        SDL_JoystickID *ids = SDL_GetGamepads(&count);
        if(!ids)
            return;
        for(int i = 0; i < count; ++i) {
            if(gamepads.count(ids[i]))
                continue;
            if(SDL_Gamepad *pad = SDL_OpenGamepad(ids[i]))
                gamepads.emplace(ids[i], pad);
        }
        SDL_free(ids);
    }

    void SdlAppDelegate::closeGamepads() {
        for(auto &entry : gamepads)
            SDL_CloseGamepad(entry.second);
        gamepads.clear();
    }

} // namespace krkr::sdl3
