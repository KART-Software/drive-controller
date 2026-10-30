// dc-sd-reader: SD リーダーファーム。docs/loop_nonblocking_spec.md §7.11。
// drive-controller (dc-main) の RebootCmd { SD_READER } で再起動すると、dc-boot (flash の先頭) がここ (0x60080000) へ飛ぶ。
// SD カードを USB Mass Storage (Teensy4TinyUSB。CDC も同居) で PC に見せる = 普通の USB メモリとして出る。
// 戻るのは PC で「取り出し」をする、console の Reboot (RebootCmd)、USB を抜く (SOF が 3 s 止まる)、電源の入れ直し。
// eFuse (OCOTP) には触らない。使うのは SRC_GPR10 (dc-boot が消してから飛んでくる) と SCB_AIRCR だけ。
#include <Arduino.h>
#include <SdFat.h>
#include <Teensy4TinyUSB.h>
#include <Teensy4USBMscSdFat.h>

#include "constants.hpp"  // dc-main のピン定義 (出力を安全側に固定するため)
#include "util/boot_layout.hpp"
#include "wire.hpp"

namespace {
constexpr uint32_t kHostGoneMs = 3000;  // SOF (USB1_FRINDEX) がこれだけ止まったらホストがいない

SdFs sd;
Teensy4USBMscSdFat sdCard;  // カードは cardBegin() の後に付ける。無ければ PC には「メディア無し」のドライブが出る
uint32_t lastFrindex = 0;
uint32_t lastHostMs = 0;
bool sdOk = false;

// USB の処理を回しながら待つ (応答や取り出しの返事を送り終えるまで)
void pumpMs(uint32_t ms) {
    const uint32_t t0 = millis();
    while (millis() - t0 < ms)
        Teensy4USB::task();
}

[[noreturn]] void rebootInto(bool sdReader, uint8_t reason) {
    using namespace boot_layout;
    BOOT_FLAG_REG = (BOOT_FLAG_REG & kFlagRomBitsMask) | (sdReader ? kFlagSdReader : (kFlagMain | reason));
    asm volatile("dsb" ::: "memory");
    SCB_AIRCR = 0x05FA0004;  // SYSRESETREQ → ROM → dc-boot (フラグ無しなら dc-main)
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

    sdOk = sd.cardBegin(SdioConfig(FIFO_SDIO));  // カードだけ初期化 (ファイルシステムはマウントしない。PC が exFAT を読む)
    if (sdOk)
        sdCard.setCard(sd.card());
    Teensy4USBMsc::setBlockDevice(&sdCard);  // USB を始める前に (最初の問い合わせで容量を答えるため)
    Teensy4USBMsc::setInquiry("KART", "SD Reader", "1.0");
    // VID / PID は pid.codes のテスト用 (0x1209 / 0x0001。試作・内部利用向け)。console / dcmon / flash_all はこの組で見分ける
    Teensy4USB::begin({.vid = 0x1209,
                       .pid = 0x0001,
                       .manufacturer = "KART",
                       .product = "KART SD Reader",
                       .cdcName = "KART SD Reader Serial",
                       .mscName = "SD Card"});
}

void loop() {
    Teensy4USB::task();  // MSC の read / write もこの中のコールバックで行う
    const uint32_t nowMs = millis();
    // LED: 速い点滅 (250 ms) = SD リーダー。カードが無い / 読めないときは 2 回点滅して休む
    const uint32_t t = nowMs % 1500;
    digitalWrite(LED_BUILTIN, sdOk ? (nowMs / 250) & 1 : (t < 100 || (t >= 250 && t < 350)));

    const int reboot = wire::pollReboot();
    if (reboot >= 0) {
        pumpMs(100);  // 応答が USB に流れてから
        rebootInto(reboot == 1, boot_layout::kReasonCommand);
    }
    if (Teensy4USBMsc::ejected()) {  // PC で「取り出し」= 読み終わった → dc-main に戻る
        pumpMs(300);                 // ホストが取り出しの返事を受け取るまで
        rebootInto(false, boot_layout::kReasonEject);
    }

    const uint32_t fr = Teensy4USB::frameIndex();
    if (fr != lastFrindex) {
        lastFrindex = fr;
        lastHostMs = nowMs;
    } else if (nowMs - lastHostMs > kHostGoneMs) {
        rebootInto(false, boot_layout::kReasonHostGone);
    }
}
