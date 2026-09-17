#include "target.h"

#include <string.h>

#include "dmi.h"

TargetResult targetWriteWord(uint32_t address, uint32_t value);

namespace {

constexpr uint8_t DMI_ABSTRACTAUTO = 0x18;
constexpr uint8_t DMI_PROGBUF1 = 0x21;
constexpr uint8_t DMI_PROGBUF2 = 0x22;
constexpr uint8_t DMI_PROGBUF3 = 0x23;
constexpr uint8_t DMI_PROGBUF4 = 0x24;
constexpr uint8_t DMI_PROGBUF5 = 0x25;

constexpr uint32_t FLASH_KEYR = 0x40022004UL;
constexpr uint32_t FLASH_OBKEYR = 0x40022008UL;
constexpr uint32_t FLASH_STATR = 0x4002200CUL;
constexpr uint32_t FLASH_CTLR = 0x40022010UL;
constexpr uint32_t FLASH_ADDR = 0x40022014UL;
constexpr uint32_t FLASH_MODEKEYR = 0x40022024UL;
constexpr uint32_t FLASH_STATR_WRPRTERR = 0x10UL;
constexpr uint32_t FLASH_CTLR_MER = 0x0004UL;
constexpr uint32_t FLASH_CTLR_STRT = 0x0040UL;
constexpr uint32_t FLASH_CTLR_PAGE_ER = 0x00020000UL;
constexpr uint32_t FLASH_CTLR_PAGE_PG = 0x00010000UL;
constexpr uint32_t FLASH_CTLR_BUF_RST = 0x00080000UL;

bool g_detected = false;
bool g_flashUnlocked = false;

TargetResult fromDmi(DmiResult result) {
  return result == DmiResult::Ok ? TargetResult::Ok : TargetResult::DmiFailure;
}

TargetResult writeReg(uint8_t address, uint32_t value) {
  return fromDmi(dmiWriteReg32(address, value));
}

TargetResult readReg(uint8_t address, uint32_t& value) {
  return fromDmi(dmiReadReg32(address, value));
}

TargetResult requireDetected() {
  return g_detected ? TargetResult::Ok : TargetResult::NotDetected;
}

TargetResult activateDebugModule() {
  TargetResult result = writeReg(DMI_DMCONTROL, 0x80000001UL);
  if (result != TargetResult::Ok) return result;
  return writeReg(DMI_DMCONTROL, 0x80000001UL);
}

TargetResult waitForDoneOp() {
  for (uint16_t attempt = 0; attempt < 1000; ++attempt) {
    uint32_t abstractcs = 0;
    TargetResult result = readReg(DMI_ABSTRACTCS, abstractcs);
    if (result != TargetResult::Ok) return result;
    if ((abstractcs & (1UL << 12)) == 0) {
      if (((abstractcs >> 8) & 0x07U) != 0) {
        (void)writeReg(DMI_ABSTRACTCS, 0x00000700UL);
        return TargetResult::AbstractError;
      }
      return TargetResult::Ok;
    }
  }
  return TargetResult::AbstractTimeout;
}

// Exact register setup from CNLohr StaticUpdatePROGBUFRegs(), specialized for
// the CH32V003 DMHARTINFO data-register offset (0xF4).
TargetResult setupProgrammingRegisters() {
  TargetResult result = activateDebugModule();
  if (result != TargetResult::Ok) return result;
  result = writeReg(DMI_DATA0, 0xE00000F4UL);
  if (result != TargetResult::Ok) return result;
  result = writeReg(DMI_COMMAND, 0x0023100AUL);  // x10 <- DATA0
  if (result != TargetResult::Ok) return result;
  result = writeReg(DMI_DATA0, 0xE00000F8UL);
  if (result != TargetResult::Ok) return result;
  result = writeReg(DMI_COMMAND, 0x0023100BUL);  // x11 <- DATA0
  if (result != TargetResult::Ok) return result;
  result = writeReg(DMI_DATA0, FLASH_STATR);
  if (result != TargetResult::Ok) return result;
  result = writeReg(DMI_COMMAND, 0x0023100CUL);  // x12 <- DATA0
  if (result != TargetResult::Ok) return result;
  result = writeReg(DMI_DATA0, FLASH_CTLR_PAGE_PG | 0x00040000UL);
  if (result != TargetResult::Ok) return result;
  return writeReg(DMI_COMMAND, 0x0023100DUL);     // x13 <- DATA0
}

TargetResult waitForFlash() {
  for (uint16_t attempt = 0; attempt < 2000; ++attempt) {
    uint32_t status = 0;
    TargetResult result = targetReadWord(FLASH_STATR, status);
    if (result != TargetResult::Ok) return result;
    if ((status & 1U) == 0) {
      (void)targetWriteWord(FLASH_STATR, 0);
      return (status & FLASH_STATR_WRPRTERR) ? TargetResult::FlashError : TargetResult::Ok;
    }
  }
  return TargetResult::FlashTimeout;
}

bool flashAddressValid(uint32_t address, uint32_t length) {
  return address >= CH32V003_FLASH_BASE && length <= CH32V003_FLASH_SIZE &&
         address - CH32V003_FLASH_BASE <= CH32V003_FLASH_SIZE - length;
}

TargetResult verifyBytes(uint32_t address, const uint8_t* data, size_t length,
                         ProgressCallback progress) {
  size_t bytesVerified = 0;

  while (bytesVerified < length) {
    const uint32_t checkAddr = address + bytesVerified;
    const uint32_t wordAlignedAddr = checkAddr & ~3UL;
    const size_t firstByte = checkAddr & 3UL;
    size_t bytesInWord = 4U - firstByte;
    uint32_t wordValue = 0;

    if (bytesInWord > length - bytesVerified) {
      bytesInWord = length - bytesVerified;
    }

    TargetResult result = targetReadWord(wordAlignedAddr, wordValue);
    if (result != TargetResult::Ok) return result;

    for (size_t byte = 0; byte < bytesInWord; ++byte) {
      const uint8_t actual =
          static_cast<uint8_t>(wordValue >> ((firstByte + byte) * 8U));
      if (actual != data[bytesVerified + byte]) {
        return TargetResult::VerifyFailed;
      }
    }

    bytesVerified += bytesInWord;
    if (progress != nullptr &&
        ((bytesVerified % 64U) == 0 || bytesVerified == length)) {
      progress(bytesVerified, length, address + bytesVerified - 1U);
    }
  }

  return TargetResult::Ok;
}

}  // namespace

TargetResult targetDetect(TargetInfo& info) {
  info = {};
  uint32_t chipId = 0;
  uint32_t hartInfo = 0;
  const DmiResult dmiResult = dmiReadCh32V003Id(chipId, hartInfo);
  if (dmiResult != DmiResult::Ok || (hartInfo & 0x7FFU) != 0x0F4U || chipId == 0 || chipId == 0xFFFFFFFFUL) {
    g_detected = false;
    g_flashUnlocked = false;
    return TargetResult::InvalidTarget;
  }
  info = {true, chipId, hartInfo};
  g_detected = true;
  g_flashUnlocked = false;
  return TargetResult::Ok;
}

TargetResult targetReadWord(uint32_t address, uint32_t& value) {
  TargetResult result = requireDetected();
  if (result != TargetResult::Ok) return result;
  result = setupProgrammingRegisters();
  if (result != TargetResult::Ok) return result;
  result = writeReg(DMI_ABSTRACTAUTO, 0);
  if (result != TargetResult::Ok) return result;
  result = writeReg(DMI_PROGBUF0, 0x40044180UL);
  if (result != TargetResult::Ok) return result;
  result = writeReg(DMI_PROGBUF1, 0xC1040001UL);
  if (result != TargetResult::Ok) return result;
  result = writeReg(DMI_PROGBUF2, 0x9002C180UL);
  if (result != TargetResult::Ok) return result;
  result = writeReg(DMI_DATA1, address);
  if (result != TargetResult::Ok) return result;
  result = writeReg(DMI_COMMAND, 0x00240000UL);
  if (result != TargetResult::Ok) return result;
  result = waitForDoneOp();
  if (result != TargetResult::Ok) return result;
  return readReg(DMI_DATA0, value);
}

TargetResult targetWriteWord(uint32_t address, uint32_t value) {
  TargetResult result = requireDetected();
  if (result != TargetResult::Ok) return result;
  result = setupProgrammingRegisters();
  if (result != TargetResult::Ok) return result;
  const bool isFlash = (address & 0xFF000000UL) == CH32V003_FLASH_BASE;
  result = writeReg(DMI_ABSTRACTAUTO, 0);
  if (result != TargetResult::Ok) return result;
  result = writeReg(DMI_PROGBUF0, 0x41844100UL);
  if (result != TargetResult::Ok) return result;
  result = writeReg(DMI_PROGBUF1, 0x0491C080UL);
  if (result != TargetResult::Ok) return result;
  result = writeReg(DMI_PROGBUF2, 0x0001C184UL);
  if (result != TargetResult::Ok) return result;
  result = writeReg(DMI_PROGBUF3, isFlash ? 0x4200C254UL : 0x90029002UL);
  if (result != TargetResult::Ok) return result;
  if (isFlash) {
    result = writeReg(DMI_PROGBUF4, 0xFC758809UL);
    if (result != TargetResult::Ok) return result;
    result = writeReg(DMI_PROGBUF5, 0x90029002UL);
    if (result != TargetResult::Ok) return result;
  }
  result = writeReg(DMI_DATA1, address);
  if (result != TargetResult::Ok) return result;
  result = writeReg(DMI_DATA0, value);
  if (result != TargetResult::Ok) return result;
  result = writeReg(DMI_COMMAND, 0x00240000UL);
  if (result != TargetResult::Ok) return result;
  return waitForDoneOp();
}

TargetResult targetUnlockFlash() {
  TargetResult result = requireDetected();
  if (result != TargetResult::Ok) return result;

  // Wait for any previous flash operations to finish
  result = waitForFlash();
  if (result != TargetResult::Ok) return result;

  uint32_t control = 0;
  result = targetReadWord(FLASH_CTLR, control);
  if (result != TargetResult::Ok) return result;

  if ((control & 0x8080U) != 0) {
    const uint32_t keys[] = {0x45670123UL, 0xCDEF89ABUL};
    
    for (uint32_t key : keys) {
      result = targetWriteWord(FLASH_KEYR, key); 
      if (result != TargetResult::Ok) return result;
    }
    result = waitForFlash();
    if (result != TargetResult::Ok) return result;

    for (uint32_t key : keys) {
      result = targetWriteWord(FLASH_OBKEYR, key); 
      if (result != TargetResult::Ok) return result;
    }
    result = waitForFlash();
    if (result != TargetResult::Ok) return result;

    for (uint32_t key : keys) {
      result = targetWriteWord(FLASH_MODEKEYR, key); 
      if (result != TargetResult::Ok) return result;
    }
    result = waitForFlash();
    if (result != TargetResult::Ok) return result;

    result = targetReadWord(FLASH_CTLR, control);
    if (result != TargetResult::Ok) return result;
    if ((control & 0x8080U) != 0) return TargetResult::FlashLocked;
  }
  g_flashUnlocked = true;
  return TargetResult::Ok;
}

TargetResult targetEraseAll() {
  TargetResult result = targetUnlockFlash();
  if (result != TargetResult::Ok) return result;
  result = targetWriteWord(FLASH_CTLR, 0); if (result != TargetResult::Ok) return result;
  result = targetWriteWord(FLASH_CTLR, FLASH_CTLR_MER); if (result != TargetResult::Ok) return result;
  result = targetWriteWord(FLASH_CTLR, FLASH_CTLR_MER | FLASH_CTLR_STRT); if (result != TargetResult::Ok) return result;
  result = waitForFlash();
  if (result != TargetResult::Ok) return result;
  return targetWriteWord(FLASH_CTLR, 0);
}

TargetResult targetErasePage(uint32_t address) {
  if (!flashAddressValid(address, 64) || (address & 0x3FU) != 0) return TargetResult::InvalidArgument;
  TargetResult result = targetUnlockFlash();
  if (result != TargetResult::Ok) return result;
  
  uint32_t ctlrBefore = 0;
  targetReadWord(FLASH_CTLR, ctlrBefore);
  Serial.printf("FLASH_CTLR before page erase: 0x%08lX\n", (unsigned long)ctlrBefore);

  result = waitForFlash(); if (result != TargetResult::Ok) return result;
  result = targetWriteWord(FLASH_CTLR, FLASH_CTLR_PAGE_ER); if (result != TargetResult::Ok) return result;
  result = targetWriteWord(FLASH_ADDR, address); if (result != TargetResult::Ok) return result;
  result = targetWriteWord(FLASH_CTLR, FLASH_CTLR_PAGE_ER | FLASH_CTLR_STRT); if (result != TargetResult::Ok) return result;
  
  result = waitForFlash();
  
  uint32_t statrAfter = 0;
  targetReadWord(FLASH_STATR, statrAfter);
  Serial.printf("FLASH_STATR after page erase wait: 0x%08lX (busy wait: %s)\n", 
                (unsigned long)statrAfter, targetResultText(result));

  if (result != TargetResult::Ok) return result;
  return targetWriteWord(FLASH_CTLR, 0);
}

TargetResult targetTestPageErase(uint32_t address, uint32_t originalData[16], uint32_t erasedData[16]) {
  if (!flashAddressValid(address, 64) || (address & 0x3FU) != 0) {
    return TargetResult::InvalidArgument;
  }
  TargetResult result = requireDetected();
  if (result != TargetResult::Ok) return result;

  // 1. Read original 64-byte contents (16 words)
  for (uint8_t word = 0; word < 16; ++word) {
    result = targetReadWord(address + word * 4U, originalData[word]);
    if (result != TargetResult::Ok) return result;
  }

  // 2. Perform 64-byte page erase
  result = targetErasePage(address);
  if (result != TargetResult::Ok) return result;

  // 3. Read back 64-byte contents and verify 0xFFFFFFFF (CH32V003 erased flash value)
  bool verified = true;
  for (uint8_t word = 0; word < 16; ++word) {
    result = targetReadWord(address + word * 4U, erasedData[word]);
    if (result != TargetResult::Ok) return result;
    if (erasedData[word] != 0xFFFFFFFFUL) {
      verified = false;
    }
  }

  return verified ? TargetResult::Ok : TargetResult::VerifyFailed;
}

TargetResult targetProgram64Page(uint32_t address, const uint8_t data[64]) {
  if (!flashAddressValid(address, 64) || (address & 0x3FU) != 0) return TargetResult::InvalidArgument;
  TargetResult result = targetUnlockFlash();
  if (result != TargetResult::Ok) return result;
  result = targetWriteWord(FLASH_CTLR, FLASH_CTLR_PAGE_PG); if (result != TargetResult::Ok) return result;
  result = targetWriteWord(FLASH_CTLR, FLASH_CTLR_BUF_RST | FLASH_CTLR_PAGE_PG); if (result != TargetResult::Ok) return result;
  result = waitForFlash(); if (result != TargetResult::Ok) return result;
  for (uint8_t word = 0; word < 16; ++word) {
    uint32_t value;
    memcpy(&value, &data[word * 4], sizeof(value));
    result = targetWriteWord(address + word * 4U, value);
    if (result != TargetResult::Ok) return result;
  }
  result = targetWriteWord(FLASH_ADDR, address); if (result != TargetResult::Ok) return result;
  result = targetWriteWord(FLASH_CTLR, FLASH_CTLR_PAGE_PG | FLASH_CTLR_STRT); if (result != TargetResult::Ok) return result;
  result = waitForFlash(); if (result != TargetResult::Ok) return result;
  return targetWriteWord(FLASH_CTLR, 0);
}

TargetResult targetProgram64(uint32_t address, const uint8_t data[64]) {
  if (!flashAddressValid(address, 64) || (address & 0x3FU) != 0) return TargetResult::InvalidArgument;
  TargetResult result = targetErasePage(address);
  if (result != TargetResult::Ok) return result;
  result = targetProgram64Page(address, data);
  if (result != TargetResult::Ok) return result;
  return targetVerifyBinary(address, data, 64);
}

TargetResult targetTestProgram64(uint32_t address, const uint8_t pattern[64], uint8_t readback[64], uint32_t& mismatchAddr, uint8_t& expectedVal, uint8_t& actualVal) {
  if (!flashAddressValid(address, 64) || (address & 0x3FU) != 0) {
    return TargetResult::InvalidArgument;
  }
  TargetResult result = requireDetected();
  if (result != TargetResult::Ok) return result;

  // 1. Verify pre-programming state: page must be erased (all 0xFF)
  for (uint8_t word = 0; word < 16; ++word) {
    uint32_t checkVal = 0;
    result = targetReadWord(address + word * 4U, checkVal);
    if (result != TargetResult::Ok) return result;
    if (checkVal != 0xFFFFFFFFUL) {
      Serial.printf("Pre-program check failed: [0x%08lX] = 0x%08lX (not erased)\n",
                    (unsigned long)(address + word * 4U), (unsigned long)checkVal);
      return TargetResult::VerifyFailed;
    }
  }

  // 2. Perform 64-byte fast page programming without extra erase
  result = targetProgram64Page(address, pattern);
  if (result != TargetResult::Ok) return result;

  // 3. Read back 64 bytes via abstract-command memory-read path
  for (uint8_t word = 0; word < 16; ++word) {
    uint32_t val = 0;
    result = targetReadWord(address + word * 4U, val);
    if (result != TargetResult::Ok) return result;
    memcpy(&readback[word * 4], &val, sizeof(val));
  }

  // 4. Byte-by-byte comparison against expected deterministic payload
  mismatchAddr = 0;
  for (uint8_t i = 0; i < 64; ++i) {
    if (readback[i] != pattern[i]) {
      mismatchAddr = address + i;
      expectedVal = pattern[i];
      actualVal = readback[i];
      return TargetResult::VerifyFailed;
    }
  }

  return TargetResult::Ok;
}

TargetResult targetVerify64(uint32_t address, const uint8_t data[64]) {
  if (!flashAddressValid(address, 64)) return TargetResult::InvalidArgument;
  for (uint8_t word = 0; word < 16; ++word) {
    uint32_t actual = 0;
    TargetResult result = targetReadWord(address + word * 4U, actual);
    if (result != TargetResult::Ok) return result;
    uint32_t expected;
    memcpy(&expected, &data[word * 4], sizeof(expected));
    if (actual != expected) return TargetResult::VerifyFailed;
  }
  return TargetResult::Ok;
}

TargetResult targetReadMemory(uint32_t address, uint8_t* buffer, size_t length) {
  if (length == 0 || buffer == nullptr) return TargetResult::InvalidArgument;
  TargetResult result = requireDetected();
  if (result != TargetResult::Ok) return result;

  for (size_t i = 0; i < length; ++i) {
    const uint32_t checkAddr = address + i;
    const uint32_t wordAlignedAddr = checkAddr & ~3UL;
    uint32_t wordVal = 0;
    result = targetReadWord(wordAlignedAddr, wordVal);
    if (result != TargetResult::Ok) return result;
    buffer[i] = (wordVal >> ((checkAddr & 3UL) * 8)) & 0xFF;
  }
  return TargetResult::Ok;
}

TargetResult targetProgramBinary(uint32_t address, const uint8_t* data, size_t length, ProgressCallback progress) {
  if (length == 0 || data == nullptr || !flashAddressValid(address, static_cast<uint32_t>(length))) {
    return TargetResult::InvalidArgument;
  }
  TargetResult result = requireDetected();
  if (result != TargetResult::Ok) return result;

  size_t bytesWritten = 0;
  while (bytesWritten < length) {
    const uint32_t curAddr = address + bytesWritten;
    const uint32_t pageAddr = curAddr & ~0x3FUL;
    const uint32_t offsetInPage = curAddr & 0x3FUL;
    const size_t bytesToCopy = (64U - offsetInPage) < (length - bytesWritten) ? (64U - offsetInPage) : (length - bytesWritten);

    // 1. Erase only the specific 64-byte page touched by this chunk
    result = targetErasePage(pageAddr);
    if (result != TargetResult::Ok) return result;

    // 2. Prepare 64-byte page buffer (pad unwritten bytes with 0xFF)
    uint8_t pageBuffer[64];
    memset(pageBuffer, 0xFF, sizeof(pageBuffer));
    memcpy(&pageBuffer[offsetInPage], &data[bytesWritten], bytesToCopy);

    // 3. Fast-program 64-byte block
    result = targetProgram64Page(pageAddr, pageBuffer);
    if (result != TargetResult::Ok) return result;

    // 4. Read each 32-bit word once and compare its relevant bytes.
    result = verifyBytes(curAddr, &data[bytesWritten], bytesToCopy, nullptr);
    if (result != TargetResult::Ok) return result;

    bytesWritten += bytesToCopy;
    if (progress != nullptr) {
      progress(bytesWritten, length, curAddr + bytesToCopy - 1U);
    }
  }

  return TargetResult::Ok;
}

TargetResult targetVerifyBinary(uint32_t address, const uint8_t* data, size_t length, ProgressCallback progress) {
  if (length == 0 || data == nullptr || !flashAddressValid(address, static_cast<uint32_t>(length))) {
    return TargetResult::InvalidArgument;
  }
  TargetResult result = requireDetected();
  if (result != TargetResult::Ok) return result;
  return verifyBytes(address, data, length, progress);
}

TargetResult targetResetRun() {
  TargetResult result = requireDetected();
  if (result != TargetResult::Ok) return result;
  result = writeReg(DMI_DMCONTROL, 0x80000003UL);
  if (result != TargetResult::Ok) return result;
  result = writeReg(DMI_DMCONTROL, 0x40000001UL);
  return result;
}

const char* targetResultText(TargetResult result) {
  switch (result) {
    case TargetResult::Ok: return "OK";
    case TargetResult::NotDetected: return "target not detected";
    case TargetResult::DmiFailure: return "DMI transaction failed";
    case TargetResult::AbstractTimeout: return "abstract command timeout";
    case TargetResult::AbstractError: return "abstract command error";
    case TargetResult::InvalidTarget: return "unexpected target identity";
    case TargetResult::FlashLocked: return "flash remains locked";
    case TargetResult::FlashTimeout: return "flash busy timeout";
    case TargetResult::FlashError: return "flash status error";
    case TargetResult::VerifyFailed: return "read-back mismatch";
    case TargetResult::InvalidArgument: return "invalid command argument";
  }
  return "unknown error";
}
