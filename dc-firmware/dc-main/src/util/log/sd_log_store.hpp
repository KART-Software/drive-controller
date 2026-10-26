#pragma once

#include <SdFat.h>

#include "interfaces/util/log/log_store.hpp"

// Teensy 4.1 内蔵 SD スロット (SDIO、FIFO モード = CPU がコピーするので DMA もキャッシュも関係しない) への LogStore 実装
// (docs/loop_nonblocking_spec.md §7.4〜7.7)。カードはログ専用 (exFAT でなければフォーマットする = 中身が消える)。
// 単一インスタンス専用 (SDIO は 1 つ)。
class SdLogStore final : public LogStore {
   public:
    bool begin() override;
    bool openNextFile(uint64_t bytes) override;
    bool write(const void* data, size_t len) override;
    bool sync() override;
    void closeFile() override;
    bool format() override;
    uint16_t dirNumber() const override { return dirNumber_; }
    uint16_t fileIndex() const override { return fileIndex_; }

   private:
    struct DirScan {
        bool any = false;
        uint16_t minNumber = 0;
        uint16_t maxNumber = 0;
        char minName[48] = {};  // 最小の番号のディレクトリ名
    };
    DirScan scanDirs();
    bool formatExFat();
    bool makeDirectory();
    bool deleteOldest();  // 一番古いファイル (または空のディレクトリ) を 1 つ消す。消せるものがなければ false

    SdFs sd_;
    FsFile file_;
    bool cardReady_ = false;  // cardBegin() 済み (以後、再初期化しない)
    bool mounted_ = false;
    char dirName_[48] = {};
    uint16_t dirNumber_ = 0;
    uint16_t fileIndex_ = 0;      // 今開いているファイル
    uint16_t nextFileIndex_ = 0;  // 次に作るファイル
};
