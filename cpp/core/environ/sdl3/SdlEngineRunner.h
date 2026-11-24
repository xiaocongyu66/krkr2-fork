#pragma once

//---------------------------------------------------------------------------
// SDL3 shell composition root: owns SdlAppDelegate and wires it to the engine,
// replacing environ/cocos2d/AppDelegate + TVPMainScene for the krkr2_sdl3
// executable.
//
// init()        applicationDidFinishLaunching equivalent: window + GL context,
//               engine hooks (frame callback, IME, DPI, close query).
// startEngine() TVPMainScene::doStartup equivalent (MainScene.cpp:700):
//               Application->StartApplication(target) boots the whole engine.
// run()         the frame loop; the installed frame callback is
//               Application->Run() (MainScene.cpp:747-748), which delivers
//               TVPDeliverAllEvents through SystemWatchTimerTimer
//               (SystemControl.cpp:121-165) and ticks every window.
//
// No cocos headers are referenced here.
//---------------------------------------------------------------------------

#include "SdlAppDelegate.h"
#include "KrkrCompositor.h"
#include "KrkrRenderContext.h"
#include "KrkrTexture.h"
#include "KrkrViewportPolicy.h"
#include "RenderManager.h"

#include <string>

namespace krkr::sdl3 {

    class SdlEngineRunner : private render::KrkrTextureNameResolver {
    public:
        SdlEngineRunner();
        ~SdlEngineRunner();

        SdlEngineRunner(const SdlEngineRunner &) = delete;
        SdlEngineRunner &operator=(const SdlEngineRunner &) = delete;

        // SDL_Init + window + GL context + engine hook installation.
        // Returns false on SDL/window failure.
        bool init();

        // Bootstrap the engine over the launch target resolved by
        // krkr::launch::PrepareDesktopLaunch (an empty target reaches the
        // engine, which handles it). Returns false when the engine aborts
        // startup; note the engine usually exits the process itself through
        // TVPExitApplication (linux/Platform.cpp:283).
        bool startEngine(const std::string &target);

        // Pumps SDL events + engine frames until the window closes or the
        // engine terminates the process.
        void run();

        [[nodiscard]] SdlAppDelegate &appDelegate();

    private:
        unsigned int resolve(const iTVPTexture2D *tex) override;
        void acceptDrawBuffer(iTVPTexture2D *tex);
        void presentFrame();
        void updateViewportFromCurrentLayer();

        // SdlIMEDelegate adapter mirroring the cocos host's IMEDelegate
        // (MainScene.cpp:1240-1307): committed text becomes per-UTF-16-unit
        // OnKeyPress, deleteBackward becomes a VK_BACK pair on non-Win32, and
        // preedit updates are delivered as complete strings through OnKeyPress.
        class EngineIME : public SdlIMEDelegate {
        public:
            void insertText(const char *text, std::size_t len) override;
            void deleteBackward() override;
            void setPreedit(const char *text, int start, int length) override;

        private:
            std::string preedit;
            int preeditStart = 0;
            int preeditLength = 0;
        };

        SdlAppDelegate app;
        EngineIME imeAdapter;
        render::KrkrRenderContext renderContext;
        render::KrkrTexture surfaceTexture;
        render::KrkrCompositor compositor;
        render::KrkrViewportPolicy viewportPolicy;
        iTVPTexture2D *pendingTexture = nullptr;
        bool pendingTextureChanged = false;
    };

} // namespace krkr::sdl3
