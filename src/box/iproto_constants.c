/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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
#include "iproto_constants.h"

const unsigned char iproto_key_type[IPROTO_KEY_MAX] =
{
	/* {{{ header */
		/* 0x00 */	MP_UINT,   /* IPROTO_REQUEST_TYPE */
		/* 0x01 */	MP_UINT,   /* IPROTO_SYNC */
		/* 0x02 */	MP_UINT,   /* IPROTO_SERVER_ID */
		/* 0x03 */	MP_UINT,   /* IPROTO_LSN */
		/* 0x04 */	MP_DOUBLE, /* IPROTO_TIMESTAMP */
		/* 0x05 */	MP_UINT,   /* IPROTO_SCHEMA_ID */
	/* }}} */

	/* {{{ unused */
		/* 0x06 */	MP_UINT,
		/* 0x07 */	MP_UINT,
		/* 0x08 */	MP_UINT,
		/* 0x09 */	MP_UINT,
		/* 0x0a */	MP_UINT,
		/* 0x0b */	MP_UINT,
		/* 0x0c */	MP_UINT,
		/* 0x0d */	MP_UINT,
		/* 0x0e */	MP_UINT,
		/* 0x0f */	MP_UINT,
	/* }}} */

	/* {{{ body -- integer keys */
		/* 0x10 */	MP_UINT, /* IPROTO_SPACE_ID */
		/* 0x11 */	MP_UINT, /* IPROTO_INDEX_ID */
		/* 0x12 */	MP_UINT, /* IPROTO_LIMIT */
		/* 0x13 */	MP_UINT, /* IPROTO_OFFSET */
		/* 0x14 */	MP_UINT, /* IPROTO_ITERATOR */
		/* 0x15 */	MP_UINT, /* IPROTO_INDEX_BASE */
	/* }}} */

	/* {{{ unused */
		/* 0x16 */	MP_UINT,
		/* 0x17 */	MP_UINT,
		/* 0x18 */	MP_UINT,
		/* 0x19 */	MP_UINT,
		/* 0x1a */	MP_UINT,
		/* 0x1b */	MP_UINT,
		/* 0x1c */	MP_UINT,
		/* 0x1d */	MP_UINT,
		/* 0x1e */	MP_UINT,
		/* 0x1f */	MP_UINT,
	/* }}} */

	/* {{{ body -- all keys */
	/* 0x20 */	MP_ARRAY, /* IPROTO_KEY */
	/* 0x21 */	MP_ARRAY, /* IPROTO_TUPLE */
	/* 0x22 */	MP_STR, /* IPROTO_FUNCTION_NAME */
	/* 0x23 */	MP_STR, /* IPROTO_USER_NAME */
	/* 0x24 */	MP_STR, /* IPROTO_SERVER_UUID */
	/* 0x25 */	MP_STR, /* IPROTO_CLUSTER_UUID */
	/* 0x26 */	MP_MAP, /* IPROTO_VCLOCK */
	/* 0x27 */	MP_STR, /* IPROTO_EXPR */
	/* 0x28 */	MP_ARRAY, /* IPROTO_OPS */
	/* }}} */
};

const char *iproto_type_strs[] =
{
	NULL,
	"SELECT",
	"INSERT",
	"REPLACE",
	"UPDATE",
	"DELETE",
	"CALL",
	"AUTH",
	"EVAL",
	"UPSERT",
};

#define bit(c) (1ULL<<IPROTO_##c)
const uint64_t iproto_body_key_map[IPROTO_UPSERT + 1] = {
	0,                                                     /* unused */
	bit(SPACE_ID) | bit(LIMIT) | bit(KEY),                 /* SELECT */
	bit(SPACE_ID) | bit(TUPLE),                            /* INSERT */
	bit(SPACE_ID) | bit(TUPLE),                            /* REPLACE */
	bit(SPACE_ID) | bit(KEY) | bit(TUPLE),                 /* UPDATE */
	bit(SPACE_ID) | bit(KEY),                              /* DELETE */
	bit(FUNCTION_NAME) | bit(TUPLE),                       /* CALL */
	bit(USER_NAME)| bit(TUPLE),                            /* AUTH */
	bit(EXPR)     | bit(TUPLE),                            /* EVAL */
	bit(SPACE_ID) | bit(OPS) | bit(TUPLE),                 /* UPSERT */
};
#undef bit

const char *iproto_key_strs[IPROTO_KEY_MAX] = {
	"type",             /* 0x00 */
	"sync",             /* 0x01 */
	"server_id",          /* 0x02 */
	"lsn",              /* 0x03 */
	"timestamp",        /* 0x04 */
	"",                 /* 0x05 */
	"",                 /* 0x06 */
	"",                 /* 0x07 */
	"",                 /* 0x08 */
	"",                 /* 0x09 */
	"",                 /* 0x0a */
	"",                 /* 0x0b */
	"",                 /* 0x0c */
	"",                 /* 0x0d */
	"",                 /* 0x0e */
	"",                 /* 0x0f */
	"space_id",         /* 0x10 */
	"index_id",         /* 0x11 */
	"limit",            /* 0x12 */
	"offset",           /* 0x13 */
	"iterator",         /* 0x14 */
	"index_base",       /* 0x15 */
	"",                 /* 0x16 */
	"",                 /* 0x17 */
	"",                 /* 0x18 */
	"",                 /* 0x19 */
	"",                 /* 0x1a */
	"",                 /* 0x1b */
	"",                 /* 0x1c */
	"",                 /* 0x1d */
	"",                 /* 0x1e */
	"",                 /* 0x1f */
	"key",              /* 0x20 */
	"tuple",            /* 0x21 */
	"function name",    /* 0x22 */
	"user name",        /* 0x23 */
	"server UUID",      /* 0x24 */
	"cluster UUID",     /* 0x25 */
	"vector clock",     /* 0x26 */
	"expression",       /* 0x27 */
	"operations",       /* 0x28 */
};

