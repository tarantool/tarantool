#ifndef TNT_RECV_H_INCLUDED
#define TNT_RECV_H_INCLUDED

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

/**
 * @defgroup Responce
 * @brief Server responce
 * @{
 */

enum tnt_recv_op {
	TNT_RECV_SELECT,
	TNT_RECV_INSERT,
	TNT_RECV_UPDATE,
	TNT_RECV_DELETE,
	TNT_RECV_PING
};

/** @} */

struct tnt_recv {
	enum tnt_recv_op op;
	uint32_t reqid;
	uint32_t code;
	char *error;
	uint32_t count;
	struct tnt_tuples tuples;
};

/** @addtogroup Responce
 *  @{
 */

#define TNT_RECV_COUNT(R) \
	((R)->count)

#define TNT_RECV_CODE(R) \
	((R)->code)

#define TNT_RECV_ID(R) \
	((R)->reqid)

#define TNT_RECV_OP(R) \
	((R)->op)

#define TNT_RECV_FOREACH(R, N) \
	TNT_TUPLES_FOREACH(&(R)->tuples, (N))

void tnt_recv_init(struct tnt_recv *rcv);
void tnt_recv_free(struct tnt_recv *rcv);

char *tnt_recv_error(struct tnt_recv *rcv);
int tnt_recv(struct tnt *t, struct tnt_recv *rcv);

/** @} */

#endif /* TNT_RECV_H_INCLUDED */
