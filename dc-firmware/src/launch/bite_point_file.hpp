#pragma once

#include "proto/drive_controller.pb.h"
#include "util/flash.hpp"

namespace launch {

// Thin wrapper around Flash that only permits reading/writing the bite point.
// Prevents launch code from accessing arbitrary flash files.
class BitePointFile {
   public:
    explicit BitePointFile(Flash& flash) : flash_(flash) {}

    float load(float defaultPosition = 50.0f) const {
        dc_BitePoint msg = dc_BitePoint_init_zero;
        if (flash_.readProto(PATH, dc_BitePoint_fields, &msg)) {
            if (msg.position > 0.0f && msg.position <= 100.0f) {
                return msg.position;
            }
        }
        return defaultPosition;
    }

    void save(float position) {
        dc_BitePoint msg = dc_BitePoint_init_zero;
        msg.position = position;
        flash_.writeProto(PATH, dc_BitePoint_fields, &msg);
    }

   private:
    static constexpr const char* PATH = "/bite_point.pb";
    Flash& flash_;
};

}  // namespace launch
