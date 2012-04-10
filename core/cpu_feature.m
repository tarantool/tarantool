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

#include <sys/types.h>
#include <errno.h>
#include <stdlib.h>

#if !defined (__x86_64__) && !defined (__i386__)
	#error "Only x86 and x86_64 architectures supported"
#endif

#ifndef __GNUC__
	#error This module uses GCC intrinsic header(s) and should be compiled using gcc.
#elif ((__GNUC__ * 10000) + (__GNUC_MINOR__ * 100)) < 40400
	#error This module should be compiled with GCC 4.4 and higher
#endif


/* GCC intrinsic headers */
#include <cpuid.h>
#include <smmintrin.h>

#include "cpu_feature.h"


u_int32_t
crc32c_hw(u_int32_t crc, const unsigned char *buf, unsigned int len)
{
#define SCALE_F	sizeof(unsigned long)
	size_t nwords = len / SCALE_F, nbytes = len % SCALE_F;
	unsigned long *pword;
	unsigned char *pbyte;

	for (pword = (unsigned long *)buf; nwords--; ++pword)
#if defined (__x86_64__)
		crc = (u_int32_t)_mm_crc32_u64((u_int64_t)crc, *pword);
#elif defined (__i386__)
		crc = _mm_crc32_u32(crc, *pword);
#endif

	if (nbytes)
		for (pbyte = (unsigned char*)pword; nbytes--; ++pbyte)
			crc = _mm_crc32_u8(crc, *pbyte);

	return crc;
}


bool
sse42_enabled_cpu()
{
	unsigned int ax, bx, cx, dx;

	if (__get_cpuid(1 /* level */, &ax, &bx, &cx, &dx) == 0)
		return 0; /* not supported */

	return (cx & (1 << 20)) != 0;
}


