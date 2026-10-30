#pragma once

#include <imxrt.h>
#include <stdint.h>

// flash 上の 3 つのイメージの配置と、起動の選択に使うフラグ (docs/loop_nonblocking_spec.md §7.11)。
// dc-boot (起動の選択だけ) / dc-main (drive-controller 本体) / dc-sd-reader (SD を USB Mass Storage で見せる) が共有する唯一の約束。
// 各プロジェクトのリンカスクリプト (imxrt1062_t41_*.ld の FLASH ORIGIN / LENGTH) と一致させること。
// 全部 1 MB 以内なのは、PlatformIO 同梱の teensy_loader_cli 2.2 が 1 MB より先のアドレスを受け付けないため。
// eFuse (OCOTP) には触らない。
namespace boot_layout {
constexpr uint32_t kBootBase = 0x60000000, kBootSize = 64u * 1024;        // dc-boot: ROM が飛んでくる唯一のイメージ
constexpr uint32_t kMainBase = 0x60010000, kMainSize = 448u * 1024;       // dc-main
constexpr uint32_t kSdReaderBase = 0x60080000, kSdReaderSize = 512u * 1024;  // dc-sd-reader

// フラグは SRC_GPR10 の下位 28 bit。POR でしか消えず、ソフトリセット (SYSRESETREQ) をまたぐ (Teensy コアも SRC_GPR5 を CrashReport の印に使う)。
// **上位 4 bit (31〜28) は ROM が使う** (bit 30 = PERSIST_SECONDARY_BOOT: 立てると ROM が fuse のオフセットに 2 つ目のイメージを探しに行き、
// Teensy では起動しない。ベンチで止まった)。読み書きとも触らない。SNVS の LPGPR0 はソフトリセットで残らなかったので使わない。
// imxrt.h は SRC_GPR5 までしか公開していない
#define BOOT_FLAG_REG (IMXRT_SRC.offset044)
constexpr uint32_t kFlagRomBitsMask = 0xF0000000;
constexpr uint32_t kFlagSdReader = 0x00534452;  // "SDR": 次の起動は dc-sd-reader
constexpr uint32_t kFlagMain = 0x004E4D00;      // "NM" + 理由コード (下位 8 bit): dc-main への再起動の理由 (診断用。dc-main が debug に出す)
constexpr uint32_t kFlagMainMask = 0x0FFFFF00;
// 理由コード (kFlagMain の下位 8 bit)
constexpr uint8_t kReasonHostGone = 1;         // dc-sd-reader: USB ホストがいなくなった
constexpr uint8_t kReasonCommand = 2;          // RebootCmd
constexpr uint8_t kReasonEject = 3;            // dc-sd-reader: PC が「取り出し」をした
constexpr uint8_t kReasonNoSdReader = 0xFE;    // dc-boot: dc-sd-reader が要求されたがイメージが無く、dc-main を起動した

constexpr uint32_t kIvtOffset = 0x1000;        // IVT はイメージ先頭 + 4 KB (FlexSPI NOR、RM Table 9-36)
constexpr uint32_t kIvtHeader = 0x002000D1;    // IVT ヘッダ (tag 0xD1、length 0x20。version は見ない)
}  // namespace boot_layout
