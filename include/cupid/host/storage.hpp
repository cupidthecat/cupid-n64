#pragma once

#include "cupid/host/options.hpp"

namespace cupid::host {

[[nodiscard]] bool validate_storage_paths(const Options& options, std::string& error);
[[nodiscard]] bool load_persistent_storage(System& system, const Options& options, std::string& error);
[[nodiscard]] bool flush_persistent_storage(const System& system, const Options& options, std::string& error);

} // namespace cupid::host
