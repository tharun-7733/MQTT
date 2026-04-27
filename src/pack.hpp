#ifndef PACK_HPP
#define PACK_HPP

#include <cstdint>
#include <cstddef>
#include "util.hpp"

/* bytestring RAII helpers */
struct bytestring *bytestring_create(size_t);
void bytestring_init(struct bytestring *, size_t);
void bytestring_release(struct bytestring *);
void bytestring_reset(struct bytestring *);

/* Unpack helpers (read from const buffer) */
uint8_t  unpack_u8(const uint8_t **buf);
uint16_t unpack_u16(const uint8_t **buf);
uint32_t unpack_u32(const uint8_t **buf);
uint8_t *unpack_bytes(const uint8_t **buf, size_t len, uint8_t *str);
uint16_t unpack_string16(const uint8_t **buf, uint8_t **dest);

/* Pack helpers (write into buffer) */
void pack_u8(uint8_t **buf, uint8_t val);
void pack_u16(uint8_t **buf, uint16_t val);
void pack_u32(uint8_t **buf, uint32_t val);
void pack_bytes(uint8_t **buf, uint8_t *str);

#endif // PACK_HPP
