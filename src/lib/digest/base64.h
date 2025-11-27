#ifndef BASE64_H
#define BASE64_H
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
/*
 * This is part of the libb64 project, and has been placed in the
 * public domain. For details, see
 * http://sourceforge.net/projects/libb64
 */
#ifdef __cplusplus
extern "C" {
#endif

#define BASE64_CHARS_PER_LINE 72

/** Options for base64 encoder. */
enum base64_options {
	/** Do not write padding symbols '='. */
	BASE64_NOPAD = 1,   /* 0 0 1 */
	/** Do not write '\n' every 72 symbols. */
	BASE64_NOWRAP = 2,  /* 0 1 0 */
	/**
	 * No-pad + no-wrap.
	 * Replace '+' -> '-' and '/' -> '_'.
	 */
	BASE64_URLSAFE = 7, /* 1 1 1 */
};

/**
 * Size of a buffer needed to encode a binary into BASE64 text.
 *
 * @param[in]  bin_len          size of the input
 * @param[in]  options          encoder options, see base64_options
 *
 * @return the max size of encoded output
 */
int
base64_encode_bufsize(int bin_len, int options);

/**
 * Encode a binary stream into BASE64 text.
 *
 * @param[in]  in_bin           the binary input stream to decode
 * @param[in]  in_len		size of the input
 * @param[out] out_base64       output buffer for the encoded data
 * @param[in]  out_len          buffer size
 * @param[in]  options          encoder options, see base64_options
 *
 * @pre the buffer size must be >= base64_encode_bufsize(in_len, options)
 *
 * @return the size of encoded output
 */
int
base64_encode(const char *in_bin, int in_len,
	      char *out_base64, int out_len, int options);

/**
 * Size of a buffer needed to decode a BASE64 text.
 *
 * @param[in]  base64_len       size of the input
 *
 * @return the max size of decoded output
 */
int
base64_decode_bufsize(int base64_len);

/**
 * Decode a BASE64 text into a binary
 *
 * @param[in]  in_base64	the BASE64 stream to decode
 * @param[in]  in_len		size of the input
 * @param[out] out_bin		output buffer size
 * @param[in]  out_len		buffer size
 *
 * @pre buffer size must be >= base64_decode_bufsize(in_len)
 *
 * @return the size of decoded output
 */
int
base64_decode(const char *in_base64, int in_len, char *out_bin, int out_len);

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* BASE64_H */

