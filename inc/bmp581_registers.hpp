/**
 * @file bmp581_registers.hpp
 * @brief BMP581 register map: addresses, bit fields and reset values (datasheet section 7).
 * @copyright Copyright (c) 2026 The driver-bmp581 authors. SPDX-License-Identifier: MIT
 *
 * @details
 * Registers are 8 bits wide with 7-bit addresses. A read burst increments the address with every
 * byte, except at FIFO_DATA, which a burst reads again and again (section 5.4). The 24-bit results
 * are little-endian: XLSB at the lowest address (sections 7.11 to 7.16).
 *
 * Section, table and figure numbers refer to the Bosch BMP581 datasheet, document
 * BST-BMP581-DS004-13, revision 1.13.
 */
#ifndef BMP581_REGISTERS_HPP
#define BMP581_REGISTERS_HPP

#include <cstdint>

#include "bmp581_detail.hpp"

/** @brief The BMP581 register map. */
namespace bmp581::reg {

using detail::operator""_u8;
using detail::operator""_u32;

/**
 * @defgroup bmp581_registers Register map
 * @brief Addresses, fields and reset values.
 * @{
 */

// Addresses (section 7, register map).
inline constexpr uint8_t kChipId = 0x01;      ///< CHIP_ID (R): 0x50 (section 7.1).
inline constexpr uint8_t kRevId = 0x02;       ///< REV_ID (R): ASIC revision, 0x32 (section 7.2).
inline constexpr uint8_t kChipStatus = 0x11;  ///< CHIP_STATUS (R): interface mode; read clears I3C
                                              ///< error flags (section 7.3).
inline constexpr uint8_t kDriveConfig = 0x13; ///< DRIVE_CONFIG (R/W, NVM-backed): SPI 3-wire, CSB
                                              ///< pull-up, pad drive (section 7.4).
inline constexpr uint8_t kIntConfig = 0x14;   ///< INT_CONFIG (R/W): the INT pin (section 7.5).
inline constexpr uint8_t kIntSource = 0x15;   ///< INT_SOURCE (R/W): interrupt sources (7.6).
inline constexpr uint8_t kFifoConfig = 0x16;  ///< FIFO_CONFIG (R/W): threshold, mode (7.7).
inline constexpr uint8_t kFifoCount = 0x17;   ///< FIFO_COUNT (R): frames in the FIFO (7.8).
inline constexpr uint8_t kFifoSel = 0x18;     ///< FIFO_SEL (R/W): frame type, decimation (7.9).
inline constexpr uint8_t kReserved0 = 0x1C;   ///< Reserved (R): always reads 0x00 (7.10).
inline constexpr uint8_t kTempXlsb = 0x1D;    ///< TEMP_DATA_XLSB (R): temperature bits 7..0.
inline constexpr uint8_t kTempLsb = 0x1E;     ///< TEMP_DATA_LSB (R): temperature bits 15..8.
inline constexpr uint8_t kTempMsb = 0x1F;     ///< TEMP_DATA_MSB (R): temperature bits 23..16.
inline constexpr uint8_t kPressXlsb = 0x20;   ///< PRESS_DATA_XLSB (R): pressure bits 7..0.
inline constexpr uint8_t kPressLsb = 0x21;    ///< PRESS_DATA_LSB (R): pressure bits 15..8.
inline constexpr uint8_t kPressMsb = 0x22;    ///< PRESS_DATA_MSB (R): pressure bits 23..16.
inline constexpr uint8_t kReserved1 = 0x23;   ///< Reserved (R): always reads 0x00 (7.17).
inline constexpr uint8_t kReserved4 = 0x26;   ///< Reserved (R): always reads 0x00 (7.20).
inline constexpr uint8_t kIntStatus = 0x27;   ///< INT_STATUS (R): clear-on-read (7.21).
inline constexpr uint8_t kStatus = 0x28;      ///< STATUS (R): NVM ready and errors (7.22).
inline constexpr uint8_t kFifoData = 0x29;    ///< FIFO_DATA (R): FIFO output port (7.23).
inline constexpr uint8_t kNvmAddr = 0x2B;     ///< NVM_ADDR (R/W) (7.24).
inline constexpr uint8_t kNvmDataLsb = 0x2C;  ///< NVM_DATA_LSB (R/W) (7.25).
inline constexpr uint8_t kNvmDataMsb = 0x2D;  ///< NVM_DATA_MSB (R/W) (7.26).
inline constexpr uint8_t kDspConfig = 0x30;   ///< DSP_CONFIG (R/W): compensation, IIR routing.
inline constexpr uint8_t kDspIir = 0x31;      ///< DSP_IIR (R/W): IIR coefficients (7.28).
inline constexpr uint8_t kOorThrPLsb = 0x32;  ///< OOR_THR_P_LSB (R/W): reference bits 7..0.
inline constexpr uint8_t kOorThrPMsb = 0x33;  ///< OOR_THR_P_MSB (R/W): reference bits 15..8.
inline constexpr uint8_t kOorRange = 0x34;    ///< OOR_RANGE (R/W): window, Pa (7.31).
inline constexpr uint8_t kOorConfig = 0x35;   ///< OOR_CONFIG (R/W): reference bit 16, count (7.32).
inline constexpr uint8_t kOsrConfig = 0x36;   ///< OSR_CONFIG (R/W): oversampling, press_en (7.33).
inline constexpr uint8_t kOdrConfig = 0x37;   ///< ODR_CONFIG (R/W): power mode, ODR (7.34).
inline constexpr uint8_t kOsrEff = 0x38;      ///< OSR_EFF (R): effective oversampling (7.35).
inline constexpr uint8_t kCmd = 0x7E;         ///< CMD (W): soft reset and NVM sequences (7.36).

// Reset values (section 7, register map), and the values the chip reports at reset.
inline constexpr uint8_t kChipIdValue = 0x50;     ///< CHIP_ID of every BMP581 (section 7.1).
inline constexpr uint8_t kIntConfigReset = 0x35;  ///< INT_CONFIG: latched, active low, open
                                                  ///< drain, pin disabled, pad drive 3.
inline constexpr uint8_t kDspConfigReset = 0x03;  ///< DSP_CONFIG: both compensated, IIR off.
inline constexpr uint8_t kOsrConfigReset = 0x00;  ///< OSR_CONFIG: x1, x1, pressure off.
inline constexpr uint8_t kOdrConfigReset = 0x70;  ///< ODR_CONFIG: standby, 1 Hz, deep standby on.
inline constexpr uint8_t kStatusReset = 0x02;     ///< STATUS: NVM ready (section 7.22).
inline constexpr uint32_t kDataReset = 0x7F7F7FU; ///< TEMP_DATA and PRESS_DATA after reset: no
                                                  ///< measurement yet (sections 7.11 to 7.16).
inline constexpr uint8_t kFifoEmpty = 0x7F; ///< FIFO_DATA of an empty or disabled FIFO (4.6.3).
inline constexpr uint8_t kIntStatusAfterReset = 0x10; ///< INT_STATUS after a power-on or soft
                                                      ///< reset: POR only (section 4.3.9).

// CMD values (section 7.36).
inline constexpr uint8_t kCmdSoftReset = 0xB6; ///< Soft reset; over I2C the data byte is NACKed.

// INT_CONFIG (section 7.5).
using IntConfigMode = detail::Field<uint8_t, 0U, 1U>;   ///< int_mode: 0 pulsed, 1 latched.
using IntConfigPol = detail::Field<uint8_t, 1U, 1U>;    ///< int_pol: 0 active low, 1 high.
using IntConfigOd = detail::Field<uint8_t, 2U, 1U>;     ///< int_od: 0 push-pull, 1 open drain.
using IntConfigEn = detail::Field<uint8_t, 3U, 1U>;     ///< int_en: the INT pin enabled.
using IntConfigPadDrv = detail::Field<uint8_t, 4U, 4U>; ///< pad_int_drv: INT pad drive.
// INT_SOURCE and INT_STATUS share the bit positions of the four sources (sections 7.6, 7.21).
inline constexpr uint8_t kIntDrdy = 1U << 0U;     ///< Data ready (drdy_data_reg).
inline constexpr uint8_t kIntFifoFull = 1U << 1U; ///< FIFO full.
inline constexpr uint8_t kIntFifoThs = 1U << 2U;  ///< FIFO threshold reached.
inline constexpr uint8_t kIntOorP = 1U << 3U;     ///< Pressure out of range.
inline constexpr uint8_t kIntPor = 1U << 4U;      ///< INT_STATUS only: POR or soft reset complete.
inline constexpr uint8_t kIntSourceMask = 0x0FU;  ///< INT_SOURCE bits 3..0 (7..4 reserved).
inline constexpr uint8_t kIntStatusMask = 0x1FU;  ///< INT_STATUS bits 4..0 (7..5 reserved).

// FIFO_CONFIG, FIFO_COUNT, FIFO_SEL (sections 7.7 to 7.9).
using FifoThreshold = detail::Field<uint8_t, 0U, 5U>;  ///< fifo_threshold: 0 off, 1 to 31 frames.
using FifoModeBit = detail::Field<uint8_t, 5U, 1U>;    ///< fifo_mode: 0 streaming, 1 stop on full.
using FifoCountField = detail::Field<uint8_t, 0U, 6U>; ///< fifo_count: frames in the FIFO.
using FifoFrameSel = detail::Field<uint8_t, 0U, 2U>;   ///< fifo_frame_sel: off, T, P, P and T.
using FifoDecSel = detail::Field<uint8_t, 2U, 3U>;     ///< fifo_dec_sel: keep every 2^n-th frame.
inline constexpr uint8_t kFifoConfigMask = 0x3FU;      ///< FIFO_CONFIG bits 5..0 (7..6 reserved).
inline constexpr uint8_t kFifoSelMask = 0x1FU;         ///< FIFO_SEL bits 4..0 (7..5 reserved).

// STATUS (section 7.22). Bit 0 is status_core_rdy in the field list but reserved_0 in the register
// map, and its reset value is 0 while the chip answers: the driver does not use it.
inline constexpr uint8_t kStatusNvmRdy = 1U << 1U;    ///< status_nvm_rdy: ready for NVM operations.
inline constexpr uint8_t kStatusNvmErr = 1U << 2U;    ///< status_nvm_err: NVM error.
inline constexpr uint8_t kStatusNvmCmdErr = 1U << 3U; ///< status_nvm_cmd_err: NVM command error.
inline constexpr uint8_t kStatusReserved = 0x60U;     ///< reserved_6_5: read 0.

// DSP_CONFIG (section 7.27).
using DspCompPtEn = detail::Field<uint8_t, 0U, 2U>;     ///< comp_pt_en: 0b11 both compensated.
inline constexpr uint8_t kDspIirFlushForced = 1U << 2U; ///< iir_flush_forced_en.
inline constexpr uint8_t kDspShdwSelIirT = 1U << 3U;    ///< Temperature registers after the IIR.
inline constexpr uint8_t kDspFifoSelIirT = 1U << 4U;    ///< Temperature FIFO after the IIR.
inline constexpr uint8_t kDspShdwSelIirP = 1U << 5U;    ///< Pressure registers after the IIR.
inline constexpr uint8_t kDspFifoSelIirP = 1U << 6U;    ///< Pressure FIFO after the IIR.
inline constexpr uint8_t kDspOorSelIirP = 1U << 7U;     ///< Out-of-range check after the IIR.
inline constexpr uint8_t kDspShdwSelIir = 0x28U;        ///< Both data registers after the IIR.
inline constexpr uint8_t kDspFifoSelIir = 0x50U;        ///< Both FIFO values after the IIR.
static_assert(kDspShdwSelIir ==
              (static_cast<uint32_t>(kDspShdwSelIirT) | static_cast<uint32_t>(kDspShdwSelIirP)));
static_assert(kDspFifoSelIir ==
              (static_cast<uint32_t>(kDspFifoSelIirT) | static_cast<uint32_t>(kDspFifoSelIirP)));

// DSP_IIR (section 7.28). The register map puts set_iir_t in bits 2..0 and set_iir_p in bits 5..3;
// the field list of section 7.28 gives both "bit offset 0" and marks bits 5..3 read-only. The
// driver follows the register map; the read-back after each write would show the other reading as
// WriteMismatch (**not verified on hardware**).
using DspIirT = detail::Field<uint8_t, 0U, 3U>; ///< set_iir_t: temperature IIR coefficient.
using DspIirP = detail::Field<uint8_t, 3U, 3U>; ///< set_iir_p: pressure IIR coefficient.
inline constexpr uint8_t kDspIirMask = 0x3FU;   ///< DSP_IIR bits 5..0.

// OOR_CONFIG (section 7.32).
using OorThrP16 = detail::Field<uint8_t, 0U, 1U>; ///< oor_thr_p_16: reference bit 16.
using OorCntLim = detail::Field<uint8_t, 6U, 2U>; ///< cnt_lim: 1, 3, 7 or 15 samples.
inline constexpr uint8_t kOorConfigMask = 0xC1U;  ///< OOR_CONFIG bits 7..6 and 0 (5..1 reserved).

// OSR_CONFIG (section 7.33).
using OsrT = detail::Field<uint8_t, 0U, 3U>;     ///< osr_t: 2^n temperature oversampling.
using OsrP = detail::Field<uint8_t, 3U, 3U>;     ///< osr_p: 2^n pressure oversampling.
inline constexpr uint8_t kOsrPressEn = 1U << 6U; ///< press_en: pressure measured as well.
inline constexpr uint8_t kOsrReserved = 0x80U;   ///< reserved_7: reads 0.

// ODR_CONFIG (section 7.34).
using OdrPwrMode = detail::Field<uint8_t, 0U, 2U>; ///< pwr_mode: what the chip does now.
using OdrOdr = detail::Field<uint8_t, 2U, 5U>;     ///< odr: output data rate code.
inline constexpr uint8_t kOdrDeepDis = 1U << 7U;   ///< deep_dis: deep standby disabled.

// OSR_EFF (section 7.35).
using OsrEffT = detail::Field<uint8_t, 0U, 3U>;      ///< osr_t_eff.
using OsrEffP = detail::Field<uint8_t, 3U, 3U>;      ///< osr_p_eff.
inline constexpr uint8_t kOsrEffOdrValid = 1U << 7U; ///< odr_is_valid.
inline constexpr uint8_t kOsrEffReserved = 1U << 6U; ///< reserved_6: reads 0.

// The typed fields are the register map above, proved at build time (KB-CPP20-012).
static_assert(IntConfigPadDrv::kMask == 0xF0U);
static_assert(IntConfigMode::Extract(kIntConfigReset) == 1U); // latched
static_assert(IntConfigOd::Extract(kIntConfigReset) == 1U);   // open drain
static_assert(IntConfigEn::Extract(kIntConfigReset) == 0U);   // pin disabled
static_assert(IntConfigPadDrv::Extract(kIntConfigReset) == 3U);
static_assert((FifoThreshold::kMask | FifoModeBit::kMask) == kFifoConfigMask);
static_assert((FifoFrameSel::kMask | FifoDecSel::kMask) == kFifoSelMask);
static_assert((DspIirT::kMask | DspIirP::kMask) == kDspIirMask);
static_assert((OorThrP16::kMask | OorCntLim::kMask) == kOorConfigMask);
static_assert(OdrOdr::Extract(kOdrConfigReset) == 0x1CU);  // 1 Hz
static_assert(OdrPwrMode::Extract(kOdrConfigReset) == 0U); // standby
static_assert(DspCompPtEn::Extract(kDspConfigReset) == 3U);
static_assert(OsrP::Insert(0x00_u8, 7_u32) == 0x38U);
static_assert(OsrP::Extract(OsrP::Insert(0xFF_u8, 2_u32)) == 2U);
static_assert((static_cast<uint32_t>(kIntDrdy) | static_cast<uint32_t>(kIntFifoFull) |
               static_cast<uint32_t>(kIntFifoThs) | static_cast<uint32_t>(kIntOorP)) ==
              static_cast<uint32_t>(kIntSourceMask));

/** @} */

} // namespace bmp581::reg

#endif // BMP581_REGISTERS_HPP
