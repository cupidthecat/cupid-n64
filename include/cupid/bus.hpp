#pragma once

#include "cupid/cic.hpp"
#include "cupid/rdp.hpp"
#include "cupid/rdram.hpp"
#include "cupid/types.hpp"
#include "cupid/vi.hpp"

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cupid {

class System;

enum class SaveType {
    None,
    Sram,
    FlashRam,
    Eeprom4K,
    Eeprom16K,
};

struct ControllerState {
    bool connected{true};
    u16 buttons{};
    s8 stick_x{};
    s8 stick_y{};
    bool controller_pak{true};
};

class Bus {
  public:
    explicit Bus(System& system);

    void reset();
    [[nodiscard]] u64 read(u32 physical, unsigned width_bytes);
    void write(u32 physical, unsigned width_bytes, u64 value);
    bool read_cache(u32 physical, std::span<u8> bytes);
    bool write_cache(u32 physical, std::span<const u8> bytes);
    void tick(u64 rcp_cycles);
    [[nodiscard]] VideoField scan_video() const;
    void set_video_output(std::function<void(VideoField)> output);
    [[nodiscard]] u64 rdram_refresh_wait() const {
        return ri_refresh_counter_;
    }

    [[nodiscard]] u8 read_ram_byte(u32 address) const;
    void write_ram_byte(u32 address, u8 value);
    void set_interrupt(unsigned source, bool level);
    [[nodiscard]] bool interrupt_pending() const;

    bool load_rom(std::vector<u8> data, std::string& error);
    void set_save_type(SaveType type);
    void set_controller_state(unsigned port, ControllerState state);

    std::vector<u8> rdram;
    Rdram memory{rdram};
    Cic cic;
    std::vector<u8> rom;
    std::array<u8, 2048> pif{};
    std::function<void(std::string_view)> debug_output;
    std::function<void(s16, s16)> audio_output;

    SaveType save_type{SaveType::None};
    std::vector<u8> sram;
    std::vector<u8> flashram;
    std::vector<u8> eeprom;
    std::array<std::array<u8, 32 * 1024>, 4> controller_paks{};
    std::array<ControllerState, 4> controllers{};

    Rdp rdp;

  private:
    friend class Rdp;
    friend class System;

    System& system_;

    [[nodiscard]] u64 next_event() const;
    void tick_devices(u64 rcp_cycles);

    u32 open_bus_{};
    u32 mi_mode_{};
    u32 mi_interrupt_{};
    u32 mi_mask_{};
    std::array<u32, 8> ri_{};
    std::array<u32, 14> vi_{};
    std::array<u32, 6> ai_{};
    std::array<u32, 15> pi_{};
    std::array<u32, 7> si_{};
    std::array<u8, 64 * 1024> isviewer_{};
    bool ri_current_loaded_{};
    u64 ri_refresh_counter_{};
    void start_rdram_refresh();
    [[nodiscard]] u64 vi_line_cycles() const;
    [[nodiscard]] u64 next_vi_line() const;

    u64 vi_counter_{};
    std::optional<u64> vi_line_period_;
    u64 vi_clock_fraction_{};
    u64 ai_counter_{};
    u64 ai_clock_rate_{};
    u64 ai_clock_period_{};
    bool ai_clock_started_{};
    bool ai_dac_rate_written_{};
    bool ai_address_carry_{};
    u64 pi_dma_counter_{};
    u64 pi_io_counter_{};
    u64 si_dma_counter_{};
    u64 si_io_counter_{};
    u64 eeprom_busy_counter_{};
    u64 flash_busy_counter_{};
    u32 vi_current_{};
    unsigned vi_leap_counter_{};
    u32 vi_field_sequence_{};
    std::shared_ptr<std::function<void(VideoField)>> video_output_;
    u32 ai_fifo_count_{};
    std::array<u32, 2> ai_addresses_{};
    std::array<u32, 2> ai_lengths_{};
    bool pi_dma_cart_to_dram_{};
    bool pi_dma_pending_{};
    bool si_dma_pif_to_dram_{};
    bool si_dma_pending_{};
    bool pi_io_busy_{};
    bool pi_dma_busy_{};
    bool pi_error_{};
    bool pi_interrupt_{};
    bool si_interrupt_{};
    bool si_dma_busy_{};
    bool si_io_busy_{};
    u32 si_bus_latch_{};
    u32 si_phase_{};
    u32 pi_bus_latch_{};
    enum class CartDevice { Open, Rom, Sram, Flash, IsViewer };
    CartDevice cart_device_{CartDevice::Open};
    u32 cart_offset_{};
    u32 cart_limit_{};
    bool pif_rom_locked_{};
    bool pif_boot_terminated_{};
    std::array<u8, 6> pif_cpu_checksum_{};
    bool pif_checksum_valid_{};

    enum class FlashMode { ReadArray, Status, LoadPage, SiliconId };
    enum class FlashErase { None, Chip, Sector };
    FlashMode flash_mode_{FlashMode::ReadArray};
    FlashErase flash_erase_{FlashErase::None};
    u32 flash_sector_{};
    std::array<u8, 128> flash_page_{};
    u8 flash_status_{0x8c};
    u8 flash_status_commands_{};
    u16 flash_command_high_{};
    u16 flash_previous_read_{};
    u32 flash_burst_index_{};
    bool flash_command_high_valid_{};
    bool flash_status_stale_{};
    bool flash_open_bus_{};

    [[nodiscard]] static u64 extract_word_lane(u32 word, u32 address, unsigned width);
    [[nodiscard]] static u32 expand_rcp_write(u32 address, unsigned width, u64 value);
    [[nodiscard]] u64 read_bytes(const u8* data, std::size_t size, u32 offset, unsigned width) const;
    void write_bytes(u8* data, std::size_t size, u32 offset, unsigned width, u64 value);
    [[nodiscard]] u32 read_word_be(const u8* data, std::size_t size, u32 offset) const;
    void write_word_be(u8* data, std::size_t size, u32 offset, u32 value);

    [[nodiscard]] u32 read_rcp_word(u32 physical);
    void write_rcp_word(u32 physical, u32 value);
    [[nodiscard]] u64 read_rdram(u32 physical, unsigned width) const;
    void write_rdram(u32 physical, unsigned width, u64 value);
    [[nodiscard]] u32 read_mi(u32 offset) const;
    void write_mi(u32 offset, u32 value);
    [[nodiscard]] u32 read_vi(u32 offset) const;
    void write_vi(u32 offset, u32 value);
    void tick_vi(u64 rcp_cycles);
    [[nodiscard]] u32 read_ai(u32 offset) const;
    void write_ai(u32 offset, u32 value);
    void tick_ai(u64 rcp_cycles);
    void sample_ai();
    void reset_ai_clock();
    void latch_ai_period();
    [[nodiscard]] u32 read_pi(u32 offset) const;
    void write_pi(u32 offset, u32 value);
    [[nodiscard]] u32 read_ri(u32 offset) const;
    void write_ri(u32 offset, u32 value);
    [[nodiscard]] u32 read_si(u32 offset) const;
    void write_si(u32 offset, u32 value);
    void tick_si(u64 rcp_cycles);

    [[nodiscard]] u64 read_sp_memory(u32 physical, unsigned width);
    void write_sp_memory(u32 physical, unsigned width, u64 value);
    [[nodiscard]] u64 read_pif(u32 physical, unsigned width);
    void write_pif(u32 physical, unsigned width, u64 value);
    [[nodiscard]] u32 read_pif_word(u32 address) const;
    void write_pif_word(u32 address, u32 value);
    [[nodiscard]] u64 read_cart(u32 physical, unsigned width);
    void write_cart(u32 physical, unsigned width, u64 value);
    void select_cart(u32 physical);
    [[nodiscard]] u16 cart_read_half();
    void cart_write_half(u16 value);
    [[nodiscard]] static unsigned pi_domain(u32 address);
    [[nodiscard]] u32 pi_page_mask(u32 address) const;
    [[nodiscard]] u64 pi_dma_cycles(u32 length) const;
    void perform_pi_dma();
    void finish_pi_dma();
    void finish_si_dma();
    void process_pif();
    void process_pif_control();
    void execute_joybus(unsigned channel, u8 send, u8 recv, const u8* input, u8* output, bool& valid,
                        bool& overflow);
    [[nodiscard]] static u8 pak_crc(const u8* data);
    [[nodiscard]] static u8 address_crc(u16 address);
    void flash_command(u32 value);
    [[nodiscard]] std::optional<u16> read_flash_half();
    void write_flash_half(u16 value);
    void tick_flash(u64 cycles);
    void emit_isviewer();
    [[nodiscard]] u8 rdp_source_byte(u32 address, bool dmem) const;
};

} // namespace cupid
