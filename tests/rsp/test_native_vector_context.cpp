#include "cupid/system.hpp"
#include "test.hpp"

#include <array>
#include <memory>
#include <span>
#include <vector>

using namespace cupid;

namespace {

constexpr u32 vector_word(unsigned function, unsigned destination, unsigned source, unsigned target,
                          unsigned element) {
    return 0x4a000000U | (element << 21U) | (target << 16U) | (source << 11U) | (destination << 6U) |
           function;
}

constexpr u32 vector_memory(unsigned opcode, unsigned target, unsigned address, unsigned base = 0U) {
    return (opcode << 26U) | ((base == 0U ? 0U : 2U) << 21U) | (target << 16U) | (4U << 11U) |
           ((address - base) / 16U);
}

void execute(System& system, std::span<const u32> words) {
    for (unsigned index = 0; index < words.size(); ++index)
        system.bus.write(0x04001000U + index * 4U, 4, words[index]);
    system.rsp.write_pc(0);
    system.rsp.write_register(0x10, 1);
    system.rsp.tick(512);
    CHECK_EQ(system.rsp.read_register(0x10) & 3U, 3U);
}

void seed(System& system, unsigned instance) {
    std::vector<u32> program;
    for (unsigned index = 0; index < 128; ++index)
        system.bus.write(0x04000200U + index * 4U, 4,
                         (0x80007fffU + index * 0x10203U) ^ (instance * 0x76543210U));
    for (unsigned reg = 0; reg < 32; ++reg)
        program.push_back(vector_memory(0x32, reg, 0x200U + reg * 16U));
    program.push_back(13);
    execute(system, program);
}

void observe(System& system) {
    std::vector<u32> program{0x24020400U};
    for (unsigned reg = 0; reg < 32; ++reg)
        program.push_back(vector_memory(0x3a, reg, 0x400U + reg * 16U, 0x400U));
    for (unsigned slice = 0; slice < 3; ++slice) {
        program.push_back(vector_word(0x1d, 29U + slice, 0, 0, 8U + slice));
        program.push_back(vector_memory(0x3a, 29U + slice, 0x600U + slice * 16U, 0x400U));
        program.push_back(0x48410000U | (slice << 11U));
        program.push_back(0xac010630U + slice * 4U);
    }
    program.push_back(13);
    execute(system, program);
}

} // namespace

TEST(rsp_native_vector_code_uses_the_current_machine_for_each_element_and_alias) {
    for (unsigned element = 0; element < 16; ++element) {
        const std::array words{
            vector_word(0x00, 1, 3, 4, element),
            vector_word(0x0f, 5, 1, 6, element),
            vector_word(0x2c, 7, 5, 7, element),
            vector_word(0x15, 9, 7, 8, element),
        };
        std::array<RspNativeInstruction, words.size()> instructions{};
        for (unsigned index = 0; index < words.size(); ++index)
            instructions[index] = {words[index], RspPipeline::Operation::Cop2};
        const auto code = RspNativeCode::compile(instructions);
        CHECK_EQ(static_cast<bool>(code), RspNativeCode::available());
        if (!code)
            continue;
        for (unsigned instance = 1; instance <= 2; ++instance) {
            auto machines = std::make_unique<std::array<System, 2>>();
            auto& [native, ordinary] = *machines;
            ordinary.rsp.set_native_execution(false);
            seed(native, instance);
            seed(ordinary, instance);
            RspNativeState state{&native.rsp, nullptr, nullptr};
            code->execute(state);
            std::vector<u32> program(words.begin(), words.end());
            program.push_back(13);
            execute(ordinary, program);
            observe(native);
            observe(ordinary);
            CHECK(native.bus.read(0x04000400U, 4) != 0U);
            for (u32 address = 0x04000400U; address < 0x0400063cU; address += 4U)
                CHECK_EQ(native.bus.read(address, 4), ordinary.bus.read(address, 4));
        }
    }
}
