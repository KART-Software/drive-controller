#ifndef _ERROR_HANDLER_H_
#define _ERROR_HANDLER_H_
#include <Arduino.h>

#define ERR_TPS_IMPLAUSIBLE 0
#define ERR_APPS_IMPLAUSIBLE 1
#define ERR_TPS_1_CIRCUIT_FAILURE 2
#define ERR_TPS_2_CIRCUIT_FAILURE 3
#define ERR_APPS_1_CIRCUIT_FAILURE 4
#define ERR_APPS_2_CIRCUIT_FAILURE 5
#define ERR_APPS_TPS_TARGET_FAILURE 6
#define ERR_BPS_CIRCUIT_FAILURE 7
#define ERR_BPS_TPS_IMPLAUSIBLE 8

namespace etc {

class ErrorHandler {
   public:
    void raise(uint8_t errID) { _bits |= (1u << errID); }
    void clear(uint8_t errID) { _bits &= ~(1u << errID); }
    void clearAll() { _bits = 0; }
    bool raised(uint8_t errID) const { return _bits & (1u << errID); }
    uint16_t bits() const { return _bits; }

   private:
    uint16_t _bits = 0;
};

}  // namespace etc

#endif