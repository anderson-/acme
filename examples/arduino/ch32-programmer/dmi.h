#pragma once

#include <Arduino.h>

enum class DmiResult : uint8_t {
  Ok,
  ReceiveTimeout,
  InvalidResponse,
  AbstractBusyTimeout,
  AbstractCommandError,
  HaltTimeout,
};

// CH32/WCH debug-module register addresses used by the proven CNLohr path.
constexpr uint8_t DMI_DATA0 = 0x04;
constexpr uint8_t DMI_DATA1 = 0x05;
constexpr uint8_t DMI_DMCONTROL = 0x10;
constexpr uint8_t DMI_DMSTATUS = 0x11;
constexpr uint8_t DMI_HARTINFO = 0x12;
constexpr uint8_t DMI_ABSTRACTCS = 0x16;
constexpr uint8_t DMI_COMMAND = 0x17;
constexpr uint8_t DMI_PROGBUF0 = 0x20;
constexpr uint8_t DMI_SHADOW_CONFIG = 0x7E;
constexpr uint8_t DMI_CONFIG = 0x7D;

// Native WCH SWIO frame: start, 7-bit MSB-first address, selector, 32-bit
// MSB-first data.  Selector=1 write, selector=0 read; no parity bit.
DmiResult dmiWriteReg32(uint8_t address, uint32_t value);
DmiResult dmiWriteReg32AndRelease(uint8_t address, uint32_t value);
DmiResult dmiReadReg32(uint8_t address, uint32_t& value);

// Enables target output and verifies the known WCH configuration readback.
DmiResult dmiSynchronize(uint32_t& configValue);

// Reads CH32V003's device ID through its proven debug abstract-command path.
DmiResult dmiReadCh32V003Id(uint32_t& chipId, uint32_t& hartInfo);

const char* dmiResultText(DmiResult result);
