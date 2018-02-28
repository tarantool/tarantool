/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
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
		/* 0x02 */	MP_UINT,   /* IPROTO_REPLICA_ID */
		/* 0x03 */	MP_UINT,   /* IPROTO_LSN */
		/* 0x04 */	MP_DOUBLE, /* IPROTO_TIMESTAMP */
		/* 0x05 */	MP_UINT,   /* IPROTO_SCHEMA_VERSION */
		/* 0x06 */	MP_UINT,   /* IPROTO_SERVER_VERSION */
	/* }}} */

	/* {{{ unused */
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
	/* 0x24 */	MP_STR, /* IPROTO_INSTANCE_UUID */
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
	NULL, /* CALL_16 */
	"AUTH",
	"EVAL",
	"UPSERT",
	"CALL",
	"EXECUTE",
	NULL, /* NOP */
};

#define bit(c) (1ULL<<IPROTO_##c)
const uint64_t iproto_body_key_map[IPROTO_TYPE_STAT_MAX] = {
	0,                                                     /* unused */
	bit(SPACE_ID) | bit(LIMIT) | bit(KEY),                 /* SELECT */
	bit(SPACE_ID) | bit(TUPLE),                            /* INSERT */
	bit(SPACE_ID) | bit(TUPLE),                            /* REPLACE */
	bit(SPACE_ID) | bit(KEY) | bit(TUPLE),                 /* UPDATE */
	bit(SPACE_ID) | bit(KEY),                              /* DELETE */
	0,                                                     /* CALL_16 */
	0,                                                     /* AUTH */
	0,                                                     /* EVAL */
	bit(SPACE_ID) | bit(OPS) | bit(TUPLE),                 /* UPSERT */
	0,                                                     /* CALL */
	0,                                                     /* EXECUTE */
	bit(SPACE_ID),                                         /* NOP */
};
#undef bit

const char *iproto_key_strs[IPROTO_KEY_MAX] = {
	"type",             /* 0x00 */
	"sync",             /* 0x01 */
	"replica id",       /* 0x02 */
	"lsn",              /* 0x03 */
	"timestamp",        /* 0x04 */
	"schema version",   /* 0x05 */
	"server version",   /* 0x06 */
	NULL,               /* 0x07 */
	NULL,               /* 0x08 */
	NULL,               /* 0x09 */
	NULL,               /* 0x0a */
	NULL,               /* 0x0b */
	NULL,               /* 0x0c */
	NULL,               /* 0x0d */
	NULL,               /* 0x0e */
	NULL,               /* 0x0f */
	"space id",         /* 0x10 */
	"index id",         /* 0x11 */
	"limit",            /* 0x12 */
	"offset",           /* 0x13 */
	"iterator",         /* 0x14 */
	"index base",       /* 0x15 */
	NULL,               /* 0x16 */
	NULL,               /* 0x17 */
	NULL,               /* 0x18 */
	NULL,               /* 0x19 */
	NULL,               /* 0x1a */
	NULL,               /* 0x1b */
	NULL,               /* 0x1c */
	NULL,               /* 0x1d */
	NULL,               /* 0x1e */
	NULL,               /* 0x1f */
	"key",              /* 0x20 */
	"tuple",            /* 0x21 */
	"function name",    /* 0x22 */
	"user name",        /* 0x23 */
	"instance uuid",    /* 0x24 */
	"cluster uuid",     /* 0x25 */
	"vector clock",     /* 0x26 */
	"expression",       /* 0x27 */
	"operations",       /* 0x28 */
	NULL,               /* 0x29 */
	NULL,               /* 0x2a */
	NULL,               /* 0x2b */
	NULL,               /* 0x2c */
	NULL,               /* 0x2d */
	NULL,               /* 0x2e */
	NULL,               /* 0x2f */
	"data",             /* 0x30 */
	"error",            /* 0x31 */
	"metadata",         /* 0x32 */
	NULL,               /* 0x33 */
	NULL,               /* 0x34 */
	NULL,               /* 0x35 */
	NULL,               /* 0x36 */
	NULL,               /* 0x37 */
	NULL,               /* 0x38 */
	NULL,               /* 0x39 */
	NULL,               /* 0x3a */
	NULL,               /* 0x3b */
	NULL,               /* 0x3c */
	NULL,               /* 0x3d */
	NULL,               /* 0x3e */
	NULL,               /* 0x3f */
	"SQL text",         /* 0x40 */
	"SQL bind",         /* 0x41 */
	"SQL options",      /* 0x42 */
	"SQL info",         /* 0x43 */
	"SQL row count",    /* 0x44 */
};

const char *vy_page_info_key_strs[VY_PAGE_INFO_KEY_MAX] = {
	NULL,
	"offset",
	"size",
	"unpacked size",
	"row count",
	"min key",
	"row index offset"
};

const char *vy_run_info_key_strs[VY_RUN_INFO_KEY_MAX] = {
	NULL,
	"min key",
	"max key",
	"min lsn",
	"max lsn",
	"page count",
	"bloom filter"
};

const char *vy_row_index_key_strs[VY_ROW_INDEX_KEY_MAX] = {
	NULL,
	"row index",
};
