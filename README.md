# NativeQcomTsens

NativeQcomTsens is an XNU C++ reader for Qualcomm TSENS v2 temperature monitors.
The caller supplies the SROT address, TM address and a 16-bit sensor selection.
The module maps both register windows, checks conversion readiness and sensor
validity, and returns temperatures in integer millidegrees Celsius.

The implementation reads temperatures already converted by firmware-configured,
calibrated hardware. Firmware owns calibration, conversion enablement and
interrupt thresholds. The module performs no register writes. CPU frequency
policy belongs to the board package and the CPU frequency driver.

## Register interface

| Window and offset | Meaning used by this implementation |
| --- | --- |
| SROT `0x00` | Raw version register, exposed in diagnostics |
| SROT `0x04` | Bit 0 enables the controller; bits 18:3 enable sensors 15:0 |
| TM `0xe4` | Bit 3 indicates completion of the first conversion round |
| TM `0xa0 + sensor * 4` | Bit 21 marks a valid sample; bits 11:0 contain a signed 12-bit temperature code |

One temperature code unit represents 0.1 C, so the signed code is multiplied
by 100 to obtain millidegrees Celsius. A selected sensor receives at most three
consecutive status reads while looking for the valid bit. The implementation
reports an invalid result when none qualifies; repeated invalid codes are not
accepted as a temperature.

The mapped sizes are 8 bytes for SROT and `0x200` bytes for TM. Physical addresses
and sensor masks are supplied by the caller rather than embedded in this module.

## API and ownership

The caller must zero-initialize `NativeQcomTsens`, serialize its methods and keep
the object alive while its mappings are in use. `initialize()` releases previous
mappings before mapping the supplied addresses. A successful return means the
MMIO mappings exist; controller and sensor readiness are reported by `sample()`.
`release()` releases both mappings. There is no implicit destructor.

```cpp
NativeQcomTsens controller = {};
if (controller.initialize(srotPhysical, tmPhysical, sensorMask)) {
    QcomTsensSample sample;
    controller.sample(sample);
    if (sample.validMask & (1u << sensor)) {
        int32_t temperatureMilliC = sample.milliC[sensor];
        // The board consumes this qualified temperature.
    }
}
controller.release();
```

`QcomTsensSample` includes raw registers, requested/enabled/valid masks and a
status for each selected sensor. Statuses distinguish an absent mapping,
disabled controller, disabled sensor, unfinished first round, invalid reading
and valid temperature. The `milliC` field is meaningful only when that sensor's
bit is present in `validMask`; an invalid field initialized to zero is not a
measurement of 0 C.

The caller schedules sampling in a thread and supplies its own locking for
published snapshots. The reader does not allocate during sampling, sleep,
install interrupt handlers or call the CPU frequency driver.

## SM-T733 integration

The device package supplies these SM7325 TSENS26xx resources:

| Controller | SROT | TM | Requested sensors |
| --- | --- | --- | --- |
| 0 | `0x0c222000` | `0x0c263000` | 0 through 14 |
| 1 | `0x0c223000` | `0x0c265000` | 0 through 11 |

`package/T733Thermal.cpp` samples every 200 ms, qualifies CPU sensor groups and
publishes atomic caps. `package/T733CpuPower.cpp` alone selects and writes EPSS
frequency requests. `scripts/build-cpu-power.py` currently compiles this module
and the board policy into `libT733CpuPower.a`, using the existing XNU IOKit C++
compile recipe and this directory as an include path.

On 2026-10-24, `system-cpu-thermal-45c` completed the unified build and its full
512 MiB FAT was written and synchronized. Temperature measurements and thermal
cap transitions from a new boot have not yet been recorded.

## Sources and license

This module is original implementation under BSD-2-Clause-Patent; see `LICENSE`
and `NOTICE`. Register facts were researched from the SM7325 vendor kernel and
the tablet's selected stock device tree. Board thresholds and frequency policy
are documented separately in the device package and
`notes/THERMAL_IMPLEMENTATION.md` in the development workspace.
