/**
 * @file bmp581.ipp
 * @brief BMP581 driver implementation, included by `bmp581.hpp`.
 * @copyright Copyright (c) 2026 The driver-bmp581 authors. SPDX-License-Identifier: MIT
 *
 * Section, table and figure numbers refer to the Bosch BMP581 datasheet, document
 * BST-BMP581-DS004-13, revision 1.13.
 */
#ifndef BMP581_IPP
#define BMP581_IPP

namespace bmp581 {

namespace detail {

/// INT_STATUS bits 7..5 are reserved and read 0 (section 7.21).
inline constexpr uint8_t kIntStatusReserved = 0xE0U;
/// FIFO_COUNT bits 7..6 are reserved and read 0 (section 7.8).
inline constexpr uint8_t kFifoCountReserved = 0xC0U;

/// Whether `ReadRegister()` may read `address`: an implemented register whose read changes nothing
/// the driver relies on (FIFO_DATA and CMD are refused).
[[nodiscard]] constexpr bool RawReadable(uint8_t a) noexcept {
  return (a == reg::kChipId) || (a == reg::kRevId) || (a == reg::kChipStatus) ||
         ((a >= reg::kDriveConfig) && (a <= reg::kFifoSel)) ||
         ((a >= reg::kReserved0) && (a <= reg::kStatus)) ||
         ((a >= reg::kNvmAddr) && (a <= reg::kNvmDataMsb)) ||
         ((a >= reg::kDspConfig) && (a <= reg::kOsrEff));
}

/// Whether INT_CONFIG to FIFO_SEL read back as written; FIFO_COUNT only has its reserved bits
/// checked (it counts frames).
[[nodiscard]] constexpr bool BlockAMatches(std::span<const uint8_t, 5> got,
                                           const Config& c) noexcept {
  const std::array<uint8_t, 5> want = ExpectedBlockA(c);
  return (got[0] == want[0]) && (got[1] == want[1]) && (got[2] == want[2]) &&
         !Has(got[3], kFifoCountReserved) && (got[4] == want[4]);
}

/// Whether DSP_CONFIG to ODR_CONFIG read back as written, in standby.
[[nodiscard]] constexpr bool BlockBMatches(std::span<const uint8_t, 8> got,
                                           const Config& c) noexcept {
  const std::array<uint8_t, 8> want = ExpectedBlockB(c);
  bool same = true;
  for (std::size_t i = 0U; i < want.size(); ++i) {
    same = same && (got[i] == want[i]);
  }
  return same;
}

/// ODR_CONFIG without pwr_mode, which the chip changes on its own (a forced measurement ends).
[[nodiscard]] constexpr uint8_t OdrWithoutMode(uint8_t odr) noexcept {
  return reg::OdrPwrMode::Insert(odr, 0_u32);
}

/// The wait before the first poll of a forced measurement: the typical conversion less 5 % (table
/// 4), the earliest the chip can be done.
[[nodiscard]] constexpr uint32_t ForcedFirstWaitUs(const Config& c) noexcept {
  const uint32_t typical =
      kConvPressureUs[static_cast<uint32_t>(c.pressure_oversampling) & 0x07U] +
      kConvTemperatureUs[static_cast<uint32_t>(c.temperature_oversampling) & 0x07U];
  return (typical * 95U) / 100U;
}

static_assert(RawReadable(reg::kIntStatus) && !RawReadable(reg::kFifoData) &&
              !RawReadable(reg::kCmd) && !RawReadable(0x00U) && RawReadable(reg::kOsrEff));
static_assert(ForcedFirstWaitUs(Config{}) == 1900U);
static_assert(OdrWithoutMode(0x71U) == reg::kOdrConfigReset);

} // namespace detail

// -------------------------------------------------------------------------------------------------
// Lifecycle
// -------------------------------------------------------------------------------------------------

template <BusPort Port>
Result<void> BMP581<Port>::EnsureInitialized() noexcept {
  if (initialized_) {
    // Not a shortcut over a chip that may have reset: the reset check runs first (one read). A
    // configuration in doubt starts again from the soft reset.
    const auto r = CheckForReset();
    if ((r && config_known_) || (!r && (r.error != Error::DeviceReset))) {
      return r;
    }
  }
  if (auto r = initialize(); !r) {
    initialized_ = false;
    return r;
  }
  initialized_ = true;
  detail::Log(bus_.GetPort(), LogLevel::Info, "bmp581: initialised");
  return Result<void>::Success();
}

template <BusPort Port>
Result<void> BMP581<Port>::initialize() noexcept {
  using detail::operator""_u8;
  if (Validate(config_) != Error::Ok) {
    return fail(Error::InvalidArgument, "bmp581: invalid Config");
  }
  if (!bus_.AddressValid()) {
    return fail(Error::InvalidArgument, "bmp581: invalid I2C address");
  }
  if (!bus_.GetPort().EnsureInitialized()) {
    return fail(Error::Bus, "bmp581: the port could not bring the bus up");
  }
  initialized_ = false;
  config_known_ = false;
  check_pending_ = false;
  por_unconfirmed_ = false;
  // t_powup: no communication before it (section 4.3.9, table 4).
  detail::DelayUs(bus_.GetPort(), kPowerUpUs);
  // From the first write on the chip may measure: standby is never refused after this.
  started_once_ = true;
  if (auto r = resetChip(); !r) {
    return r;
  }
  if (auto r = identify(); !r) {
    return r;
  }
  if (auto r = waitNvmReady(); !r) {
    return r;
  }
  if (auto r = checkResetFlags(); !r) {
    return r;
  }
  latched_int_ = 0;      // the por of the driver's own soft reset is not news
  marker_fresh_ = false; // the reset cleared it: configure() writes it again
  const Config config = config_;
  // After a reset the chip is in deep standby (section 4.3): configure() leaves it first.
  return configure(config, reg::kOdrConfigReset);
}

template <BusPort Port>
Result<void> BMP581<Port>::ensureMarker() noexcept {
  // In standby with deep standby off, after the call's reset check and before the configuration:
  // no NVM download can follow until the sequence ends (it comes with a measurement from deep
  // standby, section 7.22). NVM_DATA is read; anything but the marker (0x00 0x00 of a reset or of
  // an unprogrammed NVM word included) is overwritten at once. A 0x00 0x00 at the read-back is
  // then a reset during the sequence. Inert without an NVM command in CMD (section 4.8.1).
  // While the marker is fresh, 0x00 0x00 here is a reset after the call's reset check (confirmed
  // by a second read, markerCleared()): it is reported, never written over.
  if (marker_fresh_) {
    const auto cleared = markerCleared();
    if (!cleared) {
      return fail(cleared.error, "bmp581: NVM_DATA read failed");
    }
    if (cleared.value) {
      return fail(markReset(), "bmp581: the chip reset");
    }
  } else if (const Error e = read(reg::kNvmDataLsb, detail::kMarker.size()); e != Error::Ok) {
    return fail(e, "bmp581: NVM_DATA read failed");
  } else {
    // Read: compared below.
  }
  if (marker_fresh_ || ((buffer_[0] == detail::kMarker[0]) && (buffer_[1] == detail::kMarker[1]))) {
    marker_fresh_ = true;
    return Result<void>::Success();
  }
  for (std::size_t i = 0U; i < detail::kMarker.size(); ++i) {
    if (const Error e = write(static_cast<uint8_t>(reg::kNvmDataLsb + i), detail::kMarker[i]);
        e != Error::Ok) {
      return fail(e, "bmp581: NVM_DATA write failed");
    }
  }
  marker_fresh_ = true;
  if (initialized_) {
    // The marker was rewritten: a reset between the call's reset check and this write would hide
    // behind it. The sentinel, which the sequence has not written yet, still shows such a reset.
    if (const auto c = checkSentinel(); !c) {
      return fail(c.error, "bmp581: reset check failed");
    }
  }
  return Result<void>::Success();
}

template <BusPort Port>
Result<void> BMP581<Port>::wake(const Config& config) noexcept {
  // ODR_CONFIG = standby with deep_dis = 1, then t_standby: the chip leaves deep standby (sections
  // 4.3.2, 4.3.7). The datasheet gives no other time for it.
  if (const Error e = write(reg::kOdrConfig, detail::OdrAwakeStandby(config)); e != Error::Ok) {
    return fail(e, "bmp581: ODR_CONFIG write failed");
  }
  detail::DelayUs(bus_.GetPort(), kStandbyUs);
  return Result<void>::Success();
}

template <BusPort Port>
Result<void> BMP581<Port>::resetChip() noexcept {
  // SPI: the dummy read that leaves I2C mode (section 5.1); nothing on I2C.
  if (const Error e = bus_.SelectInterface(); e != Error::Ok) {
    return fail(e, "bmp581: the interface selection read failed");
  }
  leaveDeepStandby();
  // CMD = 0xB6 (section 4.3.10). Over I2C the chip does not acknowledge it (section 7.36).
  if (const Error e = bus_.WriteResetCommand(); e != Error::Ok) {
    return fail(e, "bmp581: the soft reset write failed");
  }
  detail::DelayUs(bus_.GetPort(), kSoftResetUs); // t_soft_res, table 4
  // A soft reset returns the interface to I2C mode (table 16): select SPI again.
  if (const Error e = bus_.SelectInterface(); e != Error::Ok) {
    return fail(e, "bmp581: the interface selection read failed");
  }
  return Result<void>::Success();
}

template <BusPort Port>
void BMP581<Port>::leaveDeepStandby() noexcept {
  // The datasheet does not say that deep standby takes CMD: before the soft reset, ODR_CONFIG =
  // standby with deep_dis = 1 and t_standby (sections 4.3.1, 4.3.2). Harmless in any mode; its
  // result is not needed, since INT_STATUS proves the reset that follows.
  (void)write(reg::kOdrConfig, detail::OdrAwakeStandby(config_));
  detail::DelayUs(bus_.GetPort(), kStandbyUs);
}

template <BusPort Port>
Result<void> BMP581<Port>::identify() noexcept {
  // CHIP_ID and REV_ID in one read; CHIP_ID must not be 0 (section 4.3.9) and is 0x50 (7.1).
  if (const Error e = read(reg::kChipId, std::size_t{2U}); e != Error::Ok) {
    return fail(e, "bmp581: CHIP_ID read failed");
  }
  if (buffer_[0] != kChipId) {
    return fail(Error::DeviceIdMismatch, "bmp581: CHIP_ID is not 0x50");
  }
  revision_ = buffer_[1];
  return Result<void>::Success();
}

template <BusPort Port>
Result<void> BMP581<Port>::waitNvmReady() noexcept {
  // STATUS.status_nvm_rdy = 1 and status_nvm_err = 0 (section 4.3.9): a correct CHIP_ID is not
  // readiness. Polled with a deadline from Config, validated against kTimeoutMaxMs (KB-CPP-124).
  const uint32_t polls = ((config_.startup_timeout_ms * 1000U) / kPollUs) + 1U;
  for (uint32_t i = 0U; i < polls; ++i) {
    const auto status = readByte(reg::kStatus);
    if (!status) {
      return fail(status.error, "bmp581: STATUS read failed");
    }
    last_status_ = status.value;
    if (detail::Has(status.value, reg::kStatusReserved)) {
      return fail(Error::InvalidReply, "bmp581: STATUS reserved bits set");
    }
    if (detail::Has(status.value, reg::kStatusNvmRdy)) {
      if (detail::Has(status.value,
                      static_cast<uint8_t>(static_cast<uint32_t>(reg::kStatusNvmErr) |
                                           static_cast<uint32_t>(reg::kStatusNvmCmdErr)))) {
        return fail(Error::DeviceFault, "bmp581: NVM error at start-up");
      }
      return Result<void>::Success();
    }
    detail::DelayUs(bus_.GetPort(), kPollUs);
  }
  return fail(Error::Timeout, "bmp581: NVM not ready");
}

template <BusPort Port>
Result<void> BMP581<Port>::checkResetFlags() noexcept {
  // INT_STATUS = 0x10 after the reset (section 4.3.9), then 0x00: the read cleared it. A bit the
  // chip toggles, which no stuck, floating or looped-back line can show (KB-CPP-117).
  const auto first = readByte(reg::kIntStatus);
  if (!first) {
    return fail(first.error, "bmp581: INT_STATUS read failed");
  }
  last_int_status_ = first.value;
  if (first.value != reg::kIntStatusAfterReset) {
    return fail(Error::DeviceIdMismatch, "bmp581: INT_STATUS does not show the soft reset");
  }
  const auto second = readByte(reg::kIntStatus);
  if (!second) {
    return fail(second.error, "bmp581: INT_STATUS read failed");
  }
  last_int_status_ = second.value;
  if (second.value != 0U) {
    return fail(Error::DeviceIdMismatch, "bmp581: INT_STATUS did not clear on read");
  }
  return Result<void>::Success();
}

template <BusPort Port>
Result<void> BMP581<Port>::Deinitialize() noexcept {
  if (!bus_.AddressValid()) {
    return fail(Error::InvalidArgument, "bmp581: invalid I2C address");
  }
  initialized_ = false;
  config_known_ = false;
  check_pending_ = false;
  por_unconfirmed_ = false;
  if (!bus_.GetPort().EnsureInitialized()) {
    return fail(Error::Bus, "bmp581: the port could not bring the bus up");
  }
  // On SPI the chip may be back in I2C mode after a reset: select SPI first, whatever the result,
  // then the soft reset; the por it sets proves it arrived.
  (void)bus_.SelectInterface();
  leaveDeepStandby();
  if (const Error e = bus_.WriteResetCommand(); e != Error::Ok) {
    return fail(e, "bmp581: the soft reset write failed");
  }
  detail::DelayUs(bus_.GetPort(), kSoftResetUs); // t_soft_res, table 4
  (void)bus_.SelectInterface();
  const auto status = readByte(reg::kIntStatus);
  if (!status) {
    return fail(status.error, "bmp581: INT_STATUS read failed");
  }
  last_int_status_ = status.value;
  if (detail::Has(status.value, detail::kIntStatusReserved) ||
      !detail::Has(status.value, reg::kIntPor)) {
    return fail(Error::WriteMismatch, "bmp581: the soft reset did not happen");
  }
  detail::Log(bus_.GetPort(), LogLevel::Info, "bmp581: deinitialised");
  return Result<void>::Success();
}

template <BusPort Port>
Result<void> BMP581<Port>::ApplyConfig(const Config& config) noexcept {
  if (Validate(config) != Error::Ok) {
    return fail(Error::InvalidArgument, "bmp581: invalid Config");
  }
  if (!initialized_) {
    config_ = config;
    return Result<void>::Success();
  }
  if (auto g = gate(); !g) {
    return g;
  }
  // The reset check before any write: nothing cached is written over a reset chip (rule 12a).
  const auto sentinel = checkBeforeWrite();
  if (!sentinel) {
    return fail(sentinel.error, "bmp581: reset check failed");
  }
  return configure(config, sentinel.value);
}

template <BusPort Port>
Result<void> BMP581<Port>::CheckForReset() noexcept {
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  check_pending_ = true;
  if (const Error e = settle(Error::Ok); e != Error::Ok) {
    return fail(e, "bmp581: reset check failed");
  }
  return Result<void>::Success();
}

// -------------------------------------------------------------------------------------------------
// Configuration and power modes
// -------------------------------------------------------------------------------------------------

template <BusPort Port>
Result<void> BMP581<Port>::configure(const Config& config, uint8_t odr) noexcept {
  // From here until the read-back, the chip may hold any mix of the old and the new values.
  config_ = config;
  config_known_ = false;
  // Every register takes a write in standby only, and deep standby need not take them (sections
  // 4.3.1, 4.3.8): from a measuring mode to standby, or out of a possible deep standby, with
  // deep_dis = 1 either way. ODR_CONFIG gets the configured deep_dis last (ConfigWrites()).
  Config awake = config;
  awake.deep_standby = false;
  if (reg::OdrPwrMode::Extract(odr) != 0U) {
    if (auto r = enterStandby(awake); !r) {
      return r;
    }
  } else if (!detail::Has(odr, reg::kOdrDeepDis)) {
    if (auto r = wake(config); !r) {
      return r;
    }
  } else {
    // In standby with deep standby off already: nothing to do.
  }
  if (auto r = ensureMarker(); !r) {
    return r;
  }
  // From the first configuration write on (writeConfig() sets sentinel_doubt_), a reset the driver
  // has not seen may be covered by the sequence's own OSR_CONFIG write (afterFailedSequence()).
  if (auto r = writeConfig(config); !r) {
    return afterFailedSequence(r.error);
  }
  if (auto r = readBackConfig(config); !r) {
    return afterFailedSequence(r.error);
  }
  // A reset during the sequence leaves por set; the sequence itself may have written OSR_CONFIG
  // over it, so por here is taken as the reset (the safe direction for a sensor: uninitialised).
  const auto status = readIntStatus();
  if (!status) {
    return afterFailedSequence(status.error);
  }
  if (detail::Has(status.value, reg::kIntPor)) {
    return fail(markReset(), "bmp581: the chip reset during configuration");
  }
  sentinel_doubt_ = false;
  if (config.mode != PowerMode::Standby) {
    if (auto r = enterMode(config, config.mode); !r) {
      return r;
    }
  }
  waitFifoFlush();
  config_known_ = true;
  return Result<void>::Success();
}

template <BusPort Port>
void BMP581<Port>::waitFifoFlush() noexcept {
  // A change of OSR, ODR, pwr_mode or FIFO_SEL flushes the FIFO, complete within t_reconf, and
  // FIFO_COUNT is not to be read before (section 4.6.4). With the FIFO on, the chip is never in
  // deep standby (4.3.2), so t_reconf_deep does not apply.
  if (config_.fifo_frame != FifoFrame::Disabled) {
    detail::DelayUs(bus_.GetPort(), kReconfUs);
  }
}

template <BusPort Port>
Result<void> BMP581<Port>::afterFailedSequence(Error own) noexcept {
  // The sequence stopped part-way. Once its configuration writes have started, a reset during it
  // may be covered by its own OSR_CONFIG write, and a lost or corrupt INT_STATUS read would leave
  // no evidence at all: the simplest rule that holds is to start again. The driver is
  // uninitialised, and the next EnsureInitialized() starts from the soft reset. INT_STATUS is read
  // once more, to report a reset it shows as DeviceReset.
  config_known_ = false;
  check_pending_ = true;
  if (own == Error::DeviceReset) {
    return Result<void>::Failure(own);
  }
  const auto status = readIntStatus();
  if (status && detail::Has(status.value, reg::kIntPor)) {
    return fail(markReset(), "bmp581: the chip reset during configuration");
  }
  if (sentinel_doubt_) {
    initialized_ = false;
    sentinel_doubt_ = false;
  }
  return fail(own, "bmp581: the configuration sequence failed");
}

template <BusPort Port>
Result<void> BMP581<Port>::writeConfig(const Config& config) noexcept {
  using detail::operator""_u8;
  // INT_SOURCE = 0 and an INT_STATUS read come before INT_CONFIG: the order section 4.7.3.2 asks
  // for when the INT mode changes, used every time. Each write is one register in one transfer.
  if (const Error e = write(reg::kIntSource, 0_u8); e != Error::Ok) {
    return fail(e, "bmp581: INT_SOURCE write failed");
  }
  if (const auto status = readIntStatus(); !status) {
    return fail(status.error, "bmp581: INT_STATUS read failed");
  }
  if (const Error e = settle(Error::Ok); e != Error::Ok) {
    return fail(e, "bmp581: reset check failed");
  }
  sentinel_doubt_ = true; // from here the sequence's own writes may cover a reset
  const std::array<detail::RegisterWrite, 12> writes = detail::ConfigWrites(config);
  for (const detail::RegisterWrite& w : writes) {
    if (const Error e = write(w.reg, w.value); e != Error::Ok) {
      return fail(e, "bmp581: configuration write failed");
    }
  }
  return Result<void>::Success();
}

template <BusPort Port>
Result<void> BMP581<Port>::readBackConfig(const Config& config) noexcept {
  // INT_CONFIG is read back after an idle time of 1 us (sections 5.5.4, 7.5).
  detail::DelayUs(bus_.GetPort(), kReadBackIdleUs);
  if (const Error e = read(reg::kIntConfig, std::size_t{5U}); e != Error::Ok) {
    return fail(e, "bmp581: configuration read-back failed");
  }
  std::array<uint8_t, 5> block_a{};
  for (std::size_t i = 0U; i < block_a.size(); ++i) {
    block_a[i] = buffer_[i];
  }
  if (const Error e = read(reg::kDspConfig, std::size_t{8U}); e != Error::Ok) {
    return fail(e, "bmp581: configuration read-back failed");
  }
  std::array<uint8_t, 8> block_b{};
  for (std::size_t i = 0U; i < block_b.size(); ++i) {
    block_b[i] = buffer_[i]; // a copy: the reset check below reads into the same buffer
  }
  if (detail::OsrShowsReset(block_b[6])) {
    // press_en = 0 right after writing it: a reset, or a corrupt read. Confirm first (rule 12d).
    if (const auto c = checkSentinel(); !c) {
      return fail(c.error, "bmp581: reset check failed");
    }
  }
  if (!detail::BlockAMatches(block_a, config) || !detail::BlockBMatches(block_b, config)) {
    return fail(Error::WriteMismatch, "bmp581: configuration read back differently");
  }
  // The marker the sequence never writes: a reset during the sequence shows here even when the
  // sequence wrote OSR_CONFIG over it.
  const auto cleared = markerCleared();
  if (!cleared) {
    return fail(cleared.error, "bmp581: NVM_DATA read failed");
  }
  if (cleared.value) {
    return fail(markReset(), "bmp581: the chip reset during configuration");
  }
  return Result<void>::Success();
}

template <BusPort Port>
Result<bool> BMP581<Port>::markerCleared() noexcept {
  // NVM_DATA_LSB and NVM_DATA_MSB in one read, read again on a mismatch (rule 12d). Called only
  // while the marker is fresh: written or verified in standby, with no measurement from deep
  // standby since (marker_fresh_). Only the reset value, 0x00 and 0x00 in both reads, is then the
  // reset. Any other value is the marker lost, not a reset: it is not relied on until the next
  // configuration sequence writes it again (ensureMarker()).
  uint32_t zeros = 0U;
  for (uint32_t attempt = 0U; attempt < 2U; ++attempt) {
    if (const Error e = read(reg::kNvmDataLsb, detail::kMarker.size()); e != Error::Ok) {
      check_pending_ = true;
      return Result<bool>::Failure(e);
    }
    if ((buffer_[0] == detail::kMarker[0]) && (buffer_[1] == detail::kMarker[1])) {
      return Result<bool>::Success(false);
    }
    if ((buffer_[0] == 0U) && (buffer_[1] == 0U)) {
      ++zeros;
    }
  }
  if (zeros == 2U) {
    return Result<bool>::Success(true);
  }
  marker_fresh_ = false;
  detail::Log(bus_.GetPort(), LogLevel::Warning, "bmp581: NVM_DATA marker overwritten");
  return Result<bool>::Success(false);
}

template <BusPort Port>
Result<void> BMP581<Port>::enterStandby(const Config& config) noexcept {
  if (const Error e = write(reg::kOdrConfig, detail::OdrConfigValue(config, PowerMode::Standby));
      e != Error::Ok) {
    config_known_ = false;
    return fail(e, "bmp581: ODR_CONFIG write failed");
  }
  // t_standby, then the mode read back (section 4.3.7).
  detail::DelayUs(bus_.GetPort(), kStandbyUs);
  const auto sentinel = checkSentinel();
  if (!sentinel) {
    return fail(sentinel.error, "bmp581: standby check failed");
  }
  if (reg::OdrPwrMode::Extract(sentinel.value) != 0U) {
    config_known_ = false;
    return fail(Error::Timeout, "bmp581: standby not reached");
  }
  return Result<void>::Success();
}

template <BusPort Port>
Result<void> BMP581<Port>::enterMode(const Config& config, PowerMode mode) noexcept {
  // A measurement from deep standby downloads the NVM (section 7.22), which may reach NVM_DATA:
  // from here the marker is no longer fresh.
  if (EntersDeepStandby(config)) {
    marker_fresh_ = false;
  }
  if (const Error e = write(reg::kOdrConfig, detail::OdrConfigValue(config, mode));
      e != Error::Ok) {
    config_known_ = false; // the write may have landed: the chip may measure
    return fail(e, "bmp581: ODR_CONFIG write failed");
  }
  // The mode and, in normal mode, OSR_EFF.odr_is_valid are read back until they show the change,
  // at most the start-up time from deep standby: the datasheet gives no time for either (sections
  // 4.3.7, 7.35). A read is idempotent; the loop has a fixed bound.
  // odr_is_valid = 0 is read again too: it may show after pwr_mode does, and only the deadline
  // makes it ConfigRejected.
  const uint32_t polls = (kStartupDeepUs / kPollUs) + 1U;
  Error last = Error::Ok;
  for (uint32_t i = 0U; i < polls; ++i) {
    last = checkModeReadBack(config, mode);
    if ((last != Error::Busy) && (last != Error::ConfigRejected)) {
      break;
    }
    detail::DelayUs(bus_.GetPort(), kPollUs);
  }
  if (last == Error::Ok) {
    return Result<void>::Success();
  }
  config_known_ = false;
  if (last == Error::ConfigRejected) {
    // The chip measures with its default oversampling (section 4.3.4): stop it.
    (void)enterStandby(config);
    config_known_ = false;
  }
  return fail((last == Error::Busy) ? Error::WriteMismatch : last,
              "bmp581: the power mode read back differently");
}

template <BusPort Port>
Error BMP581<Port>::checkModeReadBack(const Config& config, PowerMode mode) noexcept {
  if (const Error e = read(reg::kOsrConfig, std::size_t{3U}); e != Error::Ok) {
    check_pending_ = true;
    return e;
  }
  const uint8_t osr = buffer_[0];
  const uint8_t odr = buffer_[1];
  last_osr_eff_ = buffer_[2];
  if (detail::OsrShowsReset(osr) || detail::Has(osr, reg::kOsrReserved) ||
      detail::Has(last_osr_eff_, reg::kOsrEffReserved)) {
    const auto c = checkSentinel();
    return c ? Error::WriteMismatch : c.error;
  }
  if ((osr != detail::OsrConfigValue(config)) ||
      (detail::OdrWithoutMode(odr) != detail::OdrConfigValue(config, PowerMode::Standby))) {
    return Error::WriteMismatch;
  }
  if (reg::OdrPwrMode::Extract(odr) != static_cast<uint32_t>(mode)) {
    return Error::Busy; // not there yet: read again
  }
  if ((mode == PowerMode::Normal) && !detail::Has(last_osr_eff_, reg::kOsrEffOdrValid)) {
    return Error::ConfigRejected;
  }
  return Error::Ok;
}

template <BusPort Port>
Result<void> BMP581<Port>::SetPowerMode(PowerMode mode) noexcept {
  if (!detail::IsEnumerator(mode) || (mode == PowerMode::Forced)) {
    return fail(Error::InvalidArgument, "bmp581: invalid power mode");
  }
  if (mode == PowerMode::Standby) {
    return standbyAfterStart();
  }
  Config next = config_;
  next.mode = mode;
  if (initialized_ && (Validate(next) != Error::Ok)) {
    return fail(Error::InvalidArgument, "bmp581: the data rate is too fast for normal mode");
  }
  if (auto g = gate(); !g) {
    return g;
  }
  if (!config_known_) {
    return fail(Error::StateUnknown, "bmp581: the configuration is in doubt");
  }
  const auto sentinel = checkBeforeWrite();
  if (!sentinel) {
    return fail(sentinel.error, "bmp581: reset check failed");
  }
  // Every transition goes through standby (section 4.3).
  if (reg::OdrPwrMode::Extract(sentinel.value) != 0U) {
    if (auto r = enterStandby(config_); !r) {
      return r;
    }
  }
  if (auto r = enterMode(next, mode); !r) {
    return r;
  }
  config_.mode = mode;
  waitFifoFlush();
  return Result<void>::Success();
}

template <BusPort Port>
Result<void> BMP581<Port>::standbyAfterStart() noexcept {
  // Never refused once started: no gate, no doubt check. The write is one byte (section 4.3.7).
  if (!started_once_) {
    return fail(Error::NotInitialized, "bmp581: not started");
  }
  if (const Error e = write(reg::kOdrConfig, detail::OdrConfigValue(config_, PowerMode::Standby));
      e != Error::Ok) {
    config_known_ = false;
    return fail(e, "bmp581: ODR_CONFIG write failed");
  }
  detail::DelayUs(bus_.GetPort(), kStandbyUs); // t_standby, table 4
  uint8_t odr = 0U;
  if (initialized_) {
    const auto sentinel = checkSentinel();
    if (!sentinel) {
      return fail(sentinel.error, "bmp581: standby check failed");
    }
    odr = sentinel.value;
  } else {
    const auto read_back = readByte(reg::kOdrConfig);
    if (!read_back) {
      return fail(read_back.error, "bmp581: standby check failed");
    }
    odr = read_back.value;
  }
  if (reg::OdrPwrMode::Extract(odr) != 0U) {
    config_known_ = false;
    return fail(Error::Timeout, "bmp581: standby not reached");
  }
  config_.mode = PowerMode::Standby;
  waitFifoFlush();
  return Result<void>::Success();
}

// -------------------------------------------------------------------------------------------------
// Measurement
// -------------------------------------------------------------------------------------------------

template <BusPort Port>
Result<Measurement> BMP581<Port>::ReadMeasurement(Verify verify) noexcept {
  if (!detail::IsEnumerator(verify)) {
    return Result<Measurement>::Failure(
        fail(Error::InvalidArgument, "bmp581: invalid verify").error);
  }
  if (auto g = gate(); !g) {
    return Result<Measurement>::Failure(g.error);
  }
  if (verify == Verify::ReadBack) {
    check_pending_ = true;
  }
  return readData(verify);
}

template <BusPort Port>
Result<Measurement> BMP581<Port>::readData(Verify verify) noexcept {
  // One burst of TEMP_DATA to STATUS: the shadowing of section 4.5.1 keeps temperature and
  // pressure from one conversion, and the reserved registers and INT_STATUS come with them.
  if (const Error e = read(reg::kTempXlsb, detail::kDataFrameBytes); e != Error::Ok) {
    check_pending_ = true; // INT_STATUS may have been cleared on the chip and lost
    return Result<Measurement>::Failure(fail(e, "bmp581: data read failed").error);
  }
  // A copy: the reset check below reads into the same buffer.
  std::array<uint8_t, detail::kDataFrameBytes> frame{};
  for (std::size_t i = 0U; i < frame.size(); ++i) {
    frame[i] = buffer_[i];
  }
  last_status_ = frame[detail::kDataStatus];
  if (!detail::DataFrameValid(frame)) {
    // Not data. It may be the first SPI frame after a power-on reset: the reset check runs now,
    // and a confirmed reset outranks InvalidReply.
    check_pending_ = true;
    return Result<Measurement>::Failure(
        fail(settle(Error::InvalidReply), "bmp581: the data frame cannot come from the chip")
            .error);
  }
  noteIntStatus(frame[detail::kDataIntStatus]);
  if (const Error e = settle(Error::Ok); e != Error::Ok) {
    return Result<Measurement>::Failure(fail(e, "bmp581: reset check failed").error);
  }
  const std::span<const uint8_t, detail::kDataFrameBytes> bytes{frame};
  const auto m = detail::DecodeMeasurement(detail::Le24(bytes.template subspan<0U, 3U>()),
                                           detail::Le24(bytes.template subspan<3U, 3U>()));
  if (m.error == Error::NoData) {
    // The data registers at their reset value are reset evidence too (sections 7.11 to 7.16):
    // the reset check runs, and a confirmed reset outranks NoData.
    check_pending_ = true;
    return Result<Measurement>::Failure(
        fail(settle(Error::NoData), "bmp581: no measurement").error);
  }
  if (!m) {
    return Result<Measurement>::Failure(fail(m.error, "bmp581: no valid measurement").error);
  }
  if (verify == Verify::ReadBack) {
    return confirmData(bytes.template first<6U>());
  }
  return m;
}

template <BusPort Port>
Result<Measurement> BMP581<Port>::confirmData(std::span<const uint8_t, 6> first) noexcept {
  using detail::operator""_u8;
  // The bus carries no CRC, so a bit flipped inside a data byte gives a plausible value; only a
  // second read finds it. TEMP_DATA and PRESS_DATA again (6 bytes, INT_STATUS untouched): equal is
  // Ok. Otherwise a third read decides: equal to the second (a new measurement landed in between,
  // or the first read was disturbed) or to the first (the second was disturbed); a third different
  // value is InvalidReply. Two reads of a stuck or floating line agree, so they never decide.
  std::array<uint8_t, 6> second{};
  for (uint32_t attempt = 0U; attempt < 2U; ++attempt) {
    if (const Error e = read(reg::kTempXlsb, second.size()); e != Error::Ok) {
      return Result<Measurement>::Failure(fail(e, "bmp581: data read failed").error);
    }
    const std::span<const uint8_t, 6> again{buffer_.data(), 6U};
    const bool same_first = std::ranges::equal(again, first);
    const bool stuck = detail::AllBytes(again, 0x00_u8) || detail::AllBytes(again, 0xFF_u8);
    const bool same_second = (attempt == 1U) && !stuck && std::ranges::equal(again, second);
    if (same_first || same_second) {
      const auto m = detail::DecodeMeasurement(detail::Le24(again.template subspan<0U, 3U>()),
                                               detail::Le24(again.template subspan<3U, 3U>()));
      if (!m) {
        check_pending_ = true;
        return Result<Measurement>::Failure(
            fail(settle(m.error), "bmp581: no valid measurement").error);
      }
      return m;
    }
    (void)std::ranges::copy(again, second.begin());
  }
  return Result<Measurement>::Failure(
      fail(Error::InvalidReply, "bmp581: the data read differently three times").error);
}

template <BusPort Port>
Result<Measurement> BMP581<Port>::MeasureOnce(Verify verify) noexcept {
  if (!detail::IsEnumerator(verify)) {
    return Result<Measurement>::Failure(
        fail(Error::InvalidArgument, "bmp581: invalid verify").error);
  }
  if (auto g = gate(); !g) {
    return Result<Measurement>::Failure(g.error);
  }
  if (!config_known_) {
    return Result<Measurement>::Failure(
        fail(Error::StateUnknown, "bmp581: the configuration is in doubt").error);
  }
  if (config_.mode != PowerMode::Standby) {
    return Result<Measurement>::Failure(Error::Busy);
  }
  const auto before = checkBeforeWrite();
  if (!before) {
    return Result<Measurement>::Failure(fail(before.error, "bmp581: reset check failed").error);
  }
  if (reg::OdrPwrMode::Extract(before.value) != 0U) {
    return Result<Measurement>::Failure(Error::Busy); // a forced measurement still runs
  }
  if (EntersDeepStandby(config_)) {
    marker_fresh_ = false; // the NVM download of a measurement from deep standby (section 7.22)
  }
  if (const Error e = write(reg::kOdrConfig, detail::OdrConfigValue(config_, PowerMode::Forced));
      e != Error::Ok) {
    return Result<Measurement>::Failure(fail(e, "bmp581: ODR_CONFIG write failed").error);
  }
  // The write took: pwr_mode reads forced until the measurement ends (section 7.34). It is polled
  // for at most t_startup, before which no measurement starts, let alone ends (table 4); the
  // datasheet gives no time for the mode to show. A dropped write would otherwise return the old
  // data.
  if (const auto started = waitForcedShown(); !started) {
    return Result<Measurement>::Failure(started.error);
  }
  const uint32_t first_wait = detail::ForcedFirstWaitUs(config_);
  detail::DelayUs(bus_.GetPort(), first_wait);
  const uint32_t polls = ((ForcedMeasurementMaxUs(config_) - first_wait) / kPollUs) + 2U;
  for (uint32_t i = 0U; i < polls; ++i) {
    const auto poll = checkSentinel();
    if (!poll) {
      return Result<Measurement>::Failure(fail(poll.error, "bmp581: mode read failed").error);
    }
    if (reg::OdrPwrMode::Extract(poll.value) == 0U) {
      return readData(verify);
    }
    detail::DelayUs(bus_.GetPort(), kPollUs);
  }
  return Result<Measurement>::Failure(
      fail(Error::Timeout, "bmp581: the forced measurement did not end").error);
}

template <BusPort Port>
Result<void> BMP581<Port>::waitForcedShown() noexcept {
  const uint32_t polls = (kStartupUs / kPollUs) + 1U;
  for (uint32_t i = 0U; i < polls; ++i) {
    const auto mode = checkSentinel();
    if (!mode) {
      return fail(mode.error, "bmp581: mode read failed");
    }
    if (reg::OdrPwrMode::Extract(mode.value) == static_cast<uint32_t>(PowerMode::Forced)) {
      return Result<void>::Success();
    }
    detail::DelayUs(bus_.GetPort(), kPollUs);
  }
  return fail(Error::WriteMismatch, "bmp581: the forced measurement did not start");
}

template <BusPort Port>
Result<void> BMP581<Port>::WaitDataReady() noexcept {
  if (auto g = gate(); !g) {
    return g;
  }
  if (!config_.source_data_ready || (config_.mode == PowerMode::Standby)) {
    return fail(Error::Unsupported, "bmp581: no data-ready source, or standby");
  }
  if (!config_known_) {
    return fail(Error::StateUnknown, "bmp581: the configuration is in doubt");
  }
  const uint32_t polls = ((config_.data_ready_timeout_ms * 1000U) / kPollUs) + 1U;
  for (uint32_t i = 0U; i < polls; ++i) {
    const auto status = readIntStatus();
    if (!status) {
      return fail(status.error, "bmp581: INT_STATUS read failed");
    }
    if (const Error e = settle(Error::Ok); e != Error::Ok) {
      return fail(e, "bmp581: reset check failed");
    }
    if (detail::Has(status.value, reg::kIntDrdy)) {
      return checkSelectionFrame();
    }
    detail::DelayUs(bus_.GetPort(), kPollUs);
  }
  return fail(Error::Timeout, "bmp581: no data ready");
}

template <BusPort Port>
Result<void> BMP581<Port>::checkSelectionFrame() noexcept {
  // On SPI the first frame after a reset only selects SPI again and its bytes are whatever MISO
  // shows (section 5.1): an INT_STATUS or FIFO_DATA read cannot tell such a frame from data. The
  // reset check runs before the call returns what it read; on I2C only when one is due.
  if constexpr (detail::BusFor<Port>::kSelectionFrame) {
    check_pending_ = true;
  }
  if (const Error e = settle(Error::Ok); e != Error::Ok) {
    return fail(e, "bmp581: reset check failed");
  }
  return Result<void>::Success();
}

template <BusPort Port>
Result<InterruptStatus> BMP581<Port>::ReadInterruptStatus() noexcept {
  if (auto g = gate(); !g) {
    return Result<InterruptStatus>::Failure(g.error);
  }
  const auto status = readIntStatus();
  if (!status) {
    return Result<InterruptStatus>::Failure(
        fail(status.error, "bmp581: INT_STATUS read failed").error);
  }
  if (const auto checked = checkSelectionFrame(); !checked) {
    return Result<InterruptStatus>::Failure(checked.error);
  }
  return Result<InterruptStatus>::Success(detail::DecodeIntStatus(status.value));
}

// -------------------------------------------------------------------------------------------------
// FIFO
// -------------------------------------------------------------------------------------------------

template <BusPort Port>
Result<uint8_t> BMP581<Port>::ReadFifoCount() noexcept {
  if (auto g = gate(); !g) {
    return Result<uint8_t>::Failure(g.error);
  }
  if (!config_known_) {
    return Result<uint8_t>::Failure(
        fail(Error::StateUnknown, "bmp581: the configuration is in doubt").error);
  }
  const auto count = readFifoCountRaw();
  if (!count) {
    return count;
  }
  if ((count.value == 0U) && (config_.fifo_frame != FifoFrame::Disabled)) {
    // A reset empties and disables the FIFO (section 4.6.4): an empty FIFO is checked.
    check_pending_ = true;
  }
  if (const auto checked = checkSelectionFrame(); !checked) {
    return Result<uint8_t>::Failure(checked.error);
  }
  return count;
}

template <BusPort Port>
Result<uint8_t> BMP581<Port>::readFifoCountRaw() noexcept {
  const auto count = readByte(reg::kFifoCount);
  if (!count) {
    return Result<uint8_t>::Failure(fail(count.error, "bmp581: FIFO_COUNT read failed").error);
  }
  const uint32_t capacity =
      (config_.fifo_frame == FifoFrame::Disabled) ? 0U : detail::FifoCapacity(config_.fifo_frame);
  if (detail::Has(count.value, detail::kFifoCountReserved) ||
      (static_cast<uint32_t>(count.value) > capacity)) {
    check_pending_ = true;
    return Result<uint8_t>::Failure(
        fail(settle(Error::InvalidReply), "bmp581: FIFO_COUNT cannot come from the chip").error);
  }
  return count;
}

template <BusPort Port>
Result<std::size_t> BMP581<Port>::ReadFifo(std::span<FifoSample> out, Verify verify) noexcept {
  if (!detail::IsEnumerator(verify) || out.empty()) {
    return Result<std::size_t>::Failure(
        fail(Error::InvalidArgument, "bmp581: invalid FIFO read").error);
  }
  if (auto g = gate(); !g) {
    return Result<std::size_t>::Failure(g.error);
  }
  if (config_.fifo_frame == FifoFrame::Disabled) {
    return Result<std::size_t>::Failure(fail(Error::Unsupported, "bmp581: the FIFO is off").error);
  }
  if (!config_known_) {
    return Result<std::size_t>::Failure(
        fail(Error::StateUnknown, "bmp581: the configuration is in doubt").error);
  }
  const auto count = readFifoCountRaw();
  if (!count) {
    return Result<std::size_t>::Failure(count.error);
  }
  // A reset empties and disables the FIFO: an empty FIFO is checked against a reset.
  if ((count.value == 0U) || (verify == Verify::ReadBack)) {
    check_pending_ = true;
  }
  auto frames = (count.value == 0U) ? Result<std::size_t>::Success(std::size_t{0U})
                                    : readFifoFrames(out, static_cast<uint32_t>(count.value));
  if (!frames) {
    return frames;
  }
  if (const auto checked = checkSelectionFrame(); !checked) {
    return Result<std::size_t>::Failure(checked.error);
  }
  return frames;
}

template <BusPort Port>
Result<std::size_t> BMP581<Port>::readFifoFrames(std::span<FifoSample> out,
                                                 uint32_t count) noexcept {
  using detail::operator""_u8;
  const std::size_t frame_bytes = detail::FifoFrameBytes(config_.fifo_frame);
  const std::size_t frames = (count < out.size()) ? static_cast<std::size_t>(count) : out.size();
  const std::size_t bytes = frames * frame_bytes; // at most 96: count <= capacity
  // One burst from FIFO_DATA, whole frames only (sections 4.6.3, 5.4).
  if (const Error e = read(reg::kFifoData, bytes); e != Error::Ok) {
    return Result<std::size_t>::Failure(fail(e, "bmp581: FIFO read failed").error);
  }
  const std::span<const uint8_t> data{buffer_.data(), bytes};
  if (detail::AllBytes(data, 0x00_u8) || detail::AllBytes(data, 0xFF_u8)) {
    check_pending_ = true;
    return Result<std::size_t>::Failure(
        fail(settle(Error::InvalidReply), "bmp581: FIFO read of a stuck line").error);
  }
  for (std::size_t i = 0U; i < frames; ++i) {
    std::array<uint8_t, 6> frame{};
    for (std::size_t j = 0U; j < frame_bytes; ++j) {
      frame[j] = data[(i * frame_bytes) + j];
    }
    out[i] = detail::DecodeFifoFrame(config_.fifo_frame, frame);
    if ((out[i].error == Error::NoData) || (out[i].error == Error::OutOfRange)) {
      // The empty frame (a flush or a reset between the two reads), or a value no measurement
      // gives: the reset check runs before the samples are returned.
      check_pending_ = true;
    }
  }
  return Result<std::size_t>::Success(frames);
}

// -------------------------------------------------------------------------------------------------
// Diagnostics
// -------------------------------------------------------------------------------------------------

template <BusPort Port>
Result<uint8_t> BMP581<Port>::ReadRegister(uint8_t address) noexcept {
  if (!initialized_) {
    return Result<uint8_t>::Failure(Error::NotInitialized);
  }
  if (!detail::RawReadable(address)) {
    return Result<uint8_t>::Failure(
        fail(Error::InvalidArgument, "bmp581: not a readable register").error);
  }
  if (auto g = gate(); !g) {
    return Result<uint8_t>::Failure(g.error);
  }
  // INT_STATUS goes through the same checks as every other INT_STATUS read: reserved bits, the
  // latch and the reset flag.
  const auto value = (address == reg::kIntStatus) ? readIntStatus() : readByte(address);
  if (!value) {
    return Result<uint8_t>::Failure(fail(value.error, "bmp581: register read failed").error);
  }
  if (address == reg::kStatus) {
    last_status_ = value.value;
  }
  if (const auto checked = checkSelectionFrame(); !checked) {
    return Result<uint8_t>::Failure(checked.error);
  }
  return value;
}

// -------------------------------------------------------------------------------------------------
// Private helpers
// -------------------------------------------------------------------------------------------------

template <BusPort Port>
Result<uint8_t> BMP581<Port>::readIntStatus() noexcept {
  const auto status = readByte(reg::kIntStatus);
  if (!status) {
    check_pending_ = true; // clear-on-read: a failed read may have lost a por
    return status;
  }
  last_int_status_ = status.value;
  if (detail::Has(status.value, detail::kIntStatusReserved)) {
    check_pending_ = true;
    return Result<uint8_t>::Failure(settle(Error::InvalidReply));
  }
  noteIntStatus(status.value);
  return status;
}

template <BusPort Port>
void BMP581<Port>::noteIntStatus(uint8_t raw) noexcept {
  last_int_status_ = raw;
  latched_int_ = static_cast<uint8_t>(static_cast<uint32_t>(latched_int_) |
                                      (static_cast<uint32_t>(raw) & reg::kIntStatusMask));
  if (detail::Has(raw, reg::kIntPor)) {
    por_unconfirmed_ = true;
  }
}

template <BusPort Port>
Result<std::array<uint8_t, 2>> BMP581<Port>::readSentinelPair() noexcept {
  // OSR_CONFIG to OSR_EFF: the reserved bits of OSR_CONFIG and OSR_EFF read 0 (sections 7.33,
  // 7.35). No constant byte passes both these and press_en = 1, so neither does a stuck line or the
  // SPI frame that only selects SPI again (section 5.1).
  if (const Error e = read(reg::kOsrConfig, std::size_t{3U}); e != Error::Ok) {
    return Result<std::array<uint8_t, 2>>::Failure(e);
  }
  if (detail::Has(buffer_[0], reg::kOsrReserved) || detail::Has(buffer_[2], reg::kOsrEffReserved)) {
    return Result<std::array<uint8_t, 2>>::Failure(Error::InvalidReply);
  }
  last_osr_eff_ = buffer_[2];
  return Result<std::array<uint8_t, 2>>::Success({buffer_[0], buffer_[1]});
}

template <BusPort Port>
bool BMP581<Port>::sentinelEvidence(const Result<std::array<uint8_t, 2>>& pair) const noexcept {
  // press_en = 0 (the driver always writes 1); a reply that cannot come from the chip (on SPI the
  // first frame after a power-on reset only selects SPI again, section 5.1); or, with the copy
  // known, OSR_CONFIG or ODR_CONFIG differing from what the driver wrote and read back.
  if (!pair) {
    return pair.error == Error::InvalidReply;
  }
  const bool differs = (pair.value[0] != detail::OsrConfigValue(config_)) ||
                       (detail::OdrWithoutMode(pair.value[1]) !=
                        detail::OdrConfigValue(config_, PowerMode::Standby));
  return detail::OsrShowsReset(pair.value[0]) || (config_known_ && differs);
}

template <BusPort Port>
Result<uint8_t> BMP581<Port>::checkSentinel() noexcept {
  // OSR_CONFIG to OSR_EFF in one read. Evidence is confirmed by a second read before the driver
  // acts (rule 12d): a second read that shows it again is the reset, also when it is a
  // configuration the chip no longer holds. Until a read passes, the check stays pending.
  auto pair = readSentinelPair();
  if (sentinelEvidence(pair)) {
    pair = readSentinelPair();
    if (pair && sentinelEvidence(pair)) {
      return Result<uint8_t>::Failure(markReset());
    }
  }
  if (!pair) {
    check_pending_ = true;
    return Result<uint8_t>::Failure(pair.error);
  }
  check_pending_ = false;
  return Result<uint8_t>::Success(pair.value[1]);
}

template <BusPort Port>
Error BMP581<Port>::settle(Error own) noexcept {
  if (!check_pending_ && !por_unconfirmed_) {
    return own;
  }
  const auto checked = checkSentinel();
  if (!checked) {
    // A confirmed reset outranks the call's own error; otherwise a failed call keeps its own.
    return ((checked.error == Error::DeviceReset) || (own == Error::Ok)) ? checked.error : own;
  }
  if (por_unconfirmed_) {
    return porContradicted(own);
  }
  return own;
}

template <BusPort Port>
Error BMP581<Port>::porContradicted(Error own) noexcept {
  // INT_STATUS showed por, but OSR_CONFIG says no reset. Either the por read was corrupt, or a
  // reset the driver did not see was followed by an OSR_CONFIG write (a sequence whose reads were
  // disturbed). With the configuration known, the whole configuration and the marker decide: a
  // chip that no longer holds them reset. The doubt moves to the next call either way (a por read
  // is never re-read: it clears on read), and the decision too when its reads fail.
  por_unconfirmed_ = false;
  check_pending_ = true;
  if (config_known_) {
    const auto held = configHeld();
    if (!held) {
      por_unconfirmed_ = true; // undecided: the next call reads the configuration again
      return (own == Error::Ok) ? held.error : own;
    }
    if (!held.value) {
      return markReset();
    }
  }
  return (own == Error::Ok) ? Error::InvalidReply : own;
}

template <BusPort Port>
Result<bool> BMP581<Port>::configHeld() noexcept {
  // INT_CONFIG to FIFO_SEL and DSP_CONFIG to ODR_CONFIG, as readBackConfig() reads them; ODR_CONFIG
  // without pwr_mode, which the chip changes on its own.
  if (const Error e = read(reg::kIntConfig, std::size_t{5U}); e != Error::Ok) {
    return Result<bool>::Failure(e);
  }
  std::array<uint8_t, 5> block_a{};
  for (std::size_t i = 0U; i < block_a.size(); ++i) {
    block_a[i] = buffer_[i];
  }
  if (const Error e = read(reg::kDspConfig, std::size_t{8U}); e != Error::Ok) {
    return Result<bool>::Failure(e);
  }
  std::array<uint8_t, 8> block_b{};
  for (std::size_t i = 0U; i < block_b.size(); ++i) {
    block_b[i] = buffer_[i];
  }
  block_b[7] = detail::OdrWithoutMode(block_b[7]);
  const bool same =
      detail::BlockAMatches(block_a, config_) && detail::BlockBMatches(block_b, config_);
  if (!same || !marker_fresh_) {
    return Result<bool>::Success(same);
  }
  const auto cleared = markerCleared();
  if (!cleared) {
    return Result<bool>::Failure(cleared.error);
  }
  return Result<bool>::Success(!cleared.value);
}

template <BusPort Port>
Result<uint8_t> BMP581<Port>::checkBeforeWrite() noexcept {
  // Before a setter writes: INT_STATUS (its por survives a reset the sentinel no longer shows),
  // then OSR_CONFIG and ODR_CONFIG. Returns ODR_CONFIG.
  if (const auto status = readIntStatus(); !status) {
    return Result<uint8_t>::Failure(status.error);
  }
  if (const Error e = settle(Error::Ok); e != Error::Ok) {
    return Result<uint8_t>::Failure(e);
  }
  return checkSentinel();
}

template <BusPort Port>
Result<void> BMP581<Port>::gate() noexcept {
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  if (const Error e = settle(Error::Ok); e != Error::Ok) {
    return fail(e, "bmp581: reset check failed");
  }
  return Result<void>::Success();
}

template <BusPort Port>
Error BMP581<Port>::read(uint8_t reg, std::size_t count) noexcept {
  return bus_.Read(reg, std::span<uint8_t>{buffer_.data(), count});
}

template <BusPort Port>
Result<uint8_t> BMP581<Port>::readByte(uint8_t reg) noexcept {
  if (const Error e = read(reg, std::size_t{1U}); e != Error::Ok) {
    return Result<uint8_t>::Failure(e);
  }
  return Result<uint8_t>::Success(buffer_[0]);
}

template <BusPort Port>
Error BMP581<Port>::write(uint8_t reg, uint8_t value) noexcept {
  return bus_.WriteByte(reg, value);
}

template <BusPort Port>
Error BMP581<Port>::markReset() noexcept {
  initialized_ = false;
  sentinel_doubt_ = false;
  config_known_ = false;
  check_pending_ = false;
  por_unconfirmed_ = false;
  detail::Log(bus_.GetPort(), LogLevel::Warning, "bmp581: the chip reset; call EnsureInitialized");
  return Error::DeviceReset;
}

template <BusPort Port>
Result<void> BMP581<Port>::fail(Error error, const char* message) noexcept {
  detail::Log(bus_.GetPort(), LogLevel::Error, message);
  return Result<void>::Failure(error);
}

} // namespace bmp581

#endif // BMP581_IPP
