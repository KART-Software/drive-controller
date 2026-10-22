#include "sd_log_store.hpp"

#include <TimeLib.h>
#include <stdio.h>
#include <string.h>

#include <optional>

#include "debug_logger.hpp"

namespace {

// 先頭の数字列 (4 桁以上。9999 を超えると 5 桁になる) を読む。数字が 4 桁未満か 65535 を超えれば nullopt。
// 戻り値は {番号, 数字列の長さ}
struct Digits {
    uint16_t value;
    size_t length;
};
std::optional<Digits> leadingDigits(const char* s) {
    uint32_t n = 0;
    size_t len = 0;
    while (s[len] >= '0' && s[len] <= '9') {
        n = n * 10 + (uint32_t)(s[len] - '0');
        if (n > 0xFFFF)
            return std::nullopt;
        len++;
    }
    if (len < 4)
        return std::nullopt;
    return Digits{.value = (uint16_t)n, .length = len};
}

// ルートの名前 "dc_NNNN" / "dc_NNNN_…" から番号を取り出す。形が違えば nullopt
std::optional<uint16_t> dirNumberOf(const char* name) {
    if (strncmp(name, "dc_", 3) != 0)
        return std::nullopt;
    const std::optional<Digits> d = leadingDigits(name + 3);
    if (!d)
        return std::nullopt;
    const char next = name[3 + d->length];
    if (next != '\0' && next != '_')
        return std::nullopt;
    return d->value;
}

// ディレクトリ内の名前 "NNNN.bin" から番号を取り出す。形が違えば nullopt
std::optional<uint16_t> fileIndexOf(const char* name) {
    const std::optional<Digits> d = leadingDigits(name);
    if (!d || strcmp(name + d->length, ".bin") != 0)
        return std::nullopt;
    return d->value;
}

}  // namespace

bool SdLogStore::begin() {
    mounted_ = false;
    // カードの初期化は 1 回だけ (format() の直後に CMD0 で初期化し直すと、カードが最後のセクタを書き終える前に中断して FS が壊れる)
    if (!cardReady_ && !sd_.cardBegin(SdioConfig(FIFO_SDIO))) {
        DebugLogger::log("SD: no card, logging disabled");
        return false;
    }
    cardReady_ = true;
    const bool mounted = sd_.volumeBegin();
    if (!mounted || sd_.fatType() != FAT_TYPE_EXFAT) {
        // exFAT 以外は使わない (確保が遅く、電源断の後に末尾へ古いデータが見える)。カードはログ専用なので作り直す
        DebugLogger::log("SD: %s, formatting as exFAT", mounted ? "not exFAT" : "no filesystem");
        if (!formatExFat() || !sd_.volumeBegin()) {
            DebugLogger::log("SD: format failed, logging disabled");
            return false;
        }
    }
    mounted_ = true;
    return makeDirectory();
}

bool SdLogStore::formatExFat() {
    uint8_t sectorBuf[512];
    ExFatFormatter formatter;
    const bool ok = formatter.format(sd_.card(), sectorBuf, nullptr);
    sd_.card()->syncDevice();  // カードが書き終えるのを待ってからマウントする
    return ok;
}

bool SdLogStore::makeDirectory() {
    const DirScan scan = scanDirs();
    dirNumber_ = scan.any ? (uint16_t)(scan.maxNumber + 1) : 1;
    if (year() >= 2020) {
        snprintf(dirName_, sizeof(dirName_), "dc_%04u_%04d%02d%02d_%02d%02d%02d", (unsigned)dirNumber_, year(),
                 month(), day(), hour(), minute(), second());
    } else {
        snprintf(dirName_, sizeof(dirName_), "dc_%04u", (unsigned)dirNumber_);
    }
    if (!sd_.mkdir(dirName_)) {
        DebugLogger::log("SD: mkdir failed (%s)", dirName_);
        return false;
    }
    nextFileIndex_ = 0;
    return true;
}

SdLogStore::DirScan SdLogStore::scanDirs() {
    DirScan s;
    FsFile root;
    if (!root.open(&sd_, "/", O_RDONLY))
        return s;
    FsFile entry;
    while (entry.openNext(&root, O_RDONLY)) {
        char name[48];
        entry.getName(name, sizeof(name));
        const bool isDir = entry.isDir();
        entry.close();
        if (!isDir)
            continue;
        const std::optional<uint16_t> n = dirNumberOf(name);
        if (!n)
            continue;
        if (!s.any || *n > s.maxNumber)
            s.maxNumber = *n;
        if (!s.any || *n < s.minNumber) {
            s.minNumber = *n;
            strlcpy(s.minName, name, sizeof(s.minName));
        }
        s.any = true;
    }
    root.close();
    return s;
}

bool SdLogStore::openNextFile(uint64_t bytes) {
    if (!mounted_)
        return false;
    closeFile();
    char path[64];
    snprintf(path, sizeof(path), "%s/%04u.bin", dirName_, (unsigned)nextFileIndex_);
    // 確保できなければ古いものを 1 つ消して (1 回に 1 ファイル分空く) やり直す。消せるものがなくなれば諦める
    for (int attempt = 0; attempt < 1000; attempt++) {
        if (!file_.open(&sd_, path, O_RDWR | O_CREAT | O_TRUNC)) {
            DebugLogger::log("SD: open failed (%s)", path);
            return false;
        }
        if (file_.preAllocate(bytes)) {
            fileIndex_ = nextFileIndex_;
            nextFileIndex_++;
            return true;
        }
        file_.close();  // 確保できなかった空のファイルは残さない
        sd_.remove(path);
        if (!deleteOldest()) {
            DebugLogger::log("SD: full, nothing to delete");
            return false;
        }
    }
    return false;
}

bool SdLogStore::deleteOldest() {
    const DirScan scan = scanDirs();
    if (!scan.any)
        return false;
    FsFile dir;
    if (!dir.open(&sd_, scan.minName, O_RDONLY))
        return false;
    char oldestName[16] = {};
    bool found = false;
    uint16_t oldestIndex = 0;
    FsFile entry;
    while (entry.openNext(&dir, O_RDONLY)) {
        char name[32];
        entry.getName(name, sizeof(name));
        const bool isDir = entry.isDir();
        entry.close();
        if (isDir)
            continue;
        const std::optional<uint16_t> n = fileIndexOf(name);
        if (!n)
            continue;
        if (!found || *n < oldestIndex) {
            oldestIndex = *n;
            strlcpy(oldestName, name, sizeof(oldestName));
        }
        found = true;
    }
    dir.close();

    const bool isCurrent = strcmp(scan.minName, dirName_) == 0;
    if (!found) {
        // ファイルのない (または名前の形が違うものだけの) ディレクトリ。今回の起動のものなら消せるものはない
        if (isCurrent)
            return false;
        DebugLogger::log("SD: rmdir %s", scan.minName);
        return sd_.rmdir(scan.minName);
    }
    char path[64];
    snprintf(path, sizeof(path), "%s/%s", scan.minName, oldestName);
    DebugLogger::log("SD: delete %s", path);
    if (!sd_.remove(path))
        return false;
    if (!isCurrent && sd_.rmdir(scan.minName))  // 空になっていれば消える (空でなければ失敗するだけ)
        DebugLogger::log("SD: rmdir %s (empty)", scan.minName);
    return true;
}

bool SdLogStore::write(const void* data, size_t len) {
    return file_.isOpen() && file_.write(data, len) == len;
}

bool SdLogStore::sync() {
    return file_.isOpen() && file_.sync();
}

void SdLogStore::closeFile() {
    if (!file_.isOpen())
        return;
    file_.truncate();  // 書いた分だけにする (確保した残りの領域を返す)
    file_.sync();
    file_.close();
}

bool SdLogStore::format() {
    closeFile();
    mounted_ = false;
    sd_.end();  // ボリュームを捨てる (カードはそのまま。begin() が同じカードを使う)
    // 起動時にカードがなかった場合もここで初期化するので、後から挿したカードに format_sd を送るとそこから記録が始まる
    // (「起動時に挿さっているときだけ」の例外。意図した機能ではないが害はない)
    if (!cardReady_ && !sd_.cardBegin(SdioConfig(FIFO_SDIO))) {
        DebugLogger::log("SD: no card");
        return false;
    }
    cardReady_ = true;
    return formatExFat();
}
