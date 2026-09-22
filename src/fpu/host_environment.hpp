#pragma once

#include <cfenv>

namespace cupid::fpu_host {

struct EnvironmentApi {
    static int get_environment(fenv_t* environment) noexcept {
        return std::fegetenv(environment);
    }

    static int set_environment(const fenv_t* environment) noexcept {
        return std::fesetenv(environment);
    }

    static int get_rounding() noexcept {
        return std::fegetround();
    }

    static int set_rounding(int rounding) noexcept {
        return std::fesetround(rounding);
    }
};

template <typename Api = EnvironmentApi> class ScopedEnvironment {
  public:
    explicit ScopedEnvironment(int rounding) noexcept {
        saved_ = Api::get_environment(&environment_) == 0;
        if (saved_) {
            const bool defaults_installed = Api::set_environment(FE_DFL_ENV) == 0;
            if (!defaults_installed || rounding != FE_TONEAREST)
                static_cast<void>(Api::set_rounding(rounding));
            return;
        }

        previous_rounding_ = Api::get_rounding();
        static_cast<void>(Api::set_rounding(rounding));
    }

    ~ScopedEnvironment() noexcept {
        if (saved_)
            static_cast<void>(Api::set_environment(&environment_));
        else if (previous_rounding_ != -1)
            static_cast<void>(Api::set_rounding(previous_rounding_));
    }

    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

  private:
    fenv_t environment_{};
    bool saved_{};
    int previous_rounding_{-1};
};

} // namespace cupid::fpu_host
