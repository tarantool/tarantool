
/*
 * Copyright (C) 2011 Mail.RU
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

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <connector/c/include/tarantool/tnt_enc.h>

int
tnt_enc_read(char *buf, uint32_t *value)
{
	*value = 0;
	if (!(buf[0] & 0x80)) {
		*value = buf[0] & 0x7f;
		return 1;
	}
	if (!(buf[1] & 0x80)) {
		*value = (buf[0] & 0x7f) << 7 |
		         (buf[1] & 0x7f);
		return 2;
	}
	if (!(buf[2] & 0x80)) {
		*value = (buf[0] & 0x7f) << 14 |
			 (buf[1] & 0x7f) << 7  |
			 (buf[2] & 0x7f);
		return 3;
	}
	if (!(buf[3] & 0x80)) {
		*value = (buf[0] & 0x7f) << 21 |
			 (buf[1] & 0x7f) << 14 |
			 (buf[2] & 0x7f) << 7  |
			 (buf[3] & 0x7f);
		return 4;
	}
	if (!(buf[4] & 0x80)) {
		*value = (buf[0] & 0x7f) << 28 |
		         (buf[1] & 0x7f) << 21 |
			 (buf[2] & 0x7f) << 14 |
			 (buf[3] & 0x7f) << 7  |
			 (buf[4] & 0x7f);
		return 5;
	}
	return -1;
}

void
tnt_enc_write(char *buf, uint32_t value)
{
	if (value >= (1 << 7)) {
		if (value >= (1 << 14)) {
			if (value >= (1 << 21)) {
				if (value >= (1 << 28))
					*(buf++) = (value >> 28) | 0x80;
				*(buf++) = (value >> 21) | 0x80;
			}
			*(buf++) = ((value >> 14) | 0x80);
		}
		*(buf++) = ((value >> 7) | 0x80);
	}
	*(buf++) = ((value) & 0x7F);
}

int
tnt_enc_size(uint32_t value)
{
	if (value < (1 << 7))
		return 1;
	if (value < (1 << 14))
		return 2;
	if (value < (1 << 21))
		return 3;
	if (value < (1 << 28))
		return 4;
	return 5;
}
