/*
** Working with LEB128/ULEB128 encoding.
**
** Major portions taken verbatim or adapted from the LuaVela.
** Copyright (C) 2015-2019 IPONWEB Ltd.
*/

#define lj_utils_leb128_c
#define LUA_CORE

#include "lj_utils.h"
#include "lj_obj.h"

#define LINK_BIT          (0x80)
#define MIN_TWOBYTE_VALUE (0x80)
#define PAYLOAD_MASK      (0x7f)
#define SHIFT_STEP        (7)
#define LEB_SIGN_BIT      (0x40)

/* ------------------------- Reading LEB128/ULEB128 ------------------------- */

/*
** XXX: For each LEB128 type (signed/unsigned) we have two versions of read
** functions: The one consuming unlimited number of input octets and the one
** consuming not more than given number of input octets. Currently reading
** is not used in performance critical places, so these two functions are
** implemented via single low-level function + run-time mode check. Feel free
** to change if this becomes a bottleneck.
*/

static LJ_AINLINE size_t _read_leb128(int64_t *out, const uint8_t *buffer,
				      size_t n)
{
  size_t i = 0;
  uint64_t shift = 0;
  int64_t value = 0;
  uint8_t octet;

  for(;;) {
    if (n != 0 && i + 1 > n)
      return 0;
    octet = buffer[i++];
    value |= ((int64_t)(octet & PAYLOAD_MASK)) << shift;
    shift += SHIFT_STEP;
    if (!(octet & LINK_BIT))
      break;
  }

  if (octet & LEB_SIGN_BIT && shift < sizeof(int64_t) * 8)
    value |= -(1 << shift);

  *out = value;
  return i;
}

size_t LJ_FASTCALL lj_utils_read_leb128(int64_t *out, const uint8_t *buffer)
{
  return _read_leb128(out, buffer, 0);
}

size_t LJ_FASTCALL lj_utils_read_leb128_n(int64_t *out, const uint8_t *buffer,
					  size_t n)
{
  return _read_leb128(out, buffer, n);
}


static LJ_AINLINE size_t _read_uleb128(uint64_t *out, const uint8_t *buffer,
				       size_t n)
{
  size_t i = 0;
  uint64_t value = 0;
  uint64_t shift = 0;
  uint8_t octet;

  for(;;) {
    if (n != 0 && i + 1 > n)
      return 0;
    octet = buffer[i++];
    value |= ((uint64_t)(octet & PAYLOAD_MASK)) << shift;
    shift += SHIFT_STEP;
    if (!(octet & LINK_BIT))
      break;
  }

  *out = value;
  return i;
}

size_t LJ_FASTCALL lj_utils_read_uleb128(uint64_t *out, const uint8_t *buffer)
{
  return _read_uleb128(out, buffer, 0);
}

size_t LJ_FASTCALL lj_utils_read_uleb128_n(uint64_t *out, const uint8_t *buffer,
					   size_t n)
{
  return _read_uleb128(out, buffer, n);
}

/* ------------------------- Writing LEB128/ULEB128 ------------------------- */

size_t LJ_FASTCALL lj_utils_write_leb128(uint8_t *buffer, int64_t value)
{
  size_t i = 0;

  /* LEB_SIGN_BIT propagation to check the remaining value. */
  while ((uint64_t)(value + LEB_SIGN_BIT) >= MIN_TWOBYTE_VALUE) {
    buffer[i++] = (uint8_t)((value & PAYLOAD_MASK) | LINK_BIT);
    value >>= SHIFT_STEP;
  }

  /* Omit LINK_BIT in case of overflow. */
  buffer[i++] = (uint8_t)(value & PAYLOAD_MASK);

  lj_assertX(i <= LEB128_U64_MAXSIZE, "bad leb128 size");

  return i;
}

size_t LJ_FASTCALL lj_utils_write_uleb128(uint8_t *buffer, uint64_t value)
{
  size_t i = 0;

  for (; value >= MIN_TWOBYTE_VALUE; value >>= SHIFT_STEP)
    buffer[i++] = (uint8_t)((value & PAYLOAD_MASK) | LINK_BIT);

  buffer[i++] = (uint8_t)value;

  lj_assertX(i <= LEB128_U64_MAXSIZE, "bad uleb128 size");

  return i;
}
