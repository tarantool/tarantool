/*
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
#include "base64.h"
#include <trivia/util.h>

/*
 * This is part of the libb64 project, and has been placed in the
 * public domain. For details, see
 * http://sourceforge.net/projects/libb64
 */

/* {{{ encode */

int
base64_encode_bufsize(int bin_len, int options)
{
	int base64_len = bin_len * 4 / 3;
	if ((options & BASE64_NOWRAP) == 0) {
		/* Account '\n' symbols. */
		base64_len += ((base64_len + BASE64_CHARS_PER_LINE - 1)/
			       BASE64_CHARS_PER_LINE);
	} else if (bin_len % 3 != 0) {
		base64_len++;
	}
	if ((options & BASE64_NOPAD) == 0)
		base64_len += 4;
	return base64_len;
}

enum base64_encodestep { step_A, step_B, step_C };

static const char default_encoding[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char urlsafe_encoding[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

struct base64_encodestate {
	enum base64_encodestep step;
	char result;
	int stepcount;
};

static inline void
base64_encodestate_init(struct base64_encodestate *state)
{
	state->step = step_A;
	state->result = 0;
	state->stepcount = 0;
}

static inline char
base64_encode_value(char value, const char *encoding)
{
	unsigned codepos = (unsigned) value;
	if (codepos > 64)
		return '=';
	return encoding[codepos];
}

static int
base64_encode_block(const char *in_bin, int in_len,
		    char *out_base64, int out_len,
		    struct base64_encodestate *state, const char *encoding,
		    int options)
{
	const char *const in_end = in_bin + in_len;
	const char *in_pos = in_bin;
	char *out_pos = out_base64;
	char *out_end = out_base64  + out_len;
	char result;
	char fragment;

	result = state->result;

	switch (state->step)
	{
		while (1)
		{
	case step_A:
			if (in_pos == in_end || out_pos >= out_end) {
				state->step = step_A;
				goto out;
			}
			fragment = *in_pos++;
			result = (fragment & 0x0fc) >> 2;
			*out_pos++ = base64_encode_value(result, encoding);
			result = (fragment & 0x003) << 4;
			FALLTHROUGH;
	case step_B:
			if (in_pos == in_end || out_pos >= out_end) {
				state->step = step_B;
				goto out;
			}
			fragment = *in_pos++;
			result |= (fragment & 0x0f0) >> 4;
			*out_pos++ = base64_encode_value(result, encoding);
			result = (fragment & 0x00f) << 2;
			FALLTHROUGH;
	case step_C:
			if (in_pos == in_end || out_pos >= out_end) {
				state->step = step_C;
				goto out;
			}
			fragment = *in_pos++;
			result |= (fragment & 0x0c0) >> 6;
			*out_pos++ = base64_encode_value(result, encoding);
			if (out_pos >= out_end)
				goto out;
			result  = (fragment & 0x03f) >> 0;
			*out_pos++ = base64_encode_value(result, encoding);

			/*
			 * Each full step (A->B->C) yields
			 * 4 characters.
			 */
			if ((options & BASE64_NOWRAP) == 0 &&
			    ++state->stepcount * 4 == BASE64_CHARS_PER_LINE) {
				if (out_pos >= out_end)
					return out_pos - out_base64;
				*out_pos++ = '\n';
				state->stepcount = 0;
			}
		}
	}
out:
	state->result = result;
	return out_pos - out_base64;
}

static int
base64_encode_blockend(char *out_base64, int out_len,
		       struct base64_encodestate *state, const char *encoding,
		       int options)
{
	char *out_pos = out_base64;
	char *out_end = out_base64 + out_len;

	switch (state->step) {
	case step_B:
		if (out_pos >= out_end)
			return out_pos - out_base64;
		*out_pos++ = base64_encode_value(state->result, encoding);
		if (out_pos + 1 >= out_end || (options & BASE64_NOPAD) != 0)
			return out_pos - out_base64;
		*out_pos++ = '=';
		*out_pos++ = '=';
		break;
	case step_C:
		if (out_pos >= out_end)
			return out_pos - out_base64;
		*out_pos++ = base64_encode_value(state->result, encoding);
		if (out_pos >= out_end || (options & BASE64_NOPAD) != 0)
			return out_pos - out_base64;
		*out_pos++ = '=';
		break;
	case step_A:
		break;
	}
	if (out_pos >= out_end)
		return out_pos - out_base64;
#if 0
	/* Sometimes the output is useful without a newline. */
	*out_pos++ = '\n';
	if (out_pos >= out_end)
		return out_pos - out_base64;
#endif
	*out_pos = '\0';
	return out_pos - out_base64;
}

int
base64_encode(const char *in_bin, int in_len,
	      char *out_base64, int out_len, int options)
{
	const char *encoding;
	if ((options & BASE64_URLSAFE) == BASE64_URLSAFE)
		encoding = urlsafe_encoding;
	else
		encoding = default_encoding;
	struct base64_encodestate state;
	base64_encodestate_init(&state);
	int res = base64_encode_block(in_bin, in_len, out_base64,
				      out_len, &state, encoding, options);
	return res + base64_encode_blockend(out_base64 + res, out_len - res,
					    &state, encoding, options);
}

/* }}} */

/* {{{ decode */

int
base64_decode_bufsize(int base64_len)
{
	return 3 * base64_len / 4 + 1;
}

static int
base64_decode_value(int value)
{
	static const int decoding[] = {
		62, -1, 62, -1, 63, 52, 53, 54, 55, 56, 57, 58,
		59, 60, 61, -1, -1, -1, -2, -1, -1, -1,  0,  1,
		2,   3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13,
		14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
		-1, -1, -1, -1, 63, -1, 26, 27, 28, 29, 30, 31,
		32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43,
		44, 45, 46, 47, 48, 49, 50, 51
	};
	static const int decoding_size =
		sizeof(decoding) / sizeof(decoding[0]);
	int codepos = value;
	codepos -= 43;
	if (codepos < 0 || codepos >= decoding_size)
		return -1;
	return decoding[codepos];
}

int
base64_decode(const char *in_base64, int in_len,
	      char *out_bin, int out_len)
{
	const char *in_pos = in_base64;
	const char *in_end = in_base64 + in_len;
	char *out_pos = out_bin;
	char *out_end = out_bin + out_len;
	int fragment;

	while (1)
	{
		do {
			if (in_pos == in_end || out_pos >= out_end)
				return out_pos - out_bin;
			fragment = base64_decode_value(*in_pos++);
		} while (fragment < 0);
		*out_pos = (fragment & 0x03f) << 2;
		do {
			if (in_pos == in_end || out_pos >= out_end)
				return out_pos - out_bin;
			fragment = base64_decode_value(*in_pos++);
		} while (fragment < 0);
		*out_pos++ |= (fragment & 0x030) >> 4;
		if (out_pos < out_end)
			*out_pos = (fragment & 0x00f) << 4;
		do {
			if (in_pos == in_end || out_pos >= out_end)
				return out_pos - out_bin;
			fragment = base64_decode_value(*in_pos++);
		} while (fragment < 0);
		*out_pos++ |= (fragment & 0x03c) >> 2;
		if (out_pos < out_end)
			*out_pos = (fragment & 0x003) << 6;
		do {
			if (in_pos == in_end || out_pos >= out_end)
				return out_pos - out_bin;
			fragment = base64_decode_value(*in_pos++);
		} while (fragment < 0);
		*out_pos++ |= (fragment & 0x03f);
	}
	/* control should not reach here */
	return out_pos - out_bin;
}

/* }}} */
