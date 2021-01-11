#pragma once

#include <cstring>

namespace llsm {
namespace key_utils {

// Returns the data pointed to by `p` as if it is a pointer to type `T`.
//
// The caller should ensure that the size of the buffer pointed to by `p` is at
// least as large as `sizeof(T)`.
//
// Acknowledgement: This function was originally written by Viktor Leis.
template <class T>
static T LoadUnaligned(const void* p) {
  T x;
  memcpy(&x, p, sizeof(T));
  return x;
}

// Extracts a 4 byte order-preserving prefix of a given key.
//
// This function assumes that keys are ordered lexicographically and that the
// system is little endian. Its purpose is to extract a prefix that can be used
// for fast comparisons.
//
// Acknowledgement: This function was originally written by Viktor Leis.
static uint32_t ExtractHead(const uint8_t* key, unsigned key_length) {
  switch (key_length) {
    case 0:
      return 0;
    case 1:
      return static_cast<uint32_t>(key[0]) << 24;
    case 2:
      return static_cast<uint32_t>(
                 __builtin_bswap16(LoadUnaligned<uint16_t>(key)))
             << 16;
    case 3:
      return (static_cast<uint32_t>(
                  __builtin_bswap16(LoadUnaligned<uint16_t>(key)))
              << 16) |
             (static_cast<uint32_t>(key[2]) << 8);
    default:
      return __builtin_bswap32(LoadUnaligned<uint32_t>(key));
  }
}

}  // namespace key_utils
}  // namespace llsm