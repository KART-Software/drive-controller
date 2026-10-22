#pragma once

#include <stdint.h>

#include "util/boot_layout.hpp"

// 起動モード (docs/loop_nonblocking_spec.md §7.11)。
// 起動の選択は別の薄いファーム dc-boot (flash の先頭) がする。このファームは RebootCmd { SD_READER } で SRC_GPR10 にフラグを置いて
// ソフトリセットするだけで、dc-sd-reader のことは知らない (配置とフラグの約束は util/boot_layout.hpp)。
// dc-boot はフラグを読んだ時点で消すので、その後の起動 (電源の入れ直しを含む) は必ずこのファーム。eFuse (OCOTP) には触らない。
enum class BootMode : uint8_t { Normal, SdReader };

namespace boot_mode {
// setup の先頭で 1 回: 前回の再起動の理由 (SRC_GPR10 の下位 28 bit) とリセットの理由 (SRC_SRSR) を debug に出し、消す
void logAndClear();
using boot_layout::kReasonCommand;
// reason: 次の起動の debug に出す理由 (boot_layout の kReason*。診断用)
[[noreturn]] void rebootInto(BootMode mode, uint8_t reason = 0);  // フラグを書いてソフトリセット (戻らない)
}  // namespace boot_mode
