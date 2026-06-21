#pragma once

#include <LittleFS.h>
#include <pb.h>

#define BEGIN_FS_LIMIT_TIMES 5

class Flash {
   public:
    bool initialize();
    bool writeProto(const char* fileName, const pb_msgdesc_t* fields, const void* msg);
    bool readProto(const char* fileName, const pb_msgdesc_t* fields, void* msg);
    void remove(const char* fileName);
    bool format();  // FS 全消去 (呼び出し側で保持したいデータを事前に RAM へ退避すること)
    size_t usedSize() { return fs.usedSize(); }
    size_t totalSize() { return fs.totalSize(); }

   private:
    LittleFS_Program fs;
    static constexpr size_t kBufSize = 512;
};
