// Why DLSS 5 (NVIDIA's neural rendering, vk_dlss.cpp) does not run, shared by the menu (gpu/vk/menu/dlss5.cpp: the DLSS 5
// row's line) and the renderer (its log line). No GPU-name guesses: NGX's own answer decides.
#pragma once

namespace mkwvk::dlss_nr {
enum class Issue {
    none, not_built, platform, unsupported_gpu, missing_runtime, needs_upscaler, needs_driver, missing_dlss_runtime, disabled,
    unavailable, render_failed, configuration_rejected, out_of_memory, linux_opt_in, linux_runtime, runtime_path
};
inline constexpr int kIssues = (int)Issue::runtime_path + 1;
struct Requirements {
    bool built = true, nvidia = true, runtime = true, upscaler = true;
    Issue upstream = Issue::none;
};
/// (a model refused new settings: the last ones it took still run)
inline bool blocking(Issue issue) { return issue != Issue::none && issue != Issue::configuration_rejected; }
// Native Linux NR is opt-in. A Windows DLL is not a Linux implementation.
inline Issue linux_gate(bool experimental) { return experimental ? Issue::none : Issue::linux_opt_in; }
inline Issue prerequisite(const Requirements& r) {
    if (!r.built) return Issue::not_built;
    if (!r.nvidia) return Issue::unsupported_gpu;
    if (!r.runtime) return Issue::missing_runtime;
    if (r.upstream != Issue::none) return r.upstream;
    if (!r.upscaler) return Issue::needs_upscaler;
    return Issue::none;
}
inline Issue capability(bool queried, bool available, bool unsupported, bool needs_driver) {
    if (needs_driver) return Issue::needs_driver;
    if (unsupported) return Issue::unsupported_gpu;
    if (!queried || !available) return Issue::unavailable;
    return Issue::none;
}
inline Issue linux_capability(bool queried, bool available, bool unsupported, bool needs_driver) {
    const auto issue = capability(queried, available, unsupported, needs_driver);
    return issue == Issue::unavailable ? Issue::linux_runtime : issue;
}
inline const char* message(Issue issue) {
    switch (issue) {
    case Issue::missing_runtime:
        return "DLSS 5 is off: NVIDIA's nvngx_dlssnr.dll is not in the game's ngx folder (or LOCALAPPDATA's Longshot/ngx). "
               "Longshot does not include or download it.";
    case Issue::unsupported_gpu:
        return "DLSS 5 is off: NVIDIA says this GPU and driver do not run its neural rendering (DLSS itself is not enough).";
    case Issue::needs_upscaler:
        return "DLSS 5 is off: choose NVIDIA DLSS, DLAA or AMD FSR as the Upscaler. The game's own upscaler cannot run it.";
    case Issue::needs_driver:
        return "DLSS 5 is off: NVIDIA says it needs a newer graphics driver. Update the driver and restart the game.";
    case Issue::missing_dlss_runtime:
#ifdef _WIN32
        return "DLSS 5 is off: DLSS's own runtime, nvngx_dlss.dll, is not in the game's ngx folder. Put it back and restart "
               "the game.";
#else
        return "DLSS 5 is off: DLSS's native Linux runtime, libnvidia-ngx-dlss.so, is not in the game's ngx folder. Put it "
               "back and restart the game.";
#endif
    case Issue::disabled:
        return "DLSS 5 is off: DLSS is off for this run (RX_VK_DLSS=0).";
    case Issue::not_built:
        return "DLSS 5 is off: this build has no NVIDIA DLSS. Use a build with DLSS.";
    case Issue::platform:
        return "DLSS 5 is off: it runs on Windows, and on Linux only as an experiment (RX_VK_DLSS_NR_LINUX=1).";
    case Issue::linux_opt_in:
        return "Linux DLSS 5 is an experiment, off by default: start with RX_VK_DLSS_NR_LINUX=1 to ask the native NGX for it. "
               "nvngx_dlssnr.dll is a Windows file and cannot be used on Linux.";
    case Issue::linux_runtime:
        return "Linux DLSS 5 is off: the native NGX does not offer neural rendering. The Windows nvngx_dlssnr.dll cannot be "
               "used on Linux.";
    case Issue::runtime_path:
        return "DLSS 5 is off: the native Linux NGX cannot load DLSS's runtime from a folder whose path is not ASCII. Put it in "
               "an ASCII path, set RX_VK_DLSS_DIR to it and restart.";
    case Issue::unavailable:
#ifdef _WIN32
        return "DLSS 5 is off: NVIDIA could not start its neural rendering. Check that nvngx_dlssnr.dll suits this GPU and "
               "driver (NVIDIA's FP8 model runs on RTX 50 only), then restart the game. The log has NVIDIA's answer.";
#else
        return "Linux DLSS 5 is off: NVIDIA could not start its native neural rendering. Check the native NGX runtime and "
               "driver, then restart the game. The log has NVIDIA's answer.";
#endif
    case Issue::out_of_memory:
        return "DLSS 5 stopped: the GPU has no memory left for it. Lower the DLSS 5 model resolution or the Render "
               "resolution, then turn DLSS 5 Off and On.";
    case Issue::render_failed:
        return "DLSS 5 stopped: the model failed (the log says how). Turn DLSS 5 Off and On to try again, or restart the game.";
    case Issue::configuration_rejected:
        return "NVIDIA's model refused these settings: the last ones it took still run. Change a setting to try again.";
    default: return "";
    }
}
} // namespace mkwvk::dlss_nr
