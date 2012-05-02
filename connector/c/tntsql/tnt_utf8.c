
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

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <connector/c/include/tarantool/tnt.h>
#include <connector/c/include/tarantool/tnt_utf8.h>

bool
tnt_utf8_init(struct tnt_utf8 *u, unsigned char *data, size_t size)
{
	u->size = size;
	u->data = (unsigned char*)tnt_mem_alloc(u->size + 1);
	u->data[u->size] = 0;
	memcpy(u->data, data, u->size);
	ssize_t len = tnt_utf8_strlen(u->data, u->size);
	if (len == -1) {
		tnt_mem_free(u->data);
		return false;
	}
	u->len = len;
	return true;
}

void
tnt_utf8_free(struct tnt_utf8 *u)
{
	if (u->data)
		tnt_mem_free(u->data);
	u->data = NULL;
	u->size = 0;
	u->len = 0;
}

ssize_t
tnt_utf8_chrlen(unsigned char *data, size_t size)
{
#define tnt_bit(I) (1 << (I))
#define tnt_bit_is(B, I) ((B) & tnt_bit(I))
	/* U-00000000 – U-0000007F: ASCII representation */
	if (data[0] < 0x7F)
		return 1;
	/* The first byte of a multibyte sequence that represents a non-ASCII
	 * character is always in the range 0xC0 to 0xFD and it indicates
	 * how many bytes follow for this character */
	if (data[0] < 0xC0 || data[0] > 0xFD )
		return -1;
	unsigned int i, count = 0;
	/* U-00000080 – U-000007FF */
	if (tnt_bit_is(data[0], 7) && tnt_bit_is(data[0], 6)) {
		count = 2;
		/* U-00000800 – U-0000FFFF */
		if (tnt_bit_is(data[0], 5)) {
			count = 3;
			/* U-00010000 – U-001FFFFF */
			if (tnt_bit_is(data[0], 4)) {
				count = 4;
				/* it is possible to declare more than 4 bytes,
				 * but practically unused */
			}
		}
	}
	if (count == 0)
		return -1;
	if (size < count)
		return -1;
	/* no ASCII byte (0x00-0x7F) can appear as part of
	 * any other character */
	for (i = 1 ; i < count ; i++)
		if (data[i] < 0x7F)
			return -1;
	return count; 
#undef tnt_bit
#undef tnt_bit_is
}

ssize_t
tnt_utf8_strlen(unsigned char *data, size_t size)
{
	register size_t i = 0;
	register ssize_t c = 0, r = 0;
	while (i < size) {
		r =tnt_utf8_chrlen(data + i, size - i);
		if (r == -1)
			return -1;
		c++;
		i += r;
	}
	return c;
}

ssize_t
tnt_utf8_sizeof(unsigned char *data, size_t size, size_t n)
{
	register size_t i = 0, c = 0;
	register ssize_t r = 0;
	while ((i < size) && (c < n)) {
		r = tnt_utf8_chrlen(data + i, size - i);
		if (r == -1)
			return -1;
		c++;
		i += r;
	}
	if (c != n)
		return -1;
	return i;
}

bool
tnt_utf8_cmp(struct tnt_utf8 *u, struct tnt_utf8 *us)
{
	if (u->size != us->size)
		return false;
	if (u->len != us->len)
		return false;
	return !memcmp(u->data, us->data, u->size);
}

ssize_t
tnt_utf8_next(struct tnt_utf8 *u, size_t off)
{
	if (off == u->size)
		return 0;
	ssize_t r = tnt_utf8_chrlen(u->data + off, u->size - off);
	if (r == -1)
		return -1;
	return off + r;
}
