# Physical bus and register matrix

CPU physical accesses route through one big-endian RCP bus model. The
table below records the behavior that is implemented and covered by local tests.
It is an implementation matrix, not a claim that every undocumented electrical
edge of the console bus is modeled.

## Common access rules

RDRAM data accepts byte, halfword, word, and doubleword accesses with normal
memory lanes. RCP devices use a 32-bit word bus: byte and halfword reads perform
the device word read and select the requested big-endian lane, while byte and
halfword stores drive a full word with zeroes on inactive lanes. A doubleword
store to an RCP target drives only its upper word. A doubleword read from any
non-RDRAM target stalls the CPU before the target is read, so read side effects
do not occur. `physical_bus_sp_memory_subword_and_doubleword_stores_use_rcp_word_lanes`,
`physical_bus_rcp_doubleword_store_drives_only_the_high_word`, and
`physical_bus_unsupported_doubleword_read_stalls_before_register_side_effects`
lock these rules down. Pinned hardware tests `spmem: SH`, `spmem: SB`, `spmem:
SD`, `pifram: SB (offset 0)` through `pifram: SB (offset 3)`, `pifram: LB`, and
`pifram: LH` provide independent lane coverage.

Unsupported physical windows stall the CPU rather than returning a reusable
device value. The stall leaves device state unchanged and does not synthesize a
CPU address exception. `bus_unmapped_rcp_accesses_stall_instead_of_returning_old_data`,
`cpu_unmapped_load_does_not_commit_its_destination`,
`physical_bus_supported_window_edges_do_not_fall_into_adjacent_stalls`, and
`physical_bus_unmapped_stalls_preserve_mi_register_state` cover those outcomes.

## Physical decode

| Physical range | Target | Mirroring / boundary behavior | Access and side effects | Tests |
| --- | --- | --- | --- | --- |
| `0x00000000..0x03efffff` | RDRAM data path | Installed memory and configured chip mapping decide whether an address responds; absent/out-of-range hardware accesses can latch RI acknowledgement/range errors. | 8/16/32/64-bit memory accesses. MI repeat and EBUS modes apply to CPU physical writes/reads as documented by the memory-bus tests. | `memory_bus_repeat_preserves_doubleword_phase_at_unaligned_start`, `memory_bus_repeat_byte_lane_uses_the_full_bus_word`, `memory_bus_ebus_mode_changes_uncached_accesses_without_affecting_dma`, `memory_bus_acknowledgement_error_is_visible_and_clearable`, `ri_out_of_range_error_is_sticky_and_does_not_alias_a_bank` |
| `0x03f00000..0x03ffffff` | RDRAM chip registers | Chip/select/broadcast fields are decoded from the physical address. MI RDRAM-register-select controls second-word visibility. | RCP word-lane rules. 64-bit register stores use the high word; cache reads expose one register word. | `memory_bus_register_select_controls_second_word_reads`, `memory_bus_register_doubleword_store_only_uses_its_high_word`, `memory_bus_cached_register_read_returns_one_word`, `rdram_row_commands_broadcast_and_ignore_register_index_aliases`, `rdram_read_only_identification_registers_ignore_writes` |
| `0x04000000..0x0403ffff` | SP DMEM / IMEM | The 8 KiB DMEM+IMEM image repeats throughout the full window. `0x1000` selects IMEM inside each 8 KiB cycle. | RCP word-lane stores; byte/half reads select lanes. 64-bit read stalls; 64-bit write uses the high word. | `physical_bus_sp_memory_subword_and_doubleword_stores_use_rcp_word_lanes`, `physical_bus_sp_memory_and_register_windows_mirror_to_their_last_cycle`; pinned `spmem: SW`, `spmem: SH`, `spmem: SB`, `spmem: SD`, `spmem: SW (out of bounds)` |
| `0x04040000..0x0407ffff` | SP DMA/status registers | Register index repeats every `0x20` bytes throughout the window. | See SP register matrix below. | `physical_bus_sp_memory_and_register_windows_mirror_to_their_last_cycle`, `rsp_status_pairs_and_semaphore`, `rsp_dma_alignment_wrap_queue_and_postincrement` |
| `0x04080000..0x040bffff` | SP PC / IBIST | Register index repeats every `0x20` bytes. | PC writes keep bits `11:2`; IBIST is currently inert. | `physical_bus_sp_memory_and_register_windows_mirror_to_their_last_cycle`; pinned `RSP PC REG` |
| `0x040c0000..0x040fffff` | Unmapped RCP | No mirror into SP. | Read/write stalls CPU. | `bus_unmapped_rcp_accesses_stall_instead_of_returning_old_data`, `physical_bus_supported_window_edges_do_not_fall_into_adjacent_stalls` |
| `0x04100000..0x041fffff` | DPC / RDP command registers | Low `0x20` bytes repeat throughout the 1 MiB window. | RCP word-lane rules. START/END latch command ranges; STATUS control pairs change source/freeze/flush/counter state. | `physical_bus_rcp_register_blocks_apply_their_documented_decode_masks`, `rdp_start_end_masking_and_start_valid_latch`, `rdp_status_control_pairs_and_dps_test_data_masks`, `rdp_freeze_defers_commands_until_clear`, `rdp_xbus_wraps_dmem_while_current_tracks_unmasked_range` |
| `0x04200000..0x042fffff` | DPS test registers | Only offsets `0x00..0x0c` decode; the rest of the window reads zero and ignores writes. | Test mode/address/data masking and state are handled by the RDP test-register path. | `rdp_status_control_pairs_and_dps_test_data_masks`, `physical_bus_dps_only_decodes_the_first_four_test_registers` |
| `0x04300000..0x043fffff` | MI | Low `0x10` bytes repeat. | RCP lanes; MODE command pairs, VERSION/INTR read-only behavior, interrupt-mask command pairs. | `mi_mode_set_clear_pairs_apply_in_bus_order_and_ignore_reserved_bits`, `mi_register_mirrors_and_subword_lanes_use_the_rcp_word_bus`, `mi_simultaneous_interrupt_sources_and_masks_remain_independent` |
| `0x04400000..0x044fffff` | VI | Low `0x40` bytes repeat. | Register-specific masks; writing CURRENT acknowledges VI interrupt. VI control bit 16 is retained as the dither-restoration control while register readback remains the low 16 hardware bits. | `physical_bus_rcp_register_blocks_apply_their_documented_decode_masks`, `vi_clock_interlaced_fields_have_distinct_interrupt_comparisons`, `vi_interrupt_target_changes_do_not_raise_retroactive_interrupts`, `vi_register_changes_keep_tick_size_independence_with_refresh_and_dma`, `vi_scanout_dither_restoration_control_is_retained_without_changing_readback` |
| `0x04500000..0x045fffff` | AI | Low `0x20` bytes repeat. | All reads except STATUS return current DMA length. STATUS exposes FIFO/control state and acknowledges AI interrupt on write. Address/length/DAC rate/bitrate masks are applied on writes. | `physical_bus_rcp_register_blocks_apply_their_documented_decode_masks`, `ai_length_counts_stereo_samples_at_the_dac_rate`, `ai_fifo_promotion_raises_an_interrupt_and_preserves_elapsed_time`, `ai_address_carry_crosses_fifo_boundaries_and_full_fifo_writes_are_ignored`, `ai_divider_latches_the_latest_pending_value_with_register_masking` |
| `0x04600000..0x046fffff` | PI registers | Low `0x40` bytes repeat. | Register masks, busy/error/IRQ state, domain timing, DMA progress, and latch registers are described in [Peripheral interface](peripheral-interface.md). | `physical_bus_rcp_register_blocks_apply_their_documented_decode_masks`, `pi_domain_timing_registers_mask_unused_bits`, `pi_dma_busy_register_writes_set_error_without_retargeting_the_transfer`, `pi_dma_cpu_cartridge_io_uses_the_bus_without_retargeting_dma_progress` |
| `0x04700000..0x047fffff` | RI | Low `0x20` bytes repeat. | CURRENT_LOAD has mixed readback; ERROR write clears error latch; BANK_STATUS write invalidates rows/sets dirty state. | `physical_bus_rcp_register_blocks_apply_their_documented_decode_masks`, `memory_bus_current_load_and_select_enable_the_interface_together`, `ri_bank_status_write_clears_valid_and_sets_dirty_bits`, `ri_absent_expansion_memory_sets_ack_error_on_the_identity_path`, `ri_refresh_stalls_blocking_cpu_memory_requests_but_not_cache_hits_or_device_reads` |
| `0x04800000..0x048fffff` | SI registers | Low `0x20` bytes repeat. | DRAM address aligns to 8 bytes; PIF DMA addresses align to halfwords; STATUS write acknowledges SI IRQ. Reserved indexes remain inert. | `physical_bus_rcp_register_blocks_apply_their_documented_decode_masks`, `si_dma_read_uses_the_programmed_pif_address_and_wraps_at_two_kibibytes`, `si_dma_status_reports_the_transfer_direction_and_clears_on_completion`, `si_status_acknowledgement_does_not_cancel_a_pending_pif_store`, `physical_bus_supported_window_edges_do_not_fall_into_adjacent_stalls` |
| `0x04900000..0x04ffffff` | Unmapped RCP | No device mirror. | Read/write stalls CPU. | `bus_unmapped_rcp_accesses_stall_instead_of_returning_old_data`, `physical_bus_unmapped_stalls_preserve_mi_register_state` |
| `0x05000000..0x1fbfffff` | PI cartridge bus | Device response depends on selected cartridge/save hardware. Unmapped areas retain PI address/data latch behavior. | PI halfword-bus transactions provide CPU byte/half/word lanes. CPU stores drive the full RCP word; CPU reads advance PI cartridge address. | `bus_cart_subword_reads_follow_pi_halfword_bus_lanes`, `pi_bus_unmapped_cpu_reads_retain_the_address_phase_latch`, `pi_bus_cpu_stores_preserve_the_full_word_latch`, `sram_subword_cpu_stores_drive_a_full_word_and_respect_the_selected_window_end` |
| `0x1fc00000..0x1fcfffff` | PIF ROM/RAM through SI | The 2 KiB PIF image repeats throughout the 1 MiB window. PIF ROM can be locked out while RAM remains visible. | RCP word lanes; CPU stores use SI I/O busy/latch timing. | `bus_pif_address_window_mirrors_the_two_kibibyte_image`, `bus_pif_rom_lockout_hides_boot_code_until_reset`, `bus_pif_rom_is_immutable_but_ram_keeps_rcp_store_lane_behavior`, `si_pif_read_during_io_busy_returns_the_store_latch_once`; pinned `pifram: SB (offset 0)` through `pifram: LH` |
| `0x1fd00000..0x7fffffff` | PI cartridge bus | Same PI cartridge path as the lower cartridge window. | Same PI latch/device semantics. | `bus_pi_upper_address_window_reaches_the_cartridge_bus`, `physical_bus_supported_window_edges_do_not_fall_into_adjacent_stalls` |
| `0x80000000..0xffffffff` | Outside physical bus | No supported physical target. | Direct physical access stalls CPU. Virtual CPU accesses reach physical devices only after address translation. | `bus_unmapped_rcp_accesses_stall_instead_of_returning_old_data`, `physical_bus_supported_window_edges_do_not_fall_into_adjacent_stalls` |

## Register side-effect matrix

| Block | Registers / implemented behavior | Read/write details and reserved behavior | Tests |
| --- | --- | --- | --- |
| SP DMA/status | `MEM_ADDR`, `DRAM_ADDR`, `RD_LEN`, `WR_LEN`, `STATUS`, `DMA_FULL`, `DMA_BUSY`, `SEMAPHORE` | Address/length fields are masked on DMA start. STATUS uses paired clear/set commands; a simultaneous pair keeps the existing state. DMA_FULL/BUSY are read-only. Reading SEMAPHORE returns its old bit then sets it; any write clears it. | `rsp_status_pairs_and_semaphore`, `rsp_dma_alignment_wrap_queue_and_postincrement`, `rsp_dma_sp_to_rdram_wraps_inside_selected_bank`, `physical_bus_unsupported_doubleword_read_stalls_before_register_side_effects` |
| SP status window | `PC`, inert `IBIST` | PC write keeps `0xffc`; the supported CPU-visible read path exposes the modeled SP PC. | pinned `RSP PC REG`, `physical_bus_sp_memory_and_register_windows_mirror_to_their_last_cycle` |
| DPC | `START`, `END`, `CURRENT`, `STATUS`, `CLOCK`, `BUF_BUSY`, `PIPE_BUSY`, `TMEM_BUSY` | START/END addresses keep 8-byte alignment and 24-bit range. STATUS control bits change XBUS/freeze/flush and clear counters. Busy/current/counter registers are read-only through the CPU path. | `rdp_start_end_masking_and_start_valid_latch`, `rdp_status_control_pairs_and_dps_test_data_masks`, `rdp_partial_multiword_command_is_buffered_before_following_fullsync` |
| DPS | `TBIST`, `TEST_MODE`, `BUFTEST_ADDR`, `BUFTEST_DATA` | Test data is available only for enabled test addresses; unsupported indexes read zero and ignore writes. | `rdp_status_control_pairs_and_dps_test_data_masks` |
| MI | `MODE`, `VERSION`, `INTR`, `INTR_MASK` | MODE low seven bits are repeat length. Clear/set command pairs control repeat, EBUS, and RDRAM-register-select; set wins if both commands are present because commands are applied in bus order. MODE bit 11 lowers DP only. VERSION and INTR are read-only. MASK has six clear/set pairs for SP/SI/AI/VI/PI/DP with set winning simultaneous pairs. Bits outside implemented fields do not persist. | `mi_mode_set_clear_pairs_apply_in_bus_order_and_ignore_reserved_bits`, `mi_register_mirrors_and_subword_lanes_use_the_rcp_word_bus`, `mi_simultaneous_interrupt_sources_and_masks_remain_independent`, `memory_bus_repeat_byte_lane_uses_the_full_bus_word` |
| VI | `CONTROL`, `ORIGIN`, `WIDTH`, `V_INTR`, `CURRENT`, `BURST/TIMING`, `V_SYNC`, `H_SYNC`, `LEAP`, `H_START`, `V_START`, `V_BURST`, `X_SCALE`, `Y_SCALE` | Each stored field is masked to its hardware width. CURRENT read is live; CURRENT write acknowledges VI. The dither-restoration control is retained internally at bit 16 and intentionally omitted from normal CONTROL readback. | `vi_clock_progressive_interrupt_ignores_the_comparator_low_bit`, `vi_clock_zero_timing_values_do_not_use_default_periods`, `vi_scanout_dither_restoration_control_is_retained_without_changing_readback`, `vi_snapshot_blank_reserved_and_reversed_windows_are_black` |
| AI | `DRAM_ADDR`, `LEN`, `CONTROL`, `STATUS`, `DACRATE`, `BITRATE` | Reads of every index except STATUS mirror current LEN. FIFO-full writes are ignored. STATUS write lowers AI IRQ. DACRATE keeps 14 bits; BITRATE keeps four. | `ai_length_counts_stereo_samples_at_the_dac_rate`, `ai_final_buffer_completion_does_not_raise_an_interrupt`, `ai_fifo_promotion_raises_an_interrupt_and_preserves_elapsed_time`, `ai_divider_latches_the_latest_pending_value_with_register_masking` |
| PI | `DRAM_ADDR`, `CART_ADDR`, `RD_LEN`, `WR_LEN`, `STATUS`, two domain timing sets, two latch aliases | Address and length masks, busy-write error behavior, IRQ acknowledge/reset, progressive DMA visibility, and CPU cartridge overlap are covered separately. | [Peripheral interface](peripheral-interface.md), `pi_dma_completion_uses_programmed_bus_timing`, `pi_dma_busy_register_writes_set_error_without_retargeting_the_transfer`, `pi_domain_timing_registers_mask_unused_bits`, `pi_bus_unmapped_cpu_reads_retain_the_address_phase_latch` |
| RI | `MODE`, `CONFIG`, `CURRENT_LOAD`, `SELECT`, `REFRESH`, `LATENCY`, `ERROR`, `BANK_STATUS` | CURRENT_LOAD read mixes acknowledgement bit, constant bits 1/2, MODE bit 3, and SELECT bit 4. ERROR write clears errors. BANK_STATUS write invalidates open rows and marks banks dirty. | `memory_bus_current_load_and_select_enable_the_interface_together`, `ri_bank_status_write_clears_valid_and_sets_dirty_bits`, `ri_absent_expansion_memory_sets_ack_error_on_the_identity_path`, `ri_refresh_uses_the_selected_clean_or_dirty_delay` |
| SI | `DRAM_ADDR`, `PIF_ADDR_RD64B`, reserved indexes 2/3, `PIF_ADDR_WR64B`, reserved index 5, `STATUS` | Starting read/write DMA records direction-specific phase bits. STATUS exposes busy/phase/IRQ state and write acknowledges IRQ. Reserved registers read zero and ignore writes. | `si_dma_read_uses_the_programmed_pif_address_and_wraps_at_two_kibibytes`, `si_dma_status_reports_the_transfer_direction_and_clears_on_completion`, `si_status_acknowledgement_does_not_cancel_a_pending_pif_store` |

## MI interrupt aggregation

MI keeps six independent source lines and six independent mask bits in SP, SI,
AI, VI, PI, DP order. `MI_INTR` reports source lines regardless of masking;
`MI_INTR_MASK` reports masks. The CPU RCP interrupt is asserted whenever at least
one line is both asserted and unmasked. Masking a live source does not clear the
source, so later unmasking exposes the still-active interrupt. Clearing DP through
`MI_MODE` does not acknowledge any other source. These simultaneous-source rules
are covered by `mi_simultaneous_interrupt_sources_and_masks_remain_independent`;
individual device acknowledgement paths are covered by `rsp_status_pairs_and_semaphore`,
`si_status_acknowledgement_does_not_cancel_a_pending_pif_store`,
`ai_fifo_promotion_raises_an_interrupt_and_preserves_elapsed_time`, VI timing
tests, PI timing/status tests, and `rdp_bus_construction_and_reset_leave_dp_interrupt_clear`.

## Known limits

The matrix describes hardware behavior currently represented by the emulator and
its pinned tests. It does not add per-edge PI cartridge timing or shared RDRAM
arbitration beyond the granularity documented in [Peripheral interface](peripheral-interface.md).
SP PC write masking is verified; nondeterministic PC values while the RSP is
actively running are not modeled as a separate physical-bus feature. SI normal
DMA/I/O phase bits are represented, while error/read-pending states that are not
generated by the supported SI command paths remain zero. Unsupported cached
accesses to non-RDRAM hardware and non-RDRAM 64-bit loads are modeled as CPU bus
stalls rather than host faults; those paths are explicitly regression-tested so
they do not execute target side effects or crash the host.
