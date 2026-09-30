#pragma once

#include "cupid/rsp/pipeline.hpp"
#include "cupid/types.hpp"

#include <memory>
#include <span>

namespace cupid {

class Rsp;

struct RspNativeInstruction {
    u32 word{};
    RspPipeline::Operation operation{};
    bool operator==(const RspNativeInstruction&) const = default;
};

struct RspNativeState {
    Rsp* rsp{};
    u32* scalar{};
    u8* dmem{};
    u16* vectors{};
    u16* accumulator{};
    u8* carry_low{};
    u8* carry_high{};
};

class RspNativeCode {
  public:
    ~RspNativeCode();
    RspNativeCode(const RspNativeCode&) = delete;
    RspNativeCode& operator=(const RspNativeCode&) = delete;

    [[nodiscard]] static bool available();
    [[nodiscard]] static std::shared_ptr<const RspNativeCode>
    compile(std::span<const RspNativeInstruction> instructions);
    void execute(RspNativeState& state) const;

  private:
    explicit RspNativeCode(void* code, bool inline_vectors, bool inline_carry)
        : code_(code), inline_vectors_(inline_vectors), inline_carry_(inline_carry) {}
    void* code_{};
    bool inline_vectors_{};
    bool inline_carry_{};

    template <unsigned Function, unsigned Element> static void vector(Rsp* rsp, u32 word);
    static void cop2(Rsp* rsp, u32 word);
    static void load_vector(Rsp* rsp, u32 word);
    static void store_vector(Rsp* rsp, u32 word);
    static void load_wrapped(Rsp* rsp, u32 word);
    static void store_byte(Rsp* rsp, u32 address, u32 value);
    static void store_halfword(Rsp* rsp, u32 address, u32 value);
    static void store_word(Rsp* rsp, u32 address, u32 value);
};

class RspNativeCache {
  public:
    static constexpr std::size_t capacity = 4096;
    struct Lookup {
        std::shared_ptr<const RspNativeCode> code;
        bool complete{};
    };

    RspNativeCache();
    ~RspNativeCache();
    RspNativeCache(const RspNativeCache&);
    RspNativeCache& operator=(const RspNativeCache&);
    RspNativeCache(RspNativeCache&&) noexcept;
    RspNativeCache& operator=(RspNativeCache&&) noexcept;

    [[nodiscard]] Lookup lookup(std::span<const RspNativeInstruction> instructions);
    [[nodiscard]] std::size_t size() const;
    void reset();

  private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
};

} // namespace cupid
