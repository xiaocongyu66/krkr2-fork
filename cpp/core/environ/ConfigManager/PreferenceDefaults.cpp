#include "GlobalConfigManager.h"
#include "PreferenceDefaults.h"

#include <cstring>

void TVPInitializeRuntimePreferences() {
    static bool initialized = false;
    if(initialized)
        return;
    initialized = true;
    const char *renderer = TVPGetGLRendererString();
    TVPApplyRuntimePreferenceDefaults(*GlobalConfigManager::GetInstance(),
                                      renderer &&
                                          std::strstr(renderer, "Adreno"));
}
