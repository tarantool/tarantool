/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "trivia/config.h"
#include "trivia/util.h"
#include <sys/types.h>
#include <errno.h>
#include <stdlib.h>
#include <stdbool.h>

#include "cpu_feature.h"

#if defined(HAVE_CPUID) && (defined (__x86_64__) || defined (__i386__))

#include <cpuid.h>

#define SCALE_F		sizeof(unsigned long)

#if defined (__x86_64__)
	#define REX_PRE "0x48, "
#elif defined (__i386__)
	#define REX_PRE
#endif


static uint32_t
crc32c_hw_byte(uint32_t crc, char const *data, unsigned int length)
{
	while (length--) {
		__asm__ __volatile__(
			".byte 0xf2, 0xf, 0x38, 0xf0, 0xf1"
			:"=S"(crc)
			:"0"(crc), "c"(*data)
		);
		data++;
	}

	return crc;
}


uint32_t
crc32c_hw(uint32_t crc, const char *buf, unsigned int len)
{
	const unsigned int align = alignof(unsigned long);
	unsigned int not_aligned_prefix =
		(align - (unsigned long)buf % align) % align;
	/*
	 * Calculate CRC32 for the prefix byte-by-byte so as to
	 * then use aligned words to calculate the rest. This is
	 * twice less loads, because every load takes exactly one
	 * word from memory. Not 2 words, which would need to be
	 * partially merged then.
	 * But the main reason is that unaligned loads are just
	 * unsafe, because this is an undefined behaviour.
	 */
	if (not_aligned_prefix < len) {
		crc = crc32c_hw_byte(crc, buf, not_aligned_prefix);
		buf += not_aligned_prefix;
		len -= not_aligned_prefix;
	} else {
		return crc32c_hw_byte(crc, buf, len);
	}
	unsigned int iquotient = len / SCALE_F;
	unsigned int iremainder = len % SCALE_F;
	unsigned long *ptmp = (unsigned long *)buf;

	while (iquotient--) {
		__asm__ __volatile__(
			".byte 0xf2, " REX_PRE "0xf, 0x38, 0xf1, 0xf1;"
			:"=S"(crc)
			:"0"(crc), "c"(*ptmp)
		);
		ptmp++;
	}
	return crc32c_hw_byte(crc, (const char *)ptmp, iremainder);
}

bool
sse42_enabled_cpu()
{
	unsigned int ax, bx, cx, dx;

	if (__get_cpuid(1, &ax, &bx, &cx, &dx) == 0)
		return 0;

	return (cx & (1 << 20)) != 0;
}

#else /* !(defined (__x86_64__) || defined (__i386__)) */

bool
sse42_enabled_cpu()
{
	return false;
}

#endif
