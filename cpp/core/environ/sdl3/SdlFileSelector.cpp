#include <SDL3/SDL.h>

#include <string>

namespace {
    struct DialogResult {
        bool done = false;
        std::string path;
    };

    void SDLCALL onFileDialog(void *userdata, const char *const *filelist,
                              int filter) {
        (void)filter;
        auto *result = static_cast<DialogResult *>(userdata);
        if(filelist && filelist[0])
            result->path = filelist[0];
        result->done = true;
    }
} // namespace

std::string TVPShowFileSelector(const std::string &title,
                                const std::string &filename,
                                std::string initdir, bool issave) {
    DialogResult result;
    const std::string defaultLocation = initdir.empty() ? filename : initdir;
    if(issave) {
        SDL_ShowSaveFileDialog(
            onFileDialog, &result, nullptr, nullptr, 0,
            defaultLocation.empty() ? nullptr : defaultLocation.c_str());
    } else {
        SDL_ShowOpenFileDialog(
            onFileDialog, &result, nullptr, nullptr, 0,
            defaultLocation.empty() ? nullptr : defaultLocation.c_str(), false);
    }
    while(!result.done) {
        SDL_Event event;
        if(!SDL_WaitEvent(&event))
            return {};
    }
    return result.path;
}
