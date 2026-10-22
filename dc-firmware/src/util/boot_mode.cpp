#include "boot_mode.hpp"

#include <Arduino.h>

#include "util/boot_layout.hpp"
#include "util/log/debug_logger.hpp"

using namespace boot_layout;

void boot_mode::logAndClear() {
    const uint32_t v = BOOT_FLAG_REG & ~kFlagRomBitsMask;
    BOOT_FLAG_REG &= kFlagRomBitsMask;
    // リセットの理由 (SRC_SRSR、w1c): bit0 電源投入、bit1 ソフト (SCB_AIRCR)、bit2 CSU、bit4 外部リセット、bit5/8 ウォッチドッグ、bit16 温度
    const uint32_t srsr = SRC_SRSR;
    SRC_SRSR = srsr;
    const char* why = "";
    if ((v & kFlagMainMask) == kFlagMain) {
        switch ((uint8_t)v) {
            case kReasonHostGone:   why = " (sd-reader: USB host gone)"; break;
            case kReasonCommand:    why = " (reboot command)"; break;
            case kReasonNoSdReader: why = " (sd-reader requested but no image at 0x60080000)"; break;
            default:                why = " (reboot)"; break;
        }
    }
    DebugLogger::log("boot: SRC_GPR10=%08lx SRSR=%08lx%s", (unsigned long)v, (unsigned long)srsr, why);
}

void boot_mode::rebootInto(BootMode mode, uint8_t reason) {
    BOOT_FLAG_REG = (BOOT_FLAG_REG & kFlagRomBitsMask) | (mode == BootMode::SdReader ? kFlagSdReader : (kFlagMain | reason));
    asm volatile("dsb" ::: "memory");
    SCB_AIRCR = 0x05FA0004;  // SYSRESETREQ (Teensy のソフトリセット) → ROM → dc-boot
    for (;;) {
    }
}
