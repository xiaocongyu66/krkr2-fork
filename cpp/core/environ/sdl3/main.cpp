//---------------------------------------------------------------------------
// krkr2_sdl3 executable entry point. Replaces platforms/{linux,windows,apple}
// /main.cpp + environ/cocos2d/AppDelegate for the SDL3 desktop build.
//
// The launch argument parsing is shared with the cocos host through
// krkr::launch::PrepareDesktopLaunch (environ/launch/DesktopLaunch.cpp);
// with the Cocos management UI disabled it enforces --game PATH, which is
// the desktop CLI the SDL3 shell starts with (the file-selector UI lives in
// core_environ_cocos and is therefore unavailable in this build).
//---------------------------------------------------------------------------

#include "SdlEngineRunner.h"

#include "TVPInitLog.h" // TVPInitLogging, TVPExtractLogLevelFromArgv
#include "environ/launch/DesktopLaunch.h"

int main(int argc, char **argv) {
    TVPLoggingOptions log_options;
    log_options.log_level = TVPExtractLogLevelFromArgv(argc, argv);
    TVPInitLogging(log_options);

    const int launch = krkr::launch::PrepareDesktopLaunch(argc, argv);
    if(launch >= 0)
        return launch;

    krkr::sdl3::SdlEngineRunner runner;
    if(!runner.init())
        return 1;
    if(!runner.startEngine(krkr::launch::DesktopTarget()))
        return 1;
    runner.run();
    return krkr::launch::DesktopExitCode() ? krkr::launch::DesktopExitCode()
                                           : 0;
}
