// SPDX-License-Identifier: BSD-2-Clause-Patent
#pragma once
#include <stdint.h>

class IOMemoryMap;

enum class QcomTsensStatus : uint8_t {
    Unmapped,
    ControllerDisabled,
    SensorDisabled,
    NotReady,
    Invalid,
    Valid,
};

// QcomTsensSample contains one bounded observation of a TSENS v2 controller.
// Temperature fields are meaningful only for bits present in validMask.
struct QcomTsensSample {
    uint32_t version;
    uint32_t control;
    uint32_t ready;
    uint16_t requestedMask;
    uint16_t enabledMask;
    uint16_t validMask;
    uint32_t raw[16];
    int32_t milliC[16];
    QcomTsensStatus status[16];
};

// NativeQcomTsens retains two MMIO mappings and reads firmware-calibrated
// temperatures. Its caller serializes initialize, sample and release. Firmware
// retains conversion, calibration and interrupt configuration ownership.
struct NativeQcomTsens {
    IOMemoryMap *srotMap;
    IOMemoryMap *tmMap;
    uintptr_t srot;
    uintptr_t tm;
    uint16_t requestedMask;

    bool initialize(uint64_t srotPhysical, uint64_t tmPhysical, uint16_t sensors);
    void sample(QcomTsensSample &result) const;
    void release();
};

const char *native_qcom_tsens_status_name(QcomTsensStatus status);
