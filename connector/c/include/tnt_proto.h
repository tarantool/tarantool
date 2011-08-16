#ifndef TNT_PROTO_H_INCLUDED
#define TNT_PROTO_H_INCLUDED

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

#define TNT_PROTO_TYPE_INSERT 13
#define TNT_PROTO_TYPE_SELECT 17
#define TNT_PROTO_TYPE_UPDATE 19
#define TNT_PROTO_TYPE_DELETE 20
#define TNT_PROTO_TYPE_PING   65280

struct tnt_proto_header {
	uint32_t type;
	uint32_t len;
	uint32_t reqid;
};

#define TNT_PROTO_IS_OK(V) ((V) == 0x0)

struct tnt_proto_header_resp {
	struct tnt_proto_header hdr;
	uint32_t code;
};

struct tnt_proto_tuple {
	uint32_t card;
	unsigned char field[];
};

#define TNT_PROTO_FLAG_RETURN    0x01
#define TNT_PROTO_FLAG_ADD       0x02
#define TNT_PROTO_FLAG_REPLACE   0x04
#define TNT_PROTO_FLAG_BOX_QUIET 0x08
#define TNT_PROTO_FLAG_NOT_STORE 0x10

struct tnt_proto_insert {
	uint32_t ns;
	uint32_t flags;
	/* tuple data */
};

#define TNT_PROTO_UPDATE_ASSIGN 0
#define TNT_PROTO_UPDATE_ADD    1
#define TNT_PROTO_UPDATE_AND    2
#define TNT_PROTO_UPDATE_XOR    3
#define TNT_PROTO_UPDATE_OR     4
#define TNT_PROTO_UPDATE_SPLICE 5

struct tnt_proto_update {
	uint32_t ns;
	uint32_t flags;
	/* tuple data */
	/* count */
	/* operation */
};

struct tnt_proto_update_op {
	uint32_t field;
	unsigned char op;
	/* op_arg */
};

struct tnt_proto_delete {
	uint32_t ns;
	/* tuple data */
};

struct tnt_proto_select {
	uint32_t ns;
	uint32_t index;
	uint32_t offset;
	uint32_t limit;
	/* tuple data */
};

#endif /* TNT_PROTO_H_INCLUDED */
