#ifndef TNT_REPLY_H_INCLUDED
#define TNT_REPLY_H_INCLUDED

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

typedef ssize_t (*tnt_reply_t)(void *ptr, char *dst, ssize_t size);

struct tnt_reply {
	uint32_t op;
	uint32_t reqid;
	uint32_t code;
	char *error;
	struct tnt_list tuples;
	uint32_t count;
};

#define TNT_REPLY_ERR(R) ((R)->code >> 8)
#define TNT_REPLY_LIST(R) (&(R)->tuples)

void tnt_reply_init(struct tnt_reply *r);
void tnt_reply_free(struct tnt_reply *r);

int tnt_reply(struct tnt_reply *r, char *buf, size_t size, size_t *off);
int tnt_reply_from(struct tnt_reply *r, tnt_reply_t rcv, void *ptr);

#endif /* TNT_REPLY_H_INCLUDED */
