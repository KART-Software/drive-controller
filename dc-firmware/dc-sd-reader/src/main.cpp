// dc-sd-reader: SD リーダーファーム。docs/loop_nonblocking_spec.md §7.11。
// drive-controller (dc-firmware) の RebootCmd { SD_READER } で再起動すると、dc-boot (flash の先頭) がここ (0x60080000) へ飛ぶ。
// SD カードを USB の MTP で PC に見せる。戻るのは USB を抜く (SOF が 3 s 止まる) か、console の Reboot (RebootCmd)、電源の入れ直し。
// eFuse (OCOTP) には触らない。使うのは SRC_GPR10 (dc-boot が消してから飛んでくる) と SCB_AIRCR だけ。
#include <Arduino.h>
#include <MTP_Teensy.h>
#include <SD.h>

#include "constants.hpp"  // dc-firmware のピン定義 (出力を安全側に固定するため)
#include "util/boot_layout.hpp"
#include "wire.hpp"

namespace {
constexpr uint32_t kHostGoneMs = 3000;  // SOF (USB1_FRINDEX) がこれだけ止まったらホストがいない

uint32_t lastFrindex = 0;
uint32_t lastHostMs = 0;
bool sdOk = false;  // SD.begin() が成功した (失敗なら MTP に空の装置が出るだけなので、LED で知らせる)

[[noreturn]] void rebootInto(bool sdReader, uint8_t reason) {
    using namespace boot_layout;
    BOOT_FLAG_REG = (BOOT_FLAG_REG & kFlagRomBitsMask) | (sdReader ? kFlagSdReader : (kFlagMain | reason));
    asm volatile("dsb" ::: "memory");
    SCB_AIRCR = 0x05FA0004;  // SYSRESETREQ → ROM → dc-boot (フラグ無しなら dc-firmware)
    for (;;) {
    }
}
}  // namespace

void setup() {
    // 出力を安全側に固定。点火リレー LOW (エンジンは掛からない)、モータードライバ sleep、リレー / シフト出力 LOW
    for (uint8_t pin : {(uint8_t)SHUTDOWN_RELAY_PIN, (uint8_t)DC_MOTOR_SLP_PIN, (uint8_t)DC_MOTOR_PWM_PIN,
                        (uint8_t)DC_MOTOR_DIR_PIN, (uint8_t)DC_MOTOR_RELAY_PIN, (uint8_t)STARTER_RELAY_PIN,
                        (uint8_t)AUTO_SHIFT_UP_OUT_PIN, (uint8_t)AUTO_SHIFT_DOWN_OUT_PIN}) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
    }
    pinMode(LED_BUILTIN, OUTPUT);

    MTP.begin();
    sdOk = SD.begin(BUILTIN_SDCARD);
    if (sdOk)
        MTP.addFilesystem(SD, "SD Card");
}

void loop() {
    MTP.loop();  // ファイル転送中は数秒戻ってこない。時刻はこの後に読む
    const uint32_t nowMs = millis();
    // LED: 速い点滅 (250 ms) = SD リーダー。カードが無い / 読めないときは 2 回点滅して休む
    const uint32_t t = nowMs % 1500;
    digitalWrite(LED_BUILTIN, sdOk ? (nowMs / 250) & 1 : (t < 100 || (t >= 250 && t < 350)));

    const int reboot = wire::pollReboot();
    if (reboot >= 0) {
        delay(100);  // 応答が USB に流れてから
        rebootInto(reboot == 1, boot_layout::kReasonCommand);
    }

    const uint32_t fr = USB1_FRINDEX;
    if (fr != lastFrindex) {
        lastFrindex = fr;
        lastHostMs = nowMs;
    } else if (nowMs - lastHostMs > kHostGoneMs) {
        rebootInto(false, boot_layout::kReasonHostGone);  // USB が抜かれた → dc-firmware
    }
}
