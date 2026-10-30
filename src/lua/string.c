/*
 * Copyright 2010-2019 Tarantool AUTHORS: please see AUTHORS file.
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
#include "string.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * Implements the lstrip functionality of string_strip_helper.
 *
 * @param inp     The input string to perform lstrip on.
 * @param inp_len The length of the input string.
 * @param arr     An array of 256 values defining
 *                the set of characters to strip.
 *
 * @return The number of characters to strip from the left.
 */
static size_t
lstrip_helper(const char *inp, size_t inp_len, uint8_t *arr)
{
	size_t i;
	for (i = 0; i < inp_len; ++i) {
		unsigned char c = inp[i];
		if (arr[c] == 0)
			break;
	}
	return i;
}

/*
 * Same as lstrip_helper, but for rstrip.
 *
 * @param inp     The input string to perform rstrip on.
 * @param inp_len The length of the input string.
 * @param arr     An array of 256 values defining
 *                the set of characters to strip.
 *
 * @return The number of characters to strip from the right.
 */
static size_t
rstrip_helper(const char *inp, size_t inp_len, uint8_t *arr)
{
	size_t i;
	for (i = inp_len - 1; i != (size_t)(-1); --i) {
		unsigned char c = inp[i];
		if (arr[c] == 0)
			break;
	}
	return inp_len - i - 1;
}

/*
 * Perform a combination of lstrip and rstrip on the input string,
 * and return the position and length of the resulting substring.
 *
 * @param[in]  inp       The input string to perform stripping on.
 * @param[in]  inp_len   The length of the input string.
 * @param[in]  chars     A string representing the unwanted chars.
 * @param[in]  chars_len The length of chars
 * @param[in]  lstrip    Whether to perform lstrip or not.
 * @param[in]  rstrip    Whether to perform rstrip or not.
 * @param[out] newstart  The position of the resulting substring
 *                       in the input string.
 * @param[out] newlen    The length of the resulting substring.
 */
void
string_strip_helper(const char *inp, size_t inp_len, const char *chars,
		    size_t chars_len, bool lstrip, bool rstrip,
		    size_t *newstart, size_t *newlen)
{
	size_t skipped = 0;
	uint8_t arr[256] = {0};

	for (size_t i = 0; i < chars_len; ++i) {
		unsigned char c = chars[i];
		arr[c] = 1;
	}

	if (lstrip)
		skipped += lstrip_helper(inp, inp_len, arr);

	*newstart = skipped;

	if (rstrip)
		skipped += rstrip_helper(inp + skipped, inp_len - skipped, arr);

	*newlen = inp_len - skipped;
}
