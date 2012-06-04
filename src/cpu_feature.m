/*
 * Copyright (C) 2010 Mail.RU
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <config.h>
#include <sys/types.h>
#include <errno.h>
#include <stdlib.h>

#if !defined (__x86_64__) && !defined (__i386__)
	#error "Only x86 and x86_64 architectures supported"
#endif

#ifdef HAVE_CPUID_H
#include <cpuid.h>
#else
#include <third_party/compat/sys/cpuid.h>
#endif
#include "cpu_feature.h"

#define SCALE_F		sizeof(unsigned long)

#if defined (__x86_64__)
	#define REX_PRE "0x48, "
#elif defined (__i386__)
	#define REX_PRE
#else
	#error "Only x86 and x86_64 architectures supported"
#endif


static u_int32_t
crc32c_hw_byte(u_int32_t crc, unsigned char const *data, unsigned int length)
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


u_int32_t
crc32c_hw(u_int32_t crc, const unsigned char *buf, unsigned int len)
{
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

	if (iremainder) {
		crc = crc32c_hw_byte(crc, (unsigned char const*)ptmp,
				 			iremainder);
	}

	return crc;
}


bool
sse42_enabled_cpu()
{
	unsigned int ax, bx, cx, dx;

	if (__get_cpuid(1, &ax, &bx, &cx, &dx) == 0)
		return 0;

	return (cx & (1 << 20)) != 0;
}


