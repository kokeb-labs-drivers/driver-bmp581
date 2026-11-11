"""What EnsureInitialized() does, step by step, and the error each failing step returns.

Reads: Top to bottom on the left, the steps of the first EnsureInitialized() call; once it has succeeded a later call
runs the reset check (one 3-byte read of OSR_CONFIG to OSR_EFF) and returns Ok, or starts again at the top when
the chip reset. The configuration and the I2C address are checked before any I/O. Step 1 waits the power-up time
and, on SPI, sends the dummy read that changes the chip from I2C to SPI. Step 2 is the soft reset: standby with deep
standby off first (the datasheet does not say that deep standby takes CMD), then CMD = 0xB6, which the chip does not
acknowledge over I2C, then the soft reset time, and on SPI the selection read again, since a reset returns the
interface to I2C. Step 3 is the datasheet's post-power-up check: CHIP_ID, STATUS polled until the NVM is
ready with no error, and INT_STATUS, which must read 0x10 (the reset happened) and then 0x00 (the read cleared it), a
check no stuck, floating or looped-back line can pass. Step 4 leaves the deep standby the reset left the chip in (standby with deep
standby off, then t_standby), writes the marker to NVM_DATA once, writes the configuration one register per transfer
with ODR_CONFIG last, reads all of it and the marker back, reads INT_STATUS once more, and enters the configured
power mode, read back too.
Each card on the right is the error a step returns; after any failure the driver stays uninitialised and the next
call starts at the top.
"""

import _source as S
from figkit import Fig


def data():
    fns = S.functions()
    S.order(fns["initialize"], ["Validate(config_)", "AddressValid()", "GetPort().EnsureInitialized()",
                                "kPowerUpUs", "resetChip()", "identify()", "waitNvmReady()", "checkResetFlags()",
                                "marker_fresh_ = false", "configure(config"], "initialize()")
    S.order(fns["EnsureInitialized"], ["if (initialized_)", "CheckForReset()", "initialize()"],
            "EnsureInitialized() checks for a reset when started")
    S.order(fns["resetChip"], ["SelectInterface()", "leaveDeepStandby()", "WriteResetCommand()", "kSoftResetUs"],
            "resetChip()")
    S.order(fns["checkResetFlags"], ["kIntStatusAfterReset", "second.value != 0U"], "checkResetFlags()")
    S.order(fns["configure"], ["enterStandby(awake)", "wake(config)", "ensureMarker()", "writeConfig(config)", "readBackConfig(config)",
                               "readIntStatus()", "enterMode(config, config.mode)"], "configure()")
    return {
        "chip_id": S.hexb(S.const("kChipIdValue")),
        "after_reset": S.hexb(S.const("kIntStatusAfterReset")),
        "cmd": S.hexb(S.const("kCmdSoftReset")),
        "powerup_us": S.const("kPowerUpUs", S.TYPES),
        "reset_us": S.const("kSoftResetUs", S.TYPES),
        "sources": [f"{S.IPP}:EnsureInitialized, initialize, resetChip, identify, waitNvmReady, checkResetFlags, "
                    "configure", f"{S.REGS}:kChipIdValue, kIntStatusAfterReset, kCmdSoftReset",
                    f"{S.TYPES}:kPowerUpUs, kSoftResetUs"],
    }


def build(d):
    f = Fig(
        "start-up",
        "What EnsureInitialized() does",
        alt=("Flow chart of EnsureInitialized(): check the configuration and the address, wait the power-up time, "
             "soft reset, check CHIP_ID, NVM ready and the reset flag, write and read back the configuration, enter "
             "the power mode; each failing step returns its error."),
        width=1200,
        kicker="Contract",
        subtitle=("A soft reset first, whatever an earlier run left. A failed call leaves the driver "
                  "uninitialised, and the next call starts again at the top."),
        legend=[("teal", "your call"), ("blue", "driver steps"), ("slate", "fails, nothing sent"),
                ("rose", "fails"), ("green", "Ok"), ("~solid", "next step")],
    )
    W = f.body_w
    M = 640
    ex = M + 100
    EW = W - ex
    row = 46

    def exit_card(main, title, lines, tone, label):
        h = S.card_h(f, EW, title, lines=lines)
        box = f.card(ex, main.cy - h / 2, EW, title, lines, tone=tone, accent=True)
        f.edge([(main.r + 4, main.cy), (box.x - 4, main.cy)], tone=tone, label=label)
        return box

    def step(prev, title, lines, label=""):
        box = f.card(0, prev.b + row, M, title, lines, tone="blue", accent=True)
        f.connect(prev, box, label=label)
        return box

    call = f.card(0, 0, M, "EnsureInitialized()", ["started already?"], tone="teal", accent=True)
    exit_card(call, "Ok, or the start-up again", ["the reset check: one read; a reset found: from step 1"],
              "green", "yes")
    s0 = step(call, "Check the configuration and the address",
              ["Validate(config); on I2C 0x46 or 0x47"], "no")
    exit_card(s0, "InvalidArgument", ["nothing sent"], "slate", "bad")
    s1 = step(s0, "1  Bus up, power-up time",
              [f"port.EnsureInitialized(); wait t_powup ({d['powerup_us']} us)",
               "SPI: a dummy CHIP_ID read selects SPI"])
    exit_card(s1, "Bus", ["the port could not bring the bus up"], "rose", "fails")
    s2 = step(s1, "2  Soft reset",
              ["ODR_CONFIG = standby, deep standby off; t_standby",
               f"CMD = {d['cmd']} (NACKed over I2C by design)", f"wait t_soft_res ({d['reset_us']} us)",
               "SPI: select SPI again"])
    exit_card(s2, "NoDevice or Bus", ["an address NACK; an SPI frame that failed"], "rose", "fails")
    s3 = step(s2, "3  Post-power-up checks (section 4.3.9)",
              [f"CHIP_ID = {d['chip_id']}", "STATUS polled: nvm_rdy = 1, no NVM error",
               f"INT_STATUS = {d['after_reset']}, then 0x00"])
    exit_card(s3, "DeviceIdMismatch, Timeout or DeviceFault",
              ["another part, or no reset seen; NVM never ready; an NVM error"], "rose", "fails")
    s4 = step(s3, "4  Configuration, in standby",
              ["standby, deep standby off; the NVM_DATA marker once", "INT_SOURCE = 0, INT_STATUS read, 12 one-byte writes",
               "read all and the marker back, INT_STATUS again", "the power mode, read back with OSR_EFF"])
    exit_card(s4, "WriteMismatch, ConfigRejected or DeviceReset",
              ["a write not kept; ODR too fast for the OSR; a reset meanwhile"], "rose", "fails")
    ok = f.card(0, s4.b + row, M, "Started: Ok", ["the chip runs the configuration, pressure enabled"],
                tone="green", accent=True)
    f.connect(s4, ok, label="all match")
    return f
