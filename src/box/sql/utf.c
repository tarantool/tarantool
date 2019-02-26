/*
 * Copyright 2010-2017, Tarantool AUTHORS, please see AUTHORS file.
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

/*
 * This file contains routines used to translate between UTF-8.
 *
 * Notes on UTF-8:
 *
 *   Byte-0    Byte-1    Byte-2    Byte-3    Value
 *  0xxxxxxx                                 00000000 00000000 0xxxxxxx
 *  110yyyyy  10xxxxxx                       00000000 00000yyy yyxxxxxx
 *  1110zzzz  10yyyyyy  10xxxxxx             00000000 zzzzyyyy yyxxxxxx
 *  11110uuu  10uuzzzz  10yyyyyy  10xxxxxx   000uuuuu zzzzyyyy yyxxxxxx
 *
 *
 */
#include "sqlInt.h"

/*
 * This lookup table is used to help decode the first byte of
 * a multi-byte UTF8 character.
 */
static const unsigned char sqlUtf8Trans1[] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x00, 0x01, 0x02, 0x03, 0x00, 0x01, 0x00, 0x00,
};

u32
sqlUtf8Read(const unsigned char **pz	/* Pointer to string from which to read char */
    )
{
	unsigned int c;

	/* Same as READ_UTF8() above but without the zTerm parameter.
	 * For this routine, we assume the UTF8 string is always zero-terminated.
	 */
	c = *((*pz)++);
	if (c >= 0xc0) {
		c = sqlUtf8Trans1[c - 0xc0];
		while ((*(*pz) & 0xc0) == 0x80) {
			c = (c << 6) + (0x3f & *((*pz)++));
		}
		if (c < 0x80
		    || (c & 0xFFFFF800) == 0xD800
		    || (c & 0xFFFFFFFE) == 0xFFFE) {
			c = 0xFFFD;
		}
	}
	return c;
}

int
sql_utf8_char_count(const unsigned char *str, int byte_len)
{
	int symbol_count = 0;
	for (int i = 0; i < byte_len;) {
		SQL_UTF8_FWD_1(str, i, byte_len);
		symbol_count++;
	}
	return symbol_count;
}
