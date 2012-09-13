#ifndef TNT_UTF8_H_INCLUDED
#define TNT_UTF8_H_INCLUDED

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

struct tnt_utf8 {
	unsigned char *data;
	size_t size;
	size_t len;
};

#define TNT_UTF8_CHAR(U, P) ((U)->data + (P))

bool tnt_utf8_init(struct tnt_utf8 *u, const unsigned char *data, size_t size);
void tnt_utf8_free(struct tnt_utf8 *u);

ssize_t tnt_utf8_chrlen(const unsigned char *data, size_t size);
ssize_t tnt_utf8_strlen(const unsigned char *data, size_t size);

ssize_t tnt_utf8_sizeof(const unsigned char *data, size_t size, size_t n);
bool tnt_utf8_cmp(struct tnt_utf8 *u, struct tnt_utf8 *us);

ssize_t tnt_utf8_next(struct tnt_utf8 *u, size_t off);

#endif /* TNT_UTF8_H_INCLUDED */
