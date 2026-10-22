// dc-boot: 起動の選択だけ (docs/loop_nonblocking_spec.md §7.11)。
// Teensy コアの ResetHandler が FlexRAM と SP を設定した直後、RAM の初期化前に呼ぶ startup_early_hook() で、SRC_GPR10 のフラグを見て
// dc-firmware か dc-sd-reader の IVT の entry へジャンプする。飛び先の ResetHandler は FlexRAM / SP / RAM の初期化を自分でやり直す。
// どちらのイメージも無いときだけここの setup() / loop() まで落ち、LED を点滅させる (USB には "KART Boot" のシリアルが出る)。
// eFuse (OCOTP) には触らない。
#include <Arduino.h>

#include "util/boot_layout.hpp"

namespace {
// ITCM / DTCM がまだ無いので FLASHMEM に置き、グローバル変数を使わない (レジスタと定数だけ)
FLASHMEM bool hasImage(uint32_t base, uint32_t size) {
    const volatile uint32_t* ivt = (const volatile uint32_t*)(base + boot_layout::kIvtOffset);
    if ((ivt[0] & 0x00FFFFFF) != boot_layout::kIvtHeader)
        return false;
    const uint32_t entry = ivt[1];
    return entry >= base && entry < base + size;
}

FLASHMEM [[noreturn]] void jumpTo(uint32_t base) {
    const volatile uint32_t* ivt = (const volatile uint32_t*)(base + boot_layout::kIvtOffset);
    ((void (*)(void))ivt[1])();
    __builtin_unreachable();
}
}  // namespace

extern "C" FLASHMEM void startup_early_hook() {
    using namespace boot_layout;
    const bool wantSdReader = (BOOT_FLAG_REG & ~kFlagRomBitsMask) == kFlagSdReader;
    if (wantSdReader)
        BOOT_FLAG_REG &= kFlagRomBitsMask;  // 読んだら消す (sd-reader で止まっても次は dc-firmware)。ほかの値は dc-firmware が debug に出して消す

    // 望んだ方 → もう一方 → (無ければ LED へ)
    const uint32_t first = wantSdReader ? kSdReaderBase : kMainBase;
    const uint32_t firstSize = wantSdReader ? kSdReaderSize : kMainSize;
    const uint32_t second = wantSdReader ? kMainBase : kSdReaderBase;
    const uint32_t secondSize = wantSdReader ? kMainSize : kSdReaderSize;
    if (hasImage(first, firstSize))
        jumpTo(first);
    if (wantSdReader)  // 望んだ dc-sd-reader が無い: 印を置いて dc-firmware へ (dc-firmware が debug に出す)
        BOOT_FLAG_REG = (BOOT_FLAG_REG & kFlagRomBitsMask) | kFlagMain | kReasonNoSdReader;
    if (hasImage(second, secondSize))
        jumpTo(second);
}

void setup() {
    pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
    // 3 回短く点滅して休む = イメージが無い
    for (int i = 0; i < 3; i++) {
        digitalWrite(LED_BUILTIN, HIGH);
        delay(100);
        digitalWrite(LED_BUILTIN, LOW);
        delay(150);
    }
    delay(800);
}
