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
#include "lua/decimal.h"
#include "mp_interval.h"
#include "mp_datetime.h"
#include "mp_decimal.h"
#include "mp_uuid.h"

#define CMP_OLD_NEW(a, b, type) (((a) > (type)(b)) - ((a) < (type)(b)))

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

/**
 * Analogue of enum mp_class for enum mp_type. The order of the classes must be
 * the same as in the enum mp_class.
 */
enum mem_class {
	MEM_CLASS_NULL,
	MEM_CLASS_BOOL,
	MEM_CLASS_NUMBER,
	MEM_CLASS_STR,
	MEM_CLASS_BIN,
	MEM_CLASS_UUID,
	MEM_CLASS_DATETIME,
	mem_class_max,
};

static inline enum mem_class
mem_type_class(enum mem_type type)
{
	switch (type) {
	case MEM_TYPE_NULL:
		return MEM_CLASS_NULL;
	case MEM_TYPE_UINT:
	case MEM_TYPE_INT:
	case MEM_TYPE_DEC:
	case MEM_TYPE_DOUBLE:
		return MEM_CLASS_NUMBER;
	case MEM_TYPE_STR:
		return MEM_CLASS_STR;
	case MEM_TYPE_BIN:
		return MEM_CLASS_BIN;
	case MEM_TYPE_BOOL:
		return MEM_CLASS_BOOL;
	case MEM_TYPE_UUID:
		return MEM_CLASS_UUID;
	case MEM_TYPE_DATETIME:
		return MEM_CLASS_DATETIME;
	default:
		break;
	}
	return mem_class_max;
}

bool
mem_is_field_compatible(const struct Mem *mem, enum field_type type)
{
	if (mem->type == MEM_TYPE_UUID)
		return (field_ext_type[type] & (1U << MP_UUID)) != 0;
	if (mem->type == MEM_TYPE_DEC)
		return (field_ext_type[type] & (1U << MP_DECIMAL)) != 0;
	if (mem->type == MEM_TYPE_DATETIME)
		return (field_ext_type[type] & (1U << MP_DATETIME)) != 0;
	if (mem->type == MEM_TYPE_INTERVAL)
		return (field_ext_type[type] & (1U << MP_INTERVAL)) != 0;
	enum mp_type mp_type = mem_mp_type(mem);
	assert(mp_type != MP_EXT);
	return field_mp_plain_type_is_compatible(type, mp_type, true);
}

int
mem_snprintf(char *buf, uint32_t size, const struct Mem *mem)
{
	int res = -1;
	switch (mem->type) {
	case MEM_TYPE_NULL:
		res = snprintf(buf, size, "NULL");
		break;
	case MEM_TYPE_STR:
	case MEM_TYPE_BIN:
		res = snprintf(buf, size, "%.*s", mem->n, mem->z);
		break;
	case MEM_TYPE_INT:
		res = snprintf(buf, size, "%lld", mem->u.i);
		break;
	case MEM_TYPE_UINT:
		res = snprintf(buf, size, "%llu", (unsigned long long)mem->u.u);
		break;
	case MEM_TYPE_DOUBLE: {
		char str[BUF_SIZE];
		sql_snprintf(BUF_SIZE, str, "%!.15g", mem->u.r);
		res = snprintf(buf, size, "%s", str);
		break;
	}
	case MEM_TYPE_DEC:
		res = snprintf(buf, size, "%s", decimal_str(&mem->u.d));
		break;
	case MEM_TYPE_MAP:
	case MEM_TYPE_ARRAY:
		res = mp_snprint(buf, size, mem->z);
		break;
	case MEM_TYPE_UUID:
		res = snprintf(buf, size, "%s", tt_uuid_str(&mem->u.uuid));
		break;
	case MEM_TYPE_DATETIME: {
		char str[DT_TO_STRING_BUFSIZE];
		datetime_to_string(&mem->u.dt, str, DT_TO_STRING_BUFSIZE);
		res = snprintf(buf, size, "%s", str);
		break;
	}
	case MEM_TYPE_INTERVAL:
		res = interval_to_string(&mem->u.itv, buf, size);
		break;
	case MEM_TYPE_BOOL:
		res = snprintf(buf, size, mem->u.b ? "TRUE" : "FALSE");
		break;
	default:
		unreachable();
	}
	assert(res >= 0);
	return res;
}

char *
mem_strdup(const struct Mem *mem)
{
	int size = mem_snprintf(NULL, 0, mem);
	assert(size >= 0);
	char *str = sqlDbMallocRawNN(sql_get(), size + 1);
	if (str == NULL)
		return NULL;
	mem_snprintf(str, size + 1, mem);
	return str;
}

const char *
mem_str(const struct Mem *mem)
{
	if (mem->type == MEM_TYPE_NULL)
		return "NULL";
	const char *type = mem_type_to_str(mem);
	if (mem->type == MEM_TYPE_STR) {
		if (mem->n <= STR_VALUE_MAX_LEN)
			return tt_sprintf("%s('%.*s')", type, mem->n, mem->z);
		return tt_sprintf("%s('%.*s...)", type, STR_VALUE_MAX_LEN,
				  mem->z);
	}
	char buf[STR_VALUE_MAX_LEN];
	if (mem->type == MEM_TYPE_BIN) {
		int len = MIN(mem->n, STR_VALUE_MAX_LEN / 2);
		for (int i = 0; i < len; ++i) {
			int n = (mem->z[i] & 0xF0) >> 4;
			buf[2 * i] = n < 10 ? ('0' + n) : ('A' + n - 10);
			n = (mem->z[i] & 0x0F);
			buf[2 * i + 1] = n < 10 ? ('0' + n) : ('A' + n - 10);
		}
		if (mem->n > len)
			return tt_sprintf("%s(x'%.*s...)", type, len * 2, buf);
		return tt_sprintf("%s(x'%.*s')", type, len * 2, buf);
	}
	int size = mem_snprintf(buf, STR_VALUE_MAX_LEN, mem);
	if (size <= STR_VALUE_MAX_LEN)
		return tt_sprintf("%s(%s)", type, buf);
	return tt_sprintf("%s(%.*s...)", type, STR_VALUE_MAX_LEN, buf);
}

static const char *
mem_type_class_to_str(const struct Mem *mem)
{
	switch (mem->type) {
	case MEM_TYPE_NULL:
		return "NULL";
	case MEM_TYPE_UINT:
	case MEM_TYPE_INT:
	case MEM_TYPE_DEC:
	case MEM_TYPE_DOUBLE:
		return "number";
	case MEM_TYPE_STR:
		return "string";
	case MEM_TYPE_BIN:
		return "varbinary";
	case MEM_TYPE_BOOL:
		return "boolean";
	case MEM_TYPE_UUID:
		return "uuid";
	case MEM_TYPE_ARRAY:
		return "array";
	case MEM_TYPE_MAP:
		return "map";
	case MEM_TYPE_DATETIME:
		return "datetime";
	case MEM_TYPE_INTERVAL:
		return "interval";
	default:
		break;
	}
	return "unknown";
}

void
mem_create(struct Mem *mem)
{
	mem->type = MEM_TYPE_NULL;
	mem->flags = 0;
	mem->n = 0;
	mem->z = NULL;
	mem->zMalloc = NULL;
	mem->szMalloc = 0;
	mem->uTemp = 0;
	mem->db = sql_get();
#ifdef SQL_DEBUG
	mem->pScopyFrom = NULL;
	mem->pFiller = NULL;
#endif
}

static inline void
mem_clear(struct Mem *mem)
{
	if (mem->type == MEM_TYPE_FRAME) {
		struct VdbeFrame *frame = mem->u.pFrame;
		frame->pParent = frame->v->pDelFrame;
		frame->v->pDelFrame = frame;
	}
	mem->type = MEM_TYPE_NULL;
	mem->flags = 0;
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
}

void
mem_set_uint(struct Mem *mem, uint64_t value)
{
	mem_clear(mem);
	mem->u.u = value;
	mem->type = MEM_TYPE_UINT;
	assert(mem->flags == 0);
}

void
mem_set_nint(struct Mem *mem, int64_t value)
{
	assert(value < 0);
	mem_clear(mem);
	mem->u.i = value;
	mem->type = MEM_TYPE_INT;
	assert(mem->flags == 0);
}

void
mem_set_bool(struct Mem *mem, bool value)
{
	mem_clear(mem);
	mem->u.b = value;
	mem->type = MEM_TYPE_BOOL;
	assert(mem->flags == 0);
}

void
mem_set_double(struct Mem *mem, double value)
{
	mem_clear(mem);
	assert(mem->flags == 0);
	if (sqlIsNaN(value))
		return;
	mem->u.r = value;
	mem->type = MEM_TYPE_DOUBLE;
}

void
mem_set_dec(struct Mem *mem, const decimal_t *d)
{
	mem_clear(mem);
	mem->u.d = *d;
	mem->type = MEM_TYPE_DEC;
	assert(mem->flags == 0);
}

void
mem_set_uuid(struct Mem *mem, const struct tt_uuid *uuid)
{
	mem_clear(mem);
	mem->u.uuid = *uuid;
	mem->type = MEM_TYPE_UUID;
	assert(mem->flags == 0);
}

void
mem_set_datetime(struct Mem *mem, const struct datetime *dt)
{
	mem_clear(mem);
	mem->u.dt = *dt;
	mem->type = MEM_TYPE_DATETIME;
	assert(mem->flags == 0);
}

void
mem_set_interval(struct Mem *mem, const struct interval *itv)
{
	mem_clear(mem);
	mem->u.itv = *itv;
	mem->type = MEM_TYPE_INTERVAL;
	assert(mem->flags == 0);
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
}

static inline void
set_str_dynamic(struct Mem *mem, char *value, uint32_t len, int alloc_type)
{
	assert(mem->szMalloc == 0 || value != mem->zMalloc);
	mem_destroy(mem);
	mem->z = value;
	mem->n = len;
	mem->type = MEM_TYPE_STR;
	mem->flags = alloc_type;
	mem->zMalloc = mem->z;
	mem->szMalloc = sqlDbMallocSize(mem->db, mem->zMalloc);
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
mem_set_str_allocated(struct Mem *mem, char *value, uint32_t len)
{
	set_str_dynamic(mem, value, len, 0);
}

void
mem_set_str0_ephemeral(struct Mem *mem, char *value)
{
	set_str_const(mem, value, strlen(value), MEM_Ephem);
}

void
mem_set_str0_static(struct Mem *mem, char *value)
{
	set_str_const(mem, value, strlen(value), MEM_Static);
}

void
mem_set_str0_allocated(struct Mem *mem, char *value)
{
	set_str_dynamic(mem, value, strlen(value), 0);
}

static int
mem_copy_bytes(struct Mem *mem, const char *value, uint32_t size,
	       enum mem_type type)
{
	if (mem_is_bytes(mem) && mem->z == value) {
		/* Own value, but might be ephemeral. Make it own if so. */
		if (sqlVdbeMemGrow(mem, size, 1) != 0)
			return -1;
		mem->type = type;
		mem->flags = 0;
		return 0;
	}
	mem_clear(mem);
	if (sqlVdbeMemGrow(mem, size, 0) != 0)
		return -1;
	memcpy(mem->z, value, size);
	mem->n = size;
	mem->type = type;
	assert(mem->flags == 0);
	return 0;
}

int
mem_copy_str(struct Mem *mem, const char *value, uint32_t len)
{
	return mem_copy_bytes(mem, value, len, MEM_TYPE_STR);
}

int
mem_copy_str0(struct Mem *mem, const char *value)
{
	uint32_t len = strlen(value);
	if (mem_copy_str(mem, value, len + 1) != 0)
		return -1;
	mem->n = len;
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
}

static inline void
set_bin_dynamic(struct Mem *mem, char *value, uint32_t size, int alloc_type)
{
	assert(mem->szMalloc == 0 || value != mem->zMalloc);
	mem_destroy(mem);
	mem->z = value;
	mem->n = size;
	mem->type = MEM_TYPE_BIN;
	mem->flags = alloc_type;
	mem->zMalloc = mem->z;
	mem->szMalloc = sqlDbMallocSize(mem->db, mem->zMalloc);
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
mem_set_bin_allocated(struct Mem *mem, char *value, uint32_t size)
{
	set_bin_dynamic(mem, value, size, 0);
}

int
mem_copy_bin(struct Mem *mem, const char *value, uint32_t size)
{
	return mem_copy_bytes(mem, value, size, MEM_TYPE_BIN);
}

static inline void
set_msgpack_value(struct Mem *mem, char *value, uint32_t size, int alloc_type,
		  enum mem_type type)
{
	if (alloc_type == MEM_Ephem || alloc_type == MEM_Static)
		set_bin_const(mem, value, size, alloc_type);
	else
		set_bin_dynamic(mem, value, size, alloc_type);
	mem->type = type;
}

void
mem_set_map_ephemeral(struct Mem *mem, char *value, uint32_t size)
{
	assert(mp_typeof(*value) == MP_MAP);
	set_msgpack_value(mem, value, size, MEM_Ephem, MEM_TYPE_MAP);
}

void
mem_set_map_static(struct Mem *mem, char *value, uint32_t size)
{
	assert(mp_typeof(*value) == MP_MAP);
	set_msgpack_value(mem, value, size, MEM_Static, MEM_TYPE_MAP);
}

void
mem_set_map_allocated(struct Mem *mem, char *value, uint32_t size)
{
	assert(mp_typeof(*value) == MP_MAP);
	set_msgpack_value(mem, value, size, 0, MEM_TYPE_MAP);
}

int
mem_copy_map(struct Mem *mem, const char *value, uint32_t size)
{
	return mem_copy_bytes(mem, value, size, MEM_TYPE_MAP);
}

void
mem_set_array_ephemeral(struct Mem *mem, char *value, uint32_t size)
{
	assert(mp_typeof(*value) == MP_ARRAY);
	set_msgpack_value(mem, value, size, MEM_Ephem, MEM_TYPE_ARRAY);
}

void
mem_set_array_static(struct Mem *mem, char *value, uint32_t size)
{
	assert(mp_typeof(*value) == MP_ARRAY);
	set_msgpack_value(mem, value, size, MEM_Static, MEM_TYPE_ARRAY);
}

void
mem_set_array_allocated(struct Mem *mem, char *value, uint32_t size)
{
	assert(mp_typeof(*value) == MP_ARRAY);
	set_msgpack_value(mem, value, size, 0, MEM_TYPE_ARRAY);
}

int
mem_copy_array(struct Mem *mem, const char *value, uint32_t size)
{
	return mem_copy_bytes(mem, value, size, MEM_TYPE_ARRAY);
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
	mem->flags = 0;
	return 0;
}

static inline int
int_to_double_precise(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_INT);
	double d;
	d = (double)mem->u.i;
	if (mem->u.i != (int64_t)d)
		return -1;
	mem->u.r = d;
	mem->type = MEM_TYPE_DOUBLE;
	mem->flags = 0;
	return 0;
}

/**
 * Cast MEM with negative INTEGER to DOUBLE. Doesn't fail. The return value
 * is < 0 if the original value is less than the result, > 0 if the original
 * value is greater than the result, and 0 if the cast is precise.
 */
static inline int
int_to_double_forced(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_INT);
	int64_t i = mem->u.i;
	double d = (double)i;
	mem->u.r = d;
	mem->type = MEM_TYPE_DOUBLE;
	mem->flags = 0;
	return CMP_OLD_NEW(i, d, int64_t);
}

static inline int
int_to_dec(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_INT);
	int64_t i = mem->u.i;
	decimal_from_int64(&mem->u.d, i);
	mem->type = MEM_TYPE_DEC;
	mem->flags = 0;
	return 0;
}

static inline int
uint_to_double_precise(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_UINT);
	double d;
	d = (double)mem->u.u;
	if (d == (double)UINT64_MAX || mem->u.u != (uint64_t)d)
		return -1;
	mem->u.r = d;
	mem->flags = 0;
	mem->type = MEM_TYPE_DOUBLE;
	return 0;
}

/**
 * Cast MEM with positive INTEGER to DOUBLE. Doesn't fail. The return value
 * is < 0 if the original value is less than the result, > 0 if the original
 * value is greater than the result, and 0 if the cast is precise.
 */
static inline int
uint_to_double_forced(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_UINT);
	uint64_t u = mem->u.u;
	double d = (double)u;
	mem->u.r = d;
	mem->flags = 0;
	mem->type = MEM_TYPE_DOUBLE;
	return CMP_OLD_NEW(u, d, uint64_t);
}

static inline int
uint_to_dec(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_UINT);
	int64_t u = mem->u.u;
	decimal_from_uint64(&mem->u.d, u);
	mem->type = MEM_TYPE_DEC;
	mem->flags = 0;
	return 0;
}

static inline int
int_to_str0(struct Mem *mem)
{
	assert((mem->type & (MEM_TYPE_INT | MEM_TYPE_UINT)) != 0);
	const char *str;
	if (mem->type == MEM_TYPE_UINT)
		str = tt_sprintf("%llu", (unsigned long long)mem->u.u);
	else
		str = tt_sprintf("%lld", (long long)mem->u.i);
	return mem_copy_str0(mem, str);
}

static inline int
str_to_bin(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_STR);
	mem->type = MEM_TYPE_BIN;
	mem->flags &= ~(MEM_Scalar | MEM_Any);
	return 0;
}

static inline int
str_to_uuid(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_STR);
	struct tt_uuid uuid;
	if (tt_uuid_from_strl(mem->z, mem->n, &uuid) != 0)
		return -1;
	mem_set_uuid(mem, &uuid);
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
	mem->type = MEM_TYPE_STR;
	mem->flags &= ~(MEM_Scalar | MEM_Any);
	return 0;
}

static inline int
bin_to_uuid(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_BIN);
	if (mem->n != UUID_LEN ||
	    tt_uuid_validate((struct tt_uuid *)mem->z) != 0)
		return -1;
	mem_set_uuid(mem, (struct tt_uuid *)mem->z);
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
str_to_dec(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_STR);
	decimal_t dec;
	decimal_t *d;
	d = decimal_from_string(&dec, mem->z);
	if (d == NULL)
		return -1;
	mem_set_dec(mem, &dec);
	return 0;
}

/** Convert MEM from STRING to DATETIME. */
static inline int
str_to_datetime(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_STR);
	struct datetime dt;
	if (datetime_parse_full(&dt, mem->z, mem->n, NULL, 0) <= 0)
		return -1;
	mem_set_datetime(mem, &dt);
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
		mem->flags = 0;
		return 0;
	}
	if (d > -1.0 && d < (double)UINT64_MAX) {
		mem->u.u = (uint64_t)d;
		mem->type = MEM_TYPE_UINT;
		mem->flags = 0;
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
		mem->flags = 0;
		return 0;
	}
	if (d > -1.0 && d < (double)UINT64_MAX && (double)(uint64_t)d == d) {
		mem->u.u = (uint64_t)d;
		mem->type = MEM_TYPE_UINT;
		mem->flags = 0;
		return 0;
	}
	return -1;
}

/**
 * Cast MEM with DOUBLE to INTEGER. Doesn't fail. The return value is < 0 if the
 * original value is less than the result, > 0 if the original value is greater
 * than the result, and 0 if the cast is precise.
 */
static inline int
double_to_int_forced(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_DOUBLE);
	double d = mem->u.r;
	int64_t i;
	enum mem_type type;
	int res;
	if (d < (double)INT64_MIN) {
		i = INT64_MIN;
		type = MEM_TYPE_INT;
		res = -1;
	} else if (d >= (double)UINT64_MAX) {
		i = (int64_t)UINT64_MAX;
		type = MEM_TYPE_UINT;
		res = 1;
	} else if (d <= -1.0) {
		i = (int64_t)d;
		type = MEM_TYPE_INT;
		res = CMP_OLD_NEW(d, i, double);
	} else {
		uint64_t u = (uint64_t)d;
		i = (int64_t)u;
		type = MEM_TYPE_UINT;
		res = CMP_OLD_NEW(d, u, double);
	}
	mem->u.i = i;
	mem->type = type;
	mem->flags = 0;
	return res;
}

static inline int
double_to_uint(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_DOUBLE);
	double d = mem->u.r;
	if (d > -1.0 && d < (double)UINT64_MAX) {
		mem->u.u = (uint64_t)d;
		mem->type = MEM_TYPE_UINT;
		mem->flags = 0;
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
		mem->flags = 0;
		return 0;
	}
	return -1;
}

/**
 * Cast MEM with DOUBLE to UNSIGNED. Doesn't fail. The return value is < 0 if
 * the original value is less than the result, > 0 if the original value is
 * greater than the result, and 0 if the cast is precise.
 */
static inline int
double_to_uint_forced(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_DOUBLE);
	double d = mem->u.r;
	uint64_t u;
	int res;
	if (d < 0.0) {
		u = 0;
		res = -1;
	} else if (d >= (double)UINT64_MAX) {
		u = UINT64_MAX;
		res = 1;
	} else {
		u = (uint64_t)d;
		res = CMP_OLD_NEW(d, u, double);
	}
	mem->u.u = u;
	mem->type = MEM_TYPE_UINT;
	mem->flags = 0;
	return res;
}

static inline int
double_to_dec(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_DOUBLE);
	double d = mem->u.r;
	decimal_t dec;
	if (decimal_from_double(&dec, d) == NULL)
		return -1;
	mem->u.d = dec;
	mem->type = MEM_TYPE_DEC;
	mem->flags = 0;
	return 0;
}

static inline int
double_to_dec_precise(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_DOUBLE);
	double d = mem->u.r;
	decimal_t dec;
	if (decimal_from_double(&dec, d) == NULL ||
	    atof(decimal_str(&dec)) != d)
		return -1;
	mem->u.d = dec;
	mem->type = MEM_TYPE_DEC;
	mem->flags = 0;
	return 0;
}

/**
 * Cast MEM with DOUBLE to DECIMAL. Doesn't fail. The return value is < 0 if
 * the original value is less than the result, > 0 if the original value is
 * greater than the result, and 0 if the cast is precise.
 */
static inline int
double_to_dec_forced(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_DOUBLE);
	double d = mem->u.r;
	mem->type = MEM_TYPE_DEC;
	mem->flags = 0;
	if (d >= 1e38) {
		const char *val = "99999999999999999999999999999999999999";
		assert(strlen(val) == 38);
		decimal_from_string(&mem->u.d, val);
		return 1;
	}
	if (d <= -1e38) {
		const char *val = "-99999999999999999999999999999999999999";
		assert(strlen(val) == 39);
		decimal_from_string(&mem->u.d, val);
		return -1;
	}
	decimal_from_double(&mem->u.d, d);
	double tmp = atof(decimal_str(&mem->u.d));
	if (d > tmp)
		return 1;
	if (d < tmp)
		return -1;
	return 0;
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
	mem->flags = 0;
	return 0;
}

static inline int
dec_to_int(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_DEC);
	if (decimal_is_neg(&mem->u.d)) {
		int64_t i;
		if (decimal_to_int64(&mem->u.d, &i) == NULL)
			return -1;
		assert(i <= 0);
		mem->u.i = i;
		mem->type = i == 0 ? MEM_TYPE_UINT : MEM_TYPE_INT;
		mem->flags = 0;
		return 0;
	}
	uint64_t u;
	if (decimal_to_uint64(&mem->u.d, &u) == NULL)
		return -1;
	mem->u.u = u;
	mem->type = MEM_TYPE_UINT;
	mem->flags = 0;
	return 0;
}

static inline int
dec_to_int_precise(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_DEC);
	if (!decimal_is_int(&mem->u.d))
		return -1;
	return dec_to_int(mem);
}

static inline int
dec_to_int_forced(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_DEC);
	bool is_dec_int = decimal_is_int(&mem->u.d);
	if (decimal_is_neg(&mem->u.d)) {
		int64_t i;
		mem->flags = 0;
		if (decimal_to_int64(&mem->u.d, &i) == NULL) {
			mem->u.i = INT64_MIN;
			mem->type = MEM_TYPE_INT;
			return -1;
		}
		assert(i <= 0);
		mem->u.i = i;
		mem->type = i == 0 ? MEM_TYPE_UINT : MEM_TYPE_INT;
		/*
		 * Decimal is floored when cast to int, which means that after
		 * cast it becomes bigger if it was not integer.
		 */
		return is_dec_int ? 0 : -1;
	}
	uint64_t u;
	mem->type = MEM_TYPE_UINT;
	mem->flags = 0;
	if (decimal_to_uint64(&mem->u.d, &u) == NULL) {
		mem->u.u = UINT64_MAX;
		return 1;
	}
	mem->u.u = u;
	/*
	 * Decimal is floored when cast to uint, which means that after cast it
	 * becomes less if it was not integer.
	 */
	return is_dec_int ? 0 : 1;
}

static inline int
dec_to_uint(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_DEC);
	uint64_t u;
	if (decimal_to_uint64(&mem->u.d, &u) == NULL)
		return -1;
	mem->u.u = u;
	mem->type = MEM_TYPE_UINT;
	mem->flags = 0;
	return 0;
}

static inline int
dec_to_uint_precise(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_DEC);
	if (!decimal_is_int(&mem->u.d))
		return -1;
	return dec_to_uint(mem);
}

static inline int
dec_to_uint_forced(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_DEC);
	uint64_t u;
	mem->type = MEM_TYPE_UINT;
	mem->flags = 0;
	if (decimal_to_uint64(&mem->u.d, &u) == NULL) {
		if (decimal_is_neg(&mem->u.d)) {
			mem->u.u = 0;
			return -1;
		}
		mem->u.u = UINT64_MAX;
		return 1;
	}
	mem->u.u = u;
	/*
	 * Decimal is floored when cast to uint, which means that after cast if
	 * it was not integer it becomes less if it was positive, and move if it
	 * was negative. For example, DECIMAL value -1.5 becomes -1 after cast
	 * to INTEGER and DECIMAL value 1.5 becomes 1 after cast to INTEGER.
	 */
	if (decimal_is_int(&mem->u.d))
		return 0;
	return decimal_is_neg(&mem->u.d) ? -1 : 1;
}

static inline int
dec_to_double(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_DEC);
	double r = atof(decimal_str(&mem->u.d));
	mem->u.r = r;
	mem->type = MEM_TYPE_DOUBLE;
	mem->flags = 0;
	return 0;
}

static inline int
dec_to_double_precise(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_DEC);
	double r = atof(decimal_str(&mem->u.d));
	decimal_t d;
	decimal_t *dec = decimal_from_double(&d, r);
	if (dec == NULL || decimal_compare(dec, &mem->u.d) != 0)
		return -1;
	mem->u.r = r;
	mem->type = MEM_TYPE_DOUBLE;
	mem->flags = 0;
	return 0;
}

static inline int
dec_to_double_forced(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_DEC);
	mem->type = MEM_TYPE_DOUBLE;
	mem->flags = 0;
	double r = atof(decimal_str(&mem->u.d));
	int res;
	decimal_t d;
	if (r <= -1e38)
		res = 1;
	else if (r >= 1e38)
		res = -1;
	else
		res = decimal_compare(&mem->u.d, decimal_from_double(&d, r));
	mem->u.r = r;
	return res;
}

static inline int
dec_to_str0(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_DEC);
	return mem_copy_str0(mem, decimal_str(&mem->u.d));
}

static inline int
bool_to_str0(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_BOOL);
	const char *str = mem->u.b ? "TRUE" : "FALSE";
	return mem_copy_str0(mem, str);
}

static inline int
uuid_to_str0(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_UUID);
	char buf[UUID_STR_LEN + 1];
	tt_uuid_to_string(&mem->u.uuid, &buf[0]);
	return mem_copy_str0(mem, &buf[0]);
}

/** Convert MEM from DATETIME to STRING. */
static inline int
datetime_to_str0(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_DATETIME);
	char buf[DT_TO_STRING_BUFSIZE];
	uint32_t len = datetime_to_string(&mem->u.dt, buf,
					  DT_TO_STRING_BUFSIZE);
	assert(len == strlen(buf));
	return mem_copy_str(mem, buf, len);
}

/** Convert MEM from INTERVAL to STRING. */
static inline int
interval_to_str0(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_INTERVAL);
	char buf[DT_IVAL_TO_STRING_BUFSIZE];
	uint32_t len = interval_to_string(&mem->u.itv, buf,
					  DT_IVAL_TO_STRING_BUFSIZE);
	assert(len == strlen(buf));
	return mem_copy_str(mem, buf, len);
}

static inline int
uuid_to_bin(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_UUID);
	return mem_copy_bin(mem, (char *)&mem->u.uuid, UUID_LEN);
}

/** Convert MEM from MAP to DATETIME. */
static inline int
map_to_datetime(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_MAP);
	struct datetime dt;
	if (datetime_from_map(&dt, mem->z) != 0)
		return -1;
	mem_set_datetime(mem, &dt);
	return 0;
}

/** Convert MEM from MAP to INTERVAL. */
static inline int
map_to_interval(struct Mem *mem)
{
	assert(mem->type == MEM_TYPE_MAP);
	struct interval itv;
	if (interval_from_map(&itv, mem->z) != 0)
		return -1;
	mem_set_interval(mem, &itv);
	return 0;
}

int
mem_to_int(struct Mem *mem)
{
	assert(mem->type < MEM_TYPE_INVALID);
	if ((mem->type & (MEM_TYPE_INT | MEM_TYPE_UINT)) != 0) {
		mem->flags = 0;
		return 0;
	}
	if (mem->type == MEM_TYPE_STR)
		return str_to_int(mem);
	if (mem->type == MEM_TYPE_DOUBLE)
		return double_to_int(mem);
	if (mem->type == MEM_TYPE_DEC)
		return dec_to_int(mem);
	return -1;
}

int
mem_to_int_precise(struct Mem *mem)
{
	assert(mem->type < MEM_TYPE_INVALID);
	if ((mem->type & (MEM_TYPE_INT | MEM_TYPE_UINT)) != 0) {
		mem->flags = 0;
		return 0;
	}
	if (mem->type == MEM_TYPE_STR)
		return str_to_int(mem);
	if (mem->type == MEM_TYPE_DOUBLE)
		return double_to_int_precise(mem);
	if (mem->type == MEM_TYPE_DEC)
		return dec_to_int_precise(mem);
	return -1;
}

int
mem_to_double(struct Mem *mem)
{
	assert(mem->type < MEM_TYPE_INVALID);
	if (mem->type == MEM_TYPE_DOUBLE) {
		mem->flags = 0;
		return 0;
	}
	if ((mem->type & (MEM_TYPE_INT | MEM_TYPE_UINT)) != 0)
		return int_to_double(mem);
	if (mem->type == MEM_TYPE_STR)
		return str_to_double(mem);
	if (mem->type == MEM_TYPE_DEC)
		return dec_to_double(mem);
	return -1;
}

int
mem_to_number(struct Mem *mem)
{
	assert(mem->type < MEM_TYPE_INVALID);
	if (mem_is_num(mem)) {
		mem->flags = MEM_Number;
		return 0;
	}
	if (mem->type == MEM_TYPE_STR) {
		if (str_to_int(mem) != 0 && str_to_double(mem) != 0)
			return -1;
		mem->flags = MEM_Number;
		return 0;
	}
	return -1;
}

int
mem_to_str(struct Mem *mem)
{
	assert(mem->type < MEM_TYPE_INVALID);
	switch (mem->type) {
	case MEM_TYPE_STR:
		mem->flags &= ~(MEM_Scalar | MEM_Any);
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
	case MEM_TYPE_UUID:
		return uuid_to_str0(mem);
	case MEM_TYPE_DEC:
		return dec_to_str0(mem);
	case MEM_TYPE_DATETIME:
		return datetime_to_str0(mem);
	case MEM_TYPE_INTERVAL:
		return interval_to_str0(mem);
	default:
		return -1;
	}
}

int
mem_cast_explicit(struct Mem *mem, enum field_type type)
{
	if (mem->type == MEM_TYPE_NULL)
		return 0;
	switch (type) {
	case FIELD_TYPE_UNSIGNED:
		switch (mem->type) {
		case MEM_TYPE_UINT:
			mem->flags = 0;
			return 0;
		case MEM_TYPE_STR:
			return str_to_uint(mem);
		case MEM_TYPE_DOUBLE:
			return double_to_uint(mem);
		case MEM_TYPE_DEC:
			return dec_to_uint(mem);
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
			mem->flags = 0;
			return 0;
		case MEM_TYPE_STR:
			return str_to_bool(mem);
		default:
			return -1;
		}
	case FIELD_TYPE_VARBINARY:
		if (mem->type == MEM_TYPE_STR)
			return str_to_bin(mem);
		if (mem_is_bin(mem)) {
			mem->flags &= ~(MEM_Scalar | MEM_Any);
			return 0;
		}
		if (mem->type == MEM_TYPE_UUID)
			return uuid_to_bin(mem);
		return -1;
	case FIELD_TYPE_NUMBER:
		return mem_to_number(mem);
	case FIELD_TYPE_DECIMAL:
		switch (mem->type) {
		case MEM_TYPE_INT:
			return int_to_dec(mem);
		case MEM_TYPE_UINT:
			return uint_to_dec(mem);
		case MEM_TYPE_STR:
			return str_to_dec(mem);
		case MEM_TYPE_DOUBLE:
			return double_to_dec(mem);
		case MEM_TYPE_DEC:
			mem->flags = 0;
			return 0;
		default:
			return -1;
		}
	case FIELD_TYPE_UUID:
		if (mem->type == MEM_TYPE_UUID) {
			mem->flags = 0;
			return 0;
		}
		if (mem->type == MEM_TYPE_STR)
			return str_to_uuid(mem);
		if (mem->type == MEM_TYPE_BIN)
			return bin_to_uuid(mem);
		return -1;
	case FIELD_TYPE_DATETIME:
		if (mem->type == MEM_TYPE_STR)
			return str_to_datetime(mem);
		if (mem->type == MEM_TYPE_MAP)
			return map_to_datetime(mem);
		if (mem->type != MEM_TYPE_DATETIME)
			return -1;
		mem->flags = 0;
		return 0;
	case FIELD_TYPE_INTERVAL:
		if (mem->type == MEM_TYPE_MAP)
			return map_to_interval(mem);
		if (mem->type != MEM_TYPE_INTERVAL)
			return -1;
		mem->flags = 0;
		return 0;
	case FIELD_TYPE_ARRAY:
		if (mem->type != MEM_TYPE_ARRAY)
			return -1;
		mem->flags &= ~MEM_Any;
		return 0;
	case FIELD_TYPE_MAP:
		if (mem->type != MEM_TYPE_MAP)
			return -1;
		mem->flags &= ~MEM_Any;
		return 0;
	case FIELD_TYPE_SCALAR:
		if ((mem->type &
		     (MEM_TYPE_MAP | MEM_TYPE_ARRAY | MEM_TYPE_INTERVAL)) != 0)
			return -1;
		mem->flags |= MEM_Scalar;
		mem->flags &= ~(MEM_Number | MEM_Any);
		return 0;
	case FIELD_TYPE_ANY:
		mem->flags |= MEM_Any;
		mem->flags &= ~(MEM_Number | MEM_Scalar);
		return 0;
	default:
		break;
	}
	return -1;
}

int
mem_cast_implicit(struct Mem *mem, enum field_type type)
{
	if (mem->type == MEM_TYPE_NULL || type == field_type_MAX)
		return 0;
	if (mem_is_metatype(mem) && type != FIELD_TYPE_ANY) {
		if ((mem->flags & MEM_Number) != 0) {
			if (type != FIELD_TYPE_NUMBER &&
			    type != FIELD_TYPE_SCALAR &&
			    type != FIELD_TYPE_ANY)
				return -1;
		} else if ((mem->flags & MEM_Scalar) != 0) {
			if (type != FIELD_TYPE_SCALAR &&
			    type != FIELD_TYPE_ANY)
				return -1;
		} else {
			if (type != FIELD_TYPE_ANY)
				return -1;
		}
	}
	switch (type) {
	case FIELD_TYPE_UNSIGNED:
		if (mem->type == MEM_TYPE_UINT) {
			mem->flags = 0;
			return 0;
		}
		if (mem->type == MEM_TYPE_DOUBLE)
			return double_to_uint_precise(mem);
		if (mem->type == MEM_TYPE_DEC)
			return dec_to_uint_precise(mem);
		return -1;
	case FIELD_TYPE_STRING:
		if (mem->type == MEM_TYPE_STR) {
			mem->flags &= ~(MEM_Scalar | MEM_Any);
			return 0;
		}
		return -1;
	case FIELD_TYPE_DOUBLE:
		if (mem->type == MEM_TYPE_DOUBLE) {
			mem->flags = 0;
			return 0;
		}
		if (mem->type == MEM_TYPE_INT)
			return int_to_double_precise(mem);
		if (mem->type == MEM_TYPE_UINT)
			return uint_to_double_precise(mem);
		if (mem->type == MEM_TYPE_DEC)
			return dec_to_double_precise(mem);
		return -1;
	case FIELD_TYPE_INTEGER:
		if ((mem->type & (MEM_TYPE_INT | MEM_TYPE_UINT)) != 0) {
			mem->flags = 0;
			return 0;
		}
		if (mem->type == MEM_TYPE_DOUBLE)
			return double_to_int_precise(mem);
		if (mem->type == MEM_TYPE_DEC)
			return dec_to_int_precise(mem);
		return -1;
	case FIELD_TYPE_BOOLEAN:
		if (mem->type == MEM_TYPE_BOOL) {
			mem->flags = 0;
			return 0;
		}
		return -1;
	case FIELD_TYPE_VARBINARY:
		if (mem->type == MEM_TYPE_BIN) {
			mem->flags &= ~(MEM_Scalar | MEM_Any);
			return 0;
		}
		return -1;
	case FIELD_TYPE_NUMBER:
		if (!mem_is_num(mem))
			return -1;
		mem->flags = MEM_Number;
		return 0;
	case FIELD_TYPE_DECIMAL:
		switch (mem->type) {
		case MEM_TYPE_INT:
			return int_to_dec(mem);
		case MEM_TYPE_UINT:
			return uint_to_dec(mem);
		case MEM_TYPE_DOUBLE:
			return double_to_dec_precise(mem);
		case MEM_TYPE_DEC:
			mem->flags = 0;
			return 0;
		default:
			return -1;
		}
	case FIELD_TYPE_MAP:
		if (mem->type == MEM_TYPE_MAP)
			return 0;
		return -1;
	case FIELD_TYPE_ARRAY:
		if (mem->type == MEM_TYPE_ARRAY)
			return 0;
		return -1;
	case FIELD_TYPE_SCALAR:
		if ((mem->type &
		     (MEM_TYPE_MAP | MEM_TYPE_ARRAY | MEM_TYPE_INTERVAL)) != 0)
			return -1;
		mem->flags |= MEM_Scalar;
		mem->flags &= ~(MEM_Number | MEM_Any);
		return 0;
	case FIELD_TYPE_UUID:
		if (mem->type != MEM_TYPE_UUID)
			return -1;
		mem->flags = 0;
		return 0;
	case FIELD_TYPE_DATETIME:
		if (mem->type != MEM_TYPE_DATETIME)
			return -1;
		mem->flags = 0;
		return 0;
	case FIELD_TYPE_INTERVAL:
		if (mem->type != MEM_TYPE_INTERVAL)
			return -1;
		mem->flags = 0;
		return 0;
	case FIELD_TYPE_ANY:
		mem->flags |= MEM_Any;
		mem->flags &= ~(MEM_Number | MEM_Scalar);
		return 0;
	default:
		break;
	}
	return -1;
}

int
mem_cast_implicit_number(struct Mem *mem, enum field_type type)
{
	assert(mem_is_num(mem) && sql_type_is_numeric(type));
	switch (type) {
	case FIELD_TYPE_UNSIGNED:
		switch (mem->type) {
		case MEM_TYPE_UINT:
			mem->flags = 0;
			return 0;
		case MEM_TYPE_INT:
			mem->u.u = 0;
			mem->flags = 0;
			mem->type = MEM_TYPE_UINT;
			return -1;
		case MEM_TYPE_DOUBLE:
			return double_to_uint_forced(mem);
		case MEM_TYPE_DEC:
			return dec_to_uint_forced(mem);
		default:
			unreachable();
		}
		break;
	case FIELD_TYPE_DOUBLE:
		switch (mem->type) {
		case MEM_TYPE_INT:
			return int_to_double_forced(mem);
		case MEM_TYPE_UINT:
			return uint_to_double_forced(mem);
		case MEM_TYPE_DEC:
			return dec_to_double_forced(mem);
		case MEM_TYPE_DOUBLE:
			mem->flags = 0;
			return 0;
		default:
			unreachable();
		}
		break;
	case FIELD_TYPE_INTEGER:
		switch (mem->type) {
		case MEM_TYPE_UINT:
		case MEM_TYPE_INT:
			mem->flags = 0;
			return 0;
		case MEM_TYPE_DOUBLE:
			return double_to_int_forced(mem);
		case MEM_TYPE_DEC:
			return dec_to_int_forced(mem);
		default:
			unreachable();
		}
		break;
	case FIELD_TYPE_DECIMAL:
		switch (mem->type) {
		case MEM_TYPE_INT:
			return int_to_dec(mem);
		case MEM_TYPE_UINT:
			return uint_to_dec(mem);
		case MEM_TYPE_DEC:
			mem->flags = 0;
			return 0;
		case MEM_TYPE_DOUBLE:
			return double_to_dec_forced(mem);
		default:
			unreachable();
		}
		break;
	default:
		unreachable();
	}
	return 0;
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
	if (mem->type == MEM_TYPE_DEC) {
		if (decimal_is_neg(&mem->u.d)) {
			if (decimal_to_int64(&mem->u.d, i) == NULL)
				return -1;
			*is_neg = *i < 0;
			return 0;
		}
		if (decimal_to_uint64(&mem->u.d, (uint64_t *)i) == NULL)
			return -1;
		*is_neg = false;
		return 0;
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
	if (mem->type == MEM_TYPE_DEC) {
		if (decimal_is_neg(&mem->u.d)) {
			int64_t i;
			if (decimal_to_int64(&mem->u.d, &i) == NULL || i < 0)
				return -1;
			assert(i == 0);
			*u = 0;
			return 0;
		}
		if (decimal_to_uint64(&mem->u.d, u) == NULL)
			return -1;
		return 0;
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
	if (mem->type == MEM_TYPE_DEC) {
		*d = atof(decimal_str(&mem->u.d));
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
mem_get_dec(const struct Mem *mem, decimal_t *d)
{
	if (mem->type == MEM_TYPE_DOUBLE) {
		if (decimal_from_double(d, mem->u.r) == NULL)
			return -1;
		return 0;
	}
	if (mem->type == MEM_TYPE_INT) {
		decimal_from_int64(d, mem->u.r);
		return 0;
	}
	if (mem->type == MEM_TYPE_UINT) {
		decimal_from_int64(d, mem->u.u);
		return 0;
	}
	if (mem->type == MEM_TYPE_DEC) {
		*d = mem->u.d;
		return 0;
	}
	if (mem->type == MEM_TYPE_STR) {
		if (decimal_from_string(d, tt_cstr(mem->z, mem->n)) == NULL)
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
mem_get_bin(const struct Mem *mem, const char **s)
{
	if (mem->type == MEM_TYPE_STR) {
		*s = mem->n > 0 ? mem->z : NULL;
		return 0;
	}
	if (mem->type != MEM_TYPE_BIN)
		return -1;
	*s = mem->z;
	return 0;
}

int
mem_len(const struct Mem *mem, uint32_t *len)
{
	if (!mem_is_bytes(mem))
		return -1;
	*len = mem->n;
	return 0;
}

int
mem_copy(struct Mem *to, const struct Mem *from)
{
	mem_clear(to);
	to->u = from->u;
	to->type = from->type;
	to->flags = from->flags;
	to->n = from->n;
	to->z = from->z;
	if (!mem_is_bytes(to))
		return 0;
	if ((to->flags & MEM_Static) != 0)
		return 0;
	to->zMalloc = sqlDbRealloc(to->db, to->zMalloc, MAX(32, to->n));
	assert(to->zMalloc != NULL || sql_get()->mallocFailed != 0);
	if (to->zMalloc == NULL)
		return -1;
	to->szMalloc = sqlDbMallocSize(to->db, to->zMalloc);
	memcpy(to->zMalloc, to->z, to->n);
	to->z = to->zMalloc;
	to->flags = 0;
	return 0;
}

void
mem_copy_as_ephemeral(struct Mem *to, const struct Mem *from)
{
	mem_clear(to);
	to->u = from->u;
	to->type = from->type;
	to->flags = from->flags;
	to->n = from->n;
	to->z = from->z;
	if (!mem_is_bytes(to))
		return;
	if ((to->flags & (MEM_Static | MEM_Ephem)) != 0)
		return;
	to->flags = 0;
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

int
mem_append(struct Mem *mem, const char *value, uint32_t len)
{
	assert((mem->type & (MEM_TYPE_BIN | MEM_TYPE_STR)) != 0);
	if (len == 0)
		return 0;
	int new_size = mem->n + len;
	if (((mem->flags & (MEM_Static | MEM_Ephem)) != 0) ||
	    mem->szMalloc < new_size) {
		/*
		 * Force exponential buffer size growth to avoid having to call
		 * this routine too often.
		 */
		if (sqlVdbeMemGrow(mem, new_size + mem->n, 1) != 0)
			return -1;
	}
	memcpy(&mem->z[mem->n], value, len);
	mem->n = new_size;
	return 0;
}

int
mem_concat(const struct Mem *a, const struct Mem *b, struct Mem *result)
{
	if (mem_is_any_null(a, b)) {
		mem_set_null(result);
		return 0;
	}
	/* Concatenation operation can be applied only to strings and blobs. */
	if (((b->type & (MEM_TYPE_STR | MEM_TYPE_BIN)) == 0) ||
	    mem_is_metatype(b)) {
		diag_set(ClientError, ER_INCONSISTENT_TYPES,
			 "string or varbinary", mem_str(b));
		return -1;
	}
	if (((a->type & (MEM_TYPE_STR | MEM_TYPE_BIN)) == 0) ||
	    mem_is_metatype(a)) {
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

	uint32_t size = a->n + b->n;
	if ((int)size > sql_get()->aLimit[SQL_LIMIT_LENGTH]) {
		diag_set(ClientError, ER_SQL_EXECUTE, "string or blob too big");
		return -1;
	}
	if (sqlVdbeMemGrow(result, size, result == a) != 0)
		return -1;

	result->type = a->type;
	result->flags = 0;
	if (result != a)
		memcpy(result->z, a->z, a->n);
	memcpy(&result->z[a->n], b->z, b->n);
	result->n = size;
	return 0;
}

static inline int
check_types_numeric_arithmetic(const struct Mem *a, const struct Mem *b)
{
	if (!mem_is_num(a) || mem_is_metatype(a)) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(a),
			 "integer, decimal or double");
		return -1;
	}
	if (!mem_is_num(b) || mem_is_metatype(b)) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(b),
			 "integer, decimal or double");
		return -1;
	}
	return 0;
}

/**
 * Add the first MEM to the second MEM and write the result to the third MEM.
 * The first and the second MEMs should be of numeric types. The result is of
 * numeric type.
 */
static int
mem_add_num(const struct Mem *left, const struct Mem *right, struct Mem *result)
{
	assert(mem_is_num(left) && !mem_is_metatype(left));
	if (!mem_is_num(right) || mem_is_metatype(right)) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(right),
			 "integer, decimal or double");
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
	if (((left->type | right->type) & MEM_TYPE_DEC) != 0) {
		decimal_t a;
		decimal_t b;
		decimal_t res;
		mem_get_dec(left, &a);
		mem_get_dec(right, &b);
		if (decimal_add(&res, &a, &b) == NULL) {
			diag_set(ClientError, ER_SQL_EXECUTE,
				 "decimal is overflowed");
			return -1;
		}
		mem_set_dec(result, &res);
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

/**
 * Add the first MEM to the second MEM and write the result to the third MEM.
 * The first MEM should be of DATETIME type and the second MEMs should be of
 * INTERVAL type. The result is of DATETIME type.
 */
static int
mem_add_dt(const struct Mem *left, const struct Mem *right, struct Mem *result)
{
	assert(mem_is_datetime(left) && !mem_is_metatype(left));
	if (!mem_is_interval(right) || mem_is_metatype(right)) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(right),
			 "interval");
		return -1;
	}
	mem_set_datetime(result, &left->u.dt);
	return datetime_increment_by(&result->u.dt, 1, &right->u.itv);
}

/**
 * Add the first MEM to the second MEM and write the result to the third MEM.
 * The first MEM should be of INTERVAL type and the second MEMs should be of
 * INTERVAL or DATETIME type. The result is of the same type as the second
 * argument.
 */
static int
mem_add_itv(const struct Mem *left, const struct Mem *right, struct Mem *result)
{
	assert(mem_is_interval(left) && !mem_is_metatype(left));
	if (mem_is_datetime(right) && !mem_is_metatype(right))
		return mem_add_dt(right, left, result);
	if (!mem_is_interval(right) || mem_is_metatype(right)) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(right),
			 "datetime or interval");
		return -1;
	}
	mem_set_interval(result, &left->u.itv);
	return interval_interval_add(&result->u.itv, &right->u.itv);
}

int
mem_add(const struct Mem *left, const struct Mem *right, struct Mem *result)
{
	if (mem_is_any_null(left, right)) {
		mem_set_null(result);
		return 0;
	}
	if (!mem_is_metatype(left)) {
		if (mem_is_num(left))
			return mem_add_num(left, right, result);
		if (mem_is_datetime(left))
			return mem_add_dt(left, right, result);
		if (mem_is_interval(left))
			return mem_add_itv(left, right, result);
	}
	diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(left),
		 "integer, decimal, double, datetime or interval");
	return -1;
}

/**
 * Subtract the second MEM from the first MEM and write the result to the third
 * MEM. The first and the second MEMs should be of numeric types. The result is
 * of numeric type.
 */
static int
mem_sub_num(const struct Mem *left, const struct Mem *right, struct Mem *result)
{
	assert(mem_is_num(left) && !mem_is_metatype(left));
	if (!mem_is_num(right) || mem_is_metatype(right)) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(right),
			 "integer, decimal or double");
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
	if (((left->type | right->type) & MEM_TYPE_DEC) != 0) {
		decimal_t a;
		decimal_t b;
		decimal_t res;
		mem_get_dec(left, &a);
		mem_get_dec(right, &b);
		if (decimal_sub(&res, &a, &b) == NULL) {
			diag_set(ClientError, ER_SQL_EXECUTE,
				 "decimal is overflowed");
			return -1;
		}
		mem_set_dec(result, &res);
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

/**
 * Subtract the second MEM from the first MEM and write the result to the third
 * MEM. The first MEM should be of DATETIME type and the second MEMs should be
 * of INTERVAL or DATETIME type. The result is of INTERVAL type if the second
 * MEM is of DATETIME type and the result if of DATETIME type if the second
 * argument if of INTERVAL type.
 */
static int
mem_sub_dt(const struct Mem *left, const struct Mem *right, struct Mem *result)
{
	assert(mem_is_datetime(left) && !mem_is_metatype(left));
	if (mem_is_datetime(right) && !mem_is_metatype(right)) {
		struct interval res;
		memset(&res, 0, sizeof(res));
		mem_set_interval(result, &res);
		return datetime_datetime_sub(&result->u.itv, &left->u.dt,
					     &right->u.dt);
	}

	if (!mem_is_interval(right) || mem_is_metatype(right)) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(right),
			 "datetime or interval");
		return -1;
	}
	mem_set_datetime(result, &left->u.dt);
	return datetime_increment_by(&result->u.dt, -1, &right->u.itv);
}

/**
 * Subtract the second MEM from the first MEM and write the result to the third
 * MEM. The first and the second MEMs should be of INTERVAL types. The result is
 * of INTERVAL type.
 */
static int
mem_sub_itv(const struct Mem *left, const struct Mem *right, struct Mem *result)
{
	assert(mem_is_interval(left) && !mem_is_metatype(left));
	if (!mem_is_interval(right) || mem_is_metatype(right)) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(right),
			 "interval");
		return -1;
	}
	mem_set_interval(result, &left->u.itv);
	return interval_interval_sub(&result->u.itv, &right->u.itv);
}

int
mem_sub(const struct Mem *left, const struct Mem *right, struct Mem *result)
{
	if (mem_is_any_null(left, right)) {
		mem_set_null(result);
		return 0;
	}
	if (!mem_is_metatype(left)) {
		if (mem_is_num(left))
			return mem_sub_num(left, right, result);
		if (mem_is_datetime(left))
			return mem_sub_dt(left, right, result);
		if (mem_is_interval(left))
			return mem_sub_itv(left, right, result);
	}
	diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(left),
		 "integer, decimal, double, datetime or interval");
	return -1;
}

int
mem_mul(const struct Mem *left, const struct Mem *right, struct Mem *result)
{
	if (mem_is_any_null(left, right)) {
		mem_set_null(result);
		return 0;
	}
	if (check_types_numeric_arithmetic(left, right) != 0)
		return -1;
	if (((left->type | right->type) & MEM_TYPE_DOUBLE) != 0) {
		double a;
		double b;
		mem_get_double(left, &a);
		mem_get_double(right, &b);
		mem_set_double(result, a * b);
		return 0;
	}
	if (((left->type | right->type) & MEM_TYPE_DEC) != 0) {
		decimal_t a;
		decimal_t b;
		decimal_t res;
		mem_get_dec(left, &a);
		mem_get_dec(right, &b);
		if (decimal_mul(&res, &a, &b) == NULL) {
			diag_set(ClientError, ER_SQL_EXECUTE,
				 "decimal is overflowed");
			return -1;
		}
		mem_set_dec(result, &res);
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
		return 0;
	}
	if (check_types_numeric_arithmetic(left, right) != 0)
		return -1;
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
	if (((left->type | right->type) & MEM_TYPE_DEC) != 0) {
		decimal_t a;
		decimal_t b;
		decimal_t zero;
		decimal_t res;
		mem_get_dec(left, &a);
		mem_get_dec(right, &b);
		decimal_zero(&zero);
		if (decimal_compare(&b, &zero) == 0) {
			diag_set(ClientError, ER_SQL_EXECUTE,
				 "division by zero");
			return -1;
		}
		if (decimal_div(&res, &a, &b) == NULL) {
			diag_set(ClientError, ER_SQL_EXECUTE,
				 "decimal is overflowed");
			return -1;
		}
		mem_set_dec(result, &res);
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
		return 0;
	}
	if (!mem_is_int(left) || mem_is_metatype(left)) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(left),
			 "integer");
		return -1;
	}
	if (!mem_is_int(right) || mem_is_metatype(right)) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(right),
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

static inline int
check_types_unsigned_bitwise(const struct Mem *a, const struct Mem *b)
{
	if (a->type != MEM_TYPE_UINT || mem_is_metatype(a)) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(a),
			 "unsigned");
		return -1;
	}
	if (b->type != MEM_TYPE_UINT || mem_is_metatype(b)) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(b),
			 "unsigned");
		return -1;
	}
	return 0;
}

int
mem_bit_and(const struct Mem *left, const struct Mem *right, struct Mem *result)
{
	if (mem_is_any_null(left, right)) {
		mem_set_null(result);
		return 0;
	}
	if (check_types_unsigned_bitwise(right, left) != 0)
		return -1;
	mem_set_uint(result, left->u.u & right->u.u);
	return 0;
}

int
mem_bit_or(const struct Mem *left, const struct Mem *right, struct Mem *result)
{
	if (mem_is_any_null(left, right)) {
		mem_set_null(result);
		return 0;
	}
	if (check_types_unsigned_bitwise(right, left) != 0)
		return -1;
	mem_set_uint(result, left->u.u | right->u.u);
	return 0;
}

int
mem_shift_left(const struct Mem *left, const struct Mem *right,
	       struct Mem *result)
{
	if (mem_is_any_null(left, right)) {
		mem_set_null(result);
		return 0;
	}
	if (check_types_unsigned_bitwise(right, left) != 0)
		return -1;
	mem_set_uint(result, right->u.u >= 64 ? 0 : left->u.u << right->u.u);
	return 0;
}

int
mem_shift_right(const struct Mem *left, const struct Mem *right,
		struct Mem *result)
{
	if (mem_is_any_null(left, right)) {
		mem_set_null(result);
		return 0;
	}
	if (check_types_unsigned_bitwise(right, left) != 0)
		return -1;
	mem_set_uint(result, right->u.u >= 64 ? 0 : left->u.u >> right->u.u);
	return 0;
}

int
mem_bit_not(const struct Mem *mem, struct Mem *result)
{
	if (mem_is_null(mem)) {
		mem_set_null(result);
		return 0;
	}
	if (mem->type != MEM_TYPE_UINT || mem_is_metatype(mem)) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(mem),
			 "unsigned");
		return -1;
	}
	mem_set_uint(result, ~mem->u.u);
	return 0;
}

static int
mem_cmp_bool(const struct Mem *a, const struct Mem *b)
{
	assert((a->type & b->type & MEM_TYPE_BOOL) != 0);
	return a->u.b - b->u.b;
}

static int
mem_cmp_bin(const struct Mem *a, const struct Mem *b)
{
	assert((a->type & b->type & MEM_TYPE_BIN) != 0);
	int res = memcmp(a->z, b->z, MIN(a->n, b->n));
	return res != 0 ? res : a->n - b->n;
}

static int
mem_cmp_num(const struct Mem *a, const struct Mem *b)
{
	assert(mem_is_num(a) && mem_is_num(b));
	if ((a->type & b->type & MEM_TYPE_DOUBLE) != 0) {
		if (a->u.r > b->u.r)
			return 1;
		if (a->u.r < b->u.r)
			return -1;
		return 0;
	}
	if ((a->type & b->type & MEM_TYPE_INT) != 0) {
		if (a->u.i > b->u.i)
			return 1;
		if (a->u.i < b->u.i)
			return -1;
		return 0;
	}
	if ((a->type & b->type & MEM_TYPE_UINT) != 0) {
		if (a->u.u > b->u.u)
			return 1;
		if (a->u.u < b->u.u)
			return -1;
		return 0;
	}
	if ((a->type & b->type & MEM_TYPE_DEC) != 0)
		return decimal_compare(&a->u.d, &b->u.d);
	switch (a->type) {
	case MEM_TYPE_INT:
		switch (b->type) {
		case MEM_TYPE_UINT:
			return -1;
		case MEM_TYPE_DOUBLE:
			return double_compare_nint64(b->u.r, a->u.i, -1);
		case MEM_TYPE_DEC: {
			decimal_t dec;
			decimal_from_int64(&dec, a->u.i);
			return decimal_compare(&dec, &b->u.d);
		}
		default:
			unreachable();
		}
	case MEM_TYPE_UINT:
		switch (b->type) {
		case MEM_TYPE_INT:
			return 1;
		case MEM_TYPE_DOUBLE:
			return double_compare_uint64(b->u.r, a->u.u, -1);
		case MEM_TYPE_DEC: {
			decimal_t dec;
			decimal_from_uint64(&dec, a->u.u);
			return decimal_compare(&dec, &b->u.d);
		}
		default:
			unreachable();
		}
	case MEM_TYPE_DOUBLE:
		switch (b->type) {
		case MEM_TYPE_INT:
			return double_compare_nint64(a->u.r, b->u.i, 1);
		case MEM_TYPE_UINT:
			return double_compare_uint64(a->u.r, b->u.u, 1);
		case MEM_TYPE_DEC: {
			if (a->u.r >= 1e38)
				return 1;
			if (a->u.r <= -1e38)
				return -1;
			decimal_t dec;
			decimal_t *d = decimal_from_double(&dec, a->u.r);
			assert(d != NULL && d == &dec);
			return decimal_compare(d, &b->u.d);
		}
		default:
			unreachable();
		}
	case MEM_TYPE_DEC:
		switch (b->type) {
		case MEM_TYPE_INT: {
			decimal_t dec;
			decimal_from_int64(&dec, b->u.i);
			return decimal_compare(&a->u.d, &dec);
		}
		case MEM_TYPE_UINT: {
			decimal_t dec;
			decimal_from_uint64(&dec, b->u.u);
			return decimal_compare(&a->u.d, &dec);
		}
		case MEM_TYPE_DOUBLE: {
			if (b->u.r >= 1e38)
				return -1;
			if (b->u.r <= -1e38)
				return 1;
			decimal_t dec;
			decimal_t *d = decimal_from_double(&dec, b->u.r);
			assert(d != NULL && d == &dec);
			return decimal_compare(&a->u.d, d);
		}
		default:
			unreachable();
		}
	default:
		unreachable();
	}
	return 1;
}

static int
mem_cmp_str(const struct Mem *a, const struct Mem *b, const struct coll *coll)
{
	assert((a->type & b->type & MEM_TYPE_STR) != 0);
	if (coll != NULL)
		return coll->cmp(a->z, a->n, b->z, b->n, coll);
	int res = memcmp(a->z, b->z, MIN(a->n, b->n));
	return res != 0 ? res : a->n - b->n;
}

static int
mem_cmp_uuid(const struct Mem *a, const struct Mem *b)
{
	assert((a->type & b->type & MEM_TYPE_UUID) != 0);
	return memcmp(&a->u.uuid, &b->u.uuid, UUID_LEN);
}

/** Compare two MEMs with DATETIME. */
static int
mem_cmp_datetime(const struct Mem *a, const struct Mem *b)
{
	assert((a->type & b->type & MEM_TYPE_DATETIME) != 0);
	return datetime_compare(&a->u.dt, &b->u.dt);
}

int
mem_cmp_scalar(const struct Mem *a, const struct Mem *b,
	       const struct coll *coll)
{
	enum mem_class class_a = mem_type_class(a->type);
	enum mem_class class_b = mem_type_class(b->type);
	if (class_a != class_b)
		return class_a - class_b;
	switch (class_a) {
	case MEM_CLASS_NULL:
		return 0;
	case MEM_CLASS_BOOL:
		return mem_cmp_bool(a, b);
	case MEM_CLASS_NUMBER:
		return mem_cmp_num(a, b);
	case MEM_CLASS_STR:
		return mem_cmp_str(a, b, coll);
	case MEM_CLASS_BIN:
		return mem_cmp_bin(a, b);
	case MEM_CLASS_UUID:
		return mem_cmp_uuid(a, b);
	case MEM_CLASS_DATETIME:
		return mem_cmp_datetime(a, b);
	default:
		unreachable();
	}
	return 0;
}

int
mem_cmp_msgpack(const struct Mem *a, const char **b, int *result,
		const struct coll *coll)
{
	struct Mem mem;
	switch (mp_typeof(**b)) {
	case MP_NIL:
		mem.type = MEM_TYPE_NULL;
		mp_decode_nil(b);
		break;
	case MP_BOOL:
		mem.type = MEM_TYPE_BOOL;
		mem.u.b = mp_decode_bool(b);
		break;
	case MP_UINT:
		mem.type = MEM_TYPE_UINT;
		mem.u.u = mp_decode_uint(b);
		break;
	case MP_INT:
		mem.type = MEM_TYPE_INT;
		mem.u.i = mp_decode_int(b);
		break;
	case MP_FLOAT:
		mem.type = MEM_TYPE_DOUBLE;
		mem.u.r = mp_decode_float(b);
		break;
	case MP_DOUBLE:
		mem.type = MEM_TYPE_DOUBLE;
		mem.u.r = mp_decode_double(b);
		break;
	case MP_STR:
		mem.type = MEM_TYPE_STR;
		mem.n = mp_decode_strl(b);
		mem.z = (char *)*b;
		*b += mem.n;
		mem.flags = MEM_Ephem;
		break;
	case MP_BIN:
		mem.type = MEM_TYPE_BIN;
		mem.n = mp_decode_binl(b);
		mem.z = (char *)*b;
		*b += mem.n;
		mem.flags = MEM_Ephem;
		break;
	case MP_ARRAY:
	case MP_MAP:
		mp_next(b);
		*result = -1;
		return 0;
	case MP_EXT: {
		int8_t type;
		const char *buf = *b;
		uint32_t len = mp_decode_extl(b, &type);
		if (type == MP_UUID) {
			assert(len == UUID_LEN);
			mem.type = MEM_TYPE_UUID;
			if (uuid_unpack(b, len, &mem.u.uuid) == NULL)
				return -1;
			break;
		} else if (type == MP_DECIMAL) {
			mem.type = MEM_TYPE_DEC;
			if (decimal_unpack(b, len, &mem.u.d) == 0)
				return -1;
			break;
		} else if (type == MP_DATETIME) {
			mem.type = MEM_TYPE_DATETIME;
			if (datetime_unpack(b, len, &mem.u.dt) == 0) {
				diag_set(ClientError, ER_INVALID_MSGPACK,
					 "Invalid MP_DATETIME MsgPack format");
				return -1;
			}
			break;
		} else if (type == MP_INTERVAL) {
			diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mp_str(*b),
				 "comparable type");
			return -1;
		}
		*b += len;
		mem.type = MEM_TYPE_BIN;
		mem.z = (char *)buf;
		mem.n = *b - buf;
		mem.flags = MEM_Ephem;
		break;
	}
	default:
		unreachable();
	}
	*result = mem_cmp_scalar(a, &mem, coll);
	return 0;
}

int
mem_cmp(const struct Mem *a, const struct Mem *b, int *result,
	const struct coll *coll)
{
	enum mem_class class_a = mem_type_class(a->type);
	enum mem_class class_b = mem_type_class(b->type);
	if (mem_is_any_null(a, b)) {
		*result = class_a - class_b;
		if ((a->flags & b->flags & MEM_Cleared) != 0)
			*result = 1;
		return 0;
	}
	if (!mem_is_comparable(a)) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(a),
			 "comparable type");
		return -1;
	}
	if (!mem_is_comparable(b)) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(b),
			 "comparable type");
		return -1;
	}
	if (((a->flags | b->flags) & MEM_Scalar) != 0) {
		*result = mem_cmp_scalar(a, b, coll);
		return 0;
	}
	if (class_a != class_b) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(b),
			 mem_type_class_to_str(a));
		return -1;
	}
	switch (class_a) {
	case MEM_CLASS_BOOL:
		*result =  mem_cmp_bool(a, b);
		break;
	case MEM_CLASS_NUMBER:
		*result = mem_cmp_num(a, b);
		break;
	case MEM_CLASS_STR:
		*result = mem_cmp_str(a, b, coll);
		break;
	case MEM_CLASS_BIN:
		*result = mem_cmp_bin(a, b);
		break;
	case MEM_CLASS_UUID:
		*result = mem_cmp_uuid(a, b);
		break;
	case MEM_CLASS_DATETIME:
		*result = mem_cmp_datetime(a, b);
		break;
	default:
		unreachable();
	}
	return 0;
}

char *
mem_type_to_str(const struct Mem *p)
{
	assert(p != NULL);
	if ((p->flags & MEM_Any) != 0)
		return "any";
	if ((p->flags & MEM_Scalar) != 0)
		return "scalar";
	if ((p->flags & MEM_Number) != 0)
		return "number";
	switch (p->type) {
	case MEM_TYPE_NULL:
		return "NULL";
	case MEM_TYPE_STR:
		return "string";
	case MEM_TYPE_INT:
	case MEM_TYPE_UINT:
		return "integer";
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
	case MEM_TYPE_UUID:
		return "uuid";
	case MEM_TYPE_DEC:
		return "decimal";
	case MEM_TYPE_DATETIME:
		return "datetime";
	case MEM_TYPE_INTERVAL:
		return "interval";
	default:
		unreachable();
	}
}

enum mp_type
mem_mp_type(const struct Mem *mem)
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
	case MEM_TYPE_DEC:
	case MEM_TYPE_UUID:
	case MEM_TYPE_DATETIME:
	case MEM_TYPE_INTERVAL:
		return MP_EXT;
	default:
		unreachable();
	}
	return MP_NIL;
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
		       ((p->flags & MEM_Ephem) != 0 ? 1 : 0) +
		       ((p->flags & MEM_Static) != 0 ? 1 : 0) == 1);
	}
	return 1;
}
#endif

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

	pMem->z = pMem->zMalloc;
	pMem->flags &= ~(MEM_Ephem | MEM_Static);
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
	if (pMem->szMalloc < szNew) {
		return sqlVdbeMemGrow(pMem, szNew, 0);
	}
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
	if (mem_is_bytes(p))
		return p->n > p->db->aLimit[SQL_LIMIT_LENGTH];
	return 0;
}

int
sqlVdbeRecordCompareMsgpack(const void *key1,
				struct UnpackedRecord *key2)
{
	int rc = 0;
	u32 i, n = mp_decode_array((const char**)&key1);

	n = MIN(n, key2->nField);

	for (i = 0; i != n; i++) {
		struct key_part *part = &key2->key_def->parts[i];
		struct Mem *mem = key2->aMem + i;
		struct coll *coll = part->coll;
		if (mem_cmp_msgpack(mem, (const char **)&key1, &rc, coll) != 0)
			rc = 0;
		if (rc != 0) {
			if (part->sort_order != SORT_ORDER_ASC)
				return rc;
			return -rc;
		}
	}
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
		break;
	}
	case MP_MAP: {
		mem->z = (char *)buf;
		mp_next(&buf);
		mem->n = buf - mem->z;
		mem->type = MEM_TYPE_MAP;
		mem->flags = MEM_Ephem;
		break;
	}
	case MP_EXT: {
		int8_t type;
		const char *svp = buf;
		uint32_t size = mp_decode_extl(&buf, &type);
		if (type == MP_UUID) {
			assert(size == UUID_LEN);
			buf = svp;
			if (mp_decode_uuid(&buf, &mem->u.uuid) == NULL) {
				diag_set(ClientError, ER_INVALID_MSGPACK,
					 "Invalid MP_UUID MsgPack format");
				return -1;
			}
			mem->type = MEM_TYPE_UUID;
			mem->flags = 0;
			break;
		} else if (type == MP_DECIMAL) {
			buf = svp;
			if (mp_decode_decimal(&buf, &mem->u.d) == NULL)
				return -1;
			mem->type = MEM_TYPE_DEC;
			mem->flags = 0;
			break;
		} else if (type == MP_DATETIME) {
			if (datetime_unpack(&buf, size, &mem->u.dt) == NULL) {
				diag_set(ClientError, ER_INVALID_MSGPACK,
					 "Invalid MP_DATETIME MsgPack format");
				return -1;
			}
			mem->type = MEM_TYPE_DATETIME;
			mem->flags = 0;
			break;
		} else if (type == MP_INTERVAL) {
			if (interval_unpack(&buf, size, &mem->u.itv) == NULL) {
				diag_set(ClientError, ER_INVALID_MSGPACK,
					 "Invalid MP_INTERVAL MsgPack format");
				return -1;
			}
			mem->type = MEM_TYPE_INTERVAL;
			mem->flags = 0;
			break;
		}
		buf += size;
		mem->z = (char *)svp;
		mem->n = buf - svp;
		mem->type = MEM_TYPE_BIN;
		mem->flags = MEM_Ephem;
		break;
	}
	case MP_NIL: {
		mp_decode_nil(&buf);
		mem->type = MEM_TYPE_NULL;
		mem->flags = 0;
		break;
	}
	case MP_BOOL: {
		mem->u.b = mp_decode_bool(&buf);
		mem->type = MEM_TYPE_BOOL;
		mem->flags = 0;
		break;
	}
	case MP_UINT: {
		uint64_t v = mp_decode_uint(&buf);
		mem->u.u = v;
		mem->type = MEM_TYPE_UINT;
		mem->flags = 0;
		break;
	}
	case MP_INT: {
		mem->u.i = mp_decode_int(&buf);
		mem->type = MEM_TYPE_INT;
		mem->flags = 0;
		break;
	}
	case MP_STR: {
		/* XXX u32->int */
		mem->n = (int) mp_decode_strl(&buf);
		mem->type = MEM_TYPE_STR;
		mem->flags = MEM_Ephem;
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
		goto install_blob;
	}
	case MP_FLOAT: {
		mem->u.r = mp_decode_float(&buf);
		if (sqlIsNaN(mem->u.r)) {
			mem->type = MEM_TYPE_NULL;
			mem->flags = 0;
		} else {
			mem->type = MEM_TYPE_DOUBLE;
			mem->flags = 0;
		}
		break;
	}
	case MP_DOUBLE: {
		mem->u.r = mp_decode_double(&buf);
		if (sqlIsNaN(mem->u.r)) {
			mem->type = MEM_TYPE_NULL;
			mem->flags = 0;
		} else {
			mem->type = MEM_TYPE_DOUBLE;
			mem->flags = 0;
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
mem_to_mpstream(const struct Mem *var, struct mpstream *stream)
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
		mpstream_encode_binl(stream, var->n);
		mpstream_memcpy(stream, var->z, var->n);
		return;
	case MEM_TYPE_ARRAY:
	case MEM_TYPE_MAP:
		mpstream_memcpy(stream, var->z, var->n);
		return;
	case MEM_TYPE_BOOL:
		mpstream_encode_bool(stream, var->u.b);
		return;
	case MEM_TYPE_UUID:
		mpstream_encode_uuid(stream, &var->u.uuid);
		return;
	case MEM_TYPE_DEC:
		mpstream_encode_decimal(stream, &var->u.d);
		return;
	case MEM_TYPE_DATETIME:
		mpstream_encode_datetime(stream, &var->u.dt);
		return;
	case MEM_TYPE_INTERVAL:
		mpstream_encode_interval(stream, &var->u.itv);
		return;
	default:
		unreachable();
	}
}

char *
mem_encode_array(const struct Mem *mems, uint32_t count, uint32_t *size,
		 struct region *region)
{
	size_t used = region_used(region);
	bool is_error = false;
	struct mpstream stream;
	mpstream_init(&stream, region, region_reserve_cb, region_alloc_cb,
		      set_encode_error, &is_error);
	mpstream_encode_array(&stream, count);
	for (const struct Mem *mem = mems; mem < mems + count; mem++)
		mem_to_mpstream(mem, &stream);
	mpstream_flush(&stream);
	if (is_error) {
		region_truncate(region, used);
		diag_set(OutOfMemory, stream.pos - stream.buf,
			 "mpstream_flush", "stream");
		return NULL;
	}
	*size = region_used(region) - used;
	char *array = region_join(region, *size);
	if (array == NULL) {
		region_truncate(region, used);
		diag_set(OutOfMemory, *size, "region_join", "array");
		return NULL;
	}
	mp_tuple_assert(array, array + *size);
	return array;
}

char *
mem_encode_map(const struct Mem *mems, uint32_t count, uint32_t *size,
	       struct region *region)
{
	size_t used = region_used(region);
	bool is_error = false;
	struct mpstream stream;
	mpstream_init(&stream, region, region_reserve_cb, region_alloc_cb,
		      set_encode_error, &is_error);
	mpstream_encode_map(&stream, count);
	for (uint32_t i = 0; i < count; ++i) {
		const struct Mem *key = &mems[2 * i];
		const struct Mem *value = &mems[2 * i + 1];
		if (mem_is_metatype(key) ||
		    (key->type & (MEM_TYPE_UINT | MEM_TYPE_INT | MEM_TYPE_UUID |
				  MEM_TYPE_STR)) == 0) {
			diag_set(ClientError, ER_SQL_TYPE_MISMATCH,
				 mem_str(key), "integer, string or uuid");
			goto error;
		}
		mem_to_mpstream(key, &stream);
		mem_to_mpstream(value, &stream);
	}
	mpstream_flush(&stream);
	if (is_error) {
		diag_set(OutOfMemory, stream.pos - stream.buf,
			 "mpstream_flush", "stream");
		goto error;
	}
	*size = region_used(region) - used;
	char *map = region_join(region, *size);
	if (map != NULL)
		return map;
	diag_set(OutOfMemory, *size, "region_join", "map");
error:
	region_truncate(region, used);
	return NULL;
}

/* Locate an element in a MAP or ARRAY using the given key.*/
static int
mp_getitem(const char **data, const struct Mem *key)
{
	if (mp_typeof(**data) != MP_ARRAY && mp_typeof(**data) != MP_MAP) {
		*data = NULL;
		return 0;
	}
	if (mp_typeof(**data) == MP_ARRAY) {
		uint32_t size = mp_decode_array(data);
		if (!mem_is_uint(key) || key->u.u == 0 || key->u.u > size) {
			*data = NULL;
			return 0;
		}
		for (uint32_t i = 0; i < key->u.u - 1; ++i)
			mp_next(data);
		return 0;
	}
	struct Mem mem;
	mem_create(&mem);
	uint32_t size = mp_decode_map(data);
	for (uint32_t i = 0; i < size; ++i) {
		uint32_t len;
		if (mem_from_mp_ephemeral(&mem, *data, &len) != 0)
			return -1;
		assert(mem_is_trivial(&mem) && !mem_is_metatype(&mem));
		*data += len;
		if (mem_is_comparable(&mem) &&
		    mem_cmp_scalar(&mem, key, NULL) == 0)
			return 0;
		mp_next(data);
	}
	*data = NULL;
	return 0;
}

int
mem_getitem(const struct Mem *mem, const struct Mem *keys, int count,
	    struct Mem *res)
{
	assert(count > 0);
	assert(mem_is_map(mem) || mem_is_array(mem));
	const char *data = mem->z;
	for (int i = 0; i < count && data != NULL; ++i) {
		if (mp_getitem(&data, &keys[i]) != 0)
			return -1;
	}
	if (data == NULL) {
		mem_set_null(res);
		return 0;
	}
	uint32_t len;
	if (mem_from_mp(res, data, &len) != 0)
		return -1;
	res->flags |= MEM_Any;
	assert((res->flags & (MEM_Number | MEM_Scalar)) == 0);
	return 0;
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
			lua_pushlstring(L, mem->z, mem->n);
			break;
		case MEM_TYPE_MAP:
		case MEM_TYPE_ARRAY:
			luamp_decode(L, luaL_msgpack_default,
				     (const char **)&mem->z);
			break;
		case MEM_TYPE_NULL:
			lua_pushnil(L);
			break;
		case MEM_TYPE_BOOL:
			lua_pushboolean(L, mem->u.b);
			break;
		case MEM_TYPE_UUID:
			luaT_pushuuid(L, &mem->u.uuid);
			break;
		case MEM_TYPE_DEC:
			luaT_pushdecimal(L, &mem->u.d);
			break;
		case MEM_TYPE_DATETIME:
			luaT_pushdatetime(L, &mem->u.dt);
			break;
		case MEM_TYPE_INTERVAL:
			luaT_pushinterval(L, &mem->u.itv);
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
		mem_to_mpstream((struct Mem *)port->mem + i, &stream);
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
		int index = -1 - i;
		if (luaL_tofield(L, luaL_msgpack_default, index, &field) < 0)
			goto error;
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
		case MP_MAP:
		case MP_ARRAY: {
			size_t used = region_used(region);
			bool is_map = field.type == MP_MAP;
			struct mpstream stream;
			bool is_error = false;
			mpstream_init(&stream, region, region_reserve_cb,
				      region_alloc_cb, set_encode_error,
				      &is_error);
			lua_pushvalue(L, index);
			luamp_encode_r(L, luaL_msgpack_default, &stream,
				       &field, 0);
			lua_pop(L, 1);
			mpstream_flush(&stream);
			if (is_error) {
				diag_set(OutOfMemory, stream.pos - stream.buf,
					 "mpstream_flush", "stream");
				return NULL;
			}
			uint32_t size = region_used(region) - used;
			char *raw = region_join(region, size);
			if (raw == NULL) {
				diag_set(OutOfMemory, size, "region_join",
					 "raw");
				goto error;
			}
			int rc = is_map ? mem_copy_map(&val[i], raw, size) :
				 mem_copy_array(&val[i], raw, size);
			if (rc != 0)
				goto error;
			region_truncate(region, used);
			break;
		}
		case MP_EXT: {
			if (field.ext_type == MP_UUID) {
				mem_set_uuid(&val[i], field.uuidval);
			} else if (field.ext_type == MP_DECIMAL) {
				mem_set_dec(&val[i], field.decval);
			} else if (field.ext_type == MP_DATETIME) {
				mem_set_datetime(&val[i], field.dateval);
			} else if (field.ext_type == MP_INTERVAL) {
				mem_set_interval(&val[i], field.interval);
			} else {
				diag_set(ClientError, ER_SQL_EXECUTE,
					 "Unsupported type passed from Lua");
				goto error;
			}
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
			int8_t type;
			len = mp_decode_extl(&data, &type);
			if (type == MP_UUID) {
				assert(len == UUID_LEN);
				struct tt_uuid *uuid = &val[i].u.uuid;
				data = str;
				if (mp_decode_uuid(&data, uuid) == NULL) {
					diag_set(ClientError,
						 ER_INVALID_MSGPACK, "Invalid "
						 "MP_UUID MsgPack format");
					goto error;
				}
				val[i].type = MEM_TYPE_UUID;
				break;
			} else if (type == MP_DECIMAL) {
				decimal_t *d = &val[i].u.d;
				data = str;
				if (mp_decode_decimal(&data, d) == NULL) {
					diag_set(ClientError,
						 ER_INVALID_MSGPACK, "Invalid "
						 "MP_DECIMAL MsgPack format");
					goto error;
				}
				val[i].type = MEM_TYPE_DEC;
				break;
			} else if (type == MP_DATETIME) {
				struct datetime *dt = &val[i].u.dt;
				if (datetime_unpack(&data, len, dt) == 0) {
					diag_set(ClientError,
						 ER_INVALID_MSGPACK, "Invalid "
						 "MP_DATETIME MsgPack format");
					goto error;
				}
				val[i].type = MEM_TYPE_DATETIME;
				break;
			} else if (type == MP_INTERVAL) {
				struct interval *itv = &val[i].u.itv;
				if (interval_unpack(&data, len, itv) == NULL) {
					diag_set(ClientError,
						 ER_INVALID_MSGPACK, "Invalid "
						 "MP_INTERVAL MsgPack format");
					goto error;
				}
				val[i].type = MEM_TYPE_INTERVAL;
				break;
			}
			data += len;
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
