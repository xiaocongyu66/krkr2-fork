#pragma once

//---------------------------------------------------------------------------
// SDL3 shell application: owns the window, the GL context and the event pump,
// replacing environ/cocos2d/AppDelegate + platforms/{linux,windows,apple}/main
// for the SDL3 desktop build.
//
// Window  : SDL_CreateWindow(SDL_WINDOW_RESIZABLE |
// SDL_WINDOW_HIGH_PIXEL_DENSITY)
//           + SDL_GL context, RGBA8/depth24/stencil8 (AppDelegate.cpp:146-148)
// Loop    : SDL_PollEvent dispatch -> engine hooks -> frame callback ->
//           SDL_GL_SwapWindow (prototype tools/sdl3-prototype/main.cpp:331-448)
// No cocos headers are referenced here.
//---------------------------------------------------------------------------

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>

// Forward declaration of the engine window-native base (WindowIntf.h), so
// this header does not need to include the engine window interface chain.
class tTJSNI_BaseWindow;

namespace krkr::sdl3 {

    class SdlWindowLayer;

    // IME bridge for SDL_EVENT_TEXT_INPUT / SDL_EVENT_TEXT_EDITING.
    // The engine-side adapter -- insertText -> decodeTextInput ->
    // per-UTF-16-unit Window.OnKeyPress, deleteBackward -> VK_BACK down/up --
    // mirrors MainScene's IMEDelegate (MainScene.cpp:1278, :1291) and is
    // implemented by SdlEngineRunner::EngineIME; the shell only defines and
    // calls it.
    class SdlIMEDelegate {
    public:
        virtual ~SdlIMEDelegate() = default;
        // Committed text (UTF-8).
        virtual void insertText(const char *text, std::size_t len) = 0;
        // Backspace while text input is active (non-Win32 hosts post VK_BACK
        // down/up).
        virtual void deleteBackward() = 0;
        // Complete active preedit string and selection span from SDL.
        virtual void setPreedit(const char *text, int start, int length) = 0;
    };

    class SdlAppDelegate {
    public:
        struct Config {
            // Matches designResolutionSize (AppDelegate.cpp:24) and the window
            // GLViewImpl::createWithRect("krkr2", 960x640)
            // (AppDelegate.cpp:58-61).
            std::string title = "krkr2";
            int width = 960;
            int height = 640;
            bool fullscreen = false;
        };

        SdlAppDelegate();
        explicit SdlAppDelegate(const Config &config);
        ~SdlAppDelegate();

        SdlAppDelegate(const SdlAppDelegate &) = delete;
        SdlAppDelegate &operator=(const SdlAppDelegate &) = delete;

        // SDL_Init + window + GL context. Returns false on failure.
        bool init();

        // Pumps events until stopped. Each iteration: dispatch, engine frame
        // callback (the Application->Run() hook, MainScene.cpp:747-748), swap.
        void run();
        void stop();
        bool pumpModalFrame();

        void setFullscreen(bool fullscreen);
        [[nodiscard]] bool isFullscreen() const;

        // Engine Window::SetSize: resize the desktop window to the requested
        // client size (ignored in fullscreen, like the desktop hosts).
        void resizeWindow(int width, int height);

        // --- engine hooks
        // -----------------------------------------------------

        // Engine frame driver; SdlEngineRunner installs
        // []{ ::Application->Run(); } here (the MainScene.cpp:747-748 frame
        // equivalent).
        using FrameCallback = std::function<void()>;
        void setFrameCallback(FrameCallback callback);

        // Veto closing. Mirrors the script's onCloseQuery: return false to keep
        // the window open. SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE=0 (prototype
        // main.cpp:237-241) keeps SDL from injecting a QUIT after the first
        // vetoed CLOSE_REQUESTED, preserving the "close asked twice" semantics.
        using CloseQueryCallback = std::function<bool()>;
        void setCloseQueryCallback(CloseQueryCallback callback);

        // GL context loss / drawable size change hooks. SDL_GL has no renderer-
        // reset event; context loss is reported explicitly before detaching so
        // resources can forget names belonging to the old context.
        using DrawableSizeCallback = std::function<void()>;
        void setDrawableSizeCallback(DrawableSizeCallback callback);
        using ContextLostCallback = std::function<void()>;
        void setContextLostCallback(ContextLostCallback callback);
        [[nodiscard]] ContextLostCallback getContextLostCallback() const;
        // Explicit invalidation entry point; invoke before context teardown.
        void notifyContextLost();
        // IME: starts/stops SDL text input on the shell window. Driven by the
        // engine-side IME-mode bridge installed via
        // SdlWindowLayer::SetImeModeListener (SdlEngineRunner wires it).
        void setTextInputEnabled(bool enabled);

        void setIMEDelegate(SdlIMEDelegate *delegate);
        [[nodiscard]] SdlIMEDelegate *getIMEDelegate() const;

        // --- DPI / size queries (replace cocos2d::Device::getDPI,
        // MainScene.cpp:540 touch threshold and :832 UI scaling)
        // SdlEngineRunner hands this to SdlWindowLayer::SetDpiProvider, which
        // seeds the touch threshold the same way TVPMainScene::create did
        // (MainScene.cpp:539-542).
        // ---------------

        // SDL_GetWindowDisplayScale: content scale, 1.0 == baseline density.
        [[nodiscard]] float getDisplayScale() const;
        // Legacy DPI baseline of the scale above (96 dpi == scale 1.0).
        [[nodiscard]] float getDPI() const;
        void getWindowSize(int &width, int &height) const;
        void getWindowSizeInPixels(int &width, int &height) const;
        [[nodiscard]] SDL_WindowID getWindowID() const { return windowID; }
        [[nodiscard]] SDL_Window *getWindow() const { return window; }
        [[nodiscard]] SDL_GLContext getGLContext() const { return glContext; }

    private:
        void pollEvents();
        void handleWindowEvent(const SDL_WindowEvent &event);
        void handleKeyboard(const SDL_KeyboardEvent &event);
        void postVirtualKey(std::uint32_t vk, bool down, bool repeat);
        void openInitialGamepads();
        void closeGamepads();
        void releaseWindow();

        // Window focus <-> host lifecycle (HostLifecycle.h).
        // TVPOnHostForeground/ Background are the engine entry points
        // (Application.cpp:749-751).
        void onHostForeground();
        void onHostBackground();
        // Focus loss: synthesize key/pointer releases first, then tell the
        // engine. The layer-side half runs through
        // krkr::input::planInputCancellation (InputCancellation.h), mirroring
        // TVPWindowLayer::CancelInput semantics.
        void cancelInputOnFocusLoss();

        void dispatchRendererRecreated();
        void resetGLExtensionInfoInited();

        [[nodiscard]] bool allowClose() const;
        [[nodiscard]] tTJSNI_BaseWindow *currentNativeWindow() const;

        Config config;
        SDL_Window *window = nullptr;
        SDL_GLContext glContext = nullptr;
        SDL_WindowID windowID = 0;
        bool running = false;
        bool focused = false;
        bool hostForeground = false;
        bool ownsSDL = false;
        bool wasFullscreen = false;
        bool vsync = false;

        FrameCallback frameCallback;
        CloseQueryCallback closeQueryCallback;
        DrawableSizeCallback drawableSizeCallback;
        ContextLostCallback contextLostCallback;
        SdlIMEDelegate *imeDelegate = nullptr;
        bool textInputEnabled = false;

        std::unordered_map<SDL_JoystickID, SDL_Gamepad *> gamepads;
        std::unordered_map<SDL_JoystickID,
                           std::unordered_map<int, std::uint32_t>>
            gamepadAxisKeys;
    };

} // namespace krkr::sdl3
