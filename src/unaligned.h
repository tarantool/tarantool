#ifndef TARANTOOL_UNALIGNED_H_INCLUDED
#define TARANTOOL_UNALIGNED_H_INCLUDED

#include <stdint.h>

struct unaligned_mem
{
	union
	{
		uint8_t  u8;
		uint16_t u16;
		uint32_t u32;
		uint64_t u64;
		float	 f;
		double	 lf;
	};

} __attribute__((__packed__));

static inline uint8_t load_u8(const void *p)
{
	return ((const struct unaligned_mem *)p)->u8;
}

static inline uint16_t load_u16(const void *p)
{
	return ((const struct unaligned_mem *)p)->u16;
}

static inline uint32_t load_u32(const void *p)
{
	return ((const struct unaligned_mem *)p)->u32;
}

static inline uint64_t load_u64(const void *p)
{
	return ((const struct unaligned_mem *)p)->u64;
}

static inline float load_float(const void *p)
{
	return ((const struct unaligned_mem *)p)->f;
}

static inline double load_double(const void *p)
{
	return ((const struct unaligned_mem *)p)->lf;
}

static inline void store_u8(void *p, uint8_t v)
{
	((struct unaligned_mem *)p)->u8 = v;
}

static inline void store_u16(void *p, uint16_t v)
{
	((struct unaligned_mem *)p)->u16 = v;
}

static inline void store_u32(void *p, uint32_t v)
{
	((struct unaligned_mem *)p)->u32 = v;
}

static inline void store_u64(void *p, uint64_t v)
{
	((struct unaligned_mem *)p)->u64 = v;
}

static inline void store_float(void *p, float v)
{
	((struct unaligned_mem *)p)->f = v;
}

static inline void store_double(void *p, double v)
{
	((struct unaligned_mem *)p)->lf = v;
}

#endif
