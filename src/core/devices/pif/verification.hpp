#pragma once

namespace cupid::n64 {

struct BootVerification {
  bool checksum = true;
  bool region = false;
};

} // namespace cupid::n64
