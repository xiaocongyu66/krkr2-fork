#include "SdlEngineRunner.h"

#include "SdlWindowLayer.h"

#include "Application.h" // ::Application (tTVPApplication)
#include "DialogBackend.h" // TVPInitNativeDialogBackend
#include "EnvironFileBackendInit.h" // TVPInitEnvironFileBackends
#include "ConfigManager/LocaleConfigManager.h"
#include "PostUpdateEvent.h" // TVPInvokePostUpdateEvent
#include "RenderManager.h" // iTVPTexture2D::RecycleProcess, OGL texture resolver
#include "SysInitIntf.h" // TVPGetDefaultRendererName host hook
#include "TextInput.h" // krkr::input::decodeTextInput
#include "TVPError.h" // TVPThrowExceptionMessage
#include "TVPLog.h" // G_CoreLog / G_Tjs2Log
#include "TVPMessages.h" // TVPErrorInKrMovieDLL
#include "WindowImpl.h" // tTJSNI_Window, TVPGetCurrentShiftKeyState
#include "krmovie.h" // iTVPVideoOverlay
#include "tjsString.h" // ttstr
#include "vkdefine.h" // VK_BACK

#include <SDL3/SDL.h>

#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
// Raw GL for the diagnostic surface dump (SdlAppDelegate initialized GLEW on
// this thread); mirrors visual/ogl/ogl_common.h's GLEW_STATIC posture.
#define GLEW_STATIC
#include <GL/glew.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

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
// The engine calls InvalidateClose() during tTJSNI_Window::Invalidate; the SDL
// layer deletes itself there, matching the cocos scenegraph's removeFromParent.
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

//---------------------------------------------------------------------------
// Video overlay engine seam (krffmpeg.cpp declarations, VideoOvlImpl.cpp
// call sites). The ffmpeg/cocos player objects (MoviePlayerOverlay /
// MoviePlayerLayer) compile into core_movie_cocos, which the SDL3 link graph
// excludes; the SDL3 shell cannot play movies yet, so every factory throws
// the historical krmovie.dll failure with an SDL3-shell reason instead of
// producing an unplayable object.
//---------------------------------------------------------------------------
class tTJSNI_VideoOverlay;
struct IStream;

void GetVideoOverlayObject(tTJSNI_VideoOverlay * /*callbackwin*/,
                           IStream * /*stream*/,
                           const tjs_char * /*streamname*/,
                           const tjs_char * /*type*/, uint64_t /*size*/,
                           iTVPVideoOverlay **out) {
    *out = nullptr;
    TVPThrowExceptionMessage(
        TVPErrorInKrMovieDLL,
        TJS_W("video playback is not available in the SDL3 shell yet"));
}

void GetVideoLayerObject(tTJSNI_VideoOverlay * /*callbackwin*/,
                         IStream * /*stream*/, const tjs_char * /*streamname*/,
                         const tjs_char * /*type*/, uint64_t /*size*/,
                         iTVPVideoOverlay **out) {
    *out = nullptr;
    TVPThrowExceptionMessage(
        TVPErrorInKrMovieDLL,
        TJS_W("video playback is not available in the SDL3 shell yet"));
}

void GetMixingVideoOverlayObject(tTJSNI_VideoOverlay * /*callbackwin*/,
                                 IStream * /*stream*/,
                                 const tjs_char * /*streamname*/,
                                 const tjs_char * /*type*/, uint64_t /*size*/,
                                 iTVPVideoOverlay **out) {
    *out = nullptr;
    TVPThrowExceptionMessage(
        TVPErrorInKrMovieDLL,
        TJS_W("video playback is not available in the SDL3 shell yet"));
}

void GetMFVideoOverlayObject(tTJSNI_VideoOverlay * /*callbackwin*/,
                             IStream * /*stream*/,
                             const tjs_char * /*streamname*/,
                             const tjs_char * /*type*/, uint64_t /*size*/,
                             iTVPVideoOverlay **out) {
    *out = nullptr;
    TVPThrowExceptionMessage(
        TVPErrorInKrMovieDLL,
        TJS_W("video playback is not available in the SDL3 shell yet"));
}

// Host hook (B4, SysInitIntf.h): the SDL3 shell has no software-renderer
// presentation path (no cocos scene graph consumes software textures), so
// default to the OpenGL renderer. An explicit individual-config "renderer"
// entry still wins.
const char *TVPGetDefaultRendererName() { return "opengl"; }

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

    namespace {
        // Diagnostic only (KRKR_SDL3_DUMP_SURFACE): sample the adopted engine
        // surface texture through a scratch FBO and write a PPM, so a bad
        // present mapping can be told apart from bad engine texture content.
        void dumpSurfaceTexture(const render::KrkrTexture &tex,
                                const char *path) {
            GLuint name = tex.glName();
            if(!name)
                return;
            const int w = tex.width();
            const int h = tex.height();
            if(w <= 0 || h <= 0)
                return;
#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
            GLuint fbo = 0;
            glGenFramebuffers(1, &fbo);
            if(!fbo)
                return;
            GLint prevFbo = 0;
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_2D, name, 0);
            if(glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
               GL_FRAMEBUFFER_COMPLETE) {
                glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
                glDeleteFramebuffers(1, &fbo);
                return;
            }
            glViewport(0, 0, w, h);
            std::vector<std::uint8_t> buf((size_t)w * h * 4);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
            glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
            glDeleteFramebuffers(1, &fbo);
            if(std::FILE *f = std::fopen(path, "wb")) {
                std::fprintf(f, "P6\n%d %d\n255\n", w, h);
                std::vector<std::uint8_t> row((size_t)w * 3);
                for(int y = 0; y < h; ++y) {
                    const std::uint8_t *src =
                        buf.data() + (size_t)(h - 1 - y) * w * 4;
                    for(int x = 0; x < w; ++x) {
                        row[x * 3 + 0] = src[x * 4 + 0];
                        row[x * 3 + 1] = src[x * 4 + 1];
                        row[x * 3 + 2] = src[x * 4 + 2];
                    }
                    std::fwrite(row.data(), 1, row.size(), f);
                }
                std::fclose(f);
            }
#endif
        }
    } // namespace

    SdlEngineRunner::SdlEngineRunner() = default;

    SdlEngineRunner::~SdlEngineRunner() {
        SdlWindowLayer::SetDrawBufferCallback({});
        SdlWindowLayer::SetCurrentWindowCallback({});
        SdlWindowLayer::SetHostResizeProvider({});
        if(renderContext.window() && renderContext.context())
            renderContext.makeCurrent();
        if(pendingTexture)
            pendingTexture->Release();
        surfaceTexture.release();
        compositor.invalidate();
        render::KrkrTexture::setNameResolver(nullptr);
        app.notifyContextLost();
        renderContext.detach();
    }

    unsigned int SdlEngineRunner::resolve(const iTVPTexture2D *tex) {
        return tex ? tex->GetOGLTextureName() : 0;
    }

    void SdlEngineRunner::acceptDrawBuffer(iTVPTexture2D *tex) {
        if(pendingTexture == tex && (tex || pendingTextureChanged))
            return;
        if(tex)
            tex->AddRef();
        if(pendingTexture)
            pendingTexture->Release();
        pendingTexture = tex;
        pendingTextureChanged = true;
    }

    void SdlEngineRunner::updateViewportFromCurrentLayer() {
        auto *layer = SdlWindowLayer::GetCurrentWindowLayer();
        if(!layer)
            return;

        const int designW = layer->GetLayerWidth();
        const int designH = layer->GetLayerHeight();
        if(designW <= 0 || designH <= 0)
            return;

        float fillX = 1.f, fillY = 1.f;
        layer->getPaintBox(fillX, fillY);
        viewportPolicy.setDesignSize(designW, designH);
        viewportPolicy.setMode(fillX != 1.f || fillY != 1.f
                                   ? render::KrkrViewportPolicy::Mode::Fill
                                   : render::KrkrViewportPolicy::Mode::Fit);
    }

    void SdlEngineRunner::presentFrame() {
        if(!renderContext.isCurrent())
            return;

        if(pendingTextureChanged) {
            surfaceTexture.update(pendingTexture);
            if(pendingTexture)
                pendingTexture->Release();
            pendingTexture = nullptr;
            pendingTextureChanged = false;
        }

        int width = 0, height = 0;
        app.getWindowSizeInPixels(width, height);
        compositor.resize(width, height);
        updateViewportFromCurrentLayer();
        compositor.setViewportPolicy(&viewportPolicy);
        float scaleX = 1.f, scaleY = 1.f;
        surfaceTexture.getScale(scaleX, scaleY);
        compositor.setSurfaceTexture(
            &surfaceTexture,
            SdlWindowLayer::GetCurrentWindowLayer()
                ? SdlWindowLayer::GetCurrentWindowLayer()->GetLayerWidth()
                : surfaceTexture.width(),
            SdlWindowLayer::GetCurrentWindowLayer()
                ? SdlWindowLayer::GetCurrentWindowLayer()->GetLayerHeight()
                : surfaceTexture.height(),
            scaleX, scaleY);
        compositor.draw();
        compositor.flush();
        if(const char *dump = std::getenv("KRKR_SDL3_DUMP_SURFACE")) {
            // First frame suffices; the fixture is static after startup.
            static bool dumped = false;
            if(!dumped) {
                dumped = true;
                dumpSurfaceTexture(surfaceTexture, dump);
            }
        }
    }

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
        renderContext.attach(app.getWindow(), app.getGLContext());
        if(!renderContext.makeCurrent() ||
           !render::bindGL(SDL_GL_GetProcAddress)) {
            renderContext.detach();
            app.setIMEDelegate(nullptr);
            SdlWindowLayer::SetDpiProvider({});
            SdlWindowLayer::SetImeModeListener({});
            return false;
        }
        render::KrkrTexture::setNameResolver(this);
        compositor.setViewportPolicy(&viewportPolicy);
        SdlWindowLayer::SetDrawBufferCallback(
            [this](iTVPTexture2D *tex) { acceptDrawBuffer(tex); });
        SdlWindowLayer::SetCurrentWindowCallback(
            [this](SdlWindowLayer *) { acceptDrawBuffer(nullptr); });
        SdlWindowLayer::SetModalPump([this] { return app.pumpModalFrame(); });
        SdlWindowLayer::SetHostResizeProvider(
            [this](int w, int h) { app.resizeWindow(w, h); });
        app.setDrawableSizeCallback([this] {
            int width = 0, height = 0;
            app.getWindowSizeInPixels(width, height);
            compositor.resize(width, height);
        });
        app.setContextLostCallback([this] {
            compositor.invalidate();
            surfaceTexture.invalidate();
            render::bindGL(SDL_GL_GetProcAddress);
            renderContext.makeCurrent();
        });
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
        app.setFrameCallback([this] {
            ::Application->Run();
            iTVPTexture2D::RecycleProcess();
            TVPInvokePostUpdateEvent();
            presentFrame();
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
        // The engine exposes no composition/preedit API; mirror its existing
        // committed-text path by delivering this complete update as keypresses.
        // SDL_TEXTEDITING offsets are UTF-8 byte offsets, so convert the full
        // string and do not forward the offsets as UTF-16 indices.
        preedit = text ? std::string(text) : std::string();
        preeditStart = start;
        preeditLength = length;
        auto *layer = SdlWindowLayer::GetCurrentWindowLayer();
        if(!layer)
            return;
        if(auto units = krkr::input::decodeTextInput(preedit))
            for(auto unit : *units)
                layer->OnKeyPress(static_cast<tjs_uint16>(unit), 0, false,
                                  false);
    }

} // namespace krkr::sdl3
