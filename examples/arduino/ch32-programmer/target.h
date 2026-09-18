#pragma once

#include <Arduino.h>

enum class TargetResult : uint8_t {
  Ok,
  NotDetected,
  DmiFailure,
  AbstractTimeout,
  AbstractError,
  InvalidTarget,
  FlashLocked,
  FlashTimeout,
  FlashError,
  VerifyFailed,
  InvalidArgument,
};

struct TargetInfo {
  bool detected;
  uint32_t chipId;
  uint32_t hartInfo;
};

typedef void (*ProgressCallback)(size_t bytesProcessed, size_t totalBytes, uint32_t currentAddress);

constexpr uint32_t CH32V003_DEVICE_ID_ADDRESS = 0x1FFFF704UL;
constexpr uint32_t CH32V003_FLASH_BASE = 0x08000000UL;
constexpr uint32_t CH32V003_FLASH_SIZE = 16U * 1024U;
constexpr uint32_t CH32V003_FLASH_PAGE_SIZE = 64U;

TargetResult targetDetect(TargetInfo& info);
TargetResult targetReadWord(uint32_t address, uint32_t& value);
TargetResult targetReadMemory(uint32_t address, uint8_t* buffer, size_t length);
TargetResult targetUnlockFlash();
TargetResult targetEraseAll();
TargetResult targetErasePage(uint32_t address);
TargetResult targetTestPageErase(uint32_t address, uint32_t originalData[16], uint32_t erasedData[16]);
TargetResult targetProgram64Page(uint32_t address, const uint8_t data[64]);
TargetResult targetTestProgram64(uint32_t address, const uint8_t pattern[64], uint8_t readback[64], uint32_t& mismatchAddr, uint8_t& expectedVal, uint8_t& actualVal);
TargetResult targetProgramBinary(uint32_t address, const uint8_t* data, size_t length, ProgressCallback progress = nullptr);
TargetResult targetVerifyBinary(uint32_t address, const uint8_t* data, size_t length, ProgressCallback progress = nullptr);
TargetResult targetResetRun();
const char* targetResultText(TargetResult result);
