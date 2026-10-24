// SPDX-License-Identifier: BSD-2-Clause-Patent
#include "NativeQcomTsens.h"
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOLib.h>

static uint32_t readRegister(uintptr_t address)
{
    uint32_t value = *reinterpret_cast<volatile uint32_t *>(address);
    __asm__ volatile("dmb oshld" ::: "memory");
    return value;
}

static IOMemoryMap *mapRegisters(uint64_t physical, unsigned size)
{
    IODeviceMemory *memory = IODeviceMemory::withRange(physical, size);
    if (!memory) {
        return nullptr;
    }

    IOMemoryMap *map = memory->map(kIOMapAnywhere | kIOMapInhibitCache);
    memory->release();
    return map;
}

bool NativeQcomTsens::initialize(uint64_t srotPhysical, uint64_t tmPhysical,
                                 uint16_t sensors)
{
    release();
    requestedMask = sensors;
    srotMap = mapRegisters(srotPhysical, 8);
    tmMap = mapRegisters(tmPhysical, 0x200);
    if (!srotMap || !tmMap) {
        release();
        return false;
    }

    srot = srotMap->getVirtualAddress();
    tm = tmMap->getVirtualAddress();
    return true;
}

void NativeQcomTsens::release()
{
    if (srotMap) {
        srotMap->release();
        srotMap = nullptr;
    }
    if (tmMap) {
        tmMap->release();
        tmMap = nullptr;
    }
    srot = 0;
    tm = 0;
}

void NativeQcomTsens::sample(QcomTsensSample &result) const
{
    bzero(&result, sizeof(result));
    result.requestedMask = requestedMask;
    if (!srot || !tm) {
        return;
    }

    result.version = readRegister(srot);
    result.control = readRegister(srot + 4);
    result.enabledMask = (result.control >> 3) & 0xffff;
    result.ready = readRegister(tm + 0xe4);

    // TSENS v2 status supplies a signed 12-bit temperature in 0.1 C units.
    // The first-round and per-sensor valid bits qualify the converted value.
    for (unsigned sensor = 0; sensor < 16; ++sensor) {
        uint16_t bit = static_cast<uint16_t>(1u << sensor);
        if (!(requestedMask & bit)) {
            continue;
        }
        if (!(result.control & 1)) {
            result.status[sensor] = QcomTsensStatus::ControllerDisabled;
            continue;
        }
        if (!(result.enabledMask & bit)) {
            result.status[sensor] = QcomTsensStatus::SensorDisabled;
            continue;
        }
        if (!(result.ready & 8)) {
            result.status[sensor] = QcomTsensStatus::NotReady;
            continue;
        }

        result.status[sensor] = QcomTsensStatus::Invalid;
        for (unsigned attempt = 0; attempt < 3; ++attempt) {
            uint32_t raw = readRegister(tm + 0xa0 + sensor * 4);
            result.raw[sensor] = raw;
            if (!(raw & (1u << 21))) {
                continue;
            }

            int32_t code = static_cast<int32_t>(raw & 0xfff);
            if (code & 0x800) {
                code -= 0x1000;
            }
            result.milliC[sensor] = code * 100;
            result.validMask |= bit;
            result.status[sensor] = QcomTsensStatus::Valid;
            break;
        }
    }
}

const char *native_qcom_tsens_status_name(QcomTsensStatus status)
{
    switch (status) {
        case QcomTsensStatus::Unmapped:
            return "unmapped";
        case QcomTsensStatus::ControllerDisabled:
            return "controller-disabled";
        case QcomTsensStatus::SensorDisabled:
            return "sensor-disabled";
        case QcomTsensStatus::NotReady:
            return "not-ready";
        case QcomTsensStatus::Invalid:
            return "invalid";
        case QcomTsensStatus::Valid:
            return "valid";
    }
    return "unknown";
}
