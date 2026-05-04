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

   private:
    LittleFS_Program fs;
    static constexpr size_t kBufSize = 512;
};
