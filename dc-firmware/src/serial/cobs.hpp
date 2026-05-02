#ifndef _COBS_H_
#define _COBS_H_

#include <stddef.h>
#include <stdint.h>

// Consistent Overhead Byte Stuffing (COBS) encode/decode.
//
// Encoded output never contains 0x00 bytes; a single 0x00 is used as a frame
// delimiter on the wire. Caller is responsible for writing the trailing 0x00
// after `encode`.
//
// Buffer sizing rule: encoded length is at most `src_len + (src_len / 254) + 1`.
namespace cobs {

// Returns the number of bytes written into `dst` (excluding the terminating
// 0x00 byte, which the caller appends). `dst` must have at least
// `src_len + (src_len / 254) + 1` bytes of capacity.
size_t encode(const uint8_t *src, size_t src_len, uint8_t *dst);

// Decodes `src` (without the trailing 0x00) of length `src_len` into `dst`,
// which must have at least `src_len` bytes of capacity. Returns the number of
// bytes written, or 0 on malformed input.
size_t decode(const uint8_t *src, size_t src_len, uint8_t *dst);

} // namespace cobs

#endif // _COBS_H_
