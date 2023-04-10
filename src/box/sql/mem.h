#pragma once
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
#include "box/field_def.h"
#include "datetime.h"
#include "core/decimal.h"
#include "tt_uuid.h"

struct sql;
struct Vdbe;
struct region;
struct mpstream;
struct VdbeFrame;

enum sql_mem_type {
	MEM_TYPE_NULL		= 1,
	MEM_TYPE_UINT		= 1 << 1,
	MEM_TYPE_INT		= 1 << 2,
	MEM_TYPE_STR		= 1 << 3,
	MEM_TYPE_BIN		= 1 << 4,
	MEM_TYPE_ARRAY		= 1 << 5,
	MEM_TYPE_MAP		= 1 << 6,
	MEM_TYPE_BOOL		= 1 << 7,
	MEM_TYPE_DOUBLE		= 1 << 8,
	MEM_TYPE_UUID		= 1 << 9,
	MEM_TYPE_DEC		= 1 << 10,
	MEM_TYPE_DATETIME	= 1 << 11,
	MEM_TYPE_INTERVAL	= 1 << 12,
	MEM_TYPE_INVALID	= 1 << 13,
	MEM_TYPE_FRAME		= 1 << 14,
	MEM_TYPE_PTR		= 1 << 15,
};

enum sql_mem_group {
	MEM_GROUP_DATA = 0,
	MEM_GROUP_NUMBER,
	MEM_GROUP_SCALAR,
	MEM_GROUP_ANY,
};

/** Object that is used to store all value types managed by the VDBE. */
struct sql_mem {
	union sql_mem_value {
		/** BOOLEAN value. */
		bool b;
		/** POINTER value. */
		void *p;
		/** DOUBLE value. */
		double r;
		/** Negative INTEGER value. */
		int64_t i;
		/** Unsigned INTEGER value. */
		uint64_t u;
		/** DECIMAL value. */
		decimal_t d;
		struct {
			/** STRING, VARBINARY, MAP or ARRAY value. */
			char *z;
			/** Length of variable length value. */
			size_t n;
		};
		/** DATETIME value. */
		struct datetime dt;
		/** UUID value. */
		struct tt_uuid uuid;
		/** INTERVAL value. */
		struct interval itv;
		/** FRAME value. */
		struct VdbeFrame *frame;
	} u;
	/* The memory managed by this MEM. */
	char *buf;
	/* Size of the buf allocation. */
	size_t size;
	/** Type of the value this MEM contains. */
	enum sql_mem_type type;
	/** DATA, NUMBER, SCALAR or ANY group. */
	enum sql_mem_group group;
	/**
	 * Flag indicating that the variable length value's memory is managed by
	 * another MEM.
	 */
	bool is_ephemeral;
	/** Flag indicating NULL set by OP_Null, not from data. */
	bool is_cleared;
#ifdef SQL_DEBUG
	/**
	 * MEM that manages memory for variable length value of ephemeral MEM.
	 */
	struct sql_mem *copy_from;
#endif
};

static inline bool
mem_is_null(const struct sql_mem *mem)
{
	return mem->type == MEM_TYPE_NULL;
}

static inline bool
mem_is_uint(const struct sql_mem *mem)
{
	return mem->type == MEM_TYPE_UINT;
}

static inline bool
mem_is_nint(const struct sql_mem *mem)
{
	return mem->type == MEM_TYPE_INT;
}

static inline bool
mem_is_str(const struct sql_mem *mem)
{
	return mem->type == MEM_TYPE_STR;
}

static inline bool
mem_is_num(const struct sql_mem *mem)
{
	return (mem->type & (MEM_TYPE_UINT | MEM_TYPE_INT | MEM_TYPE_DOUBLE |
			     MEM_TYPE_DEC)) != 0;
}

static inline bool
mem_is_any(const struct sql_mem *mem)
{
	return mem->group == MEM_GROUP_ANY;
}

static inline bool
mem_is_container(const struct sql_mem *mem)
{
	return (mem->type & (MEM_TYPE_MAP | MEM_TYPE_ARRAY)) != 0;
}

static inline bool
mem_is_metatype(const struct sql_mem *mem)
{
	return mem->group != MEM_GROUP_DATA;
}

static inline bool
mem_is_double(const struct sql_mem *mem)
{
	return mem->type == MEM_TYPE_DOUBLE;
}

static inline bool
mem_is_dec(const struct sql_mem *mem)
{
	return mem->type == MEM_TYPE_DEC;
}

static inline bool
mem_is_int(const struct sql_mem *mem)
{
	return (mem->type & (MEM_TYPE_UINT | MEM_TYPE_INT)) != 0;
}

static inline bool
mem_is_bool(const struct sql_mem *mem)
{
	return mem->type == MEM_TYPE_BOOL;
}

static inline bool
mem_is_bin(const struct sql_mem *mem)
{
	return mem->type == MEM_TYPE_BIN;
}

static inline bool
mem_is_map(const struct sql_mem *mem)
{
	return mem->type == MEM_TYPE_MAP;
}

static inline bool
mem_is_array(const struct sql_mem *mem)
{
	return mem->type == MEM_TYPE_ARRAY;
}

static inline bool
mem_is_datetime(const struct sql_mem *mem)
{
	return mem->type == MEM_TYPE_DATETIME;
}

static inline bool
mem_is_interval(const struct sql_mem *mem)
{
	return mem->type == MEM_TYPE_INTERVAL;
}

static inline bool
mem_is_bytes(const struct sql_mem *mem)
{
	return (mem->type & (MEM_TYPE_BIN | MEM_TYPE_STR |
			     MEM_TYPE_MAP | MEM_TYPE_ARRAY)) != 0;
}

static inline bool
mem_is_frame(const struct sql_mem *mem)
{
	return mem->type == MEM_TYPE_FRAME;
}

static inline bool
mem_is_invalid(const struct sql_mem *mem)
{
	return mem->type == MEM_TYPE_INVALID;
}

static inline bool
mem_is_ephemeral(const struct sql_mem *mem)
{
	return mem_is_bytes(mem) && mem->is_ephemeral;
}

static inline bool
mem_is_dynamic(const struct sql_mem *mem)
{
	return mem_is_bytes(mem) && mem->u.z == mem->buf;
}

/** Return TRUE if MEM does not need to be freed or destroyed. */
static inline bool
mem_is_trivial(const struct sql_mem *mem)
{
	return mem->size == 0 && mem->type != MEM_TYPE_FRAME;
}

static inline bool
mem_is_cleared(const struct sql_mem *mem)
{
	assert(!mem->is_cleared || mem->type == MEM_TYPE_NULL);
	return mem->is_cleared;
}

static inline bool
mem_is_comparable(const struct sql_mem *mem)
{
	uint32_t incmp = MEM_TYPE_ARRAY | MEM_TYPE_MAP | MEM_TYPE_INTERVAL;
	return mem->group != MEM_GROUP_ANY && (mem->type & incmp) == 0;
}

static inline bool
mem_is_same_type(const struct sql_mem *mem1, const struct sql_mem *mem2)
{
	return mem1->type == mem2->type;
}

static inline bool
mem_is_any_null(const struct sql_mem *mem1, const struct sql_mem *mem2)
{
	return ((mem1->type| mem2->type) & MEM_TYPE_NULL) != 0;
}

/** Check if MEM is compatible with field type. */
bool
mem_is_field_compatible(const struct sql_mem *mem, enum field_type type);

/**
 * Write a NULL-terminated string representation of a MEM to buf. Returns the
 * number of bytes required to write the value, excluding '\0'. If the return
 * value is equal to or greater than size, then the value has been truncated.
 */
int
mem_snprintf(char *buf, uint32_t size, const struct sql_mem *mem);

/**
 * Returns a NULL-terminated string representation of a MEM. Memory for the
 * result was allocated using sql_xmalloc() and should be freed.
 */
char *
mem_strdup(const struct sql_mem *mem);

/**
 * Return a string that contains description of type and value of MEM. String is
 * either allocated using static_alloc() of just a static variable. This
 * function should only be used for debugging or displaying MEM values in
 * description of errors.
 */
const char *
mem_str(const struct sql_mem *mem);

/** Initialize MEM and set NULL. */
static inline void
mem_create(struct sql_mem *mem)
{
	memset(mem, 0, sizeof(*mem));
	mem->type = MEM_TYPE_NULL;
}

/** Free all allocated memory in MEM and set MEM to NULL. */
void
mem_destroy(struct sql_mem *mem);

void
mem_delete(struct sql_mem *);

/** Clear MEM and set it to NULL. */
void
mem_set_null(struct sql_mem *mem);

/** Clear MEM and set it to INTEGER. */
void
mem_set_int(struct sql_mem *mem, int64_t value, bool is_neg);

/** Clear MEM and set it to UNSIGNED. */
void
mem_set_uint(struct sql_mem *mem, uint64_t value);

/** Clear MEM and set it to NEGATIVE INTEGER. */
void
mem_set_nint(struct sql_mem *mem, int64_t value);

/** Clear MEM and set it to INT64. */
static inline void
mem_set_int64(struct sql_mem *mem, int64_t value)
{
	if (value < 0)
		mem_set_nint(mem, value);
	else
		mem_set_uint(mem, value);
}

/** Clear MEM and set it to BOOLEAN. */
void
mem_set_bool(struct sql_mem *mem, bool value);

/** Clear MEM and set it to DOUBLE. */
void
mem_set_double(struct sql_mem *mem, double value);

/** Clear MEM and set it to UUID. */
void
mem_set_uuid(struct sql_mem *mem, const struct tt_uuid *uuid);

/** Clear MEM and set it to DECIMAL. */
void
mem_set_dec(struct sql_mem *mem, const decimal_t *dec);

/** Clear MEM and set it to DATETIME. */
void
mem_set_datetime(struct sql_mem *mem, const struct datetime *dt);

/** Clear MEM and set it to INTERVAL. */
void
mem_set_interval(struct sql_mem *mem, const struct interval *itv);

/** Clear MEM and set it to STRING. */
void
mem_set_str(struct sql_mem *mem, char *value, uint32_t len);

/** Clear MEM and set it to NULL-terminated STRING. */
void
mem_set_str0(struct sql_mem *mem, char *value);

/** Copy string to a newly allocated memory. The MEM type becomes STRING. */
int
mem_copy_str(struct sql_mem *mem, const char *value, uint32_t len);

/**
 * Copy NULL-terminated string to a newly allocated memory. The MEM type becomes
 * STRING.
 */
int
mem_copy_str0(struct sql_mem *mem, const char *value);

/** Clear MEM and set it to VARBINARY. */
void
mem_set_bin(struct sql_mem *mem, char *value, uint32_t size);

/**
 * Copy binary value to a newly allocated memory. The MEM type becomes
 * VARBINARY.
 */
int
mem_copy_bin(struct sql_mem *mem, const char *value, uint32_t size);

/**
 * Clear MEM and set it to MAP. The binary value must be msgpack of MAP type.
 */
void
mem_set_map(struct sql_mem *mem, char *value, uint32_t size);

/** Copy MAP value to a newly allocated memory. The MEM type becomes MAP. */
int
mem_copy_map(struct sql_mem *mem, const char *value, uint32_t size);

/**
 * Clear MEM and set it to ARRAY. The binary value must be msgpack of ARRAY
 * type.
 */
void
mem_set_array(struct sql_mem *mem, char *value, uint32_t size);

/** Copy ARRAY value to a newly allocated memory. The MEM type becomes ARRAY. */
int
mem_copy_array(struct sql_mem *mem, const char *value, uint32_t size);

/** Clear MEM and set it to invalid state. */
void
mem_set_invalid(struct sql_mem *mem);

/** Clear MEM and set pointer to be its value. */
void
mem_set_ptr(struct sql_mem *mem, void *ptr);

/** Clear MEM and set frame to be its value. */
void
mem_set_frame(struct sql_mem *mem, struct VdbeFrame *frame);

/**
 * Clear the MEM, set the function as its value, and allocate enough memory to
 * hold the accumulation structure for the aggregate function.
 */
int
mem_set_agg(struct sql_mem *mem, struct func *func, int size);

/** Clear MEM and set it to special, "cleared", NULL. */
void
mem_set_null_clear(struct sql_mem *mem);

static inline void
mem_set_ephemeral(struct sql_mem *mem)
{
	mem->is_ephemeral = true;
}

/**
 * Make the MEM containing the variable length value manage the memory where the
 * value is located.
 */
static inline void
mem_set_dynamic(struct sql_mem *mem)
{
	if (!mem_is_bytes(mem))
		return;
	sql_xfree(mem->buf);
	mem->buf = mem->u.z;
	mem->size = mem->u.n;
}

/**
 * Copy content of MEM from one MEM to another. In case source MEM contains
 * string or binary and allocation type is not STATIC, this value is copied to
 * newly allocated by destination MEM memory.
 */
int
mem_copy(struct sql_mem *to, const struct sql_mem *from);

/**
 * Copy content of MEM from one MEM to another. In case source MEM contains
 * string or binary and allocation type is not STATIC, this value is copied as
 * value with ephemeral allocation type.
 */
void
mem_copy_as_ephemeral(struct sql_mem *to, const struct sql_mem *from);

/**
 * Move all content of source MEM to destination MEM. Source MEM is set to NULL.
 */
void
mem_move(struct sql_mem *to, struct sql_mem *from);

/**
 * Append the given string to the end of the STRING or VARBINARY contained in
 * MEM. In case MEM needs to increase the size of allocated memory, additional
 * memory is allocated in an attempt to reduce the total number of allocations.
 */
int
mem_append(struct sql_mem *mem, const char *value, uint32_t len);

/**
 * Concatenate strings or binaries from the first and the second MEMs and write
 * to the result MEM. In case the first MEM or the second MEM is NULL, the
 * result MEM is set to NULL even if the result MEM is actually the first MEM.
 */
int
mem_concat(const struct sql_mem *a, const struct sql_mem *b,
	   struct sql_mem *result);

/**
 * Add the first MEM to the second MEM and write the result to the third MEM.
 */
int
mem_add(const struct sql_mem *left, const struct sql_mem *right,
	struct sql_mem *result);

/**
 * Subtract the second MEM from the first MEM and write the result to the third
 * MEM.
 */
int
mem_sub(const struct sql_mem *left, const struct sql_mem *right,
	struct sql_mem *result);

/**
 * Multiply the first MEM by the second MEM and write the result to the third
 * MEM.
 */
int
mem_mul(const struct sql_mem *left, const struct sql_mem *right,
	struct sql_mem *result);

/**
 * Divide the first MEM by the second MEM and write the result to the third
 * MEM.
 */
int
mem_div(const struct sql_mem *left, const struct sql_mem *right,
	struct sql_mem *result);

/**
 * Divide the first MEM by the second MEM and write integer part of the result
 * to the third MEM.
 */
int
mem_rem(const struct sql_mem *left, const struct sql_mem *right,
	struct sql_mem *result);

/** Perform a bitwise AND for two MEMs and write the result to the third MEM. */
int
mem_bit_and(const struct sql_mem *left, const struct sql_mem *right,
	    struct sql_mem *result);

/** Perform a bitwise OR for two MEMs and write the result to the third MEM. */
int
mem_bit_or(const struct sql_mem *left, const struct sql_mem *right,
	   struct sql_mem *result);

/**
 * Perform a bitwise left shift for the first MEM by value from the second MEM
 * and write the result to the third MEM.
 */
int
mem_shift_left(const struct sql_mem *left, const struct sql_mem *right,
	       struct sql_mem *result);

/**
 * Perform a bitwise right shift for the first MEM by value from the second MEM
 * and write the result to the third MEM.
 */
int
mem_shift_right(const struct sql_mem *left, const struct sql_mem *right,
		struct sql_mem *result);

/** Perform a bitwise NOT to the MEM and write the result to the second MEM. */
int
mem_bit_not(const struct sql_mem *mem, struct sql_mem *result);

/**
 * Compare two MEMs using SCALAR rules and return the result of comparison. MEMs
 * should be scalars. Original MEMs are not changed.
 */
int
mem_cmp_scalar(const struct sql_mem *a, const struct sql_mem *b,
	       const struct coll *coll);

/**
 * Compare MEM and packed to msgpack value using SCALAR rules and return the
 * result of comparison. Both values should be scalars. Original MEM is not
 * changed. If successful, the second argument will contain the address
 * following the specified packed value.
 */
int
mem_cmp_msgpack(const struct sql_mem *a, const char **b, int *result,
		const struct coll *coll);

/**
 * Compare two MEMs using implicit cast rules and return the result of
 * comparison. MEMs should be scalars. Original MEMs are not changed.
 */
int
mem_cmp(const struct sql_mem *a, const struct sql_mem *b, int *result,
	const struct coll *coll);

/**
 * Convert the given MEM to INTEGER. This function and the function below define
 * the rules that are used to convert values of all other types to INTEGER. In
 * this function, the conversion from double to integer may result in loss of
 * precision.
 */
int
mem_to_int(struct sql_mem *mem);

/**
 * Convert the given MEM to INTEGER. This function and the function above define
 * the rules that are used to convert values of all other types to INTEGER. In
 * this function, the conversion from double to integer is only possible if it
 * is lossless.
 */
int
mem_to_int_precise(struct sql_mem *mem);

/**
 * Convert the given MEM to DOUBLE. This function defines the rules that are
 * used to convert values of all other types to DOUBLE.
 */
int
mem_to_double(struct sql_mem *mem);

/**
 * Convert the given MEM to NUMBER. This function defines the rules that are
 * used to convert values of all other types to NUMBER.
 */
int
mem_to_number(struct sql_mem *mem);

/**
 * Convert the given MEM to STRING. This function and the function below define
 * the rules that are used to convert values of all other types to STRING. In
 * this function, the string received after the convertion may be not
 * NULL-terminated.
 */
int
mem_to_str(struct sql_mem *mem);

/** Convert the given MEM to given type according to explicit cast rules. */
int
mem_cast_explicit(struct sql_mem *mem, enum field_type type);

/** Convert the given MEM to given type according to implicit cast rules. */
int
mem_cast_implicit(struct sql_mem *mem, enum field_type type);

/**
 * Cast MEM with numeric value to given numeric type. Doesn't fail. The return
 * value is < 0 if the original value is less than the result, > 0 if the
 * original value is greater than the result, and 0 if the cast is precise.
 */
int
mem_cast_implicit_number(struct sql_mem *mem, enum field_type type);

/**
 * Return value for MEM of INTEGER type. For MEM of all other types convert
 * value of the MEM to INTEGER if possible and return converted value. Original
 * MEM is not changed.
 */
int
mem_get_int(const struct sql_mem *mem, int64_t *i, bool *is_neg);

/**
 * Return value of MEM converted to int64_t. This function is not safe, since it
 * returns 0 if mem_get_int() fails. There is no proper handling for this case.
 * Also it works incorrectly with integer values that are more than INT64_MAX.
 */
static inline int64_t
mem_get_int_unsafe(const struct sql_mem *mem)
{
	int64_t i;
	bool is_neg;
	if (mem_get_int(mem, &i, &is_neg) != 0)
		return 0;
	return i;
}

/**
 * Return value for MEM of UNSIGNED type. For MEM of all other types convert
 * value of the MEM to UNSIGNED if possible and return converted value. Original
 * MEM is not changed.
 */
int
mem_get_uint(const struct sql_mem *mem, uint64_t *u);

/**
 * Return value of MEM converted to uint64_t. This function is not safe, since it
 * returns 0 if mem_get_uint() fails. There is no proper handling for this case.
 */
static inline uint64_t
mem_get_uint_unsafe(const struct sql_mem *mem)
{
	uint64_t u;
	if (mem_get_uint(mem, &u) != 0)
		return 0;
	return u;
}

/**
 * Return value for MEM of DOUBLE type. For MEM of all other types convert
 * value of the MEM to DOUBLE if possible and return converted value. Original
 * MEM is not changed.
 */
int
mem_get_double(const struct sql_mem *mem, double *d);

/**
 * Return value of MEM converted to double. This function is not safe since
 * there is no proper processing in case mem_get_double() return an error. In
 * this case this functions returns 0.
 */
static inline double
mem_get_double_unsafe(const struct sql_mem *mem)
{
	double d;
	if (mem_get_double(mem, &d) != 0)
		return 0.;
	return d;
}

/**
 * Return value for MEM of BOOLEAN type. For MEM of all other types convert
 * value of the MEM to BOOLEAN if possible and return converted value. Original
 * MEM is not changed.
 */
int
mem_get_bool(const struct sql_mem *mem, bool *b);

/**
 * Return value of MEM converted to boolean. This function is not safe since
 * there is no proper processing in case mem_get_bool() return an error. In
 * this case this function returns FALSE.
 */
static inline bool
mem_get_bool_unsafe(const struct sql_mem *mem)
{
	bool b;
	if (mem_get_bool(mem, &b) != 0)
		return false;
	return b;
}

/**
 * Return value for MEM of VARBINARY type. For MEM of all other types convert
 * value of the MEM to VARBINARY if possible and return converted value.
 * Original MEM is not changed.
 */
int
mem_get_bin(const struct sql_mem *mem, const char **s);

/**
 * Return length of value for MEM of STRING or VARBINARY type. Original MEM is
 * not changed.
 */
int
mem_len(const struct sql_mem *mem, uint32_t *len);

/**
 * Return length of value for MEM of STRING or VARBINARY type. This function is
 * not safe since there is no proper processing in case mem_len() return an
 * error. In this case this function returns 0.
 */
static inline int
mem_len_unsafe(const struct sql_mem *mem)
{
	uint32_t len;
	if (mem_len(mem, &len) != 0)
		return 0;
	return len;
}

/**
 * Simple type to str convertor. It is used to simplify
 * error reporting.
 */
char *
mem_type_to_str(const struct sql_mem *p);

/*
 * Return the MP_type of the value of the MEM.
 * Analogue of sql_value_type() but operates directly on
 * transparent memory cell.
 */
enum mp_type
mem_mp_type(const struct sql_mem *mem);

#ifdef SQL_DEBUG
/*
 * Return true if a memory cell is not marked as invalid.  This macro
 * is for use inside assert() statements only.
 */
#define memIsValid(M)  ((M)->type != MEM_TYPE_INVALID)
#endif

/**
 * Change the mem->buf allocation to be at least n bytes. If mem->buf already
 * meets or exceeds the requested size, this routine is a no-op.
 */
int
sqlVdbeMemClearAndResize(struct sql_mem *mem, int n);

/*
 * Release an array of N Mem elements
 */
void
releaseMemArray(struct sql_mem *p, int N);

#define VdbeFrameMem(p) \
	((struct sql_mem *)&((u8 *)p)[ROUND8(sizeof(struct VdbeFrame))])

/**
 * Return true if Mem contains a variable length with length value greater than
 * SQL_MAX_LENGTH.
 */
static inline bool
sqlVdbeMemTooBig(struct sql_mem *mem)
{
	return mem_is_bytes(mem) ? mem->u.n > SQL_MAX_LENGTH : false;
}

/* Return TRUE if Mem X contains dynamically allocated content - anything
 * that needs to be deallocated to avoid a leak.
 */
#define VdbeMemDynamic(X) (((X)->type & MEM_TYPE_FRAME) != 0)

/**
 * Perform comparison of two tuples: unpacked (key1) and packed (key2)
 *
 * @param key1 Packed key.
 * @param unpacked Unpacked key.
 *
 * @retval +1 if key1 > unpacked, -1 otherwise.
 */
int sqlVdbeRecordCompareMsgpack(const void *key1,
				    struct UnpackedRecord *key2);

/**
 * Decode msgpack and save value into VDBE memory cell. String and binary string
 * values set as ephemeral.
 *
 * @param mem Memory cell to write value into.
 * @param buf Buffer to deserialize msgpack from.
 * @param len[out] Length of decoded part.
 * @retval Return code: < 0 in case of error.
 * @retval 0 on success.
 */
int
mem_from_mp_ephemeral(struct sql_mem *mem, const char *buf, uint32_t *len);

/**
 * Decode msgpack and save value into VDBE memory cell. String and binary string
 * values copied to newly allocated memory.
 *
 * @param mem Memory cell to write value into.
 * @param buf Buffer to deserialize msgpack from.
 * @param len[out] Length of decoded part.
 * @retval Return code: < 0 in case of error.
 * @retval 0 on success.
 */
int
mem_from_mp(struct sql_mem *mem, const char *buf, uint32_t *len);

/**
 * Perform encoding of MEM to stream.
 *
 * @param var MEM to encode to stream.
 * @param stream Initialized mpstream encoder object.
 */
void
mem_to_mpstream(const struct sql_mem *var, struct mpstream *stream);

/** Encode MEM as msgpack value on region. */
char *
mem_to_mp(const struct sql_mem *mem, uint32_t *size, struct region *region);

/**
 * Encode array of MEMs as msgpack array on region.
 *
 * @param mems array of MEMs to encode.
 * @param count number of elements in the array.
 * @param[out] size Size of encoded msgpack array.
 * @param region Region to use.
 * @retval NULL on error, diag message is set.
 * @retval Pointer to valid msgpack array on success.
 */
char *
mem_encode_array(const struct sql_mem *mems, uint32_t count, uint32_t *size,
		 struct region *region);

/**
 * Encode array of MEMs as msgpack map on region. Values in even position are
 * treated as keys in MAP, values in odd position are treated as values in MAP.
 * number of MEMs should be even.
 *
 * @param mems array of MEMs to encode.
 * @param count number of elements in the array.
 * @param[out] size Size of encoded msgpack map.
 * @param region Region to use.
 * @retval NULL on error, diag message is set.
 * @retval Pointer to valid msgpack map on success.
 */
char *
mem_encode_map(const struct sql_mem *mems, uint32_t count, uint32_t *size,
	       struct region *region);

/** Return a value from ANY, MAP, or ARRAY MEM using the MEM array as keys. */
int
mem_getitem(const struct sql_mem *mem, const struct sql_mem *keys, int count,
	    struct sql_mem *res);
