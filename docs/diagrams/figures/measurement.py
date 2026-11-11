"""One measurement from the chip to a value: the forced path, the normal path and the checks every read passes.

Reads: Two entry points at the top. MeasureOnce() runs the reset check (INT_STATUS, then OSR_CONFIG to OSR_EFF in
one read), writes ODR_CONFIG with pwr_mode = forced, polls it until it shows forced (at most t_startup) to prove the
measurement started, waits the typical conversion time less 5 percent, then polls the same registers until the chip
is back in standby, at most the datasheet maximum. In normal or continuous mode, WaitDataReady() polls INT_STATUS until drdy. Both end in the same
read: one 12-byte burst of TEMP_DATA to STATUS, so temperature and pressure come from one conversion (data shadowing).
The checks below run in order, and each failure has its error card on the right: the reserved registers must read
0x00 and the frame must not be all 0x00 (a stuck or floating line); INT_STATUS goes to the latch, and a por in it
runs the reset check; the reset value 0x7F7F7F is no measurement and runs the reset check too; the values must lie
in the measurement range. Only then is the value returned, in 1/64 Pa and 1/65536 degrees Celsius.
"""

import _source as S
from figkit import Fig


def data():
    fns = S.functions()
    S.order(fns["MeasureOnce"], ["before = checkBeforeWrite()", "PowerMode::Forced", "waitForcedShown()", "ForcedFirstWaitUs",
                                 "ForcedMeasurementMaxUs", "readData(verify)"], "MeasureOnce()")
    S.order(fns["readData"], ["kTempXlsb", "DataFrameValid(frame)", "noteIntStatus", "settle(Error::Ok)",
                              "DecodeMeasurement", "Error::NoData"], "readData()")
    S.need("kIntDrdy" in fns["WaitDataReady"], "WaitDataReady() polls drdy")
    S.need("kStartupUs" in fns["waitForcedShown"], "the forced mode is polled for at most t_startup")
    return {
        "reset": S.hexb(S.const("kDataReset")).replace("0x", "0x"),
        "pmin": S.const("kPressureMinPa", S.TYPES),
        "pmax": S.const("kPressureMaxPa", S.TYPES),
        "tmin": S.const("kTemperatureMinC", S.TYPES),
        "tmax": S.const("kTemperatureMaxC", S.TYPES),
        "sources": [f"{S.IPP}:MeasureOnce, waitForcedShown, WaitDataReady, readData, settle",
                    f"{S.REGS}:kDataReset", f"{S.TYPES}:kPressureMinPa, kPressureMaxPa, kTemperatureMinC, "
                    "kTemperatureMaxC"],
    }


def build(d):
    f = Fig(
        "measurement",
        "From a conversion to a value",
        alt=("Flow chart: MeasureOnce() or WaitDataReady() leads to one 12-byte read of TEMP_DATA to STATUS, then "
             "the checks: reserved registers and stuck lines, the reset flag, the reset value, the measurement "
             "range; each failing check returns its error, and only a frame that passes them all is a value."),
        width=1200,
        kicker="Contract",
        subtitle="No all-0x00 or all-0xFF frame, no reset value and nothing out of range is ever a value.",
        legend=[("teal", "your call"), ("blue", "driver steps"), ("rose", "fails"), ("green", "Ok"),
                ("~solid", "next step")],
    )
    W = f.body_w
    half = (W - 40) / 2
    M = 640
    ex = M + 100
    EW = W - ex
    row = 46

    forced = f.card(0, 0, half, "MeasureOnce()",
                    ["reset check (INT_STATUS, OSR_CONFIG); ODR_CONFIG = forced, polled until it shows",
                     "wait, then poll until standby"],
                    tone="teal", accent=True)
    normal = f.card(half + 40, 0, half, "WaitDataReady()  ReadMeasurement()",
                    ["normal or continuous mode", "INT_STATUS polled until drdy"], tone="teal", accent=True)
    top = max(forced.b, normal.b) + row

    def exit_card(main, title, lines, tone, label):
        h = S.card_h(f, EW, title, lines=lines)
        box = f.card(ex, main.cy - h / 2, EW, title, lines, tone=tone, accent=True)
        f.edge([(main.r + 4, main.cy), (box.x - 4, main.cy)], tone=tone, label=label)
        return box

    burst = f.card(0, top, M, "One 12-byte read: TEMP_DATA to STATUS",
                   ["temperature, pressure, 4 reserved bytes, INT_STATUS, STATUS",
                    "one conversion: data shadowing (section 4.5.1)"], tone="blue", accent=True)
    f.edge([(forced.cx, forced.b + 4), (forced.cx, burst.y - 4)], label="standby again")
    f.edge([(normal.cx, normal.b + 4), (normal.cx, top - 22), (M - 40, top - 22), (M - 40, burst.y - 4)],
           label="data ready")
    c1 = f.card(0, burst.b + row, M, "Can the chip send this frame?",
                ["reserved registers 0x00, reserved bits 0", "not all 0x00 (all 0xFF fails the reserved bytes)"],
                tone="blue", accent=True)
    f.connect(burst, c1)
    exit_card(c1, "InvalidReply", ["a stuck or floating line; the reset check runs"], "rose", "no")
    c2 = f.card(0, c1.b + row, M, "Did the chip reset?",
                ["INT_STATUS to the latch; por: OSR_CONFIG read twice", "press_en = 0 is the reset"],
                tone="blue", accent=True)
    f.connect(c1, c2, label="yes")
    exit_card(c2, "DeviceReset", ["the driver is uninitialised; nothing written"], "rose", "reset")
    c3 = f.card(0, c2.b + row, M, "Is it a measurement?",
                [f"not the reset value {d['reset']}", "the reset check runs on it too"], tone="blue",
                accent=True)
    f.connect(c2, c3, label="no")
    exit_card(c3, "NoData", ["no conversion since the reset"], "rose", "no")
    c4 = f.card(0, c3.b + row, M, "In the measurement range?",
                [f"{d['pmin']} to {d['pmax']} Pa, {d['tmin']} to {d['tmax']} °C (tables 1, 2)"], tone="blue",
                accent=True)
    f.connect(c3, c4, label="yes")
    exit_card(c4, "OutOfRange", ["never clamped"], "rose", "no")
    ok = f.card(0, c4.b + row, M, "Ok: a Measurement",
                ["pressure_raw / 64 = Pa, temperature_raw / 65536 = °C, exact"], tone="green", accent=True)
    f.connect(c4, ok, label="yes")
    return f
