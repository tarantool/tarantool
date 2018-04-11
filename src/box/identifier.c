/*
 * Copyright 2010-2018, Tarantool AUTHORS, please see AUTHORS file.
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
#include "identifier.h"

#include "say.h"
#include "diag.h"

#include <unicode/utf8.h>
#include <unicode/uchar.h>

int
identifier_check(const char *str, int str_len)
{
	const char *end = str + str_len;
	if (str == end)
		goto error;

	UChar32 c;
	int offset = 0;
	while (offset < str_len) {
		U8_NEXT(str, offset, str_len, c);
		if (c == U_SENTINEL || c == 0xFFFD)
			goto error;

		int8_t type = u_charType(c);
		/**
		 * The icu library has a function named u_isprint, however,
		 * this function does not return any errors.
		 * Here the `c` symbol printability is determined by comparison
		 * with unicode category types explicitly.
		 */
		if (type == U_UNASSIGNED ||
		    type == U_LINE_SEPARATOR ||
		    type == U_CONTROL_CHAR ||
		    type == U_PARAGRAPH_SEPARATOR)
			goto error;
	}
	return 0;
error:
	diag_set(ClientError, ER_IDENTIFIER, tt_cstr(str, str_len));
	return -1;
}
