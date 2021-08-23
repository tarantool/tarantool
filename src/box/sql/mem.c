/*
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
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
#include <ctype.h>

#include "sqlInt.h"
#include "mem.h"
#include "vdbeInt.h"
#include "coll/coll.h"
#include "tarantoolInt.h"
#include "box/schema.h"
#include "box/tuple.h"
#include "mpstream/mpstream.h"
#include "box/port.h"
#include "lua/utils.h"
#include "lua/serializer.h"
#include "lua/msgpack.h"
#include "uuid/mp_uuid.h"
#include "mp_decimal.h"

/*
 * Make sure pMem->z points to a writable allocation of at least
 * min(n,32) bytes.
 *
 * If the bPreserve argument is true, then copy of the content of
 * pMem->z into the new allocation.  pMem must be either a string or
 * blob if bPreserve is true.  If bPreserve is false, any prior content
 * in pMem->z is discarded.
 */
static int
sqlVdbeMemGrow(struct Mem *pMem, int n, int preserve);

enum {
	BUF_SIZE = 32,
	STR_VALUE_MAX_LEN = 128,
};

const char *
mem_str(const struct Mem *mem)
{
	char buf[STR_VALUE_MAX_LEN];
	switch (mem->type) {
	case MEM_TYPE_NULL:
		return "NULL";
	case MEM_TYPE_STR:
		if (mem->n <= STR_VALUE_MAX_LEN)
			return tt_sprintf("string('%.*s')", mem->n, mem->z);
		return tt_sprintf("string('%.*s...)", STR_VALUE_MAX_LEN,
				  mem->z);
	case MEM_TYPE_INT:
		return tt_sprintf("integer(%lld)", mem->u.i);
	case MEM_TYPE_UINT:
		return tt_sprintf("integer(%llu)", mem->u.u);
	case MEM_TYPE_DOUBLE:
		sql_snprintf(STR_VALUE_MAX_LEN, buf, "%!.15g", mem->u.r);
		return tt_sprintf("double(%s)", buf);
	case MEM_TYPE_BIN: {
		int len = MIN(mem->n, STR_VALUE_MAX_LEN / 2);
		for (int i = 0; i < len; ++i) {
			int n = (mem->z[i] & 0xF0) >> 4;
			buf[2 * i] = n < 10 ? ('0' + n) : ('A' + n - 10);
			n = (mem->z[i] & 0x0F);
			buf[2 * i + 1] = n < 10 ? ('0' + n) : ('A' + n - 10);
		}
		if (mem->n > len)
			return tt_sprintf("varbinary(x'%.*s...)", len * 2, buf);
		return tt_sprintf("varbinary(x'%.*s')", len * 2, buf);
	}
	case MEM_TYPE_MAP:
	case MEM_TYPE_ARRAY: {
		const char *str = mp_str(mem->z);
		const char *type = mem_type_to_str(mem);
		uint32_t len = strlen(str);
		uint32_t minlen = MIN(STR_VALUE_MAX_LEN, len);
		memcpy(buf, str, minlen);
		if (len <= STR_VALUE_MAX_LEN)
			return tt_sprintf("%s(%.*s)", type, minlen, buf);
		return tt_sprintf("%s(%.*s...)", type, minlen, buf);
	}
	case MEM_TYPE_BOOL:
		return mem->u.b ? "boolean(TRUE)" : "boolean(FALSE)";
	default:
		return "unknown";
	}
}

void
mem_create(struct Mem *mem)
{
	mem->type = MEM_TYPE_NULL;
	mem->flags = 0;
	mem->field_type = field_type_MAX;
	mem->n = 0;
	mem->z = NULL;
	mem->zMalloc = NULL;
	mem->szMalloc = 0;
	mem->uTemp = 0;
	mem->db = sql_get();
	mem->xDel = NULL;
#ifdef SQL_DEBUG
	mem->pScopyFrom = NULL;
	mem->pFiller = NULL;
#endif
}

static inline void
mem_clear(struct Mem *mem)
{
	if ((mem->type & (MEM_TYPE_AGG | MEM_TYPE_FRAME)) != 0 ||
	    (mem->flags & MEM_Dyn) != 0) {
		if (mem->type == MEM_TYPE_AGG)
			sql_vdbemem_finalize(mem, mem->u.func);
		assert(mem->type != MEM_TYPE_AGG);
		if ((mem->flags & MEM_Dyn) != 0) {
			assert(mem->xDel != SQL_DYNAMIC && mem->xDel != NULL);
			mem->xDel((void *)mem->z);
		} else if (mem->type == MEM_TYPE_FRAME) {
			struct VdbeFrame *frame = mem->u.pFrame;
			frame->pParent = frame->v->pDelFrame;
			frame->v->pDelFrame = frame;
		}
	}
	mem->type = MEM_TYPE_NULL;
	mem->flags = 0;
	mem->field_type = field_type_MAX;
}

void
mem_destroy(struct Mem *mem)
{
	mem_clear(mem);
	if (mem->szMalloc > 0) {
		sqlDbFree(mem->db, mem->zMalloc);
		mem->szMalloc = 0;
		mem->zMalloc = NULL;
	}
	mem->n = 0;
	mem->z = NULL;
}

void
mem_set_null(struct Mem *mem)
{
	mem_clear(mem);
}

void
mem_set_int(struct Mem *mem, int64_t value, bool is_neg)
{
	mem_clear(mem);
	mem->u.i = value;
	mem->type = is_neg ? MEM_TYPE_INT : MEM_TYPE_UINT;
	assert(mem->flags == 0);
	mem->field_type = FIELD_TYPE_INTEGER;
}

void
mem_set_uint(struct Mem *mem, uint64_t value)
{
	mem_clear(mem);
	mem->u.u = value;
	mem->type = MEM_TYPE_UINT;
	assert(mem->flags == 0);
	mem->field_type = FIELD_TYPE_UNSIGNED;
}

void
mem_set_bool(struct Mem *mem, bool value)
{
	mem_clear(mem);
	mem->u.b = value;
	mem->type = MEM_TYPE_BOOL;
	assert(mem->flags == 0);
	mem->field_type = FIELD_TYPE_BOOLEAN;
}

void
mem_set_double(struct Mem *mem, double value)
{
	mem_clear(mem);
	mem->field_type = FIELD_TYPE_DOUBLE;
	assert(mem->flags == 0);
	if (sqlIsNaN(value))
		return;
	mem->u.r = value;
	mem->type = MEM_TYPE_DOUBLE;
}

static inline void
set_str_const(struct Mem *mem, char *value, uint32_t len, int alloc_type)
{
	assert((alloc_type & (MEM_Static | MEM_Ephem)) != 0);
	mem_clear(mem);
	mem->z = value;
	mem->n = len;
	mem->type = MEM_TYPE_STR;
	mem->flags = alloc_type;
	mem->field_type = FIELD_TYPE_STRING;
}

static inline void
set_str_dynamic(struct Mem *mem, char *value, uint32_t len, int alloc_type)
{
	assert((mem->flags & MEM_Dyn) == 0 || value != mem->z);
	assert(mem->szMalloc == 0 || value != mem->zMalloc);
	assert(alloc_type == MEM_Dyn || alloc_type == 0);
	mem_destroy(mem);
	mem->z = value;
	mem->n = len;
	mem->type = MEM_TYPE_STR;
	mem->flags = alloc_type;
	mem->field_type = FIELD_TYPE_STRING;
	if (alloc_type == MEM_Dyn) {
		mem->xDel = sql_free;
	} else {
		mem->xDel = NULL;
		mem->zMalloc = mem->z;
		mem->szMalloc = sqlDbMallocSize(mem->db, mem->zMalloc);
	}
}

void
mem_set_str_ephemeral(struct Mem *mem, char *value, uint32_t len)
{
	set_str_const(mem, value, len, MEM_Ephem);
}

void
mem_set_str_static(struct Mem *mem, char *value, uint32_t len)
{
	set_str_const(mem, value, len, MEM_Static);
}

void
mem_set_str_dynamic(struct Mem *mem, char *value, uint32_t len)
{
	set_str_dynamic(mem, value, len, MEM_Dyn);
}

void
mem_set_str_allocated(struct Mem *mem, char *value, uint32_t len)
{
	set_str_dynamic(mem, value, len, 0);
}

void
mem_set_str0_ephemeral(struct Mem *mem, char *value)
{
	set_str_const(mem, value, strlen(value), MEM_Ephem);
	mem->flags |= MEM_Term;
}

void
mem_set_str0_static(struct Mem *mem, char *value)
{
	set_str_const(mem, value, strlen(value), MEM_Static);
	mem->flags |= MEM_Term;
}

void
mem_set_str0_dynamic(struct Mem *mem, char *value)
{
	set_str_dynamic(mem, value, strlen(value), MEM_Dyn);
	mem->flags |= MEM_Term;
}

void
mem_set_str0_allocated(struct Mem *mem, char *value)
{
	set_str_dynamic(mem, value, strlen(value), 0);
	mem->flags |= MEM_Term;
}

int
mem_copy_str(struct Mem *mem, const char *value, uint32_t len)
{
	if (((mem->type & (MEM_TYPE_STR | MEM_TYPE_BIN)) != 0) &&
	    mem->z == value) {
		/* Own value, but might be ephemeral. Make it own if so. */
		if (sqlVdbeMemGrow(mem, len, 1) != 0)
			return -1;
		mem->type = MEM_TYPE_STR;
		mem->flags = 0;
		mem->field_type = FIELD_TYPE_STRING;
		return 0;
	}
	mem_clear(mem);
	if (sqlVdbeMemGrow(mem, len, 0) != 0)
		return -1;
	memcpy(mem->z, value, len);
	mem->n = len;
	mem->type = MEM_TYPE_STR;
	assert(mem->flags == 0);
	mem->field_type = FIELD_TYPE_STRING;
	return 0;
}

int
mem_copy_str0(struct Mem *mem, const char *value)
{
	uint32_t len = strlen(value);
	if (mem_copy_str(mem, value, len + 1) != 0)
		return -1;
	mem->n = len;
	mem->flags |= MEM_Term;
	return 0;
}

static inline void
set_bin_const(struct Mem *mem, char *value, uint32_t size, int alloc_type)
{
	assert((alloc_type & (MEM_Static | MEM_Ephem)) != 0);
	mem_clear(mem);
	mem->z = value;
	mem->n = size;
	mem->type = MEM_TYPE_BIN;
	mem->flags = alloc_type;
	mem->field_type = FIELD_TYPE_VARBINARY;
}

static inline void
set_bin_dynamic(struct Mem *mem, char *value, uint32_t size, int alloc_type)
{
	assert((mem->flags & MEM_Dyn) == 0 || value != mem->z);
	assert(mem->szMalloc == 0 || value != mem->zMalloc);
	assert(alloc_type == MEM_Dyn || alloc_type == 0);
	mem_destroy(mem);
	mem->z = value;
	mem->n = size;
	mem->type = MEM_TYPE_BIN;
	mem->flags = alloc_type;
	mem->field_type = FIELD_TYPE_VARBINARY;
	if (alloc_type == MEM_Dyn) {
		mem->xDel = sql_free;
	} else {
		mem->xDel = NULL;
		mem->zMalloc = mem->z;
		mem->szMalloc = sqlDbMallocSize(mem->db, mem->zMalloc);
	}
}

void
mem_set_bin_ephemeral(struct Mem *mem, char *value, uint32_t size)
{
	set_bin_const(mem, value, size, MEM_Ephem);
}

void
mem_set_bin_static(struct Mem *mem, char *value, uint32_t size)
{
	set_bin_const(mem, value, size, MEM_Static);
}

void
mem_set_bin_dynamic(struct Mem *mem, char *value, uint32_t size)
{
	set_bin_dynamic(mem, value, size, MEM_Dyn);
}

void
mem_set_bin_allocated(struct Mem *mem, char *value, uint32_t size)
{
	set_bin_dynamic(mem, value, size, 0);
}

int
mem_copy_bin(struct Mem *mem, const char *value, uint32_t size)
{
	if (((mem->type & (MEM_TYPE_STR | MEM_TYPE_BIN)) != 0) &&
	    mem->z == value) {
		/* Own value, but might be ephemeral. Make it own if so. */
		if (sqlVdbeMemGrow(mem, size, 1) != 0)
			return -1;
		mem->type = MEM_TYPE_BIN;
		mem->flags = 0;
		mem->field_type = FIELD_TYPE_VARBINARY;
		return 0;
	}
	mem_clear(mem);
	if (sqlVdbeMemGrow(mem, size, 0) != 0)
		return -1;
	memcpy(mem->z, value, size);
	mem->n = size;
	mem->type = MEM_TYPE_BIN;
	assert(mem->flags == 0);
	mem->field_type = FIELD_TYPE_VARBINARY;
	return 0;
}

void
mem_set_zerobin(struct Mem *mem, int n)
{
	mem_destroy(mem);
	if (n < 0)
		n = 0;
	mem->u.nZero = n;
	mem->z = NULL;
	mem->n = 0;
	mem->type = MEM_TYPE_BIN;
	mem->flags = MEM_Zero;
	mem->field_type = FIELD_TYPE_VARBINARY;
}

static inline void
set_msgpack_value(struct Mem *mem, char *value, uint32_t size, int alloc_type,
		  enum field_type type)
{
	assert(type == FIELD_TYPE_MAP || type == FIELD_TYPE_ARRAY);
	if (alloc_type == MEM_Ephem || alloc_type == MEM_Static)
		set_bin_const(mem, value, size, alloc_type);
	else
		set_bin_dynamic(mem, value, size, alloc_type);
	mem->type = type == FIELD_TYPE_MAP ? MEM_TYPE_MAP : MEM_TYPE_ARRAY;
	mem->field_type = type;
}

void
mem_set_map_ephemeral(struct Mem *mem, char *value, uint32_t size)
{
	assert(mp_typeof(*value) == MP_MAP);
	set_msgpack_value(mem, value, size, MEM_Ephem, FIELD_TYPE_MAP);
}

void
mem_set_map_static(struct Mem *mem, char *value, uint32_t size)
{
	assert(mp_typeof(*value) == MP_MAP);
	set_msgpack_value(mem, value, size, MEM_Static, FIELD_TYPE_MAP);
}

void
mem_set_map_dynamic(struct Mem *mem, char *value, uint32_t size)
{
	assert(mp_typeof(*value) == MP_MAP);
	set_msgpack_value(mem, value, size, MEM_Dyn, FIELD_TYPE_MAP);
}

void
mem_set_map_allocated(struct Mem *mem, char *value, uint32_t size)
{
	assert(mp_typeof(*value) == MP_MAP);
	set_msgpack_value(mem, value, size, 0, FIELD_TYPE_MAP);
}

void
mem_set_array_ephemeral(struct Mem *mem, char *value, uint32_t size)
{
	assert(mp_typeof(*value) == MP_ARRAY);
	set_msgpack_value(mem, value, size, MEM_Ephem, FIELD_TYPE_ARRAY);
}

void
mem_set_array_static(struct Mem *mem, char *value, uint32_t size)
{
	assert(mp_typeof(*value) == MP_ARRAY);
	set_msgpack_value(mem, value, size, MEM_Static, FIELD_TYPE_ARRAY);
}

void
mem_set_array_dynamic(struct Mem *mem, char *value, uint32_t size)
{
	assert(mp_typeof(*value) == MP_ARRAY);
	set_msgpack_value(mem, value, size, MEM_Dyn, FIELD_TYPE_ARRAY);
}

void
mem_set_array_allocated(struct Mem *mem, char *value, uint32_t size)
{
	assert(mp_typeof(*value) == MP_ARRAY);
	set_msgpack_value(mem, value, size, 0, FIELD_TYPE_ARRAY);
}

void
mem_set_invalid(struct Mem *mem)
{
	mem_clear(mem);
	mem->type = MEM_TYPE_INVALID;
	assert(mem->flags == 0);
}

void
mem_set_ptr(struct Mem *mem, void *ptr)
{
	mem_clear(mem);
	mem->type = MEM_TYPE_PTR;
	assert(mem->flags == 0);
	mem->u.p = ptr;
}

void
mem_set_frame(struct Mem *mem, struct VdbeFrame *frame)
{
	mem_clear(mem);
	mem->type = MEM_TYPE_FRAME;
	assert(mem->flags == 0);
	mem->u.pFrame = frame;
}

int
mem_set_agg(struct Mem *mem, struct func *func, int size)
{
	mem_clear(mem);
	if (size <= 0)
		return 0;
	if (sqlVdbeMemGrow(mem, size, 0) != 0)
		return -1;
	memset(mem->z, 0, size);
	mem->n = size;
	mem->type = MEM_TYPE_AGG;
	assert(mem->flags == 0);
	mem->u.func = func;
	mem->field_type = field_type_MAX;
	return 0;
}

void
mem_set_null_clear(struct Mem *mem)
{
	mem_clear(mem);
	mem->flags = MEM_Cleared;
}

static inline int
int_to_double(struct Mem *mem)
{
	assert((mem->type & (MEM_TYPE_INT | MEM_TYPE_UINT)) != 0);
	double d;
	if (mem->type == MEM_TYPE_UINT)
		d = (double)mem->u.u;
	else
		d = (double)mem->u.i;
	mem->u.r = d;
	mem->type = MEM_TYPE_DOUBLE;
	assert(mem->flags == 0);
	mem->field_type = FIELD_TYPE_DOUBLE;
	return 0;
}

static inline int
int_to_str0(struct Mem *mem)
{
	assert((mem->type & (MEM_TYPE_INT | MEM_TYPE_UINT)) != 0);
	const char *str;
	if (mem->type == MEM_TYPE_UINT)
		str = tt_sprintf("%llu", mem->u.u);
	else
		str = tt_sprintf("%lld", mem->u.i);
	return mem_copy_str0(mem, str);
}

static inline int
str_to_str0(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_STR);
	if (sqlVdbeMemGrow(mem, mem->n + 1, 1) != 0)
		return -1;
	mem->z[mem->n] = '\0';
	mem->flags |= MEM_Term;
	mem->field_type = FIELD_TYPE_STRING;
	return 0;
}

static inline int
str_to_bin(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_STR);
	mem->type = MEM_TYPE_BIN;
	mem->flags &= ~MEM_Term;
	mem->field_type = FIELD_TYPE_VARBINARY;
	return 0;
}

static inline int
str_to_bool(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_STR);
	char *str = mem->z;
	uint32_t len = mem->n;
	bool b;
	const char *str_true = "TRUE";
	const char *str_false = "FALSE";
	uint32_t len_true = strlen(str_true);
	uint32_t len_false = strlen(str_false);

	for (; isspace(str[0]); str++, len--);
	for (; isspace(str[len - 1]); len--);
	if (len != len_true && len != len_false)
		return -1;

	if (len == len_true && strncasecmp(str, str_true, len) == 0)
		b = true;
	else if (len == len_false && strncasecmp(str, str_false, len) == 0)
		b = false;
	else
		return -1;
	mem_set_bool(mem, b);
	return 0;
}

static inline int
bin_to_str(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_BIN);
	if (ExpandBlob(mem) != 0)
		return -1;
	mem->type = MEM_TYPE_STR;
	mem->field_type = FIELD_TYPE_STRING;
	return 0;
}

static inline int
bin_to_str0(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_BIN);
	if (ExpandBlob(mem) != 0)
		return -1;
	if (sqlVdbeMemGrow(mem, mem->n + 1, 1) != 0)
		return -1;
	mem->z[mem->n] = '\0';
	mem->type = MEM_TYPE_STR;
	mem->flags = MEM_Term;
	mem->field_type = FIELD_TYPE_STRING;
	return 0;
}

static inline int
str_to_int(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_STR);
	bool is_neg;
	int64_t i;
	if (sql_atoi64(mem->z, &i, &is_neg, mem->n) != 0)
		return -1;
	mem_set_int(mem, i, is_neg);
	return 0;
}

static inline int
str_to_uint(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_STR);
	bool is_neg;
	int64_t i;
	if (sql_atoi64(mem->z, &i, &is_neg, mem->n) != 0)
		return -1;
	if (is_neg)
		return -1;
	mem_set_uint(mem, (uint64_t)i);
	return 0;
}

static inline int
str_to_double(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_STR);
	double d;
	if (sqlAtoF(mem->z, &d, mem->n) == 0)
		return -1;
	mem_set_double(mem, d);
	return 0;
}

static inline int
double_to_int(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_DOUBLE);
	double d = mem->u.r;
	if (d <= -1.0 && d >= (double)INT64_MIN) {
		mem->u.i = (int64_t)d;
		mem->type = MEM_TYPE_INT;
		assert(mem->flags == 0);
		mem->field_type = FIELD_TYPE_INTEGER;
		return 0;
	}
	if (d > -1.0 && d < (double)UINT64_MAX) {
		mem->u.u = (uint64_t)d;
		mem->type = MEM_TYPE_UINT;
		assert(mem->flags == 0);
		mem->field_type = FIELD_TYPE_UNSIGNED;
		return 0;
	}
	return -1;
}

static inline int
double_to_int_precise(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_DOUBLE);
	double d = mem->u.r;
	if (d <= -1.0 && d >= (double)INT64_MIN && (double)(int64_t)d == d) {
		mem->u.i = (int64_t)d;
		mem->type = MEM_TYPE_INT;
		assert(mem->flags == 0);
		mem->field_type = FIELD_TYPE_INTEGER;
		return 0;
	}
	if (d > -1.0 && d < (double)UINT64_MAX && (double)(uint64_t)d == d) {
		mem->u.u = (uint64_t)d;
		mem->type = MEM_TYPE_UINT;
		assert(mem->flags == 0);
		mem->field_type = FIELD_TYPE_UNSIGNED;
		return 0;
	}
	return -1;
}

static inline int
double_to_uint(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_DOUBLE);
	double d = mem->u.r;
	if (d > -1.0 && d < (double)UINT64_MAX) {
		mem->u.u = (uint64_t)d;
		mem->type = MEM_TYPE_UINT;
		assert(mem->flags == 0);
		mem->field_type = FIELD_TYPE_UNSIGNED;
		return 0;
	}
	return -1;
}

static inline int
double_to_uint_precise(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_DOUBLE);
	double d = mem->u.r;
	if (d > -1.0 && d < (double)UINT64_MAX && (double)(uint64_t)d == d) {
		mem->u.u = (uint64_t)d;
		mem->type = MEM_TYPE_UINT;
		assert(mem->flags == 0);
		mem->field_type = FIELD_TYPE_UNSIGNED;
		return 0;
	}
	return -1;
}

static inline int
double_to_str0(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_DOUBLE);
	if (sqlVdbeMemGrow(mem, BUF_SIZE, 0) != 0)
		return -1;
	sql_snprintf(BUF_SIZE, mem->z, "%!.15g", mem->u.r);
	mem->n = strlen(mem->z);
	mem->type = MEM_TYPE_STR;
	mem->flags = MEM_Term;
	mem->field_type = FIELD_TYPE_STRING;
	return 0;
}

static inline int
bool_to_str0(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_BOOL);
	const char *str = mem->u.b ? "TRUE" : "FALSE";
	return mem_copy_str0(mem, str);
}

static inline int
array_to_str0(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_ARRAY);
	const char *str = mp_str(mem->z);
	return mem_copy_str0(mem, str);
}

static inline int
map_to_str0(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_MAP);
	const char *str = mp_str(mem->z);
	return mem_copy_str0(mem, str);
}

int
mem_to_int(struct Mem *mem)
{
	assert(mem->type < MEM_TYPE_INVALID);
	if ((mem->type & (MEM_TYPE_INT | MEM_TYPE_UINT)) != 0)
		return 0;
	if (mem->type == MEM_TYPE_STR)
		return str_to_int(mem);
	if (mem->type == MEM_TYPE_DOUBLE)
		return double_to_int(mem);
	return -1;
}

int
mem_to_int_precise(struct Mem *mem)
{
	assert(mem->type < MEM_TYPE_INVALID);
	if ((mem->type & (MEM_TYPE_INT | MEM_TYPE_UINT)) != 0)
		return 0;
	if (mem->type == MEM_TYPE_STR)
		return str_to_int(mem);
	if (mem->type == MEM_TYPE_DOUBLE)
		return double_to_int_precise(mem);
	return -1;
}

int
mem_to_double(struct Mem *mem)
{
	assert(mem->type < MEM_TYPE_INVALID);
	if (mem->type == MEM_TYPE_DOUBLE)
		return 0;
	if ((mem->type & (MEM_TYPE_INT | MEM_TYPE_UINT)) != 0)
		return int_to_double(mem);
	if (mem->type == MEM_TYPE_STR)
		return str_to_double(mem);
	return -1;
}

int
mem_to_number(struct Mem *mem)
{
	assert(mem->type < MEM_TYPE_INVALID);
	if (mem_is_num(mem))
		return 0;
	if (mem->type == MEM_TYPE_STR) {
		if (str_to_int(mem) == 0)
			return 0;
		return str_to_double(mem);
	}
	return -1;
}

int
mem_to_str0(struct Mem *mem)
{
	assert(mem->type < MEM_TYPE_INVALID);
	switch (mem->type) {
	case MEM_TYPE_STR:
		if ((mem->flags & MEM_Term) != 0)
			return 0;
		return str_to_str0(mem);
	case MEM_TYPE_INT:
	case MEM_TYPE_UINT:
		return int_to_str0(mem);
	case MEM_TYPE_DOUBLE:
		return double_to_str0(mem);
	case MEM_TYPE_BOOL:
		return bool_to_str0(mem);
	case MEM_TYPE_BIN:
		return bin_to_str0(mem);
	case MEM_TYPE_MAP:
		return map_to_str0(mem);
	case MEM_TYPE_ARRAY:
		return array_to_str0(mem);
	default:
		return -1;
	}
}

int
mem_to_str(struct Mem *mem)
{
	assert(mem->type < MEM_TYPE_INVALID);
	switch (mem->type) {
	case MEM_TYPE_STR:
		return 0;
	case MEM_TYPE_INT:
	case MEM_TYPE_UINT:
		return int_to_str0(mem);
	case MEM_TYPE_DOUBLE:
		return double_to_str0(mem);
	case MEM_TYPE_BOOL:
		return bool_to_str0(mem);
	case MEM_TYPE_BIN:
		return bin_to_str(mem);
	case MEM_TYPE_MAP:
		return map_to_str0(mem);
	case MEM_TYPE_ARRAY:
		return array_to_str0(mem);
	default:
		return -1;
	}
}

int
mem_cast_explicit(struct Mem *mem, enum field_type type)
{
	if (mem->type == MEM_TYPE_NULL) {
		mem->field_type = type;
		return 0;
	}
	switch (type) {
	case FIELD_TYPE_UNSIGNED:
		switch (mem->type) {
		case MEM_TYPE_UINT:
			return 0;
		case MEM_TYPE_STR:
			return str_to_uint(mem);
		case MEM_TYPE_DOUBLE:
			return double_to_uint(mem);
		default:
			return -1;
		}
	case FIELD_TYPE_STRING:
		return mem_to_str(mem);
	case FIELD_TYPE_DOUBLE:
		return mem_to_double(mem);
	case FIELD_TYPE_INTEGER:
		return mem_to_int(mem);
	case FIELD_TYPE_BOOLEAN:
		switch (mem->type) {
		case MEM_TYPE_BOOL:
			return 0;
		case MEM_TYPE_STR:
			return str_to_bool(mem);
		default:
			return -1;
		}
	case FIELD_TYPE_VARBINARY:
		if (mem->type == MEM_TYPE_STR)
			return str_to_bin(mem);
		if (mem_is_bytes(mem))
			return 0;
		return -1;
	case FIELD_TYPE_NUMBER:
		return mem_to_number(mem);
	case FIELD_TYPE_SCALAR:
		if ((mem->type & (MEM_TYPE_MAP | MEM_TYPE_ARRAY)) != 0)
			return -1;
		return 0;
	default:
		break;
	}
	return -1;
}

int
mem_cast_implicit(struct Mem *mem, enum field_type type)
{
	if (mem->type == MEM_TYPE_NULL) {
		mem->field_type = type;
		return 0;
	}
	switch (type) {
	case FIELD_TYPE_UNSIGNED:
		if (mem->type == MEM_TYPE_UINT)
			return 0;
		if (mem->type == MEM_TYPE_DOUBLE)
			return double_to_uint(mem);
		return -1;
	case FIELD_TYPE_STRING:
		if (mem->type == MEM_TYPE_STR)
			return 0;
		return -1;
	case FIELD_TYPE_DOUBLE:
		if (mem->type == MEM_TYPE_DOUBLE)
			return 0;
		if ((mem->type & (MEM_TYPE_INT | MEM_TYPE_UINT)) != 0)
			return int_to_double(mem);
		return -1;
	case FIELD_TYPE_INTEGER:
		if ((mem->type & (MEM_TYPE_INT | MEM_TYPE_UINT)) != 0)
			return 0;
		if (mem->type == MEM_TYPE_DOUBLE)
			return double_to_int(mem);
		return -1;
	case FIELD_TYPE_BOOLEAN:
		if (mem->type == MEM_TYPE_BOOL)
			return 0;
		return -1;
	case FIELD_TYPE_VARBINARY:
		if ((mem->type & (MEM_TYPE_BIN | MEM_TYPE_MAP |
				  MEM_TYPE_ARRAY)) != 0)
			return 0;
		return -1;
	case FIELD_TYPE_NUMBER:
		if (mem_is_num(mem))
			return 0;
		return -1;
	case FIELD_TYPE_MAP:
		if (mem->type == MEM_TYPE_MAP)
			return 0;
		return -1;
	case FIELD_TYPE_ARRAY:
		if (mem->type == MEM_TYPE_ARRAY)
			return 0;
		return -1;
	case FIELD_TYPE_SCALAR:
		if ((mem->type & (MEM_TYPE_MAP | MEM_TYPE_ARRAY)) != 0)
			return -1;
		return 0;
	case FIELD_TYPE_ANY:
		return 0;
	default:
		break;
	}
	return -1;
}

int
mem_cast_implicit_old(struct Mem *mem, enum field_type type)
{
	if (mem->type == MEM_TYPE_NULL)
		return 0;
	switch (type) {
	case FIELD_TYPE_UNSIGNED:
		if (mem->type == MEM_TYPE_UINT)
			return 0;
		if (mem->type == MEM_TYPE_DOUBLE)
			return double_to_uint_precise(mem);
		if (mem->type == MEM_TYPE_STR)
			return str_to_uint(mem);
		return -1;
	case FIELD_TYPE_STRING:
		if ((mem->type & (MEM_TYPE_STR | MEM_TYPE_BIN)) != 0)
			return 0;
		if ((mem->type & (MEM_TYPE_INT | MEM_TYPE_UINT)) != 0)
			return int_to_str0(mem);
		if (mem->type == MEM_TYPE_DOUBLE)
			return double_to_str0(mem);
		return -1;
	case FIELD_TYPE_DOUBLE:
		if (mem->type == MEM_TYPE_DOUBLE)
			return 0;
		if ((mem->type & (MEM_TYPE_INT | MEM_TYPE_UINT)) != 0)
			return int_to_double(mem);
		if (mem->type == MEM_TYPE_STR)
			return bin_to_str(mem);
		return -1;
	case FIELD_TYPE_INTEGER:
		if ((mem->type & (MEM_TYPE_INT | MEM_TYPE_UINT)) != 0)
			return 0;
		if (mem->type == MEM_TYPE_STR)
			return str_to_int(mem);
		if (mem->type == MEM_TYPE_DOUBLE)
			return double_to_int_precise(mem);
		return -1;
	case FIELD_TYPE_BOOLEAN:
		if (mem->type == MEM_TYPE_BOOL)
			return 0;
		return -1;
	case FIELD_TYPE_VARBINARY:
		if (mem->type == MEM_TYPE_BIN)
			return 0;
		return -1;
	case FIELD_TYPE_NUMBER:
		if (mem_is_num(mem))
			return 0;
		if (mem->type == MEM_TYPE_STR)
			return mem_to_number(mem);
		return -1;
	case FIELD_TYPE_MAP:
		if (mem->type == MEM_TYPE_MAP)
			return 0;
		return -1;
	case FIELD_TYPE_ARRAY:
		if (mem->type == MEM_TYPE_ARRAY)
			return 0;
		return -1;
	case FIELD_TYPE_SCALAR:
		if ((mem->type & (MEM_TYPE_MAP | MEM_TYPE_ARRAY)) != 0)
			return -1;
		return 0;
	default:
		break;
	}
	return -1;
}

int
mem_get_int(const struct Mem *mem, int64_t *i, bool *is_neg)
{
	if (mem->type == MEM_TYPE_INT) {
		*i = mem->u.i;
		*is_neg = true;
		return 0;
	}
	if (mem->type == MEM_TYPE_UINT) {
		*i = mem->u.i;
		*is_neg = false;
		return 0;
	}
	if ((mem->type & (MEM_TYPE_STR | MEM_TYPE_BIN)) != 0)
		return sql_atoi64(mem->z, i, is_neg, mem->n);
	if (mem->type == MEM_TYPE_DOUBLE) {
		double d = mem->u.r;
		if (d <= -1.0 && d >= (double)INT64_MIN) {
			*i = (int64_t)d;
			*is_neg = true;
			return 0;
		}
		if (d > -1.0 && d < (double)UINT64_MAX) {
			*i = (int64_t)(uint64_t)d;
			*is_neg = false;
			return 0;
		}
		return -1;
	}
	return -1;
}

int
mem_get_uint(const struct Mem *mem, uint64_t *u)
{
	if (mem->type == MEM_TYPE_INT)
		return -1;
	if (mem->type == MEM_TYPE_UINT) {
		*u = mem->u.u;
		return 0;
	}
	if ((mem->type & (MEM_TYPE_STR | MEM_TYPE_BIN)) != 0) {
		bool is_neg;
		if (sql_atoi64(mem->z, (int64_t *)u, &is_neg, mem->n) != 0 ||
		    is_neg)
			return -1;
		return 0;
	}
	if (mem->type == MEM_TYPE_DOUBLE) {
		double d = mem->u.r;
		if (d > -1.0 && d < (double)UINT64_MAX) {
			*u = (uint64_t)d;
			return 0;
		}
		return -1;
	}
	return -1;
}

int
mem_get_double(const struct Mem *mem, double *d)
{
	if (mem->type == MEM_TYPE_DOUBLE) {
		*d = mem->u.r;
		return 0;
	}
	if (mem->type == MEM_TYPE_INT) {
		*d = (double)mem->u.i;
		return 0;
	}
	if (mem->type == MEM_TYPE_UINT) {
		*d = (double)mem->u.u;
		return 0;
	}
	if (mem->type == MEM_TYPE_STR) {
		if (sqlAtoF(mem->z, d, mem->n) == 0)
			return -1;
		return 0;
	}
	return -1;
}

int
mem_get_bool(const struct Mem *mem, bool *b)
{
	if (mem->type == MEM_TYPE_BOOL) {
		*b = mem->u.b;
		return 0;
	}
	return -1;
}

int
mem_get_str0(const struct Mem *mem, const char **s)
{
	if (mem->type != MEM_TYPE_STR || (mem->flags & MEM_Term) == 0)
		return -1;
	*s = mem->z;
	return 0;
}

int
mem_get_bin(const struct Mem *mem, const char **s)
{
	if (mem->type == MEM_TYPE_STR) {
		*s = mem->n > 0 ? mem->z : NULL;
		return 0;
	}
	if (mem->type != MEM_TYPE_BIN || (mem->flags & MEM_Zero) != 0)
		return -1;
	*s = mem->z;
	return 0;
}

int
mem_len(const struct Mem *mem, uint32_t *len)
{
	if (!mem_is_bytes(mem))
		return -1;
	assert((mem->flags & MEM_Zero) == 0 || mem->type == MEM_TYPE_BIN);
	if ((mem->flags & MEM_Zero) != 0)
		*len = mem->n + mem->u.nZero;
	else
		*len = mem->n;
	return 0;
}

int
mem_get_agg(const struct Mem *mem, void **accum)
{
	if (mem->type != MEM_TYPE_AGG)
		return -1;
	*accum = mem->z;
	return 0;
}

int
mem_copy(struct Mem *to, const struct Mem *from)
{
	mem_clear(to);
	to->u = from->u;
	to->type = from->type;
	to->flags = from->flags;
	to->field_type = from->field_type;
	to->n = from->n;
	to->z = from->z;
	if (!mem_is_bytes(to))
		return 0;
	if ((to->flags & MEM_Static) != 0)
		return 0;
	assert((to->flags & MEM_Zero) == 0 || to->type == MEM_TYPE_BIN);
	if ((to->flags & MEM_Zero) != 0)
		return sqlVdbeMemExpandBlob(to);
	to->zMalloc = sqlDbRealloc(to->db, to->zMalloc, MAX(32, to->n));
	assert(to->zMalloc != NULL || sql_get()->mallocFailed != 0);
	if (to->zMalloc == NULL)
		return -1;
	to->szMalloc = sqlDbMallocSize(to->db, to->zMalloc);
	memcpy(to->zMalloc, to->z, to->n);
	to->z = to->zMalloc;
	to->flags &= MEM_Term;
	return 0;
}

void
mem_copy_as_ephemeral(struct Mem *to, const struct Mem *from)
{
	mem_clear(to);
	to->u = from->u;
	to->type = from->type;
	to->flags = from->flags;
	to->field_type = from->field_type;
	to->n = from->n;
	to->z = from->z;
	if (!mem_is_bytes(to))
		return;
	if ((to->flags & (MEM_Static | MEM_Ephem)) != 0)
		return;
	to->flags &= MEM_Term | MEM_Zero;
	to->flags |= MEM_Ephem;
	return;
}

void
mem_move(struct Mem *to, struct Mem *from)
{
	mem_destroy(to);
	memcpy(to, from, sizeof(*to));
	from->type = MEM_TYPE_NULL;
	from->flags = 0;
	from->szMalloc = 0;
	from->zMalloc = NULL;
}

static bool
try_return_null(const struct Mem *a, const struct Mem *b, struct Mem *result,
		enum field_type type)
{
	mem_clear(result);
	result->field_type = type;
	return ((a->type | b->type) & MEM_TYPE_NULL) != 0;
}

int
mem_concat(struct Mem *a, struct Mem *b, struct Mem *result)
{
	assert(result != b);
	if (a != result) {
		if (try_return_null(a, b, result, FIELD_TYPE_STRING))
			return 0;
	} else {
		if (((a->type | b->type) & MEM_TYPE_NULL) != 0) {
			mem_clear(a);
			result->field_type = FIELD_TYPE_STRING;
			return 0;
		}
	}

	/* Concatenation operation can be applied only to strings and blobs. */
	if (((b->type & (MEM_TYPE_STR | MEM_TYPE_BIN)) == 0)) {
		diag_set(ClientError, ER_INCONSISTENT_TYPES,
			 "string or varbinary", mem_str(b));
		return -1;
	}
	if (((a->type & (MEM_TYPE_STR | MEM_TYPE_BIN)) == 0)) {
		diag_set(ClientError, ER_INCONSISTENT_TYPES,
			 "string or varbinary", mem_str(a));
		return -1;
	}

	/* Moreover, both operands must be of the same type. */
	if (b->type != a->type) {
		diag_set(ClientError, ER_INCONSISTENT_TYPES,
			 mem_type_to_str(a), mem_str(b));
		return -1;
	}

	if (ExpandBlob(a) != 0 || ExpandBlob(b) != 0)
		return -1;

	uint32_t size = a->n + b->n;
	if ((int)size > sql_get()->aLimit[SQL_LIMIT_LENGTH]) {
		diag_set(ClientError, ER_SQL_EXECUTE, "string or blob too big");
		return -1;
	}
	if (sqlVdbeMemGrow(result, size, result == a) != 0)
		return -1;

	result->type = a->type;
	result->flags = 0;
	if (result->type == MEM_TYPE_BIN)
		result->field_type = FIELD_TYPE_VARBINARY;
	if (result != a)
		memcpy(result->z, a->z, a->n);
	memcpy(&result->z[a->n], b->z, b->n);
	result->n = size;
	return 0;
}

struct sql_num {
	union {
		int64_t i;
		uint64_t u;
		double d;
	};
	enum mem_type type;
	bool is_neg;
};

static int
get_number(const struct Mem *mem, struct sql_num *number)
{
	if (mem->type == MEM_TYPE_DOUBLE) {
		number->d = mem->u.r;
		number->type = MEM_TYPE_DOUBLE;
		return 0;
	}
	if (mem->type == MEM_TYPE_INT) {
		number->i = mem->u.i;
		number->type = MEM_TYPE_INT;
		number->is_neg = true;
		return 0;
	}
	if (mem->type == MEM_TYPE_UINT) {
		number->u = mem->u.u;
		number->type = MEM_TYPE_UINT;
		number->is_neg = false;
		return 0;
	}
	if ((mem->type & (MEM_TYPE_STR | MEM_TYPE_BIN)) == 0)
		return -1;
	if (sql_atoi64(mem->z, &number->i, &number->is_neg, mem->n) == 0) {
		number->type = number->is_neg ? MEM_TYPE_INT : MEM_TYPE_UINT;
		/*
		 * The next line should be removed along with the is_neg field
		 * of struct sql_num. The integer type tells us about the sign.
		 * However, if it is removed, the behavior of arithmetic
		 * operations will change.
		 */
		number->is_neg = false;
		return 0;
	}
	if (sqlAtoF(mem->z, &number->d, mem->n) != 0) {
		number->type = MEM_TYPE_DOUBLE;
		return 0;
	}
	return -1;
}

int
mem_add(const struct Mem *left, const struct Mem *right, struct Mem *result)
{
	if (mem_is_any_null(left, right)) {
		mem_set_null(result);
		result->field_type = FIELD_TYPE_NUMBER;
		return 0;
	}
	if (!mem_is_num(right)) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(right),
			 "number");
		return -1;
	}
	if (!mem_is_num(left)) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(left),
			 "number");
		return -1;
	}
	if (((left->type | right->type) & MEM_TYPE_DOUBLE) != 0) {
		double a;
		double b;
		mem_get_double(left, &a);
		mem_get_double(right, &b);
		mem_set_double(result, a + b);
		return 0;
	}
	int64_t res;
	bool is_neg;
	if (sql_add_int(left->u.i, left->type == MEM_TYPE_INT, right->u.i,
			right->type == MEM_TYPE_INT, &res, &is_neg) != 0) {
		diag_set(ClientError, ER_SQL_EXECUTE, "integer is overflowed");
		return -1;
	}
	mem_set_int(result, res, is_neg);
	return 0;
}

int
mem_sub(const struct Mem *left, const struct Mem *right, struct Mem *result)
{
	if (mem_is_any_null(left, right)) {
		mem_set_null(result);
		result->field_type = FIELD_TYPE_NUMBER;
		return 0;
	}
	if (!mem_is_num(right)) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(right),
			 "number");
		return -1;
	}
	if (!mem_is_num(left)) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(left),
			 "number");
		return -1;
	}
	if (((left->type | right->type) & MEM_TYPE_DOUBLE) != 0) {
		double a;
		double b;
		mem_get_double(left, &a);
		mem_get_double(right, &b);
		mem_set_double(result, a - b);
		return 0;
	}
	int64_t res;
	bool is_neg;
	if (sql_sub_int(left->u.i, left->type == MEM_TYPE_INT, right->u.i,
			right->type == MEM_TYPE_INT, &res, &is_neg) != 0) {
		diag_set(ClientError, ER_SQL_EXECUTE, "integer is overflowed");
		return -1;
	}
	mem_set_int(result, res, is_neg);
	return 0;
}

int
mem_mul(const struct Mem *left, const struct Mem *right, struct Mem *result)
{
	if (mem_is_any_null(left, right)) {
		mem_set_null(result);
		result->field_type = FIELD_TYPE_NUMBER;
		return 0;
	}
	if (!mem_is_num(right)) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(right),
			 "number");
		return -1;
	}
	if (!mem_is_num(left)) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(left),
			 "number");
		return -1;
	}
	if (((left->type | right->type) & MEM_TYPE_DOUBLE) != 0) {
		double a;
		double b;
		mem_get_double(left, &a);
		mem_get_double(right, &b);
		mem_set_double(result, a * b);
		return 0;
	}
	int64_t res;
	bool is_neg;
	if (sql_mul_int(left->u.i, left->type == MEM_TYPE_INT, right->u.i,
			right->type == MEM_TYPE_INT, &res, &is_neg) != 0) {
		diag_set(ClientError, ER_SQL_EXECUTE, "integer is overflowed");
		return -1;
	}
	mem_set_int(result, res, is_neg);
	return 0;
}

int
mem_div(const struct Mem *left, const struct Mem *right, struct Mem *result)
{
	if (mem_is_any_null(left, right)) {
		mem_set_null(result);
		result->field_type = FIELD_TYPE_NUMBER;
		return 0;
	}
	if (!mem_is_num(right)) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(right),
			 "number");
		return -1;
	}
	if (!mem_is_num(left)) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(left),
			 "number");
		return -1;
	}
	if (((left->type | right->type) & MEM_TYPE_DOUBLE) != 0) {
		double a;
		double b;
		mem_get_double(left, &a);
		mem_get_double(right, &b);
		if (b == 0.0) {
			diag_set(ClientError, ER_SQL_EXECUTE,
				 "division by zero");
			return -1;
		}
		mem_set_double(result, a / b);
		return 0;
	}
	if (right->u.u == 0) {
		diag_set(ClientError, ER_SQL_EXECUTE, "division by zero");
		return -1;
	}
	int64_t res;
	bool is_neg;
	if (sql_div_int(left->u.i, left->type == MEM_TYPE_INT, right->u.i,
			right->type == MEM_TYPE_INT, &res, &is_neg) != 0) {
		diag_set(ClientError, ER_SQL_EXECUTE, "integer is overflowed");
		return -1;
	}
	mem_set_int(result, res, is_neg);
	return 0;
}

int
mem_rem(const struct Mem *left, const struct Mem *right, struct Mem *result)
{
	if (mem_is_any_null(left, right)) {
		mem_set_null(result);
		result->field_type = FIELD_TYPE_INTEGER;
		return 0;
	}
	if (!mem_is_int(right)) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(right),
			 "integer");
		return -1;
	}
	if (!mem_is_int(left)) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(left),
			 "integer");
		return -1;
	}
	if (right->u.u == 0) {
		diag_set(ClientError, ER_SQL_EXECUTE, "division by zero");
		return -1;
	}
	int64_t res;
	bool is_neg;
	if (sql_rem_int(left->u.i, left->type == MEM_TYPE_INT, right->u.i,
			right->type == MEM_TYPE_INT, &res, &is_neg) != 0) {
		diag_set(ClientError, ER_SQL_EXECUTE, "integer is overflowed");
		return -1;
	}
	mem_set_int(result, res, is_neg);
	return 0;
}

int
mem_bit_and(const struct Mem *left, const struct Mem *right, struct Mem *result)
{
	if (mem_is_any_null(left, right)) {
		mem_set_null(result);
		result->field_type = FIELD_TYPE_UNSIGNED;
		return 0;
	}
	if (right->type != MEM_TYPE_UINT) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(right),
			 "unsigned");
		return -1;
	}
	if (left->type != MEM_TYPE_UINT) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(left),
			 "unsigned");
		return -1;
	}
	mem_set_uint(result, left->u.u & right->u.u);
	return 0;
}

int
mem_bit_or(const struct Mem *left, const struct Mem *right, struct Mem *result)
{
	if (mem_is_any_null(left, right)) {
		mem_set_null(result);
		result->field_type = FIELD_TYPE_UNSIGNED;
		return 0;
	}
	if (right->type != MEM_TYPE_UINT) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(right),
			 "unsigned");
		return -1;
	}
	if (left->type != MEM_TYPE_UINT) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(left),
			 "unsigned");
		return -1;
	}
	mem_set_uint(result, left->u.u | right->u.u);
	return 0;
}

int
mem_shift_left(const struct Mem *left, const struct Mem *right,
	       struct Mem *result)
{
	if (mem_is_any_null(left, right)) {
		mem_set_null(result);
		result->field_type = FIELD_TYPE_UNSIGNED;
		return 0;
	}
	if (right->type != MEM_TYPE_UINT) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(right),
			 "unsigned");
		return -1;
	}
	if (left->type != MEM_TYPE_UINT) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(left),
			 "unsigned");
		return -1;
	}
	mem_set_uint(result, right->u.u >= 64 ? 0 : left->u.u << right->u.u);
	return 0;
}

int
mem_shift_right(const struct Mem *left, const struct Mem *right,
		struct Mem *result)
{
	if (mem_is_any_null(left, right)) {
		mem_set_null(result);
		result->field_type = FIELD_TYPE_UNSIGNED;
		return 0;
	}
	if (right->type != MEM_TYPE_UINT) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(right),
			 "unsigned");
		return -1;
	}
	if (left->type != MEM_TYPE_UINT) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(left),
			 "unsigned");
		return -1;
	}
	mem_set_uint(result, right->u.u >= 64 ? 0 : left->u.u >> right->u.u);
	return 0;
}

int
mem_bit_not(const struct Mem *mem, struct Mem *result)
{
	if (mem_is_null(mem)) {
		mem_set_null(result);
		result->field_type = FIELD_TYPE_UNSIGNED;
		return 0;
	}
	if (mem->type != MEM_TYPE_UINT) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(mem),
			 "unsigned");
		return -1;
	}
	mem_set_uint(result, ~mem->u.u);
	return 0;
}

int
mem_cmp_bool(const struct Mem *a, const struct Mem *b, int *result)
{
	if ((a->type & b->type & MEM_TYPE_BOOL) == 0)
		return -1;
	if (a->u.b == b->u.b)
		*result = 0;
	else if (a->u.b)
		*result = 1;
	else
		*result = -1;
	return 0;
}

int
mem_cmp_bin(const struct Mem *a, const struct Mem *b, int *result)
{
	if ((a->type & b->type & MEM_TYPE_BIN) == 0)
		return -1;
	int an = a->n;
	int bn = b->n;
	int minlen = MIN(an, bn);

	/*
	 * It is possible to have a Blob value that has some non-zero content
	 * followed by zero content.  But that only comes up for Blobs formed
	 * by the OP_MakeRecord opcode, and such Blobs never get passed into
	 * mem_compare().
	 */
	assert((a->flags & MEM_Zero) == 0 || an == 0);
	assert((b->flags & MEM_Zero) == 0 || bn == 0);

	if ((a->flags & b->flags & MEM_Zero) != 0) {
		*result = a->u.nZero - b->u.nZero;
		return 0;
	}
	if ((a->flags & MEM_Zero) != 0) {
		for (int i = 0; i < minlen; ++i) {
			if (b->z[i] != 0) {
				*result = -1;
				return 0;
			}
		}
		*result = a->u.nZero - bn;
		return 0;
	}
	if ((b->flags & MEM_Zero) != 0) {
		for (int i = 0; i < minlen; ++i) {
			if (a->z[i] != 0){
				*result = 1;
				return 0;
			}
		}
		*result = b->u.nZero - an;
		return 0;
	}
	*result = memcmp(a->z, b->z, minlen);
	if (*result != 0)
		return 0;
	*result = an - bn;
	return 0;
}

int
mem_cmp_num(const struct Mem *left, const struct Mem *right, int *result)
{
	struct sql_num a, b;
	/* TODO: Here should be check for right value type. */
	if (get_number(right, &b) != 0) {
		*result = -1;
		return 0;
	}
	if (get_number(left, &a) != 0)
		return -1;
	if (a.type == MEM_TYPE_DOUBLE) {
		if (b.type == MEM_TYPE_DOUBLE) {
			if (a.d > b.d)
				*result = 1;
			else if (a.d < b.d)
				*result = -1;
			else
				*result = 0;
			return 0;
		}
		if (b.type == MEM_TYPE_INT)
			*result = double_compare_nint64(a.d, b.i, 1);
		else
			*result = double_compare_uint64(a.d, b.u, 1);
		return 0;
	}
	if (a.type == MEM_TYPE_INT) {
		if (b.type == MEM_TYPE_INT) {
			if (a.i > b.i)
				*result = 1;
			else if (a.i < b.i)
				*result = -1;
			else
				*result = 0;
			return 0;
		}
		if (b.type == MEM_TYPE_UINT)
			*result = -1;
		else
			*result = double_compare_nint64(b.d, a.i, -1);
		return 0;
	}
	assert(a.type == MEM_TYPE_UINT);
	if (b.type == MEM_TYPE_UINT) {
		if (a.u > b.u)
			*result = 1;
		else if (a.u < b.u)
			*result = -1;
		else
			*result = 0;
		return 0;
	}
	if (b.type == MEM_TYPE_INT)
		*result = 1;
	else
		*result = double_compare_uint64(b.d, a.u, -1);
	return 0;
}

int
mem_cmp_str(const struct Mem *left, const struct Mem *right, int *result,
	    const struct coll *coll)
{
	char *a;
	uint32_t an;
	char bufl[BUF_SIZE];
	if (left->type == MEM_TYPE_STR) {
		a = left->z;
		an = left->n;
	} else {
		assert(mem_is_num(left));
		a = &bufl[0];
		if (left->type == MEM_TYPE_INT)
			sql_snprintf(BUF_SIZE, a, "%lld", left->u.i);
		else if (left->type == MEM_TYPE_UINT)
			sql_snprintf(BUF_SIZE, a, "%llu", left->u.u);
		else
			sql_snprintf(BUF_SIZE, a, "%!.15g", left->u.r);
		an = strlen(a);
	}

	char *b;
	uint32_t bn;
	char bufr[BUF_SIZE];
	if (right->type == MEM_TYPE_STR) {
		b = right->z;
		bn = right->n;
	} else {
		assert(mem_is_num(right));
		b = &bufr[0];
		if (right->type == MEM_TYPE_INT)
			sql_snprintf(BUF_SIZE, b, "%lld", right->u.i);
		else if (right->type == MEM_TYPE_UINT)
			sql_snprintf(BUF_SIZE, b, "%llu", right->u.u);
		else
			sql_snprintf(BUF_SIZE, b, "%!.15g", right->u.r);
		bn = strlen(b);
	}
	if (coll != NULL) {
		*result = coll->cmp(a, an, b, bn, coll);
		return 0;
	}
	uint32_t minlen = MIN(an, bn);
	*result = memcmp(a, b, minlen);
	if (*result != 0)
		return 0;
	*result = an - bn;
	return 0;
}

/*
 * Both *pMem1 and *pMem2 contain string values. Compare the two values
 * using the collation sequence pColl. As usual, return a negative , zero
 * or positive value if *pMem1 is less than, equal to or greater than
 * *pMem2, respectively. Similar in spirit to "rc = (*pMem1) - (*pMem2);".
 *
 * Strungs assume to be UTF-8 encoded
 */
static int
vdbeCompareMemString(const Mem * pMem1, const Mem * pMem2,
		     const struct coll * pColl)
{
	return pColl->cmp(pMem1->z, (size_t)pMem1->n,
			      pMem2->z, (size_t)pMem2->n, pColl);
}

/*
 * The input pBlob is guaranteed to be a Blob that is not marked
 * with MEM_Zero.  Return true if it could be a zero-blob.
 */
static int
isAllZero(const char *z, int n)
{
	int i;
	for (i = 0; i < n; i++) {
		if (z[i])
			return 0;
	}
	return 1;
}

char *
mem_type_to_str(const struct Mem *p)
{
	assert(p != NULL);
	switch (p->type) {
	case MEM_TYPE_NULL:
		return "NULL";
	case MEM_TYPE_STR:
		return "string";
	case MEM_TYPE_INT:
		return "integer";
	case MEM_TYPE_UINT:
		return "unsigned";
	case MEM_TYPE_DOUBLE:
		return "double";
	case MEM_TYPE_ARRAY:
		return "array";
	case MEM_TYPE_MAP:
		return "map";
	case MEM_TYPE_BIN:
		return "varbinary";
	case MEM_TYPE_BOOL:
		return "boolean";
	default:
		unreachable();
	}
}

enum mp_type
mem_mp_type(struct Mem *mem)
{
	assert(mem->type < MEM_TYPE_INVALID);
	switch (mem->type) {
	case MEM_TYPE_NULL:
		return MP_NIL;
	case MEM_TYPE_UINT:
		return MP_UINT;
	case MEM_TYPE_INT:
		return MP_INT;
	case MEM_TYPE_STR:
		return MP_STR;
	case MEM_TYPE_BIN:
		return MP_BIN;
	case MEM_TYPE_ARRAY:
		return MP_ARRAY;
	case MEM_TYPE_MAP:
		return MP_MAP;
	case MEM_TYPE_BOOL:
		return MP_BOOL;
	case MEM_TYPE_DOUBLE:
		return MP_DOUBLE;
	default:
		unreachable();
	}
	return MP_NIL;
}

/* EVIDENCE-OF: R-12793-43283 Every value in sql has one of five
 * fundamental datatypes: 64-bit signed integer 64-bit IEEE floating
 * point number string BLOB NULL
 */
enum mp_type
sql_value_type(sql_value *pVal)
{
	struct Mem *mem = (struct Mem *) pVal;
	return mem_mp_type(mem);
}

#ifdef SQL_DEBUG
/*
 * Check invariants on a Mem object.
 *
 * This routine is intended for use inside of assert() statements, like
 * this:    assert( sqlVdbeCheckMemInvariants(pMem) );
 */
int
sqlVdbeCheckMemInvariants(Mem * p)
{
	/* If MEM_Dyn is set then Mem.xDel!=0.
	 * Mem.xDel is might not be initialized if MEM_Dyn is clear.
	 */
	assert((p->flags & MEM_Dyn) == 0 || p->xDel != 0);

	/* MEM_Dyn may only be set if Mem.szMalloc==0.  In this way we
	 * ensure that if Mem.szMalloc>0 then it is safe to do
	 * Mem.z = Mem.zMalloc without having to check Mem.flags&MEM_Dyn.
	 * That saves a few cycles in inner loops.
	 */
	assert((p->flags & MEM_Dyn) == 0 || p->szMalloc == 0);

	/* The szMalloc field holds the correct memory allocation size */
	assert(p->szMalloc == 0 ||
	       p->szMalloc == sqlDbMallocSize(p->db, p->zMalloc));

	/* If p holds a string or blob, the Mem.z must point to exactly
	 * one of the following:
	 *
	 *   (1) Memory in Mem.zMalloc and managed by the Mem object
	 *   (2) Memory to be freed using Mem.xDel
	 *   (3) An ephemeral string or blob
	 *   (4) A static string or blob
	 */
	if ((p->type & (MEM_TYPE_STR | MEM_TYPE_BIN)) != 0 && p->n > 0) {
		assert(((p->szMalloc > 0 && p->z == p->zMalloc) ? 1 : 0) +
		       ((p->flags & MEM_Dyn) != 0 ? 1 : 0) +
		       ((p->flags & MEM_Ephem) != 0 ? 1 : 0) +
		       ((p->flags & MEM_Static) != 0 ? 1 : 0) == 1);
	}
	return 1;
}

/*
 * Write a nice string representation of the contents of cell pMem
 * into buffer zBuf, length nBuf.
 */
void
sqlVdbeMemPrettyPrint(Mem *pMem, char *zBuf)
{
	char *zCsr = zBuf;
	int f = pMem->flags;

	if (pMem->type == MEM_TYPE_BIN) {
		int i;
		char c;
		if (f & MEM_Dyn) {
			c = 'z';
			assert((f & (MEM_Static|MEM_Ephem))==0);
		} else if (f & MEM_Static) {
			c = 't';
			assert((f & (MEM_Dyn|MEM_Ephem))==0);
		} else if (f & MEM_Ephem) {
			c = 'e';
			assert((f & (MEM_Static|MEM_Dyn))==0);
		} else {
			c = 's';
		}

		sql_snprintf(100, zCsr, "%c", c);
		zCsr += sqlStrlen30(zCsr);
		sql_snprintf(100, zCsr, "%d[", pMem->n);
		zCsr += sqlStrlen30(zCsr);
		for(i=0; i<16 && i<pMem->n; i++) {
			sql_snprintf(100, zCsr, "%02X", ((int)pMem->z[i] & 0xFF));
			zCsr += sqlStrlen30(zCsr);
		}
		for(i=0; i<16 && i<pMem->n; i++) {
			char z = pMem->z[i];
			if (z<32 || z>126) *zCsr++ = '.';
			else *zCsr++ = z;
		}
		sql_snprintf(100, zCsr, "]%s", "(8)");
		zCsr += sqlStrlen30(zCsr);
		if (f & MEM_Zero) {
			sql_snprintf(100, zCsr,"+%dz",pMem->u.nZero);
			zCsr += sqlStrlen30(zCsr);
		}
		*zCsr = '\0';
	} else if (pMem->type == MEM_TYPE_STR) {
		int j, k;
		zBuf[0] = ' ';
		if (f & MEM_Dyn) {
			zBuf[1] = 'z';
			assert((f & (MEM_Static|MEM_Ephem))==0);
		} else if (f & MEM_Static) {
			zBuf[1] = 't';
			assert((f & (MEM_Dyn|MEM_Ephem))==0);
		} else if (f & MEM_Ephem) {
			zBuf[1] = 'e';
			assert((f & (MEM_Static|MEM_Dyn))==0);
		} else {
			zBuf[1] = 's';
		}
		k = 2;
		sql_snprintf(100, &zBuf[k], "%d", pMem->n);
		k += sqlStrlen30(&zBuf[k]);
		zBuf[k++] = '[';
		for(j=0; j<15 && j<pMem->n; j++) {
			u8 c = pMem->z[j];
			if (c>=0x20 && c<0x7f) {
				zBuf[k++] = c;
			} else {
				zBuf[k++] = '.';
			}
		}
		zBuf[k++] = ']';
		sql_snprintf(100,&zBuf[k],"(8)");
		k += sqlStrlen30(&zBuf[k]);
		zBuf[k++] = 0;
	}
}

/*
 * Print the value of a register for tracing purposes:
 */
static void
memTracePrint(Mem *p)
{
	switch (p->type) {
	case MEM_TYPE_NULL:
		printf(" NULL");
		return;
	case MEM_TYPE_INT:
		printf(" i:%lld", p->u.i);
		return;
	case MEM_TYPE_UINT:
		printf(" u:%"PRIu64"", p->u.u);
		return;
	case MEM_TYPE_DOUBLE:
		printf(" r:%g", p->u.r);
		return;
	case MEM_TYPE_INVALID:
		printf(" undefined");
		return;
	case MEM_TYPE_BOOL:
		printf(" bool:%s", SQL_TOKEN_BOOLEAN(p->u.b));
		return;
	default: {
		char zBuf[200];
		sqlVdbeMemPrettyPrint(p, zBuf);
		printf(" %s", zBuf);
		if ((p->type & (MEM_TYPE_MAP | MEM_TYPE_ARRAY)) != 0)
			printf(" subtype=0x%02x", SQL_SUBTYPE_MSGPACK);
		return;
	}
	}
}

void
registerTrace(int iReg, Mem *p) {
	printf("REG[%d] = ", iReg);
	memTracePrint(p);
	printf("\n");
}
#endif

/*
 * If the given Mem* has a zero-filled tail, turn it into an ordinary
 * blob stored in dynamically allocated space.
 */
int
sqlVdbeMemExpandBlob(Mem * pMem)
{
	int nByte;
	assert(pMem->flags & MEM_Zero);
	assert(pMem->type == MEM_TYPE_BIN);

	/* Set nByte to the number of bytes required to store the expanded blob. */
	nByte = pMem->n + pMem->u.nZero;
	if (nByte <= 0) {
		nByte = 1;
	}
	if (sqlVdbeMemGrow(pMem, nByte, 1)) {
		return -1;
	}

	memset(&pMem->z[pMem->n], 0, pMem->u.nZero);
	pMem->n += pMem->u.nZero;
	pMem->flags &= ~(MEM_Zero | MEM_Term);
	return 0;
}

static int
sqlVdbeMemGrow(struct Mem *pMem, int n, int bPreserve)
{
	assert(sqlVdbeCheckMemInvariants(pMem));
	testcase(pMem->db == 0);

	/* If the bPreserve flag is set to true, then the memory cell must already
	 * contain a valid string or blob value.
	 */
	assert(bPreserve == 0 || mem_is_bytes(pMem));
	testcase(bPreserve && pMem->z == 0);

	assert(pMem->szMalloc == 0 ||
	       pMem->szMalloc == sqlDbMallocSize(pMem->db, pMem->zMalloc));
	if (pMem->szMalloc < n) {
		if (n < 32)
			n = 32;
		if (bPreserve && pMem->szMalloc > 0 && pMem->z == pMem->zMalloc) {
			pMem->z = pMem->zMalloc =
			    sqlDbReallocOrFree(pMem->db, pMem->z, n);
			bPreserve = 0;
		} else {
			if (pMem->szMalloc > 0)
				sqlDbFree(pMem->db, pMem->zMalloc);
			pMem->zMalloc = sqlDbMallocRaw(pMem->db, n);
		}
		if (pMem->zMalloc == 0) {
			mem_clear(pMem);
			pMem->z = 0;
			pMem->szMalloc = 0;
			return -1;
		} else {
			pMem->szMalloc = sqlDbMallocSize(pMem->db,
							 pMem->zMalloc);
		}
	}

	if (bPreserve && pMem->z && pMem->z != pMem->zMalloc) {
		memcpy(pMem->zMalloc, pMem->z, pMem->n);
	}
	if ((pMem->flags & MEM_Dyn) != 0) {
		assert(pMem->xDel != 0 && pMem->xDel != SQL_DYNAMIC);
		pMem->xDel((void *)(pMem->z));
	}

	pMem->z = pMem->zMalloc;
	pMem->flags &= ~(MEM_Dyn | MEM_Ephem | MEM_Static);
	return 0;
}

/*
 * Change the pMem->zMalloc allocation to be at least szNew bytes.
 * If pMem->zMalloc already meets or exceeds the requested size, this
 * routine is a no-op.
 *
 * Any prior string or blob content in the pMem object may be discarded.
 * The pMem->xDel destructor is called, if it exists. Though STRING, VARBINARY,
 * MAP and ARRAY values may be discarded, all other values are preserved.
 *
 * Return 0 on success or -1 if unable to complete the resizing.
 */
int
sqlVdbeMemClearAndResize(Mem * pMem, int szNew)
{
	assert(szNew > 0);
	assert((pMem->flags & MEM_Dyn) == 0 || pMem->szMalloc == 0);
	if (pMem->szMalloc < szNew) {
		return sqlVdbeMemGrow(pMem, szNew, 0);
	}
	assert((pMem->flags & MEM_Dyn) == 0);
	pMem->z = pMem->zMalloc;
	return 0;
}

/*
 * Free an sql_value object
 */
void
sqlValueFree(sql_value * v)
{
	if (!v)
		return;
	mem_destroy(v);
	sqlDbFree(((Mem *) v)->db, v);
}

/*
 * Create a new sql_value object.
 */
sql_value *
sqlValueNew(sql * db)
{
	Mem *p = sqlDbMallocZero(db, sizeof(*p));
	if (p) {
		p->type = MEM_TYPE_NULL;
		assert(p->flags == 0);
		p->db = db;
	}
	return p;
}

void
releaseMemArray(Mem * p, int N)
{
	if (p && N) {
		Mem *pEnd = &p[N];
		do {
			assert((&p[1]) == pEnd || p[0].db == p[1].db);
			assert(sqlVdbeCheckMemInvariants(p));
			mem_destroy(p);
			p->type = MEM_TYPE_INVALID;
			assert(p->flags == 0);
		} while ((++p) < pEnd);
	}
}

/*
 * Return true if the Mem object contains a TEXT or BLOB that is
 * too large - whose size exceeds SQL_MAX_LENGTH.
 */
int
sqlVdbeMemTooBig(Mem * p)
{
	assert(p->db != 0);
	if (mem_is_bytes(p)) {
		int n = p->n;
		if (p->flags & MEM_Zero) {
			n += p->u.nZero;
		}
		return n > p->db->aLimit[SQL_LIMIT_LENGTH];
	}
	return 0;
}

/*
 * Compare the values contained by the two memory cells, returning
 * negative, zero or positive if pMem1 is less than, equal to, or greater
 * than pMem2. Sorting order is NULL's first, followed by numbers (integers
 * and reals) sorted numerically, followed by text ordered by the collating
 * sequence pColl and finally blob's ordered by memcmp().
 *
 * Two NULL values are considered equal by this function.
 */
int
sqlMemCompare(const Mem * pMem1, const Mem * pMem2, const struct coll * pColl)
{
	int res;

	enum mem_type type1 = pMem1->type;
	enum mem_type type2 = pMem2->type;

	/* If one value is NULL, it is less than the other. If both values
	 * are NULL, return 0.
	 */
	if (((type1 | type2) & MEM_TYPE_NULL) != 0)
		return (int)(type2 == MEM_TYPE_NULL) -
		       (int)(type1 == MEM_TYPE_NULL);

	if (((type1 | type2) & MEM_TYPE_BOOL) != 0) {
		if ((type1 & type2 & MEM_TYPE_BOOL) != 0) {
			if (pMem1->u.b == pMem2->u.b)
				return 0;
			if (pMem1->u.b)
				return 1;
			return -1;
		}
		if (type2 == MEM_TYPE_BOOL)
			return +1;
		return -1;
	}

	/* At least one of the two values is a number
	 */
	if (((type1 | type2) &
	     (MEM_TYPE_INT | MEM_TYPE_UINT | MEM_TYPE_DOUBLE)) != 0) {
		if (!mem_is_num(pMem1))
			return +1;
		if (!mem_is_num(pMem2))
			return -1;
		mem_cmp_num(pMem1, pMem2, &res);
		return res;
	}

	/* If one value is a string and the other is a blob, the string is less.
	 * If both are strings, compare using the collating functions.
	 */
	if (((type1 | type2) & MEM_TYPE_STR) != 0) {
		if (type1 != MEM_TYPE_STR) {
			return 1;
		}
		if (type2 != MEM_TYPE_STR) {
			return -1;
		}
		mem_cmp_str(pMem1, pMem2, &res, pColl);
		return res;
	}

	/* Both values must be blobs.  Compare using memcmp().  */
	mem_cmp_bin(pMem1, pMem2, &res);
	return res;
}

int
sql_vdbemem_finalize(struct Mem *mem, struct func *func)
{
	assert(func != NULL);
	assert(func->def->language == FUNC_LANGUAGE_SQL_BUILTIN);
	assert(func->def->aggregate == FUNC_AGGREGATE_GROUP);
	assert(mem->type == MEM_TYPE_NULL || func == mem->u.func);
	sql_context ctx;
	memset(&ctx, 0, sizeof(ctx));
	Mem t;
	memset(&t, 0, sizeof(t));
	t.type = MEM_TYPE_NULL;
	assert(t.flags == 0);
	t.db = mem->db;
	t.field_type = field_type_MAX;
	ctx.pOut = &t;
	ctx.pMem = mem;
	ctx.func = func;
	((struct func_sql_builtin *)func)->finalize(&ctx);
	assert((mem->flags & MEM_Dyn) == 0);
	if (mem->szMalloc > 0)
		sqlDbFree(mem->db, mem->zMalloc);
	memcpy(mem, &t, sizeof(t));
	return ctx.is_aborted ? -1 : 0;
}

int
sqlVdbeCompareMsgpack(const char **key1,
			  struct UnpackedRecord *unpacked, int key2_idx)
{
	const char *aKey1 = *key1;
	Mem *pKey2 = unpacked->aMem + key2_idx;
	Mem mem1;
	int rc = 0;
	switch (mp_typeof(*aKey1)) {
	default:{
			/* FIXME */
			rc = -1;
			break;
		}
	case MP_NIL:{
			rc = -(pKey2->type != MEM_TYPE_NULL);
			mp_decode_nil(&aKey1);
			break;
		}
	case MP_BOOL:{
			mem1.u.b = mp_decode_bool(&aKey1);
			if (pKey2->type == MEM_TYPE_BOOL) {
				if (mem1.u.b != pKey2->u.b)
					rc = mem1.u.b ? 1 : -1;
			} else {
				rc = pKey2->type == MEM_TYPE_NULL ? 1 : -1;
			}
			break;
		}
	case MP_UINT:{
			mem1.u.u = mp_decode_uint(&aKey1);
			if (pKey2->type == MEM_TYPE_INT) {
				rc = +1;
			} else if (pKey2->type == MEM_TYPE_UINT) {
				if (mem1.u.u < pKey2->u.u)
					rc = -1;
				else if (mem1.u.u > pKey2->u.u)
					rc = +1;
			} else if (pKey2->type == MEM_TYPE_DOUBLE) {
				rc = double_compare_uint64(pKey2->u.r,
							   mem1.u.u, -1);
			} else if (pKey2->type == MEM_TYPE_NULL) {
				rc = 1;
			} else if (pKey2->type == MEM_TYPE_BOOL) {
				rc = 1;
			} else {
				rc = -1;
			}
			break;
		}
	case MP_INT:{
			mem1.u.i = mp_decode_int(&aKey1);
			if (pKey2->type == MEM_TYPE_UINT) {
				rc = -1;
			} else if (pKey2->type == MEM_TYPE_INT) {
				if (mem1.u.i < pKey2->u.i) {
					rc = -1;
				} else if (mem1.u.i > pKey2->u.i) {
					rc = +1;
				}
			} else if (pKey2->type == MEM_TYPE_DOUBLE) {
				rc = double_compare_nint64(pKey2->u.r, mem1.u.i,
							   -1);
			} else if (pKey2->type == MEM_TYPE_NULL) {
				rc = 1;
			} else if (pKey2->type == MEM_TYPE_BOOL) {
				rc = 1;
			} else {
				rc = -1;
			}
			break;
		}
	case MP_FLOAT:{
			mem1.u.r = mp_decode_float(&aKey1);
			goto do_float;
		}
	case MP_DOUBLE:{
			mem1.u.r = mp_decode_double(&aKey1);
 do_float:
			if (pKey2->type == MEM_TYPE_INT) {
				rc = double_compare_nint64(mem1.u.r, pKey2->u.i,
							   1);
			} else if (pKey2->type == MEM_TYPE_UINT) {
				rc = double_compare_uint64(mem1.u.r,
							   pKey2->u.u, 1);
			} else if (pKey2->type == MEM_TYPE_DOUBLE) {
				if (mem1.u.r < pKey2->u.r) {
					rc = -1;
				} else if (mem1.u.r > pKey2->u.r) {
					rc = +1;
				}
			} else if (pKey2->type == MEM_TYPE_NULL) {
				rc = 1;
			} else if (pKey2->type == MEM_TYPE_BOOL) {
				rc = 1;
			} else {
				rc = -1;
			}
			break;
		}
	case MP_STR:{
			if (pKey2->type == MEM_TYPE_STR) {
				struct key_def *key_def = unpacked->key_def;
				mem1.n = mp_decode_strl(&aKey1);
				mem1.z = (char *)aKey1;
				aKey1 += mem1.n;
				struct coll *coll =
					key_def->parts[key2_idx].coll;
				if (coll != NULL) {
					mem1.type = MEM_TYPE_STR;
					mem1.flags = 0;
					rc = vdbeCompareMemString(&mem1, pKey2,
								  coll);
				} else {
					goto do_bin_cmp;
				}
			} else {
				rc = pKey2->type == MEM_TYPE_BIN ? -1 : +1;
			}
			break;
		}
	case MP_BIN:{
			mem1.n = mp_decode_binl(&aKey1);
			mem1.z = (char *)aKey1;
			aKey1 += mem1.n;
 do_blob:
			if (pKey2->type == MEM_TYPE_BIN) {
				if (pKey2->flags & MEM_Zero) {
					if (!isAllZero
					    ((const char *)mem1.z, mem1.n)) {
						rc = 1;
					} else {
						rc = mem1.n - pKey2->u.nZero;
					}
				} else {
					int nCmp;
 do_bin_cmp:
					nCmp = MIN(mem1.n, pKey2->n);
					rc = memcmp(mem1.z, pKey2->z, nCmp);
					if (rc == 0)
						rc = mem1.n - pKey2->n;
				}
			} else {
				rc = 1;
			}
			break;
		}
	case MP_ARRAY:
	case MP_MAP:
	case MP_EXT:{
			mem1.z = (char *)aKey1;
			mp_next(&aKey1);
			mem1.n = aKey1 - (char *)mem1.z;
			goto do_blob;
		}
	}
	*key1 = aKey1;
	return rc;
}

int
sqlVdbeRecordCompareMsgpack(const void *key1,
				struct UnpackedRecord *key2)
{
	int rc = 0;
	u32 i, n = mp_decode_array((const char**)&key1);

	n = MIN(n, key2->nField);

	for (i = 0; i != n; i++) {
		rc = sqlVdbeCompareMsgpack((const char**)&key1, key2, i);
		if (rc != 0) {
			if (key2->key_def->parts[i].sort_order !=
			    SORT_ORDER_ASC) {
				rc = -rc;
			}
			return rc;
		}
	}

	key2->eqSeen = 1;
	return key2->default_rc;
}

int
mem_from_mp_ephemeral(struct Mem *mem, const char *buf, uint32_t *len)
{
	const char *start_buf = buf;
	switch (mp_typeof(*buf)) {
	case MP_ARRAY: {
		mem->z = (char *)buf;
		mp_next(&buf);
		mem->n = buf - mem->z;
		mem->type = MEM_TYPE_ARRAY;
		mem->flags = MEM_Ephem;
		mem->field_type = FIELD_TYPE_ARRAY;
		break;
	}
	case MP_MAP: {
		mem->z = (char *)buf;
		mp_next(&buf);
		mem->n = buf - mem->z;
		mem->type = MEM_TYPE_MAP;
		mem->flags = MEM_Ephem;
		mem->field_type = FIELD_TYPE_MAP;
		break;
	}
	case MP_EXT: {
		mem->z = (char *)buf;
		mp_next(&buf);
		mem->n = buf - mem->z;
		mem->type = MEM_TYPE_BIN;
		mem->flags = MEM_Ephem;
		mem->field_type = FIELD_TYPE_VARBINARY;
		break;
	}
	case MP_NIL: {
		mp_decode_nil(&buf);
		mem->type = MEM_TYPE_NULL;
		mem->flags = 0;
		mem->field_type = field_type_MAX;
		break;
	}
	case MP_BOOL: {
		mem->u.b = mp_decode_bool(&buf);
		mem->type = MEM_TYPE_BOOL;
		mem->flags = 0;
		mem->field_type = FIELD_TYPE_BOOLEAN;
		break;
	}
	case MP_UINT: {
		uint64_t v = mp_decode_uint(&buf);
		mem->u.u = v;
		mem->type = MEM_TYPE_UINT;
		mem->flags = 0;
		mem->field_type = FIELD_TYPE_INTEGER;
		break;
	}
	case MP_INT: {
		mem->u.i = mp_decode_int(&buf);
		mem->type = MEM_TYPE_INT;
		mem->flags = 0;
		mem->field_type = FIELD_TYPE_INTEGER;
		break;
	}
	case MP_STR: {
		/* XXX u32->int */
		mem->n = (int) mp_decode_strl(&buf);
		mem->type = MEM_TYPE_STR;
		mem->flags = MEM_Ephem;
		mem->field_type = FIELD_TYPE_STRING;
install_blob:
		mem->z = (char *)buf;
		buf += mem->n;
		break;
	}
	case MP_BIN: {
		/* XXX u32->int */
		mem->n = (int) mp_decode_binl(&buf);
		mem->type = MEM_TYPE_BIN;
		mem->flags = MEM_Ephem;
		mem->field_type = FIELD_TYPE_VARBINARY;
		goto install_blob;
	}
	case MP_FLOAT: {
		mem->u.r = mp_decode_float(&buf);
		if (sqlIsNaN(mem->u.r)) {
			mem->type = MEM_TYPE_NULL;
			mem->flags = 0;
			mem->field_type = FIELD_TYPE_DOUBLE;
		} else {
			mem->type = MEM_TYPE_DOUBLE;
			mem->flags = 0;
			mem->field_type = FIELD_TYPE_DOUBLE;
		}
		break;
	}
	case MP_DOUBLE: {
		mem->u.r = mp_decode_double(&buf);
		if (sqlIsNaN(mem->u.r)) {
			mem->type = MEM_TYPE_NULL;
			mem->flags = 0;
			mem->field_type = FIELD_TYPE_DOUBLE;
		} else {
			mem->type = MEM_TYPE_DOUBLE;
			mem->flags = 0;
			mem->field_type = FIELD_TYPE_DOUBLE;
		}
		break;
	}
	default:
		unreachable();
	}
	*len = (uint32_t)(buf - start_buf);
	return 0;
}

int
mem_from_mp(struct Mem *mem, const char *buf, uint32_t *len)
{
	if (mem_from_mp_ephemeral(mem, buf, len) != 0)
		return -1;
	if (mem_is_bytes(mem)) {
		assert((mem->flags & MEM_Ephem) != 0);
		if (sqlVdbeMemGrow(mem, mem->n, 1) != 0)
			return -1;
	}
	return 0;
}

void
mpstream_encode_vdbe_mem(struct mpstream *stream, struct Mem *var)
{
	assert(memIsValid(var));
	switch (var->type) {
	case MEM_TYPE_NULL:
		mpstream_encode_nil(stream);
		return;
	case MEM_TYPE_STR:
		mpstream_encode_strn(stream, var->z, var->n);
		return;
	case MEM_TYPE_INT:
		mpstream_encode_int(stream, var->u.i);
		return;
	case MEM_TYPE_UINT:
		mpstream_encode_uint(stream, var->u.u);
		return;
	case MEM_TYPE_DOUBLE:
		mpstream_encode_double(stream, var->u.r);
		return;
	case MEM_TYPE_BIN:
		if ((var->flags & MEM_Zero) != 0) {
			mpstream_encode_binl(stream, var->n + var->u.nZero);
			mpstream_memcpy(stream, var->z, var->n);
			mpstream_memset(stream, 0, var->u.nZero);
		} else {
			mpstream_encode_binl(stream, var->n);
			mpstream_memcpy(stream, var->z, var->n);
		}
		return;
	case MEM_TYPE_ARRAY:
	case MEM_TYPE_MAP:
		mpstream_memcpy(stream, var->z, var->n);
		return;
	case MEM_TYPE_BOOL:
		mpstream_encode_bool(stream, var->u.b);
		return;
	default:
		unreachable();
	}
}

char *
sql_vdbe_mem_encode_tuple(struct Mem *fields, uint32_t field_count,
			  uint32_t *tuple_size, struct region *region)
{
	size_t used = region_used(region);
	bool is_error = false;
	struct mpstream stream;
	mpstream_init(&stream, region, region_reserve_cb, region_alloc_cb,
		      set_encode_error, &is_error);
	mpstream_encode_array(&stream, field_count);
	for (struct Mem *field = fields; field < fields + field_count; field++)
		mpstream_encode_vdbe_mem(&stream, field);
	mpstream_flush(&stream);
	if (is_error) {
		diag_set(OutOfMemory, stream.pos - stream.buf,
			 "mpstream_flush", "stream");
		return NULL;
	}
	*tuple_size = region_used(region) - used;
	char *tuple = region_join(region, *tuple_size);
	if (tuple == NULL) {
		diag_set(OutOfMemory, *tuple_size, "region_join", "tuple");
		return NULL;
	}
	mp_tuple_assert(tuple, tuple + *tuple_size);
	return tuple;
}

/**
 * Allocate a sequence of initialized vdbe memory registers
 * on region.
 */
static struct Mem *
vdbemem_alloc_on_region(uint32_t count)
{
	struct region *region = &fiber()->gc;
	size_t size;
	struct Mem *ret = region_alloc_array(region, typeof(*ret), count,
					     &size);
	if (ret == NULL) {
		diag_set(OutOfMemory, size, "region_alloc_array", "ret");
		return NULL;
	}
	memset(ret, 0, count * sizeof(*ret));
	for (uint32_t i = 0; i < count; i++) {
		mem_create(&ret[i]);
		assert(memIsValid(&ret[i]));
	}
	return ret;
}

static void
port_vdbemem_dump_lua(struct port *base, struct lua_State *L, bool is_flat)
{
	(void) is_flat;
	struct port_vdbemem *port = (struct port_vdbemem *) base;
	assert(is_flat == true);
	for (uint32_t i = 0; i < port->mem_count; i++) {
		struct Mem *mem = (struct Mem *)port->mem + i;
		switch (mem->type) {
		case MEM_TYPE_INT:
			luaL_pushint64(L, mem->u.i);
			break;
		case MEM_TYPE_UINT:
			luaL_pushuint64(L, mem->u.u);
			break;
		case MEM_TYPE_DOUBLE:
			lua_pushnumber(L, mem->u.r);
			break;
		case MEM_TYPE_STR:
		case MEM_TYPE_BIN:
		case MEM_TYPE_MAP:
		case MEM_TYPE_ARRAY:
			lua_pushlstring(L, mem->z, mem->n);
			break;
		case MEM_TYPE_NULL:
			lua_pushnil(L);
			break;
		case MEM_TYPE_BOOL:
			lua_pushboolean(L, mem->u.b);
			break;
		default:
			unreachable();
		}
	}
}

static const char *
port_vdbemem_get_msgpack(struct port *base, uint32_t *size)
{
	struct port_vdbemem *port = (struct port_vdbemem *) base;
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	bool is_error = false;
	struct mpstream stream;
	mpstream_init(&stream, region, region_reserve_cb, region_alloc_cb,
		      set_encode_error, &is_error);
	mpstream_encode_array(&stream, port->mem_count);
	for (uint32_t i = 0; i < port->mem_count && !is_error; i++)
		mpstream_encode_vdbe_mem(&stream, (struct Mem *)port->mem + i);
	mpstream_flush(&stream);
	*size = region_used(region) - region_svp;
	if (is_error)
		goto error;
	const char *ret = (char *)region_join(region, *size);
	if (ret == NULL)
		goto error;
	return ret;
error:
	diag_set(OutOfMemory, *size, "region", "ret");
	return NULL;
}

static const struct port_vtab port_vdbemem_vtab;

void
port_vdbemem_create(struct port *base, struct sql_value *mem,
		    uint32_t mem_count)
{
	struct port_vdbemem *port = (struct port_vdbemem *) base;
	port->vtab = &port_vdbemem_vtab;
	port->mem = mem;
	port->mem_count = mem_count;
}

static struct sql_value *
port_vdbemem_get_vdbemem(struct port *base, uint32_t *mem_count)
{
	struct port_vdbemem *port = (struct port_vdbemem *) base;
	assert(port->vtab == &port_vdbemem_vtab);
	*mem_count = port->mem_count;
	return port->mem;
}

static const struct port_vtab port_vdbemem_vtab = {
	.dump_msgpack = NULL,
	.dump_msgpack_16 = NULL,
	.dump_lua = port_vdbemem_dump_lua,
	.dump_plain = NULL,
	.get_msgpack = port_vdbemem_get_msgpack,
	.get_vdbemem = port_vdbemem_get_vdbemem,
	.destroy = NULL,
};

struct sql_value *
port_lua_get_vdbemem(struct port *base, uint32_t *size)
{
	struct port_lua *port = (struct port_lua *) base;
	struct lua_State *L = port->L;
	int argc = lua_gettop(L);
	if (argc == 0 || argc > 1) {
		diag_set(ClientError, ER_SQL_FUNC_WRONG_RET_COUNT, "Lua", argc);
		return NULL;
	}
	*size = argc;
	/** FIXME: Implement an ability to return a vector. */
	assert(*size == 1);
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	struct Mem *val = vdbemem_alloc_on_region(argc);
	if (val == NULL)
		return NULL;
	for (int i = 0; i < argc; i++) {
		struct luaL_field field;
		if (luaL_tofield(L, luaL_msgpack_default,
				 NULL, -1 - i, &field) < 0) {
			goto error;
		}
		mem_clear(&val[i]);
		switch (field.type) {
		case MP_BOOL:
			val[i].type = MEM_TYPE_BOOL;
			assert(val[i].flags == 0);
			val[i].u.b = field.bval;
			break;
		case MP_FLOAT:
			val[i].type = MEM_TYPE_DOUBLE;
			assert(val[i].flags == 0);
			val[i].u.r = field.fval;
			break;
		case MP_DOUBLE:
			val[i].type = MEM_TYPE_DOUBLE;
			assert(val[i].flags == 0);
			val[i].u.r = field.dval;
			break;
		case MP_INT:
			val[i].type = MEM_TYPE_INT;
			assert(val[i].flags == 0);
			val[i].u.i = field.ival;
			break;
		case MP_UINT:
			val[i].type = MEM_TYPE_UINT;
			assert(val[i].flags == 0);
			val[i].u.i = field.ival;
			break;
		case MP_STR:
			if (mem_copy_str(&val[i], field.sval.data,
					 field.sval.len) != 0)
				goto error;
			break;
		case MP_EXT: {
			assert(field.ext_type == MP_UUID ||
			       field.ext_type == MP_DECIMAL);
			char *buf;
			uint32_t size;
			uint32_t svp = region_used(&fiber()->gc);
			if (field.ext_type == MP_UUID) {
				size = mp_sizeof_uuid();
				buf = region_alloc(&fiber()->gc, size);
				if (buf == NULL) {
					diag_set(OutOfMemory, size,
						 "region_alloc", "buf");
					goto error;
				}
				mp_encode_uuid(buf, field.uuidval);
			} else {
				size = mp_sizeof_decimal(field.decval);
				buf = region_alloc(&fiber()->gc, size);
				if (buf == NULL) {
					diag_set(OutOfMemory, size,
						 "region_alloc", "buf");
					goto error;
				}
				mp_encode_decimal(buf, field.decval);
			}
			int rc = mem_copy_bin(&val[i], buf, size);
			region_truncate(&fiber()->gc, svp);
			if (rc != 0)
				goto error;
			break;
		}
		case MP_NIL:
			break;
		default:
			diag_set(ClientError, ER_SQL_EXECUTE,
				 "Unsupported type passed from Lua");
			goto error;
		}
	}
	return (struct sql_value *)val;
error:
	for (int i = 0; i < argc; i++)
		mem_destroy(&val[i]);
	region_truncate(region, region_svp);
	return NULL;
}

struct sql_value *
port_c_get_vdbemem(struct port *base, uint32_t *size)
{
	struct port_c *port = (struct port_c *)base;
	*size = port->size;
	if (*size == 0 || *size > 1) {
		diag_set(ClientError, ER_SQL_FUNC_WRONG_RET_COUNT, "C", *size);
		return NULL;
	}
	/** FIXME: Implement an ability to return a vector. */
	assert(*size == 1);
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	struct Mem *val = vdbemem_alloc_on_region(port->size);
	if (val == NULL)
		return NULL;
	int i = 0;
	const char *data;
	struct port_c_entry *pe;
	for (pe = port->first; pe != NULL; pe = pe->next) {
		if (pe->mp_size == 0) {
			data = tuple_data(pe->tuple);
			if (mp_decode_array(&data) != 1) {
				diag_set(ClientError, ER_SQL_EXECUTE,
					 "Unsupported type passed from C");
				goto error;
			}
		} else {
			data = pe->mp;
		}
		uint32_t len;
		mem_clear(&val[i]);
		const char *str;
		switch (mp_typeof(*data)) {
		case MP_BOOL:
			val[i].type = MEM_TYPE_BOOL;
			assert(val[i].flags == 0);
			val[i].u.b = mp_decode_bool(&data);
			break;
		case MP_FLOAT:
			val[i].type = MEM_TYPE_DOUBLE;
			assert(val[i].flags == 0);
			val[i].u.r = mp_decode_float(&data);
			break;
		case MP_DOUBLE:
			val[i].type = MEM_TYPE_DOUBLE;
			assert(val[i].flags == 0);
			val[i].u.r = mp_decode_double(&data);
			break;
		case MP_INT:
			val[i].type = MEM_TYPE_INT;
			assert(val[i].flags == 0);
			val[i].u.i = mp_decode_int(&data);
			break;
		case MP_UINT:
			val[i].type = MEM_TYPE_UINT;
			assert(val[i].flags == 0);
			val[i].u.u = mp_decode_uint(&data);
			break;
		case MP_STR:
			str = mp_decode_str(&data, &len);
			if (mem_copy_str(&val[i], str, len) != 0)
				goto error;
			break;
		case MP_BIN:
			str = mp_decode_bin(&data, &len);
			if (mem_copy_bin(&val[i], str, len) != 0)
				goto error;
			break;
		case MP_EXT:
			str = data;
			mp_next(&data);
			if (mem_copy_bin(&val[i], str, data - str) != 0)
				goto error;
			break;
		case MP_NIL:
			break;
		default:
			diag_set(ClientError, ER_SQL_EXECUTE,
				 "Unsupported type passed from C");
			goto error;
		}
		i++;
	}
	return (struct sql_value *) val;
error:
	for (int i = 0; i < port->size; i++)
		mem_destroy(&val[i]);
	region_truncate(region, region_svp);
	return NULL;
}
