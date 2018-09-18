#ifndef __BSD_CRC32_H_
#define __BSD_CRC32_H_

#include <stdint.h>

#define crc32 tnt_crc32
uint32_t tnt_crc32(const void *buf, size_t size);
#define crc32c tnt_crc32c
uint32_t tnt_crc32c(uint32_t crc32c, const char *buffer, unsigned int length);

#endif
