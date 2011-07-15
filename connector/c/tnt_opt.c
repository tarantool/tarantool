
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

#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <tnt_error.h>
#include <tnt_mem.h>
#include <tnt_opt.h>

void
tnt_opt_init(struct tnt_opt *opt)
{
	memset(opt, 0, sizeof(struct tnt_opt));
	opt->port = 15312;
	opt->proto = TNT_PROTO_RW;
	opt->auth = TNT_AUTH_NONE;
	opt->recv_buf = 16384;
	opt->send_buf = 16384;
}

void
tnt_opt_free(struct tnt_opt *opt)
{
	if (opt->hostname)
		free(opt->hostname);
	if (opt->auth_id)
		free(opt->auth_id);
	if (opt->auth_key)
		free(opt->auth_key);
	if (opt->auth_mech)
		free(opt->auth_mech);
}

enum tnt_error
tnt_opt_set(struct tnt_opt *opt, enum tnt_opt_type name,
	    va_list args)
{
	char *key;
	switch (name) {
	case TNT_OPT_PROTO:
		opt->proto = va_arg(args, enum tnt_proto);
		break;
	case TNT_OPT_HOSTNAME:
		if (opt->hostname)
			tnt_mem_free(opt->hostname);
		opt->hostname = strdup(va_arg(args, char*));
		if (opt->hostname == NULL)
			return TNT_EMEMORY;
		break;
	case TNT_OPT_PORT:
		opt->port = va_arg(args, int);
		break;
	case TNT_OPT_TMOUT_CONNECT:
		opt->tmout_connect = va_arg(args, int);
		break;
	case TNT_OPT_TMOUT_RECV:
		opt->tmout_recv = va_arg(args, int);
		break;
	case TNT_OPT_TMOUT_SEND:
		opt->tmout_send = va_arg(args, int);
		break;
	case TNT_OPT_SEND_CB:
		opt->send_cb = va_arg(args, void*);
		break;
	case TNT_OPT_SEND_CBV:
		opt->send_cbv = va_arg(args, void*);
		break;
	case TNT_OPT_SEND_CB_ARG:
		opt->send_cb_arg = va_arg(args, void*);
		break;
	case TNT_OPT_SEND_BUF:
		opt->send_buf = va_arg(args, int);
		break;
	case TNT_OPT_RECV_CB:
		opt->recv_cb = va_arg(args, void*);
		break;
	case TNT_OPT_RECV_CB_ARG:
		opt->recv_cb_arg = va_arg(args, void*);
		break;
	case TNT_OPT_RECV_BUF:
		opt->recv_buf = va_arg(args, int);
		break;
	case TNT_OPT_AUTH:
		opt->auth = va_arg(args, enum tnt_auth);
		break;
	case TNT_OPT_AUTH_ID:
		if (opt->auth_id)
			free(opt->auth_id);
		opt->auth_id = strdup(va_arg(args, char*));
		if (opt->auth_id == NULL)
			return TNT_EMEMORY;
		opt->auth_id_size = strlen(opt->auth_id);
		break;
	case TNT_OPT_AUTH_KEY:
		if (opt->auth_key)
			free(opt->auth_key);
		key = va_arg(args, char*);
		opt->auth_key_size = va_arg(args, int);
		opt->auth_key = malloc(opt->auth_key_size + 1);
		if (opt->auth_id == NULL)
			return TNT_EMEMORY;
		memcpy(opt->auth_key, key, opt->auth_key_size);
		opt->auth_key[opt->auth_key_size] = 0;
		break;
	case TNT_OPT_AUTH_MECH:
		if (opt->auth_mech)
			free(opt->auth_mech);
		opt->auth_mech = strdup(va_arg(args, char*));
		if (opt->auth_mech == NULL)
			return TNT_EMEMORY;
		break;
	case TNT_OPT_MALLOC:
		opt->malloc = va_arg(args, void *(*)(int size));
		break;
	case TNT_OPT_REALLOC:
		opt->realloc = va_arg(args, void *(*)(void *ptr, int size));
		break;
	case TNT_OPT_DUP:
		opt->dup = va_arg(args, void *(*)(char *sz));
		break;
	case TNT_OPT_FREE:
		opt->free = va_arg(args, void (*)(void *ptr));
		break;
	default:
		return TNT_EFAIL;
	}
	return TNT_EOK;
}
