#pragma once

#include "cupid/host/options.hpp"

#include <memory>

namespace cupid::host {

[[nodiscard]] std::unique_ptr<System> create_system(const Options& options, std::string& error);
[[nodiscard]] std::string describe_hardware(const System& system);

} // namespace cupid::host
