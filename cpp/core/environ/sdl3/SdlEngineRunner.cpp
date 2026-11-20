#include "SdlEngineRunner.h"

#include "SdlWindowLayer.h"

#include "Application.h" // ::Application (tTVPApplication)
#include "DialogBackend.h" // TVPInitNativeDialogBackend
#include "EnvironFileBackendInit.h" // TVPInitEnvironFileBackends
#include "ConfigManager/LocaleConfigManager.h"
#include "PostUpdateEvent.h" // TVPInvokePostUpdateEvent
#include "RenderManager.h" // iTVPTexture2D::RecycleProcess
#include "TextInput.h" // krkr::input::decodeTextInput
#include "TVPLog.h" // G_CoreLog / G_Tjs2Log
#include "WindowImpl.h" // tTJSNI_Window, TVPGetCurrentShiftKeyState
#include "tjsString.h" // ttstr
#include "vkdefine.h" // VK_BACK

#include <SDL3/SDL.h>

#include <filesystem>
#include <string>
#include <thread>

// Same externs AppDelegate.cpp:26 carries on the cocos host; the engine never
// declared a header for them.
extern std::thread::id TVPMainThreadID; // Application.cpp:46
std::string TVPGetCurrentLanguage(); // environ/linux/Platform.cpp:104 etc.

//---------------------------------------------------------------------------
// Host hooks the engine links against. On the cocos host these live in
// MainScene.cpp / AppDelegate.cpp (core_environ_cocos); the SDL3 executable
// must define them itself because it links the non-cocos module graph.
//---------------------------------------------------------------------------

// WindowImpl.cpp:1130 calls this when a Window TJS object is constructed.
// Lifetime note: the engine drops the returned layer in
// tTJSNI_Window::Invalidate without deleting it (identical to the cocos
// host's WindowImpl contract; the scene graph freed it there). The SDL host
// has no scenegraph, so a closed layer leaks until process exit -- the B3
// window lifecycle owns the real fix, matching the engine contract today.
iWindowLayer *TVPCreateAndAddWindow(tTJSNI_Window *w) {
    return new krkr::sdl3::SdlWindowLayer(w);
}

void TVPOpenPatchLibUrl() {
    // AppDelegate.cpp:152-155 equivalent; SDL3 replaces
    // cocos2d::Application::openURL.
    SDL_OpenURL("https://zeas2.github.io/Kirikiroid2_patch/patch");
}

void TVPConsoleLog(const ttstr & /*mes*/, bool /*important*/) {
    // The cocos host rendered this into its on-screen console window
    // (MainScene.cpp:1418-1431); the SDL shell has none yet. The text is
    // mirrored into the log system by the engine, so stay silent like the
    // cocos path did whenever the console window was absent.
}

namespace TJS {
    void TVPConsoleLog(const ttstr &str) {
        // RenderManager_ogl.cpp / ComplexRect.cpp reach this through their
        // local extern declarations; MainScene.cpp:1433 supplied it before.
        G_Tjs2Log.info(str.AsStdString());
    }
} // namespace TJS

// Host hooks the cocoa host supplied via MainScene.cpp:1460-1512 and that
// non-cocos engine code links against (GlobalConfigManager / Application /
// SystemIntf). SDL3 equivalents close the krkr2_sdl3 link.

ttstr TVPGetPlatformName() {
#if defined(_WIN32)
    return "Win32";
#elif defined(__ANDROID__)
    return "Android";
#elif defined(__APPLE__)
#include <TargetConditionals.h>
    return TARGET_OS_IPHONE ? "iPhone" : "MacOS";
#elif defined(__linux__)
    return "Linux";
#else
    return "Unknown";
#endif
}

ttstr TVPGetOSName() { return TVPGetPlatformName(); }

const std::string &TVPGetInternalPreferencePath() {
    static const std::string ret = [] {
        // MainScene.cpp:1460-1471 equivalent: writable base + ".preference".
        // SDL_GetPrefPath already points at a per-user writable directory and
        // creates it; keep the extra ".preference" suffix to match the layout
        // the engine writes under.
        char *base = SDL_GetPrefPath("krkr2", "krkr2");
        std::string path = base ? base : ".preference";
        SDL_free(base);
        path += ".preference";
        std::error_code ec;
        std::filesystem::create_directories(path, ec);
        path += "/";
        return path;
    }();
    return ret;
}

namespace krkr::sdl3 {

    SdlEngineRunner::SdlEngineRunner() = default;
    SdlEngineRunner::~SdlEngineRunner() = default;

    bool SdlEngineRunner::init() {
        // AppDelegate.cpp:43-44: the engine's out-of-memory callback and the
        // main-thread asserts key off this id and the dialog backend.
        TVPMainThreadID = std::this_thread::get_id();
        TVPInitNativeDialogBackend();
        // AppDelegate.cpp:48-49 order: the locale/config managers read their
        // packs through the environ file backend chain, so install it before
        // any of them initializes.
        TVPInitEnvironFileBackends();
        G_CoreLog.debug("SDL3 shell finish launching");

        if(!app.init())
            return false;

        // Shell <- engine bridges.
        SdlWindowLayer::SetDpiProvider([this] { return app.getDPI(); });
        SdlWindowLayer::SetImeModeListener(
            [this](bool enabled) { app.setTextInputEnabled(enabled); });
        app.setIMEDelegate(&imeAdapter);
        app.setCloseQueryCallback([] {
            // The window's close button takes the layer's close-query flow:
            // a script veto (Window.onCloseQuery) keeps the window open,
            // matching the twice-to-close behavior of the old host.
            if(auto *layer = SdlWindowLayer::GetCurrentWindowLayer())
                return layer->OnCloseQuery();
            return true;
        });

        // Shell -> engine frame: TVPMainScene::update (MainScene.cpp:747-753)
        // ran ::Application->Run() once per Director frame plus the texture
        // recycle and the post-update hook; here all three run once per SDL
        // frame. ProcessMessages + SystemWatchTimerTimer inside Run() deliver
        // TVPDeliverAllEvents (SystemControl.cpp:112-119) and every window's
        // TickBeat, which drives the UseMouseKey cursor motor.
        app.setFrameCallback([] {
            ::Application->Run();
            iTVPTexture2D::RecycleProcess();
            TVPInvokePostUpdateEvent();
        });
        return true;
    }

    bool SdlEngineRunner::startEngine(const std::string &target) {
        // AppDelegate.cpp:110: the locale manager must be ready before
        // StartApplication reads its message strings (Application.cpp:308-312).
        LocaleConfigManager::GetInstance()->Initialize(TVPGetCurrentLanguage());

        // TVPMainScene::doStartup equivalent (MainScene.cpp:700): the full
        // engine bootstrap (script engine, archives, plugins, startup script).
        return ::Application->StartApplication(ttstr(target));
    }

    void SdlEngineRunner::run() { app.run(); }

    SdlAppDelegate &SdlEngineRunner::appDelegate() { return app; }

    //---------------------------------------------------------------------------
    // SdlEngineRunner::EngineIME -- MainScene's IMEDelegate semantics
    // (MainScene.cpp:1240-1307).
    //---------------------------------------------------------------------------

    void SdlEngineRunner::EngineIME::insertText(const char *text,
                                                std::size_t len) {
        // MainScene.cpp:1291-1307: decode to UTF-16 and deliver one key press
        // per code unit through the current window.
        auto *layer = SdlWindowLayer::GetCurrentWindowLayer();
        if(!layer)
            return; // Input can arrive after the last game window was closed.
        if(auto units =
               krkr::input::decodeTextInput(std::string_view(text, len)))
            for(auto unit : *units)
                layer->OnKeyPress(static_cast<tjs_uint16>(unit), 0, false,
                                  false);
    }

    void SdlEngineRunner::EngineIME::deleteBackward() {
#ifndef _WIN32
        // MainScene.cpp:1278-1288: hosts without a real backstroke synthesize
        // the VK_BACK pair so scripts observe identical input.
        if(auto *layer = SdlWindowLayer::GetCurrentWindowLayer()) {
            const auto shift = TVPGetCurrentShiftKeyState();
            layer->InternalKeyDown(VK_BACK, shift);
            layer->OnKeyUp(VK_BACK, shift);
        }
#endif
    }

    void SdlEngineRunner::EngineIME::setPreedit(const char *text, int start,
                                                int length) {
        // New capability: the desktop hosts never surfaced the composing
        // string to the engine. Record it for a later phase that plumbs
        // preedit into Window.OnKeyPress / an independent composition event.
        preedit = text ? std::string(text) : std::string();
        preeditStart = start;
        preeditLength = length;
    }

} // namespace krkr::sdl3
