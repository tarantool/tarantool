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
#include "lua/msgpack.h"

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
};

const char *
mem_str(const struct Mem *mem)
{
	char buf[BUF_SIZE];
	switch (mem->flags & MEM_PURE_TYPE_MASK) {
	case MEM_Null:
		return "NULL";
	case MEM_Str:
		if ((mem->flags & MEM_Term) != 0)
			return mem->z;
		return tt_cstr(mem->z, mem->n);
	case MEM_Int:
		return tt_sprintf("%lld", mem->u.i);
	case MEM_UInt:
		return tt_sprintf("%llu", mem->u.u);
	case MEM_Real:
		sql_snprintf(BUF_SIZE, &buf[0], "%!.15g", mem->u.r);
		return tt_sprintf("%s", buf);
	case MEM_Blob:
		if ((mem->flags & MEM_Subtype) == 0)
			return "varbinary";
		assert(mem->subtype == SQL_SUBTYPE_MSGPACK);
		return mp_str(mem->z);
	case MEM_Bool:
		return mem->u.b ? "TRUE" : "FALSE";
	default:
		return "unknown";
	}
}

void
mem_create(struct Mem *mem)
{
	mem->flags = MEM_Null;
	mem->subtype = SQL_SUBTYPE_NO;
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
	if ((mem->flags & (MEM_Agg | MEM_Dyn | MEM_Frame)) != 0) {
		if ((mem->flags & MEM_Agg) != 0)
			sql_vdbemem_finalize(mem, mem->u.func);
		assert((mem->flags & MEM_Agg) == 0);
		if ((mem->flags & MEM_Dyn) != 0) {
			assert(mem->xDel != SQL_DYNAMIC && mem->xDel != NULL);
			mem->xDel((void *)mem->z);
		} else if ((mem->flags & MEM_Frame) != 0) {
			struct VdbeFrame *frame = mem->u.pFrame;
			frame->pParent = frame->v->pDelFrame;
			frame->v->pDelFrame = frame;
		}
	}
	mem->flags = MEM_Null;
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
	mem->flags = is_neg ? MEM_Int : MEM_UInt;
	mem->field_type = FIELD_TYPE_INTEGER;
}

void
mem_set_uint(struct Mem *mem, uint64_t value)
{
	mem_clear(mem);
	mem->u.u = value;
	mem->flags = MEM_UInt;
	mem->field_type = FIELD_TYPE_UNSIGNED;
}

void
mem_set_bool(struct Mem *mem, bool value)
{
	mem_clear(mem);
	mem->u.b = value;
	mem->flags = MEM_Bool;
	mem->field_type = FIELD_TYPE_BOOLEAN;
}

void
mem_set_double(struct Mem *mem, double value)
{
	mem_clear(mem);
	mem->field_type = FIELD_TYPE_DOUBLE;
	if (sqlIsNaN(value))
		return;
	mem->u.r = value;
	mem->flags = MEM_Real;
}

static inline void
set_str_const(struct Mem *mem, char *value, uint32_t len, int alloc_type)
{
	assert((alloc_type & (MEM_Static | MEM_Ephem)) != 0);
	mem_clear(mem);
	mem->z = value;
	mem->n = len;
	mem->flags = MEM_Str | alloc_type;
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
	mem->flags = MEM_Str | alloc_type;
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
	if ((mem->flags & (MEM_Str | MEM_Blob)) != 0 && mem->z == value) {
		/* Own value, but might be ephemeral. Make it own if so. */
		if (sqlVdbeMemGrow(mem, len, 1) != 0)
			return -1;
		mem->flags = MEM_Str;
		mem->field_type = FIELD_TYPE_STRING;
		return 0;
	}
	mem_clear(mem);
	if (sqlVdbeMemGrow(mem, len, 0) != 0)
		return -1;
	memcpy(mem->z, value, len);
	mem->n = len;
	mem->flags = MEM_Str;
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
	mem->flags = MEM_Blob | alloc_type;
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
	mem->flags = MEM_Blob | alloc_type;
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
	if ((mem->flags & (MEM_Str | MEM_Blob)) != 0 && mem->z == value) {
		/* Own value, but might be ephemeral. Make it own if so. */
		if (sqlVdbeMemGrow(mem, size, 1) != 0)
			return -1;
		mem->flags = MEM_Blob;
		mem->field_type = FIELD_TYPE_VARBINARY;
		return 0;
	}
	mem_clear(mem);
	if (sqlVdbeMemGrow(mem, size, 0) != 0)
		return -1;
	memcpy(mem->z, value, size);
	mem->n = size;
	mem->flags = MEM_Blob;
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
	mem->flags = MEM_Blob | MEM_Zero;
	mem->field_type = FIELD_TYPE_VARBINARY;
}

static inline void
set_msgpack_value(struct Mem *mem, char *value, uint32_t size, int alloc_type,
		  enum field_type type)
{
	if (alloc_type == MEM_Ephem || alloc_type == MEM_Static)
		set_bin_const(mem, value, size, alloc_type);
	else
		set_bin_dynamic(mem, value, size, alloc_type);
	mem->flags |= MEM_Subtype;
	mem->subtype = SQL_SUBTYPE_MSGPACK;
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
	mem->flags = MEM_Undefined;
}

void
mem_set_ptr(struct Mem *mem, void *ptr)
{
	mem_clear(mem);
	mem->flags = MEM_Ptr;
	mem->u.p = ptr;
}

void
mem_set_frame(struct Mem *mem, struct VdbeFrame *frame)
{
	mem_clear(mem);
	mem->flags = MEM_Frame;
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
	mem->flags = MEM_Agg;
	mem->u.func = func;
	mem->field_type = field_type_MAX;
	return 0;
}

void
mem_set_null_clear(struct Mem *mem)
{
	mem_clear(mem);
	mem->flags = MEM_Null | MEM_Cleared;
}

static inline int
int_to_double(struct Mem *mem)
{
	double d;
	if ((mem->flags & MEM_UInt) != 0)
		d = (double)mem->u.u;
	else
		d = (double)mem->u.i;
	mem->u.r = d;
	mem->flags = MEM_Real;
	mem->field_type = FIELD_TYPE_DOUBLE;
	return 0;
}

static inline int
int_to_str0(struct Mem *mem)
{
	const char *str;
	if ((mem->flags & MEM_UInt) != 0)
		str = tt_sprintf("%llu", mem->u.u);
	else
		str = tt_sprintf("%lld", mem->u.i);
	return mem_copy_str0(mem, str);
}

static inline int
int_to_bool(struct Mem *mem)
{
	mem->u.b = mem->u.i != 0;
	mem->flags = MEM_Bool;
	mem->field_type = FIELD_TYPE_BOOLEAN;
	return 0;
}

static inline int
str_to_str0(struct Mem *mem)
{
	assert((mem->flags | MEM_Str) != 0);
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
	mem->flags = (mem->flags & (MEM_Dyn | MEM_Static | MEM_Ephem)) |
		     MEM_Blob;
	mem->field_type = FIELD_TYPE_VARBINARY;
	return 0;
}

static inline int
str_to_bool(struct Mem *mem)
{
	char *str = mem->z;
	bool b;
	const char *str_true = "TRUE";
	const char *str_false = "FALSE";
	uint32_t len_true = strlen(str_true);
	uint32_t len_false = strlen(str_false);

	for (; str[0] == ' '; str++);
	if (strncasecmp(str, str_true, len_true) == 0) {
		b = true;
		str += len_true;
	} else if (strncasecmp(str, str_false, len_false) == 0) {
		b = false;
		str += len_false;
	} else {
		return -1;
	}
	for (; str[0] == ' '; str++);
	if (str[0] != '\0')
		return -1;
	mem_set_bool(mem, b);
	return 0;
}

static inline int
bin_to_str(struct Mem *mem)
{
	if (ExpandBlob(mem) != 0)
		return -1;
	mem->flags = (mem->flags & (MEM_Dyn | MEM_Static | MEM_Ephem)) |
		      MEM_Str;
	mem->field_type = FIELD_TYPE_STRING;
	return 0;
}

static inline int
bin_to_str0(struct Mem *mem)
{
	if (ExpandBlob(mem) != 0)
		return -1;
	if (sqlVdbeMemGrow(mem, mem->n + 1, 1) != 0)
		return -1;
	mem->z[mem->n] = '\0';
	mem->flags = MEM_Str | MEM_Term;
	mem->field_type = FIELD_TYPE_STRING;
	return 0;
}

static inline int
bytes_to_int(struct Mem *mem)
{
	bool is_neg;
	int64_t i;
	if (sql_atoi64(mem->z, &i, &is_neg, mem->n) != 0)
		return -1;
	mem_set_int(mem, i, is_neg);
	return 0;
}

static inline int
bytes_to_uint(struct Mem *mem)
{
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
bytes_to_double(struct Mem *mem)
{
	double d;
	if (sqlAtoF(mem->z, &d, mem->n) == 0)
		return -1;
	mem_set_double(mem, d);
	return 0;
}

static inline int
double_to_int(struct Mem *mem)
{
	double d = mem->u.r;
	if (d < 0 && d >= (double)INT64_MIN) {
		mem->u.i = (int64_t)d;
		mem->flags = MEM_Int;
		mem->field_type = FIELD_TYPE_INTEGER;
		return 0;
	}
	if (d >= 0 && d < (double)UINT64_MAX) {
		mem->u.u = (uint64_t)d;
		mem->flags = MEM_UInt;
		mem->field_type = FIELD_TYPE_UNSIGNED;
		return 0;
	}
	return -1;
}

static inline int
double_to_int_precise(struct Mem *mem)
{
	double d = mem->u.r;
	if (d < 0 && d >= (double)INT64_MIN && (double)(int64_t)d == d) {
		mem->u.i = (int64_t)d;
		mem->flags = MEM_Int;
		mem->field_type = FIELD_TYPE_INTEGER;
		return 0;
	}
	if (d >= 0 && d < (double)UINT64_MAX && (double)(uint64_t)d == d) {
		mem->u.u = (uint64_t)d;
		mem->flags = MEM_UInt;
		mem->field_type = FIELD_TYPE_UNSIGNED;
		return 0;
	}
	return -1;
}

static inline int
double_to_uint(struct Mem *mem)
{
	double d = mem->u.r;
	if (d >= 0 && d < (double)UINT64_MAX) {
		mem->u.u = (uint64_t)d;
		mem->flags = MEM_UInt;
		mem->field_type = FIELD_TYPE_UNSIGNED;
		return 0;
	}
	return -1;
}

static inline int
double_to_uint_precise(struct Mem *mem)
{
	double d = mem->u.r;
	if (d >= 0 && d < (double)UINT64_MAX && (double)(uint64_t)d == d) {
		mem->u.u = (uint64_t)d;
		mem->flags = MEM_UInt;
		mem->field_type = FIELD_TYPE_UNSIGNED;
		return 0;
	}
	return -1;
}

static inline int
double_to_str0(struct Mem *mem)
{
	if (sqlVdbeMemGrow(mem, BUF_SIZE, 0) != 0)
		return -1;
	sql_snprintf(BUF_SIZE, mem->z, "%!.15g", mem->u.r);
	mem->n = strlen(mem->z);
	mem->flags = MEM_Str | MEM_Term;
	mem->field_type = FIELD_TYPE_STRING;
	return 0;
}

static inline int
double_to_bool(struct Mem *mem)
{
	mem->u.b = mem->u.r != 0.;
	mem->flags = MEM_Bool;
	mem->field_type = FIELD_TYPE_BOOLEAN;
	return 0;
}

static inline int
bool_to_int(struct Mem *mem)
{
	mem->u.u = (uint64_t)mem->u.b;
	mem->flags = MEM_UInt;
	mem->field_type = FIELD_TYPE_UNSIGNED;
	return 0;
}

static inline int
bool_to_str0(struct Mem *mem)
{
	const char *str = mem->u.b ? "TRUE" : "FALSE";
	return mem_copy_str0(mem, str);
}

static inline int
array_to_str0(struct Mem *mem)
{
	const char *str = mp_str(mem->z);
	return mem_copy_str0(mem, str);
}

static inline int
map_to_str0(struct Mem *mem)
{
	const char *str = mp_str(mem->z);
	return mem_copy_str0(mem, str);
}

int
mem_to_int(struct Mem *mem)
{
	assert((mem->flags & MEM_PURE_TYPE_MASK) != 0);
	if ((mem->flags & (MEM_Int | MEM_UInt)) != 0)
		return 0;
	if ((mem->flags & (MEM_Str | MEM_Blob)) != 0)
		return bytes_to_int(mem);
	if ((mem->flags & MEM_Real) != 0)
		return double_to_int(mem);
	if ((mem->flags & MEM_Bool) != 0)
		return bool_to_int(mem);
	return -1;
}

int
mem_to_int_precise(struct Mem *mem)
{
	assert((mem->flags & MEM_PURE_TYPE_MASK) != 0);
	if ((mem->flags & (MEM_Int | MEM_UInt)) != 0)
		return 0;
	if ((mem->flags & MEM_Str) != 0)
		return bytes_to_int(mem);
	if ((mem->flags & MEM_Real) != 0)
		return double_to_int_precise(mem);
	return -1;
}

int
mem_to_double(struct Mem *mem)
{
	assert((mem->flags & MEM_PURE_TYPE_MASK) != 0);
	if ((mem->flags & MEM_Real) != 0)
		return 0;
	if ((mem->flags & (MEM_Int | MEM_UInt)) != 0)
		return int_to_double(mem);
	if ((mem->flags & MEM_Str) != 0)
		return bytes_to_double(mem);
	return -1;
}

int
mem_to_number(struct Mem *mem)
{
	assert((mem->flags & MEM_PURE_TYPE_MASK) != 0);
	if ((mem->flags & (MEM_Int | MEM_UInt | MEM_Real)) != 0)
		return 0;
	if ((mem->flags & MEM_Bool) != 0)
		return bool_to_int(mem);
	if ((mem->flags & (MEM_Str | MEM_Blob)) != 0) {
		if (bytes_to_int(mem) == 0)
			return 0;
		return bytes_to_double(mem);
	}
	return -1;
}

int
mem_to_str0(struct Mem *mem)
{
	assert((mem->flags & MEM_PURE_TYPE_MASK) != 0);
	if ((mem->flags & (MEM_Str | MEM_Term)) == (MEM_Str | MEM_Term))
		return 0;
	if ((mem->flags & MEM_Str) != 0)
		return str_to_str0(mem);
	if ((mem->flags & (MEM_Int | MEM_UInt)) != 0)
		return int_to_str0(mem);
	if ((mem->flags & MEM_Real) != 0)
		return double_to_str0(mem);
	if ((mem->flags & MEM_Bool) != 0)
		return bool_to_str0(mem);
	if ((mem->flags & MEM_Blob) != 0) {
		if ((mem->flags & MEM_Subtype) == 0)
			return bin_to_str0(mem);
		if (mp_typeof(*mem->z) == MP_MAP)
			return map_to_str0(mem);
		return array_to_str0(mem);
	}
	return -1;
}

int
mem_to_str(struct Mem *mem)
{
	assert((mem->flags & MEM_PURE_TYPE_MASK) != 0);
	if ((mem->flags & MEM_Str) != 0)
		return 0;
	if ((mem->flags & (MEM_Int | MEM_UInt)) != 0)
		return int_to_str0(mem);
	if ((mem->flags & MEM_Real) != 0)
		return double_to_str0(mem);
	if ((mem->flags & MEM_Bool) != 0)
		return bool_to_str0(mem);
	if ((mem->flags & MEM_Blob) != 0) {
		if ((mem->flags & MEM_Subtype) == 0)
			return bin_to_str(mem);
		if (mp_typeof(*mem->z) == MP_MAP)
			return map_to_str0(mem);
		return array_to_str0(mem);
	}
	return -1;
}

int
mem_cast_explicit(struct Mem *mem, enum field_type type)
{
	if ((mem->flags & MEM_Null) != 0) {
		mem->field_type = type;
		return 0;
	}
	switch (type) {
	case FIELD_TYPE_UNSIGNED:
		if ((mem->flags & MEM_UInt) != 0)
			return 0;
		if ((mem->flags & MEM_Int) != 0)
			return -1;
		if ((mem->flags & MEM_Blob) != 0 &&
		    (mem->flags & MEM_Subtype) != 0)
			return -1;
		if ((mem->flags & (MEM_Str | MEM_Blob)) != 0)
			return bytes_to_uint(mem);
		if ((mem->flags & MEM_Real) != 0)
			return double_to_int(mem);
		if ((mem->flags & MEM_Bool) != 0)
			return bool_to_int(mem);
		return -1;
	case FIELD_TYPE_STRING:
		return mem_to_str(mem);
	case FIELD_TYPE_DOUBLE:
		return mem_to_double(mem);
	case FIELD_TYPE_INTEGER:
		return mem_to_int(mem);
	case FIELD_TYPE_BOOLEAN:
		if ((mem->flags & MEM_Bool) != 0)
			return 0;
		if ((mem->flags & (MEM_UInt | MEM_Int)) != 0)
			return int_to_bool(mem);
		if ((mem->flags & MEM_Str) != 0)
			return str_to_bool(mem);
		if ((mem->flags & MEM_Real) != 0)
			return double_to_bool(mem);
		return -1;
	case FIELD_TYPE_VARBINARY:
		if ((mem->flags & MEM_Blob) != 0)
			return 0;
		if ((mem->flags & MEM_Str) != 0)
			return str_to_bin(mem);
		return -1;
	case FIELD_TYPE_NUMBER:
		return mem_to_number(mem);
	case FIELD_TYPE_SCALAR:
		if ((mem->flags & MEM_Blob) != 0 &&
		    (mem->flags & MEM_Subtype) != 0)
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
	if ((mem->flags & MEM_Null) != 0) {
		mem->field_type = type;
		return 0;
	}
	switch (type) {
	case FIELD_TYPE_UNSIGNED:
		if ((mem->flags & MEM_UInt) != 0)
			return 0;
		if ((mem->flags & MEM_Real) != 0)
			return double_to_uint(mem);
		return -1;
	case FIELD_TYPE_STRING:
		if ((mem->flags & MEM_Str) != 0)
			return 0;
		return -1;
	case FIELD_TYPE_DOUBLE:
		if ((mem->flags & MEM_Real) != 0)
			return 0;
		if ((mem->flags & (MEM_Int | MEM_UInt)) != 0)
			return int_to_double(mem);
		return -1;
	case FIELD_TYPE_INTEGER:
		if ((mem->flags & (MEM_Int | MEM_UInt)) != 0)
			return 0;
		if ((mem->flags & MEM_Real) != 0)
			return double_to_int(mem);
		return -1;
	case FIELD_TYPE_BOOLEAN:
		if ((mem->flags & MEM_Bool) != 0)
			return 0;
		return -1;
	case FIELD_TYPE_VARBINARY:
		if ((mem->flags & MEM_Blob) != 0)
			return 0;
		return -1;
	case FIELD_TYPE_NUMBER:
		if ((mem->flags & (MEM_Int | MEM_UInt | MEM_Real)) != 0)
			return 0;
		return -1;
	case FIELD_TYPE_MAP:
		if (mem_is_map(mem))
			return 0;
		return -1;
	case FIELD_TYPE_ARRAY:
		if (mem_is_array(mem))
			return 0;
		return -1;
	case FIELD_TYPE_SCALAR:
		if ((mem->flags & MEM_Blob) != 0 &&
		    (mem->flags & MEM_Subtype) != 0)
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
	if (mem_is_null(mem))
		return 0;
	switch (type) {
	case FIELD_TYPE_UNSIGNED:
		if ((mem->flags & MEM_UInt) != 0)
			return 0;
		if ((mem->flags & MEM_Real) != 0)
			return double_to_uint_precise(mem);
		if ((mem->flags & MEM_Str) != 0)
			return bytes_to_uint(mem);
		return -1;
	case FIELD_TYPE_STRING:
		if ((mem->flags & (MEM_Str | MEM_Blob)) != 0)
			return 0;
		if ((mem->flags & (MEM_Int | MEM_UInt)) != 0)
			return int_to_str0(mem);
		if ((mem->flags & MEM_Real) != 0)
			return double_to_str0(mem);
		return -1;
	case FIELD_TYPE_DOUBLE:
		if ((mem->flags & MEM_Real) != 0)
			return 0;
		if ((mem->flags & (MEM_Int | MEM_UInt)) != 0)
			return int_to_double(mem);
		if ((mem->flags & MEM_Str) != 0)
			return bin_to_str(mem);
		return -1;
	case FIELD_TYPE_INTEGER:
		if ((mem->flags & (MEM_Int | MEM_UInt)) != 0)
			return 0;
		if ((mem->flags & MEM_Str) != 0)
			return bytes_to_int(mem);
		if ((mem->flags & MEM_Real) != 0)
			return double_to_int_precise(mem);
		return -1;
	case FIELD_TYPE_BOOLEAN:
		if ((mem->flags & MEM_Bool) != 0)
			return 0;
		return -1;
	case FIELD_TYPE_VARBINARY:
		if ((mem->flags & MEM_Blob) != 0)
			return 0;
		return -1;
	case FIELD_TYPE_NUMBER:
		if ((mem->flags & (MEM_Int | MEM_UInt | MEM_Real)) != 0)
			return 0;
		if ((mem->flags & MEM_Str) != 0)
			return mem_to_number(mem);
		return -1;
	case FIELD_TYPE_MAP:
		if (mem_is_map(mem))
			return 0;
		return -1;
	case FIELD_TYPE_ARRAY:
		if (mem_is_array(mem))
			return 0;
		return -1;
	case FIELD_TYPE_SCALAR:
		if ((mem->flags & MEM_Blob) != 0 &&
		    (mem->flags & MEM_Subtype) != 0)
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
	if ((mem->flags & MEM_Int) != 0) {
		*i = mem->u.i;
		*is_neg = true;
		return 0;
	}
	if ((mem->flags & MEM_UInt) != 0) {
		*i = mem->u.i;
		*is_neg = false;
		return 0;
	}
	if ((mem->flags & (MEM_Str | MEM_Blob)) != 0)
		return sql_atoi64(mem->z, i, is_neg, mem->n);
	if ((mem->flags & MEM_Real) != 0) {
		double d = mem->u.r;
		if (d < 0 && d >= (double)INT64_MIN) {
			*i = (int64_t)d;
			*is_neg = true;
			return 0;
		}
		if (d >= 0 && d < (double)UINT64_MAX) {
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
	if ((mem->flags & MEM_Int) != 0)
		return -1;
	if ((mem->flags & MEM_UInt) != 0) {
		*u = mem->u.u;
		return 0;
	}
	if ((mem->flags & (MEM_Str | MEM_Blob)) != 0) {
		bool is_neg;
		if (sql_atoi64(mem->z, (int64_t *)u, &is_neg, mem->n) != 0 ||
		    is_neg)
			return -1;
		return 0;
	}
	if ((mem->flags & MEM_Real) != 0) {
		double d = mem->u.r;
		if (d >= 0 && d < (double)UINT64_MAX) {
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
	if ((mem->flags & MEM_Real) != 0) {
		*d = mem->u.r;
		return 0;
	}
	if ((mem->flags & MEM_Int) != 0) {
		*d = (double)mem->u.i;
		return 0;
	}
	if ((mem->flags & MEM_UInt) != 0) {
		*d = (double)mem->u.u;
		return 0;
	}
	if ((mem->flags & MEM_Str) != 0) {
		if (sqlAtoF(mem->z, d, mem->n) == 0)
			return -1;
		return 0;
	}
	return -1;
}

int
mem_get_bool(const struct Mem *mem, bool *b)
{
	if ((mem->flags & MEM_Bool) != 0) {
		*b = mem->u.b;
		return 0;
	}
	return -1;
}

int
mem_get_str0(const struct Mem *mem, const char **s)
{
	if ((mem->flags & MEM_Str) == 0 || (mem->flags & MEM_Term) == 0)
		return -1;
	*s = mem->z;
	return 0;
}

int
mem_copy(struct Mem *to, const struct Mem *from)
{
	mem_clear(to);
	to->u = from->u;
	to->flags = from->flags;
	to->subtype = from->subtype;
	to->field_type = from->field_type;
	to->n = from->n;
	to->z = from->z;
	if ((to->flags & (MEM_Str | MEM_Blob)) == 0)
		return 0;
	if ((to->flags & MEM_Static) != 0)
		return 0;
	if ((to->flags & (MEM_Zero | MEM_Blob)) == (MEM_Zero | MEM_Blob))
		return sqlVdbeMemExpandBlob(to);
	to->zMalloc = sqlDbReallocOrFree(to->db, to->zMalloc, to->n);
	if (to->zMalloc == NULL)
		return -1;
	to->szMalloc = sqlDbMallocSize(to->db, to->zMalloc);
	memcpy(to->zMalloc, to->z, to->n);
	to->z = to->zMalloc;
	to->flags &= (MEM_Str | MEM_Blob | MEM_Term | MEM_Subtype);
	return 0;
}

void
mem_copy_as_ephemeral(struct Mem *to, const struct Mem *from)
{
	mem_clear(to);
	to->u = from->u;
	to->flags = from->flags;
	to->subtype = from->subtype;
	to->field_type = from->field_type;
	to->n = from->n;
	to->z = from->z;
	if ((to->flags & (MEM_Str | MEM_Blob)) == 0)
		return;
	if ((to->flags & (MEM_Static | MEM_Ephem)) != 0)
		return;
	to->flags &= (MEM_Str | MEM_Blob | MEM_Term | MEM_Zero | MEM_Subtype);
	to->flags |= MEM_Ephem;
	return;
}

void
mem_move(struct Mem *to, struct Mem *from)
{
	mem_destroy(to);
	memcpy(to, from, sizeof(*to));
	from->flags = MEM_Null;
	from->szMalloc = 0;
	from->zMalloc = NULL;
}

static bool
try_return_null(const struct Mem *a, const struct Mem *b, struct Mem *result,
		enum field_type type)
{
	mem_clear(result);
	result->field_type = type;
	return (((a->flags | b->flags) & MEM_Null) != 0);
}

int
mem_concat(struct Mem *a, struct Mem *b, struct Mem *result)
{
	assert(result != b);
	if (a != result) {
		if (try_return_null(a, b, result, FIELD_TYPE_STRING))
			return 0;
	} else {
		if (((a->flags | b->flags) & MEM_Null) != 0) {
			mem_clear(a);
			result->field_type = FIELD_TYPE_STRING;
			return 0;
		}
	}

	/* Concatenation operation can be applied only to strings and blobs. */
	if ((b->flags & (MEM_Str | MEM_Blob)) == 0) {
		diag_set(ClientError, ER_INCONSISTENT_TYPES,
			 "text or varbinary", mem_type_to_str(b));
		return -1;
	}
	if ((a->flags & (MEM_Str | MEM_Blob)) == 0) {
		diag_set(ClientError, ER_INCONSISTENT_TYPES,
			 "text or varbinary", mem_type_to_str(a));
		return -1;
	}

	/* Moreover, both operands must be of the same type. */
	if ((b->flags & MEM_Str) != (a->flags & MEM_Str)) {
		diag_set(ClientError, ER_INCONSISTENT_TYPES,
			 mem_type_to_str(a), mem_type_to_str(b));
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

	result->flags = a->flags & (MEM_Str | MEM_Blob);
	if ((result->flags & MEM_Blob) != 0)
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
	int type;
	bool is_neg;
};

static int
get_number(const struct Mem *mem, struct sql_num *number)
{
	if ((mem->flags & MEM_Real) != 0) {
		number->d = mem->u.r;
		number->type = MEM_Real;
		return 0;
	}
	if ((mem->flags & MEM_Int) != 0) {
		number->i = mem->u.i;
		number->type = MEM_Int;
		number->is_neg = true;
		return 0;
	}
	if ((mem->flags & MEM_UInt) != 0) {
		number->u = mem->u.u;
		number->type = MEM_UInt;
		number->is_neg = false;
		return 0;
	}
	if ((mem->flags & (MEM_Str | MEM_Blob)) == 0)
		return -1;
	if ((mem->flags & MEM_Subtype) != 0)
		return -1;
	if (sql_atoi64(mem->z, &number->i, &number->is_neg, mem->n) == 0) {
		number->type = number->is_neg ? MEM_Int : MEM_UInt;
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
		number->type = MEM_Real;
		return 0;
	}
	return -1;
}

static int
arithmetic_prepare(const struct Mem *left, const struct Mem *right,
		   struct sql_num *a, struct sql_num *b)
{
	if (get_number(right, b) != 0) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(right),
			 "numeric");
		return -1;
	}
	if (get_number(left, a) != 0) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(left),
			 "numeric");
		return -1;
	}
	assert(a->type != 0 && b->type != 0);
	if (a->type == MEM_Real && b->type != MEM_Real) {
		b->d = b->type == MEM_Int ? (double)b->i : (double)b->u;
		b->type = MEM_Real;
		return 0;
	}
	if (a->type != MEM_Real && b->type == MEM_Real) {
		a->d = a->type == MEM_Int ? (double)a->i : (double)a->u;
		a->type = MEM_Real;
		return 0;
	}
	return 0;
}

int
mem_add(const struct Mem *left, const struct Mem *right, struct Mem *result)
{
	if (try_return_null(left, right, result, FIELD_TYPE_NUMBER))
		return 0;

	struct sql_num a, b;
	if (arithmetic_prepare(left, right, &a, &b) != 0)
		return -1;

	assert(a.type != MEM_Real || a.type == b.type);
	if (a.type == MEM_Real) {
		result->u.r = a.d + b.d;
		result->flags = MEM_Real;
		return 0;
	}

	int64_t res;
	bool is_neg;
	if (sql_add_int(a.i, a.is_neg, b.i, b.is_neg, &res, &is_neg) != 0) {
		diag_set(ClientError, ER_SQL_EXECUTE, "integer is overflowed");
		return -1;
	}
	result->u.i = res;
	result->flags = is_neg ? MEM_Int : MEM_UInt;
	return 0;
}

int
mem_sub(const struct Mem *left, const struct Mem *right, struct Mem *result)
{
	if (try_return_null(left, right, result, FIELD_TYPE_NUMBER))
		return 0;

	struct sql_num a, b;
	if (arithmetic_prepare(left, right, &a, &b) != 0)
		return -1;

	assert(a.type != MEM_Real || a.type == b.type);
	if (a.type == MEM_Real) {
		result->u.r = a.d - b.d;
		result->flags = MEM_Real;
		return 0;
	}

	int64_t res;
	bool is_neg;
	if (sql_sub_int(a.i, a.is_neg, b.i, b.is_neg, &res, &is_neg) != 0) {
		diag_set(ClientError, ER_SQL_EXECUTE, "integer is overflowed");
		return -1;
	}
	result->u.i = res;
	result->flags = is_neg ? MEM_Int : MEM_UInt;
	return 0;
}

int
mem_mul(const struct Mem *left, const struct Mem *right, struct Mem *result)
{
	if (try_return_null(left, right, result, FIELD_TYPE_NUMBER))
		return 0;

	struct sql_num a, b;
	if (arithmetic_prepare(left, right, &a, &b) != 0)
		return -1;

	assert(a.type != MEM_Real || a.type == b.type);
	if (a.type == MEM_Real) {
		result->u.r = a.d * b.d;
		result->flags = MEM_Real;
		return 0;
	}

	int64_t res;
	bool is_neg;
	if (sql_mul_int(a.i, a.is_neg, b.i, b.is_neg, &res, &is_neg) != 0) {
		diag_set(ClientError, ER_SQL_EXECUTE, "integer is overflowed");
		return -1;
	}
	result->u.i = res;
	result->flags = is_neg ? MEM_Int : MEM_UInt;
	return 0;
}

int
mem_div(const struct Mem *left, const struct Mem *right, struct Mem *result)
{
	if (try_return_null(left, right, result, FIELD_TYPE_NUMBER))
		return 0;

	struct sql_num a, b;
	if (arithmetic_prepare(left, right, &a, &b) != 0)
		return -1;

	assert(a.type != MEM_Real || a.type == b.type);
	if (a.type == MEM_Real) {
		if (b.d == 0.) {
			diag_set(ClientError, ER_SQL_EXECUTE,
				 "division by zero");
			return -1;
		}
		result->u.r = a.d / b.d;
		result->flags = MEM_Real;
		return 0;
	}

	if (b.i == 0) {
		diag_set(ClientError, ER_SQL_EXECUTE, "division by zero");
		return -1;
	}
	int64_t res;
	bool is_neg;
	if (sql_div_int(a.i, a.is_neg, b.i, b.is_neg, &res, &is_neg) != 0) {
		diag_set(ClientError, ER_SQL_EXECUTE, "integer is overflowed");
		return -1;
	}
	result->u.i = res;
	result->flags = is_neg ? MEM_Int : MEM_UInt;
	return 0;
}

int
mem_rem(const struct Mem *left, const struct Mem *right, struct Mem *result)
{
	if (try_return_null(left, right, result, FIELD_TYPE_NUMBER))
		return 0;

	struct sql_num a, b;
	if (arithmetic_prepare(left, right, &a, &b) != 0)
		return -1;

	assert(a.type != MEM_Real || a.type == b.type);
	/*
	 * TODO: This operation works wrong when double d > INT64_MAX and
	 * d < UINT64_MAX. Also, there may be precision losses due to
	 * conversion integer to double and back.
	 */
	a.i = a.type == MEM_Real ? (int64_t)a.d : a.i;
	b.i = b.type == MEM_Real ? (int64_t)b.d : b.i;
	if (b.i == 0) {
		diag_set(ClientError, ER_SQL_EXECUTE, "division by zero");
		return -1;
	}
	int64_t res;
	bool is_neg;
	if (sql_rem_int(a.i, a.is_neg, b.i, b.is_neg, &res, &is_neg) != 0) {
		diag_set(ClientError, ER_SQL_EXECUTE, "integer is overflowed");
		return -1;
	}
	result->u.i = res;
	result->flags = is_neg ? MEM_Int : MEM_UInt;
	return 0;
}

static int
bitwise_prepare(const struct Mem *left, const struct Mem *right,
		int64_t *a, int64_t *b)
{
	bool unused;
	if (mem_get_int(left, a, &unused) != 0) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(left),
			 "integer");
		return -1;
	}
	if (mem_get_int(right, b, &unused) != 0) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(right),
			 "integer");
		return -1;
	}
	return 0;
}

int
mem_bit_and(const struct Mem *left, const struct Mem *right, struct Mem *result)
{
	if (try_return_null(left, right, result, FIELD_TYPE_INTEGER))
		return 0;
	int64_t a;
	int64_t b;
	if (bitwise_prepare(left, right, &a, &b) != 0)
		return -1;
	result->u.i = a & b;
	result->flags = result->u.i < 0 ? MEM_Int : MEM_UInt;
	return 0;
}

int
mem_bit_or(const struct Mem *left, const struct Mem *right, struct Mem *result)
{
	if (try_return_null(left, right, result, FIELD_TYPE_INTEGER))
		return 0;
	int64_t a;
	int64_t b;
	if (bitwise_prepare(left, right, &a, &b) != 0)
		return -1;
	result->u.i = a | b;
	result->flags = result->u.i < 0 ? MEM_Int : MEM_UInt;
	return 0;
}

int
mem_shift_left(const struct Mem *left, const struct Mem *right,
	       struct Mem *result)
{
	if (try_return_null(left, right, result, FIELD_TYPE_INTEGER))
		return 0;
	int64_t a;
	int64_t b;
	if (bitwise_prepare(left, right, &a, &b) != 0)
		return -1;
	if (b <= -64)
		result->u.i = a >= 0 ? 0 : -1;
	else if (b < 0)
		result->u.i = a >> -b;
	else if (b > 64)
		result->u.i = 0;
	else
		result->u.i = a << b;
	result->flags = result->u.i < 0 ? MEM_Int : MEM_UInt;
	return 0;
}

int
mem_shift_right(const struct Mem *left, const struct Mem *right,
		struct Mem *result)
{
	if (try_return_null(left, right, result, FIELD_TYPE_INTEGER))
		return 0;
	int64_t a;
	int64_t b;
	if (bitwise_prepare(left, right, &a, &b) != 0)
		return -1;
	if (b <= -64)
		result->u.i = 0;
	else if (b < 0)
		result->u.i = a << -b;
	else if (b > 64)
		result->u.i = a >= 0 ? 0 : -1;
	else
		result->u.i = a >> b;
	result->flags = result->u.i < 0 ? MEM_Int : MEM_UInt;
	return 0;
}

int
mem_bit_not(const struct Mem *mem, struct Mem *result)
{
	mem_clear(result);
	result->field_type = FIELD_TYPE_INTEGER;
	if ((mem->flags & MEM_Null) != 0)
		return 0;
	int64_t i;
	bool unused;
	if (mem_get_int(mem, &i, &unused) != 0) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(mem),
			 "integer");
		return -1;
	}
	result->u.i = ~i;
	result->flags = result->u.i < 0 ? MEM_Int : MEM_UInt;
	return 0;
}

int
mem_cmp_bool(const struct Mem *a, const struct Mem *b, int *result)
{
	if ((a->flags & b->flags & MEM_Bool) == 0)
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
	if ((a->flags & b->flags & MEM_Blob) == 0)
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
	if (a.type == MEM_Real) {
		if (b.type == MEM_Real) {
			if (a.d > b.d)
				*result = 1;
			else if (a.d < b.d)
				*result = -1;
			else
				*result = 0;
			return 0;
		}
		if (b.type == MEM_Int)
			*result = double_compare_nint64(a.d, b.i, 1);
		else
			*result = double_compare_uint64(a.d, b.u, 1);
		return 0;
	}
	if (a.type == MEM_Int) {
		if (b.type == MEM_Int) {
			if (a.i > b.i)
				*result = 1;
			else if (a.i < b.i)
				*result = -1;
			else
				*result = 0;
			return 0;
		}
		if (b.type == MEM_UInt)
			*result = -1;
		else
			*result = double_compare_nint64(b.d, a.i, -1);
		return 0;
	}
	assert(a.type == MEM_UInt);
	if (b.type == MEM_UInt) {
		if (a.u > b.u)
			*result = 1;
		else if (a.u < b.u)
			*result = -1;
		else
			*result = 0;
		return 0;
	}
	if (b.type == MEM_Int)
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
	if ((left->flags & MEM_Str) != 0) {
		a = left->z;
		an = left->n;
	} else {
		assert((left->flags & (MEM_Int | MEM_UInt | MEM_Real)) != 0);
		a = &bufl[0];
		if ((left->flags & MEM_Int) != 0)
			sql_snprintf(BUF_SIZE, a, "%lld", left->u.i);
		else if ((left->flags & MEM_UInt) != 0)
			sql_snprintf(BUF_SIZE, a, "%llu", left->u.u);
		else
			sql_snprintf(BUF_SIZE, a, "%!.15g", left->u.r);
		an = strlen(a);
	}

	char *b;
	uint32_t bn;
	char bufr[BUF_SIZE];
	if ((right->flags & MEM_Str) != 0) {
		b = right->z;
		bn = right->n;
	} else {
		assert((right->flags & (MEM_Int | MEM_UInt | MEM_Real)) != 0);
		b = &bufr[0];
		if ((right->flags & MEM_Int) != 0)
			sql_snprintf(BUF_SIZE, b, "%lld", right->u.i);
		else if ((right->flags & MEM_UInt) != 0)
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

static inline bool
mem_has_msgpack_subtype(struct Mem *mem)
{
	return (mem->flags & MEM_Subtype) != 0 &&
	       mem->subtype == SQL_SUBTYPE_MSGPACK;
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
	switch (p->flags & MEM_PURE_TYPE_MASK) {
	case MEM_Null:
		return "NULL";
	case MEM_Str:
		return "text";
	case MEM_Int:
		return "integer";
	case MEM_UInt:
		return "unsigned";
	case MEM_Real:
		return "real";
	case MEM_Blob:
		return "varbinary";
	case MEM_Bool:
		return "boolean";
	default:
		unreachable();
	}
}

enum mp_type
mem_mp_type(struct Mem *mem)
{
	switch (mem->flags & MEM_PURE_TYPE_MASK) {
	case MEM_Int:
		return MP_INT;
	case MEM_UInt:
		return MP_UINT;
	case MEM_Real:
		return MP_DOUBLE;
	case MEM_Str:
		return MP_STR;
	case MEM_Blob:
		if ((mem->flags & MEM_Subtype) == 0 ||
		     mem->subtype != SQL_SUBTYPE_MSGPACK)
			return MP_BIN;
		assert(mp_typeof(*mem->z) == MP_MAP ||
		       mp_typeof(*mem->z) == MP_ARRAY);
		return mp_typeof(*mem->z);
	case MEM_Bool:
		return MP_BOOL;
	case MEM_Null:
		return MP_NIL;
	default: unreachable();
	}
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

/*
 * The sqlValueBytes() routine returns the number of bytes in the
 * sql_value object assuming that it uses the encoding "enc".
 * The valueBytes() routine is a helper function.
 */
static SQL_NOINLINE int
valueBytes(sql_value * pVal)
{
	if (mem_to_str(pVal) != 0)
		return 0;
	return pVal->n;
}

int
sqlValueBytes(sql_value * pVal)
{
	Mem *p = (Mem *) pVal;
	assert((p->flags & MEM_Null) == 0
	       || (p->flags & (MEM_Str | MEM_Blob)) == 0);
	if ((p->flags & MEM_Str) != 0) {
		return p->n;
	}
	if ((p->flags & MEM_Blob) != 0) {
		if (p->flags & MEM_Zero) {
			return p->n + p->u.nZero;
		} else {
			return p->n;
		}
	}
	if (p->flags & MEM_Null)
		return 0;
	return valueBytes(pVal);
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

	/* Cannot be both MEM_Int and MEM_Real at the same time */
	assert((p->flags & (MEM_Int | MEM_Real)) != (MEM_Int | MEM_Real));
	/* Can't be both UInt and Int at the same time.  */
	assert((p->flags & (MEM_Int | MEM_UInt)) != (MEM_Int | MEM_UInt));

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
	if ((p->flags & (MEM_Str | MEM_Blob)) && p->n > 0) {
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

	if (f&MEM_Blob) {
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
	} else if (f & MEM_Str) {
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
	if (p->flags & MEM_Undefined) {
		printf(" undefined");
	} else if (p->flags & MEM_Null) {
		printf(" NULL");
	} else if ((p->flags & (MEM_Int|MEM_Str))==(MEM_Int|MEM_Str)) {
		printf(" si:%lld", p->u.i);
	} else if (p->flags & MEM_Int) {
		printf(" i:%lld", p->u.i);
	} else if (p->flags & MEM_UInt) {
		printf(" u:%"PRIu64"", p->u.u);
	} else if (p->flags & MEM_Real) {
		printf(" r:%g", p->u.r);
	} else if (p->flags & MEM_Bool) {
		printf(" bool:%s", SQL_TOKEN_BOOLEAN(p->u.b));
	} else {
		char zBuf[200];
		sqlVdbeMemPrettyPrint(p, zBuf);
		printf(" %s", zBuf);
	}
	if (p->flags & MEM_Subtype) printf(" subtype=0x%02x", p->subtype);
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
	assert(pMem->flags & MEM_Blob);

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
	assert(bPreserve == 0 || pMem->flags & (MEM_Blob | MEM_Str));
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
 * The pMem->xDel destructor is called, if it exists.  Though MEM_Str
 * and MEM_Blob values may be discarded, MEM_Int, MEM_Real, and MEM_Null
 * values are preserved.
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
	pMem->flags &= (MEM_Null | MEM_Int | MEM_Real);
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
		p->flags = MEM_Null;
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
			p->flags = MEM_Undefined;
		} while ((++p) < pEnd);
	}
}

/**************************** sql_value_  ******************************
 * The following routines extract information from a Mem or sql_value
 * structure.
 */
const void *
sql_value_blob(sql_value * pVal)
{
	Mem *p = (Mem *) pVal;
	if (p->flags & (MEM_Blob | MEM_Str)) {
		if (ExpandBlob(p) != 0) {
			assert(p->flags == MEM_Null && p->z == 0);
			return 0;
		}
		p->flags |= MEM_Blob;
		return p->n ? p->z : 0;
	} else {
		if (mem_to_str(pVal) != 0)
			return NULL;
		return pVal->z;
	}
}

int
sql_value_bytes(sql_value * pVal)
{
	return sqlValueBytes(pVal);
}

/*
 * Return a pointer to static memory containing an SQL NULL value.
 */
const Mem *
columnNullValue(void)
{
	/* Even though the Mem structure contains an element
	 * of type i64, on certain architectures (x86) with certain compiler
	 * switches (-Os), gcc may align this Mem object on a 4-byte boundary
	 * instead of an 8-byte one. This all works fine, except that when
	 * running with SQL_DEBUG defined the sql code sometimes assert()s
	 * that a Mem structure is located on an 8-byte boundary. To prevent
	 * these assert()s from failing, when building with SQL_DEBUG defined
	 * using gcc, we force nullMem to be 8-byte aligned using the magical
	 * __attribute__((aligned(8))) macro.
	 */
	static const Mem nullMem
#if defined(SQL_DEBUG) && defined(__GNUC__)
	    __attribute__ ((aligned(8)))
#endif
	    = {
		/* .u          = */  {
		0},
		    /* .flags      = */ (u16) MEM_Null,
		    /* .eSubtype   = */ (u8) 0,
		    /* .field_type = */ field_type_MAX,
		    /* .n          = */ (int)0,
		    /* .z          = */ (char *)0,
		    /* .zMalloc    = */ (char *)0,
		    /* .szMalloc   = */ (int)0,
		    /* .uTemp      = */ (u32) 0,
		    /* .db         = */ (sql *) 0,
		    /* .xDel       = */ (void (*)(void *))0,
#ifdef SQL_DEBUG
		    /* .pScopyFrom = */ (Mem *) 0,
		    /* .pFiller    = */ (void *)0,
#endif
	};
	return &nullMem;
}

/*
 * Return true if the Mem object contains a TEXT or BLOB that is
 * too large - whose size exceeds SQL_MAX_LENGTH.
 */
int
sqlVdbeMemTooBig(Mem * p)
{
	assert(p->db != 0);
	if (p->flags & (MEM_Str | MEM_Blob)) {
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
	int f1, f2;
	int res;
	int combined_flags;

	f1 = pMem1->flags;
	f2 = pMem2->flags;
	combined_flags = f1 | f2;

	/* If one value is NULL, it is less than the other. If both values
	 * are NULL, return 0.
	 */
	if (combined_flags & MEM_Null) {
		return (f2 & MEM_Null) - (f1 & MEM_Null);
	}

	if ((combined_flags & MEM_Bool) != 0) {
		if ((f1 & f2 & MEM_Bool) != 0) {
			if (pMem1->u.b == pMem2->u.b)
				return 0;
			if (pMem1->u.b)
				return 1;
			return -1;
		}
		if ((f2 & MEM_Bool) != 0)
			return +1;
		return -1;
	}

	/* At least one of the two values is a number
	 */
	if ((combined_flags & (MEM_Int | MEM_UInt | MEM_Real)) != 0) {
		if ((f1 & (MEM_Real | MEM_Int | MEM_UInt)) == 0)
			return +1;
		if ((f2 & (MEM_Real | MEM_Int | MEM_UInt)) == 0)
			return -1;
		mem_cmp_num(pMem1, pMem2, &res);
		return res;
	}

	/* If one value is a string and the other is a blob, the string is less.
	 * If both are strings, compare using the collating functions.
	 */
	if (combined_flags & MEM_Str) {
		if ((f1 & MEM_Str) == 0) {
			return 1;
		}
		if ((f2 & MEM_Str) == 0) {
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
	assert((mem->flags & MEM_Null) != 0 || func == mem->u.func);
	sql_context ctx;
	memset(&ctx, 0, sizeof(ctx));
	Mem t;
	memset(&t, 0, sizeof(t));
	t.flags = MEM_Null;
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
			rc = -((pKey2->flags & MEM_Null) == 0);
			mp_decode_nil(&aKey1);
			break;
		}
	case MP_BOOL:{
			mem1.u.b = mp_decode_bool(&aKey1);
			if ((pKey2->flags & MEM_Bool) != 0) {
				if (mem1.u.b != pKey2->u.b)
					rc = mem1.u.b ? 1 : -1;
			} else {
				rc = (pKey2->flags & MEM_Null) != 0 ? 1 : -1;
			}
			break;
		}
	case MP_UINT:{
			mem1.u.u = mp_decode_uint(&aKey1);
			if ((pKey2->flags & MEM_Int) != 0) {
				rc = +1;
			} else if ((pKey2->flags & MEM_UInt) != 0) {
				if (mem1.u.u < pKey2->u.u)
					rc = -1;
				else if (mem1.u.u > pKey2->u.u)
					rc = +1;
			} else if ((pKey2->flags & MEM_Real) != 0) {
				rc = double_compare_uint64(pKey2->u.r,
							   mem1.u.u, -1);
			} else if ((pKey2->flags & MEM_Null) != 0) {
				rc = 1;
			} else if ((pKey2->flags & MEM_Bool) != 0) {
				rc = 1;
			} else {
				rc = -1;
			}
			break;
		}
	case MP_INT:{
			mem1.u.i = mp_decode_int(&aKey1);
			if ((pKey2->flags & MEM_UInt) != 0) {
				rc = -1;
			} else if ((pKey2->flags & MEM_Int) != 0) {
				if (mem1.u.i < pKey2->u.i) {
					rc = -1;
				} else if (mem1.u.i > pKey2->u.i) {
					rc = +1;
				}
			} else if (pKey2->flags & MEM_Real) {
				rc = double_compare_nint64(pKey2->u.r, mem1.u.i,
							   -1);
			} else if ((pKey2->flags & MEM_Null) != 0) {
				rc = 1;
			} else if ((pKey2->flags & MEM_Bool) != 0) {
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
			if ((pKey2->flags & MEM_Int) != 0) {
				rc = double_compare_nint64(mem1.u.r, pKey2->u.i,
							   1);
			} else if (pKey2->flags & MEM_UInt) {
				rc = double_compare_uint64(mem1.u.r,
							   pKey2->u.u, 1);
			} else if (pKey2->flags & MEM_Real) {
				if (mem1.u.r < pKey2->u.r) {
					rc = -1;
				} else if (mem1.u.r > pKey2->u.r) {
					rc = +1;
				}
			} else if ((pKey2->flags & MEM_Null) != 0) {
				rc = 1;
			} else if ((pKey2->flags & MEM_Bool) != 0) {
				rc = 1;
			} else {
				rc = -1;
			}
			break;
		}
	case MP_STR:{
			if (pKey2->flags & MEM_Str) {
				struct key_def *key_def = unpacked->key_def;
				mem1.n = mp_decode_strl(&aKey1);
				mem1.z = (char *)aKey1;
				aKey1 += mem1.n;
				struct coll *coll =
					key_def->parts[key2_idx].coll;
				if (coll != NULL) {
					mem1.flags = MEM_Str;
					rc = vdbeCompareMemString(&mem1, pKey2,
								  coll);
				} else {
					goto do_bin_cmp;
				}
			} else {
				rc = (pKey2->flags & MEM_Blob) ? -1 : +1;
			}
			break;
		}
	case MP_BIN:{
			mem1.n = mp_decode_binl(&aKey1);
			mem1.z = (char *)aKey1;
			aKey1 += mem1.n;
 do_blob:
			if (pKey2->flags & MEM_Blob) {
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
		mem->flags = MEM_Blob | MEM_Ephem | MEM_Subtype;
		mem->subtype = SQL_SUBTYPE_MSGPACK;
		mem->field_type = FIELD_TYPE_ARRAY;
		break;
	}
	case MP_MAP: {
		mem->z = (char *)buf;
		mp_next(&buf);
		mem->n = buf - mem->z;
		mem->flags = MEM_Blob | MEM_Ephem | MEM_Subtype;
		mem->subtype = SQL_SUBTYPE_MSGPACK;
		mem->field_type = FIELD_TYPE_MAP;
		break;
	}
	case MP_EXT: {
		mem->z = (char *)buf;
		mp_next(&buf);
		mem->n = buf - mem->z;
		mem->flags = MEM_Blob | MEM_Ephem;
		mem->field_type = FIELD_TYPE_VARBINARY;
		break;
	}
	case MP_NIL: {
		mp_decode_nil(&buf);
		mem->flags = MEM_Null;
		mem->field_type = field_type_MAX;
		break;
	}
	case MP_BOOL: {
		mem->u.b = mp_decode_bool(&buf);
		mem->flags = MEM_Bool;
		mem->field_type = FIELD_TYPE_BOOLEAN;
		break;
	}
	case MP_UINT: {
		uint64_t v = mp_decode_uint(&buf);
		mem->u.u = v;
		mem->flags = MEM_UInt;
		mem->field_type = FIELD_TYPE_INTEGER;
		break;
	}
	case MP_INT: {
		mem->u.i = mp_decode_int(&buf);
		mem->flags = MEM_Int;
		mem->field_type = FIELD_TYPE_INTEGER;
		break;
	}
	case MP_STR: {
		/* XXX u32->int */
		mem->n = (int) mp_decode_strl(&buf);
		mem->flags = MEM_Str | MEM_Ephem;
		mem->field_type = FIELD_TYPE_STRING;
install_blob:
		mem->z = (char *)buf;
		buf += mem->n;
		break;
	}
	case MP_BIN: {
		/* XXX u32->int */
		mem->n = (int) mp_decode_binl(&buf);
		mem->flags = MEM_Blob | MEM_Ephem;
		mem->field_type = FIELD_TYPE_VARBINARY;
		goto install_blob;
	}
	case MP_FLOAT: {
		mem->u.r = mp_decode_float(&buf);
		if (sqlIsNaN(mem->u.r)) {
			mem->flags = MEM_Null;
			mem->field_type = FIELD_TYPE_DOUBLE;
		} else {
			mem->flags = MEM_Real;
			mem->field_type = FIELD_TYPE_DOUBLE;
		}
		break;
	}
	case MP_DOUBLE: {
		mem->u.r = mp_decode_double(&buf);
		if (sqlIsNaN(mem->u.r)) {
			mem->flags = MEM_Null;
			mem->field_type = FIELD_TYPE_DOUBLE;
		} else {
			mem->flags = MEM_Real;
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
	if ((mem->flags & (MEM_Str | MEM_Blob)) != 0) {
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
	int64_t i;
	if (var->flags & MEM_Null) {
		mpstream_encode_nil(stream);
	} else if (var->flags & MEM_Real) {
		mpstream_encode_double(stream, var->u.r);
	} else if (var->flags & MEM_Int) {
		i = var->u.i;
		mpstream_encode_int(stream, i);
	} else if (var->flags & MEM_UInt) {
		i = var->u.u;
		mpstream_encode_uint(stream, i);
	} else if (var->flags & MEM_Str) {
		mpstream_encode_strn(stream, var->z, var->n);
	} else if (var->flags & MEM_Bool) {
		mpstream_encode_bool(stream, var->u.b);
	} else {
		/*
		 * Emit BIN header iff the BLOB doesn't store
		 * MsgPack content.
		 */
		if (!mem_has_msgpack_subtype(var)) {
			uint32_t binl = var->n +
					((var->flags & MEM_Zero) ?
					var->u.nZero : 0);
			mpstream_encode_binl(stream, binl);
		}
		mpstream_memcpy(stream, var->z, var->n);
		if (var->flags & MEM_Zero)
			mpstream_memset(stream, 0, var->u.nZero);
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
		switch (mem->flags & MEM_PURE_TYPE_MASK) {
		case MEM_Int:
			luaL_pushint64(L, mem->u.i);
			break;
		case MEM_UInt:
			luaL_pushuint64(L, mem->u.u);
			break;
		case MEM_Real:
			lua_pushnumber(L, mem->u.r);
			break;
		case MEM_Str:
		case MEM_Blob:
			lua_pushlstring(L, mem->z, mem->n);
			break;
		case MEM_Null:
			lua_pushnil(L);
			break;
		case MEM_Bool:
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
			val[i].flags = MEM_Bool;
			val[i].u.b = field.bval;
			break;
		case MP_FLOAT:
			val[i].flags = MEM_Real;
			val[i].u.r = field.fval;
			break;
		case MP_DOUBLE:
			val[i].flags = MEM_Real;
			val[i].u.r = field.dval;
			break;
		case MP_INT:
			val[i].flags = MEM_Int;
			val[i].u.i = field.ival;
			break;
		case MP_UINT:
			val[i].flags = MEM_UInt;
			val[i].u.i = field.ival;
			break;
		case MP_STR:
			if (mem_copy_str(&val[i], field.sval.data,
					 field.sval.len) != 0)
				goto error;
			break;
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
			val[i].flags = MEM_Bool;
			val[i].u.b = mp_decode_bool(&data);
			break;
		case MP_FLOAT:
			val[i].flags = MEM_Real;
			val[i].u.r = mp_decode_float(&data);
			break;
		case MP_DOUBLE:
			val[i].flags = MEM_Real;
			val[i].u.r = mp_decode_double(&data);
			break;
		case MP_INT:
			val[i].flags = MEM_Int;
			val[i].u.i = mp_decode_int(&data);
			break;
		case MP_UINT:
			val[i].flags = MEM_UInt;
			val[i].u.u = mp_decode_uint(&data);
			break;
		case MP_STR:
			str = mp_decode_str(&data, &len);
			if (mem_copy_str(&val[i], str, len) != 0)
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
