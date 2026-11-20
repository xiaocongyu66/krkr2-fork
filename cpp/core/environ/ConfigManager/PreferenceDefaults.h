#pragma once

#include <string>

namespace TVPPreferenceDefaults {
#define TVP_BOOL_DEFAULT(name, value)                                          \
    inline constexpr bool value_##name = value;
#define TVP_FLOAT_DEFAULT(name, value)                                         \
    inline constexpr float value_##name = value;
#define TVP_STRING_DEFAULT(name, value)                                        \
    inline constexpr const char *value_##name = value;
#include "PreferenceDefaults.inc"
#undef TVP_BOOL_DEFAULT
#undef TVP_FLOAT_DEFAULT
#undef TVP_STRING_DEFAULT
} // namespace TVPPreferenceDefaults

// Config supplies the same GetValue/IsValueExist/SetValueInt contract as
// iSysConfigManager. Keeping policy separate permits tests without GL or UI.

// GL renderer string supplied by the host (core_environ_cocos today, the
// SDL3 shell later); "" when the host has no GL context yet. The hook lets
// TVPInitializeRuntimePreferences stay free of GL/cocos headers so this
// translation unit can live in core_environ_module.
const char *TVPGetGLRendererString();

template <typename Config>
void TVPApplyRuntimePreferenceDefaults(Config &config, bool adreno) {
    if(adreno && !config.IsValueExist("GL_EXT_shader_framebuffer_fetch"))
        config.SetValueInt("GL_EXT_shader_framebuffer_fetch", 0);
#define TVP_BOOL_DEFAULT(name, value)                                          \
    config.template GetValue<bool>(#name, TVPPreferenceDefaults::value_##name);
#define TVP_FLOAT_DEFAULT(name, value)                                         \
    config.template GetValue<float>(#name, TVPPreferenceDefaults::value_##name);
#define TVP_STRING_DEFAULT(name, value)                                        \
    config.template GetValue<std::string>(                                     \
        #name, TVPPreferenceDefaults::value_##name);
#include "PreferenceDefaults.inc"
#undef TVP_BOOL_DEFAULT
#undef TVP_FLOAT_DEFAULT
#undef TVP_STRING_DEFAULT
}

// Call after GL creation, before game initialization, in both build modes.
// The Adreno workaround reads the renderer string through
// TVPGetGLRendererString, so the caller only needs a live GL context, not GL
// headers.
void TVPInitializeRuntimePreferences();
