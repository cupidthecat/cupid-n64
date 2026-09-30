#pragma once

#include "cupid/cpu/native.hpp"

namespace cupid {

class CpuNativeCache {
  public:
    static constexpr std::size_t capacity = 4096;

    CpuNativeCache();
    ~CpuNativeCache();
    CpuNativeCache(const CpuNativeCache&);
    CpuNativeCache& operator=(const CpuNativeCache&);
    CpuNativeCache(CpuNativeCache&&) noexcept;
    CpuNativeCache& operator=(CpuNativeCache&&) noexcept;

    [[nodiscard]] std::shared_ptr<const CpuNativeCode> lookup(std::span<const u32> instructions);
    [[nodiscard]] std::size_t size() const;
    void reset();

  private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
};

} // namespace cupid
