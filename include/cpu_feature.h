#ifndef TARANTOOL_CPU_FEATURES_H
#define TARANTOOL_CPU_FEATURES_H
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

/* CPU feature capabilities to use with cpu_has (feature) */
enum {
	cpuf_ht = 0, cpuf_sse4_1, cpuf_sse4_2, cpuf_hypervisor
};

/*	return 1=feature is available, 0=unavailable, -EINVAL = unsupported CPU,
	-ERANGE = invalid feature
*/
int cpu_has (unsigned int feature);


/* hardware-calculate CRC32 for the given data buffer
 * NB: 	requires 1 == cpu_has (cpuf_sse4_2),
 * 		CALLING IT W/O CHECKING for sse4_2 CAN CAUSE SIGABRT
 */
u_int32_t crc32c_hw(u_int32_t crc, unsigned char const *p, size_t len);


#endif /* TARANTOOL_CPU_FEATURES_H */

/* __EOF__ */

