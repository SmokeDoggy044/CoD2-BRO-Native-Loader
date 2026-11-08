#pragma once

// Titles change the CPU's floating-point modes for their own math (CoD2: BRO sets x87
// 24-bit precision and SSE round-toward-zero), and host libraries called from title
// threads inherit them. Wrap calls into libraries that need the C defaults (graphics
// drivers, DXVK, SDL) in a HostFloatEnv; threads they start inherit the defaults too.

#include <cfenv>

namespace platform {

class HostFloatEnv {
public:
    HostFloatEnv() {
        std::fegetenv(&saved_);
        std::fesetenv(FE_DFL_ENV);
    }
    ~HostFloatEnv() { std::fesetenv(&saved_); }
    HostFloatEnv(const HostFloatEnv&) = delete;
    HostFloatEnv& operator=(const HostFloatEnv&) = delete;

private:
    std::fenv_t saved_;
};

}  // namespace platform
