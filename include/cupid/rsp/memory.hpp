#pragma once

#include "cupid/types.hpp"

#include <array>
#include <cstddef>

namespace cupid {

class Bus;
class Rsp;

class RspMemory {
  public:
    using Storage = std::array<u8, 8192>;
    using value_type = Storage::value_type;
    using size_type = Storage::size_type;
    using difference_type = Storage::difference_type;
    using reference = Storage::reference;
    using const_reference = Storage::const_reference;
    using pointer = Storage::pointer;
    using const_pointer = Storage::const_pointer;
    using iterator = Storage::iterator;
    using const_iterator = Storage::const_iterator;
    using reverse_iterator = Storage::reverse_iterator;
    using const_reverse_iterator = Storage::const_reverse_iterator;

    RspMemory() = default;
    RspMemory(const RspMemory& other) : bytes_(other.bytes_) {}

    RspMemory& operator=(const RspMemory& other) {
        if (this != &other)
            bytes_ = other.bytes_;
        invalidate_imem();
        return *this;
    }

    [[nodiscard]] reference operator[](size_type index) noexcept {
        expose_alias();
        return bytes_[index];
    }
    [[nodiscard]] const_reference operator[](size_type index) const noexcept {
        expose_alias();
        return bytes_[index];
    }
    [[nodiscard]] reference at(size_type index) {
        expose_alias();
        return bytes_.at(index);
    }
    [[nodiscard]] const_reference at(size_type index) const {
        expose_alias();
        return bytes_.at(index);
    }
    [[nodiscard]] reference front() noexcept {
        expose_alias();
        return bytes_.front();
    }
    [[nodiscard]] const_reference front() const noexcept {
        expose_alias();
        return bytes_.front();
    }
    [[nodiscard]] reference back() noexcept {
        expose_alias();
        return bytes_.back();
    }
    [[nodiscard]] const_reference back() const noexcept {
        expose_alias();
        return bytes_.back();
    }
    [[nodiscard]] pointer data() noexcept {
        expose_alias();
        return bytes_.data();
    }
    [[nodiscard]] const_pointer data() const noexcept {
        expose_alias();
        return bytes_.data();
    }

    [[nodiscard]] iterator begin() noexcept {
        expose_alias();
        return bytes_.begin();
    }
    [[nodiscard]] const_iterator begin() const noexcept {
        expose_alias();
        return bytes_.begin();
    }
    [[nodiscard]] const_iterator cbegin() const noexcept {
        expose_alias();
        return bytes_.cbegin();
    }
    [[nodiscard]] iterator end() noexcept {
        expose_alias();
        return bytes_.end();
    }
    [[nodiscard]] const_iterator end() const noexcept {
        expose_alias();
        return bytes_.end();
    }
    [[nodiscard]] const_iterator cend() const noexcept {
        expose_alias();
        return bytes_.cend();
    }
    [[nodiscard]] reverse_iterator rbegin() noexcept {
        expose_alias();
        return bytes_.rbegin();
    }
    [[nodiscard]] const_reverse_iterator rbegin() const noexcept {
        expose_alias();
        return bytes_.rbegin();
    }
    [[nodiscard]] const_reverse_iterator crbegin() const noexcept {
        expose_alias();
        return bytes_.crbegin();
    }
    [[nodiscard]] reverse_iterator rend() noexcept {
        expose_alias();
        return bytes_.rend();
    }
    [[nodiscard]] const_reverse_iterator rend() const noexcept {
        expose_alias();
        return bytes_.rend();
    }
    [[nodiscard]] const_reverse_iterator crend() const noexcept {
        expose_alias();
        return bytes_.crend();
    }

    [[nodiscard]] constexpr bool empty() const noexcept {
        return false;
    }
    [[nodiscard]] constexpr size_type size() const noexcept {
        return 8192;
    }
    [[nodiscard]] constexpr size_type max_size() const noexcept {
        return 8192;
    }

    void fill(u8 value) {
        bytes_.fill(value);
        invalidate_imem();
    }

    [[nodiscard]] bool imem_trusted() const noexcept {
        return !imem_untrusted_;
    }
    [[nodiscard]] u64 imem_revision() const noexcept {
        return imem_revision_;
    }

    friend bool operator==(const RspMemory& left, const RspMemory& right) noexcept {
        return left.bytes_ == right.bytes_;
    }

  private:
    friend class Bus;
    friend class Rsp;

    [[nodiscard]] pointer internal_data() noexcept {
        return bytes_.data();
    }
    [[nodiscard]] const_pointer internal_data() const noexcept {
        return bytes_.data();
    }
    [[nodiscard]] u8 internal_read(size_type index) const noexcept {
        return bytes_[index];
    }
    void internal_write(size_type index, u8 value) noexcept {
        if (index >= 0x1000U)
            invalidate_imem();
        bytes_[index] = value;
    }

    void expose_alias() const noexcept {
        if (imem_untrusted_)
            return;
        imem_untrusted_ = true;
        invalidate_imem();
    }
    void invalidate_imem() const noexcept {
        ++imem_revision_;
        if (imem_revision_ == 0) {
            imem_untrusted_ = true;
            imem_revision_ = 1;
        }
    }

    Storage bytes_{};
    mutable u64 imem_revision_{1};
    mutable bool imem_untrusted_{};
};

} // namespace cupid
