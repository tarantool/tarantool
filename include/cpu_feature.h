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
#include <stdbool.h>

/* Check whether CPU supports SSE 4.2 (needed to compute CRC32 in hardware).
 *
 * @param	feature		indetifier (see above) of the target feature
 *
 * @return	true if feature is available, false if unavailable.
 */
bool sse42_enabled_cpu();


/* Hardware-calculate CRC32 for the given data buffer.
 *
 * @param	crc 		initial CRC
 * @param	buf			data buffer
 * @param	len			buffer length
 *
 * @pre 	true == cpu_has (cpuf_sse4_2)
 * @return	CRC32 value
 */
u_int32_t crc32c_hw(u_int32_t crc, const unsigned char *buf, unsigned int len);


#endif /* TARANTOOL_CPU_FEATURES_H */

