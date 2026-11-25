#include "../gpu/vk/dlss_nr_support.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace mkwvk::dlss_nr;
int main() {
    Requirements r;
    assert(prerequisite(r) == Issue::none);
    r.upscaler = false;
    assert(prerequisite(r) == Issue::needs_upscaler); // A saved NR=On cannot bypass the upstream dependency.
    r.runtime = false;
    assert(prerequisite(r) == Issue::missing_runtime);
    r.nvidia = false;
    assert(prerequisite(r) == Issue::unsupported_gpu);
    r.built = false;
    assert(prerequisite(r) == Issue::not_built);
    r = {};
    r.upstream = Issue::needs_driver;
    assert(prerequisite(r) == Issue::needs_driver);
    r.upstream = Issue::disabled;
    assert(prerequisite(r) == Issue::disabled);
    r.upstream = Issue::missing_dlss_runtime;
    assert(prerequisite(r) == Issue::missing_dlss_runtime);
    r.upstream = Issue::missing_dlss_runtime;
    assert(prerequisite(r) == Issue::missing_dlss_runtime);
    r = {};
    // RTX + a working SR path must still be rejected when the driver does not offer NR.
    assert(prerequisite(r) == Issue::none);
    assert(capability(true, false, true, false) == Issue::unsupported_gpu);
    assert(capability(true, false, false, true) == Issue::needs_driver);
    assert(capability(false, false, false, false) == Issue::unavailable);
    assert(capability(true, false, false, false) == Issue::unavailable);
    assert(capability(true, true, false, false) == Issue::none);
    // Fixing a missing dependency must recover, without preserving a stale error in the activation policy.
    r.runtime = false;
    assert(prerequisite(r) != Issue::none);
    r.runtime = true;
    assert(prerequisite(r) == Issue::none);
    assert(linux_gate(false) == Issue::linux_opt_in && blocking(linux_gate(false)));
    assert(linux_gate(true) == Issue::none);
    assert(linux_capability(false, false, false, false) == Issue::linux_runtime);
    assert(linux_capability(true, false, false, false) == Issue::linux_runtime);
    assert(linux_capability(true, false, false, true) == Issue::needs_driver);
    assert(linux_capability(true, false, true, false) == Issue::unsupported_gpu);
    assert(linux_capability(true, true, false, false) == Issue::none);
    assert(blocking(Issue::linux_runtime));
    assert(std::strstr(message(Issue::linux_opt_in), "RX_VK_DLSS_NR_LINUX=1"));
    assert(std::strstr(message(Issue::linux_runtime), "cannot be used"));
#ifndef _WIN32
    assert(std::strstr(message(Issue::missing_dlss_runtime), "libnvidia-ngx-dlss.so"));
#endif
    assert(blocking(Issue::runtime_path) && !blocking(Issue::configuration_rejected));
    for (int i = 1; i < kIssues; ++i)   // (the DLSS 5 row's line enumerates them: menu/dlss5.cpp hn_dlss5)
        assert(std::strlen(message(static_cast<Issue>(i))) > 30);
    assert(kIssues == static_cast<int>(Issue::runtime_path) + 1);
    // (where the player puts NVIDIA's file: both folders said)
    assert(std::strstr(message(Issue::missing_runtime), "ngx folder") && std::strstr(message(Issue::missing_runtime), "Longshot/ngx"));
    assert(std::strlen(message(Issue::none)) == 0);
    std::puts("NR activation prerequisites, unsupported hardware, driver and recovery: PASS");
}
