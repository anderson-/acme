#include "swio.h"

#include "esp_cpu.h"
#include "soc/gpio_reg.h"

namespace {

constexpr uint32_t kSwioMask = 1UL << SWIO_PIN;
// Bit-bang delay primitive, kept verbatim from CNLohr's ESP32-S2 PrecDelay().
// The loop body is architecture-specific: Xtensa (S3/ESP32) uses `bbci`, while
// RISC-V (C3) needs a plain `bne` — `bbci` is not a RISC-V instruction.
// DEFAULT_T1COEFF (project.yaml) overrides the per-architecture default.
#ifdef DEFAULT_T1COEFF
constexpr int kUpstreamT1Coeff = DEFAULT_T1COEFF;
#elif defined(__riscv)
constexpr int kUpstreamT1Coeff = 7;   // C3 @ 160 MHz.
#else
constexpr int kUpstreamT1Coeff = 10;  // Xtensa LX7 (S3/ESP32) @ 240 MHz.
#endif
constexpr uint32_t kReadTimeoutSamples = 1000;

static inline void IRAM_ATTR upstreamPrecDelay(int delay) {
#if defined(__XTENSA__)
  __asm__ __volatile__(
      "1:\taddi %[delay], %[delay], -1\n"
      "\tbbci %[delay], 31, 1b\n"
      : [delay] "+r"(delay));
#elif defined(__riscv)
  __asm__ __volatile__(
      "1:\taddi %[delay], %[delay], -1\n"
      "\tbne %[delay], x0, 1b\n"
      : [delay] "+r"(delay));
#else
#error "upstreamPrecDelay: unsupported architecture"
#endif
}

inline bool IRAM_ATTR swioLineHigh() {
  return (REG_READ(GPIO_IN_REG) & kSwioMask) != 0;
}

}  // namespace

bool swioInit() {
  // Match the upstream startup state: input pull-up enabled, then output
  // enabled with its latch high.  The external pull-up remains installed.
  pinMode(SWIO_PIN, INPUT_PULLUP);
  swioDriveHigh();
  delay(5);
  return true;
}

void swioSynchronize() {
  // InitializeSWDSWIO begins its configuration writes from this driven-high
  // state; it has no 20 ms SWIO-low reset pulse.
  swioDriveHigh();
}

uint32_t swioMeasureUpstreamT1Cycles() {
    constexpr uint32_t kSamples = 128;

    const uint32_t start = esp_cpu_get_cycle_count();

    for (uint32_t i = 0; i < kSamples; ++i) {
        upstreamPrecDelay(kUpstreamT1Coeff);
    }

    return (esp_cpu_get_cycle_count() - start) / kSamples;
}
void IRAM_ATTR swioDriveLow() {
  REG_WRITE(GPIO_OUT_W1TC_REG, kSwioMask);
  REG_WRITE(GPIO_ENABLE_W1TS_REG, kSwioMask);
}

void IRAM_ATTR swioDriveHigh() {
  REG_WRITE(GPIO_OUT_W1TS_REG, kSwioMask);
  REG_WRITE(GPIO_ENABLE_W1TS_REG, kSwioMask);
}

void IRAM_ATTR swioRelease() {
  REG_WRITE(GPIO_ENABLE_W1TC_REG, kSwioMask);
}

void IRAM_ATTR swioWriteBit(bool value) {
  swioDriveLow();
  upstreamPrecDelay(value ? kUpstreamT1Coeff : kUpstreamT1Coeff * 4);
  swioDriveHigh();
  upstreamPrecDelay(kUpstreamT1Coeff);
}

SwioBitResult IRAM_ATTR swioReadBit() {
  // Port of upstream ReadBitSWIO(): low trigger; release while preloading the
  // output latch high; sample after 2*t1; then restore driven-high state.
  swioDriveLow();
  upstreamPrecDelay(kUpstreamT1Coeff);
  swioRelease();
  REG_WRITE(GPIO_OUT_W1TS_REG, kSwioMask);
  upstreamPrecDelay(kUpstreamT1Coeff * 2);

  const bool sampledHigh = swioLineHigh();
  for (uint32_t i = 0; i < kReadTimeoutSamples; ++i) {
    if (swioLineHigh()) {
      swioDriveHigh();
      upstreamPrecDelay(kUpstreamT1Coeff / 2);
      return sampledHigh ? SwioBitResult::One : SwioBitResult::Zero;
    }
  }
  // This is the upstream timeout recovery state, not an indication of target
  // communication success.
  swioDriveHigh();
  return SwioBitResult::Timeout;
}
