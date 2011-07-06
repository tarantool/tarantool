#ifndef TNT_OPT_H_INCLUDED
#define TNT_OPT_H_INCLUDED

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
 * @defgroup Options
 * @ingroup  Main
 * @{
 */
enum tnt_auth {
	TNT_AUTH_NONE,
	TNT_AUTH_CHAP,
	TNT_AUTH_SASL
};

enum tnt_proto {
	TNT_PROTO_ADMIN,
	TNT_PROTO_RW,
	TNT_PROTO_RO,
	TNT_PROTO_FEEDER
};

enum tnt_opt_type {
	TNT_OPT_PROTO,
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
	TNT_OPT_RECV_BUF,
	TNT_OPT_AUTH,
	TNT_OPT_AUTH_ID,
	TNT_OPT_AUTH_KEY,
	TNT_OPT_AUTH_MECH,
	TNT_OPT_MALLOC,
	TNT_OPT_REALLOC,
	TNT_OPT_FREE,
	TNT_OPT_DUP
};
/** @} */

struct tnt_opt {
	enum tnt_proto proto;
	char *hostname;
	int port;
	int tmout_connect;
	int tmout_recv;
	int tmout_send;
	void *send_cb;
	void *send_cbv;
	void *send_cb_arg;
	int send_buf;
	void *recv_cb;
	void *recv_cb_arg;
	int recv_buf;
	enum tnt_auth auth;
	char *auth_id;
	int auth_id_size;
	unsigned char *auth_key;
	int auth_key_size;
	char *auth_mech;
	void *(*malloc)(int size);
	void *(*realloc)(void *ptr, int size);
	void *(*dup)(char *sz);
	void (*free)(void *ptr);
};

void tnt_opt_init(struct tnt_opt *opt);
void tnt_opt_free(struct tnt_opt *opt);

enum tnt_error tnt_opt_set(struct tnt_opt *opt, enum tnt_opt_type name,
		           void *pargs);

#endif /* TNT_OPT_H_INCLUDED */
