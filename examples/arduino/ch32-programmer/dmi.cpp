#include "dmi.h"

#include "swio.h"

namespace {

portMUX_TYPE g_swioMux = portMUX_INITIALIZER_UNLOCKED;

void IRAM_ATTR writeFrame(uint8_t address, uint32_t value) {
  swioWriteBit(true);  // Start bit.
  for (uint8_t mask = 0x40; mask != 0; mask >>= 1) {
    swioWriteBit((address & mask) != 0);
  }
  swioWriteBit(true);  // WCH write selector.
  for (uint32_t mask = 0x80000000UL; mask != 0; mask >>= 1) {
    swioWriteBit((value & mask) != 0);
  }
}

DmiResult IRAM_ATTR readFrame(uint8_t address, uint32_t& value) {
  swioWriteBit(true);  // Start bit.
  for (uint8_t mask = 0x40; mask != 0; mask >>= 1) {
    swioWriteBit((address & mask) != 0);
  }
  swioWriteBit(false);  // WCH read selector.

  uint32_t received = 0;
  for (uint8_t bit = 0; bit < 32; ++bit) {
    const SwioBitResult sample = swioReadBit();
    if (sample == SwioBitResult::Timeout) {
      return DmiResult::ReceiveTimeout;
    }
    received = (received << 1) | (sample == SwioBitResult::One ? 1UL : 0UL);
  }
  value = received;
  return DmiResult::Ok;
}

bool invalidFloatingValue(uint32_t value) {
  return value == 0x00000000UL || value == 0xFFFFFFFFUL;
}

DmiResult waitForAbstractCommand() {
  for (uint16_t attempt = 0; attempt < 1000; ++attempt) {
    uint32_t abstractcs = 0;
    const DmiResult result = dmiReadReg32(DMI_ABSTRACTCS, abstractcs);
    if (result != DmiResult::Ok) {
      return result;
    }
    if ((abstractcs & (1UL << 12)) == 0) {
      if (((abstractcs >> 8) & 0x07U) != 0) {
        (void)dmiWriteReg32(DMI_ABSTRACTCS, 0x00000700UL);
        return DmiResult::AbstractCommandError;
      }
      return DmiResult::Ok;
    }
  }
  return DmiResult::AbstractBusyTimeout;
}

DmiResult waitForHalt() {
  for (uint16_t attempt = 0; attempt < 1000; ++attempt) {
    uint32_t dmstatus = 0;
    const DmiResult result = dmiReadReg32(DMI_DMSTATUS, dmstatus);
    if (result != DmiResult::Ok) {
      return result;
    }
    if ((dmstatus & 0x0300UL) != 0) {
      return DmiResult::Ok;
    }
  }
  return DmiResult::HaltTimeout;
}

}  // namespace

DmiResult dmiWriteReg32(uint8_t address, uint32_t value) {
  if (address > 0x7F) {
    return DmiResult::InvalidResponse;
  }
  portENTER_CRITICAL(&g_swioMux);
  writeFrame(address, value);
  portEXIT_CRITICAL(&g_swioMux);
  delayMicroseconds(8);
  return DmiResult::Ok;
}

DmiResult dmiWriteReg32AndRelease(uint8_t address, uint32_t value) {
  if (address > 0x7F) {
    return DmiResult::InvalidResponse;
  }
  portENTER_CRITICAL(&g_swioMux);
  writeFrame(address, value);
  swioRelease();
  portEXIT_CRITICAL(&g_swioMux);
  return DmiResult::Ok;
}

DmiResult dmiReadReg32(uint8_t address, uint32_t& value) {
  if (address > 0x7F) {
    return DmiResult::InvalidResponse;
  }
  portENTER_CRITICAL(&g_swioMux);
  const DmiResult result = readFrame(address, value);
  portEXIT_CRITICAL(&g_swioMux);
  delayMicroseconds(8);
  return result;
}

DmiResult dmiSynchronize(uint32_t& configValue) {
  constexpr uint32_t kEnableSlaveOutput = 0x5AA50000UL | (1UL << 10);
  dmiWriteReg32(DMI_SHADOW_CONFIG, kEnableSlaveOutput);
  dmiWriteReg32(DMI_CONFIG, kEnableSlaveOutput);
  dmiWriteReg32(DMI_SHADOW_CONFIG, kEnableSlaveOutput);
  dmiWriteReg32(DMI_CONFIG, kEnableSlaveOutput);
  dmiWriteReg32(DMI_DMCONTROL, 0x00000001UL);
  dmiWriteReg32(DMI_DMCONTROL, 0x00000001UL);

  const DmiResult result = dmiReadReg32(DMI_CONFIG, configValue);
  if (result != DmiResult::Ok) {
    return result;
  }
  return ((configValue & 0xFFFF0000UL) == 0x5AA50000UL &&
          !invalidFloatingValue(configValue))
             ? DmiResult::Ok
             : DmiResult::InvalidResponse;
}

DmiResult dmiReadCh32V003Id(uint32_t& chipId, uint32_t& hartInfo) {
  DmiResult result = dmiReadReg32(DMI_HARTINFO, hartInfo);
  if (result != DmiResult::Ok || (hartInfo & 0x7FFU) != 0x0F4U) {
    return result == DmiResult::Ok ? DmiResult::InvalidResponse : result;
  }

  uint32_t oldData0 = 0;
  result = dmiReadReg32(DMI_DATA0, oldData0);
  if (result != DmiResult::Ok) {
    return result;
  }

  dmiWriteReg32(DMI_DMCONTROL, 0x80000001UL);
  dmiWriteReg32(DMI_DMCONTROL, 0x80000001UL);

  result = waitForHalt();
  if (result != DmiResult::Ok) {
    return result;
  }
  dmiWriteReg32(DMI_COMMAND, 0x00221008UL);  // Save x8 in DATA0.
  uint32_t oldX8 = 0;
  result = dmiReadReg32(DMI_DATA0, oldX8);
  if (result != DmiResult::Ok) {
    return result;
  }

  dmiWriteReg32(DMI_ABSTRACTCS, 0x08000700UL);
  dmiWriteReg32(DMI_PROGBUF0, 0x90024000UL);  // c.lw x8,0(x8); c.ebreak
  dmiWriteReg32(DMI_DATA0, 0x1FFFF704UL);
  dmiWriteReg32(DMI_COMMAND, 0x00271008UL);   // x8 <- DATA0; execute.
  result = waitForAbstractCommand();
  if (result == DmiResult::Ok) {
    dmiWriteReg32(DMI_COMMAND, 0x00221008UL);  // DATA0 <- x8.
    result = dmiReadReg32(DMI_DATA0, chipId);
    if (result == DmiResult::Ok && invalidFloatingValue(chipId)) {
      result = DmiResult::InvalidResponse;
    }
  }

  // Restore target state even when the ID read failed after saving x8.
  dmiWriteReg32(DMI_DATA0, oldX8);
  dmiWriteReg32(DMI_COMMAND, 0x00231008UL);
  dmiWriteReg32(DMI_DATA0, oldData0);
  return result;
}

const char* dmiResultText(DmiResult result) {
  switch (result) {
    case DmiResult::Ok: return "OK";
    case DmiResult::ReceiveTimeout: return "receive timeout";
    case DmiResult::InvalidResponse: return "invalid/floating response";
    case DmiResult::AbstractBusyTimeout: return "abstract command timeout";
    case DmiResult::AbstractCommandError: return "abstract command error";
    case DmiResult::HaltTimeout: return "halt timeout";
  }
  return "unknown error";
}
