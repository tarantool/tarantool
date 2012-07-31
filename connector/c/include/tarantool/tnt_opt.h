#ifndef TNT_OPT_H_INCLUDED
#define TNT_OPT_H_INCLUDED

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

enum tnt_opt_type {
	TNT_OPT_HOSTNAME,
	TNT_OPT_PORT,
	TNT_OPT_TMOUT_CONNECT,
	TNT_OPT_TMOUT_RECV,
	TNT_OPT_TMOUT_SEND,
	TNT_OPT_SEND_CB,
	TNT_OPT_SEND_CBV,
	TNT_OPT_SEND_CB_ARG,
	TNT_OPT_SEND_BUF,
	TNT_OPT_RECV_CB,
	TNT_OPT_RECV_CB_ARG,
	TNT_OPT_RECV_BUF
};

struct tnt_opt {
	char *hostname;
	int port;
	struct timeval tmout_connect;
	struct timeval tmout_recv;
	struct timeval tmout_send;
	void *send_cb;
	void *send_cbv;
	void *send_cb_arg;
	int send_buf;
	void *recv_cb;
	void *recv_cb_arg;
	int recv_buf;
};

void tnt_opt_init(struct tnt_opt *opt);
void tnt_opt_free(struct tnt_opt *opt);

int
tnt_opt_set(struct tnt_opt *opt, enum tnt_opt_type name, va_list args);

#endif /* TNT_OPT_H_INCLUDED */
