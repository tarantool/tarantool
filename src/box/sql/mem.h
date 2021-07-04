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

struct sql;
struct Vdbe;
struct region;
struct mpstream;
struct VdbeFrame;

enum mem_type {
	MEM_TYPE_NULL		= 1,
	MEM_TYPE_UINT		= 1 << 1,
	MEM_TYPE_INT		= 1 << 2,
	MEM_TYPE_STR		= 1 << 3,
	MEM_TYPE_BIN		= 1 << 4,
	MEM_TYPE_ARRAY		= 1 << 5,
	MEM_TYPE_MAP		= 1 << 6,
	MEM_TYPE_BOOL		= 1 << 7,
	MEM_TYPE_DOUBLE		= 1 << 8,
	MEM_TYPE_INVALID	= 1 << 9,
	MEM_TYPE_FRAME		= 1 << 10,
	MEM_TYPE_PTR		= 1 << 11,
	MEM_TYPE_AGG		= 1 << 12,
};

/*
 * Internally, the vdbe manipulates nearly all SQL values as Mem
 * structures. Each Mem struct may cache multiple representations (string,
 * integer etc.) of the same value.
 */
struct Mem {
	union MemValue {
		double r;	/* Real value used when MEM_Real is set in flags */
		i64 i;		/* Integer value used when MEM_Int is set in flags */
		uint64_t u;	/* Unsigned integer used when MEM_UInt is set. */
		bool b;         /* Boolean value used when MEM_Bool is set in flags */
		int nZero;	/* Used when bit MEM_Zero is set in flags */
		void *p;	/* Generic pointer */
		/**
		 * A pointer to function implementation.
		 * Used only when flags==MEM_Agg.
		 */
		struct func *func;
		struct VdbeFrame *pFrame;	/* Used when flags==MEM_Frame */
	} u;
	/** Type of the value this MEM contains. */
	enum mem_type type;
	u32 flags;		/* Some combination of MEM_Null, MEM_Str, MEM_Dyn, etc. */
	/**
	 * If value is fetched from tuple, then this property
	 * contains type of corresponding space's field. If it's
	 * value field_type_MAX then we can rely on on format
	 * (msgpack) type which is represented by 'flags'.
	 */
	enum field_type field_type;
	int n;			/* size (in bytes) of string value, excluding trailing '\0' */
	char *z;		/* String or BLOB value */
	/* ShallowCopy only needs to copy the information above */
	char *zMalloc;		/* Space to hold MEM_Str or MEM_Blob if szMalloc>0 */
	int szMalloc;		/* Size of the zMalloc allocation */
	u32 uTemp;		/* Transient storage for serial_type in OP_MakeRecord */
	sql *db;		/* The associated database connection */
	void (*xDel) (void *);	/* Destructor for Mem.z - only valid if MEM_Dyn */
#ifdef SQL_DEBUG
	Mem *pScopyFrom;	/* This Mem is a shallow copy of pScopyFrom */
	void *pFiller;		/* So that sizeof(Mem) is a multiple of 8 */
#endif
};

#define MEM_Cleared   0x0200	/* NULL set by OP_Null, not from data */

/* Whenever Mem contains a valid string or blob representation, one of
 * the following flags must be set to determine the memory management
 * policy for Mem.z.  The MEM_Term flag tells us whether or not the
 * string is \000 or \u0000 terminated
 */
#define MEM_Term      0x0400	/* String rep is nul terminated */
#define MEM_Dyn       0x0800	/* Need to call Mem.xDel() on Mem.z */
#define MEM_Static    0x1000	/* Mem.z points to a static string */
#define MEM_Ephem     0x2000	/* Mem.z points to an ephemeral string */
#define MEM_Zero      0x8000	/* Mem.i contains count of 0s appended to blob */

static inline bool
mem_is_null(const struct Mem *mem)
{
	return mem->type == MEM_TYPE_NULL;
}

static inline bool
mem_is_uint(const struct Mem *mem)
{
	return mem->type == MEM_TYPE_UINT;
}

static inline bool
mem_is_nint(const struct Mem *mem)
{
	return mem->type == MEM_TYPE_INT;
}

static inline bool
mem_is_str(const struct Mem *mem)
{
	return mem->type == MEM_TYPE_STR;
}

static inline bool
mem_is_num(const struct Mem *mem)
{
	enum mem_type type = mem->type;
	return (type & (MEM_TYPE_UINT | MEM_TYPE_INT | MEM_TYPE_DOUBLE)) != 0;
}

static inline bool
mem_is_double(const struct Mem *mem)
{
	return mem->type == MEM_TYPE_DOUBLE;
}

static inline bool
mem_is_int(const struct Mem *mem)
{
	return (mem->type & (MEM_TYPE_UINT | MEM_TYPE_INT)) != 0;
}

static inline bool
mem_is_bool(const struct Mem *mem)
{
	return mem->type == MEM_TYPE_BOOL;
}

static inline bool
mem_is_bin(const struct Mem *mem)
{
	return mem->type == MEM_TYPE_BIN;
}

static inline bool
mem_is_map(const struct Mem *mem)
{
	return mem->type == MEM_TYPE_MAP;
}

static inline bool
mem_is_array(const struct Mem *mem)
{
	return mem->type == MEM_TYPE_ARRAY;
}

static inline bool
mem_is_agg(const struct Mem *mem)
{
	return mem->type == MEM_TYPE_AGG;
}

static inline bool
mem_is_bytes(const struct Mem *mem)
{
	return (mem->type & (MEM_TYPE_BIN | MEM_TYPE_STR |
			     MEM_TYPE_MAP | MEM_TYPE_ARRAY)) != 0;
}

static inline bool
mem_is_frame(const struct Mem *mem)
{
	return mem->type == MEM_TYPE_FRAME;
}

static inline bool
mem_is_invalid(const struct Mem *mem)
{
	return mem->type == MEM_TYPE_INVALID;
}

static inline bool
mem_is_static(const struct Mem *mem)
{
	assert(mem_is_bytes(mem));
	return (mem->flags & MEM_Static) != 0;
}

static inline bool
mem_is_ephemeral(const struct Mem *mem)
{
	assert(mem_is_bytes(mem));
	return (mem->flags & MEM_Ephem) != 0;
}

static inline bool
mem_is_dynamic(const struct Mem *mem)
{
	assert(mem_is_bytes(mem));
	return (mem->flags & MEM_Dyn) != 0;
}

static inline bool
mem_is_allocated(const struct Mem *mem)
{
	return mem_is_bytes(mem) && mem->z == mem->zMalloc;
}

static inline bool
mem_is_cleared(const struct Mem *mem)
{
	assert((mem->flags & MEM_Cleared) == 0 || mem->type == MEM_TYPE_NULL);
	return (mem->flags & MEM_Cleared) != 0;
}

static inline bool
mem_is_zerobin(const struct Mem *mem)
{
	assert((mem->flags & MEM_Zero) == 0 || mem->type == MEM_TYPE_BIN);
	return (mem->flags & MEM_Zero) != 0;
}

static inline bool
mem_is_same_type(const struct Mem *mem1, const struct Mem *mem2)
{
	return mem1->type == mem2->type;
}

static inline bool
mem_is_any_null(const struct Mem *mem1, const struct Mem *mem2)
{
	return ((mem1->type| mem2->type) & MEM_TYPE_NULL) != 0;
}

/**
 * Return a string that contains description of type and value of MEM. String is
 * either allocated using static_alloc() of just a static variable. This
 * function should only be used for debugging or displaying MEM values in
 * description of errors.
 */
const char *
mem_str(const struct Mem *mem);

/** Initialize MEM and set NULL. */
void
mem_create(struct Mem *mem);

/** Free all allocated memory in MEM and set MEM to NULL. */
void
mem_destroy(struct Mem *mem);

/** Clear MEM and set it to NULL. */
void
mem_set_null(struct Mem *mem);

/** Clear MEM and set it to INTEGER. */
void
mem_set_int(struct Mem *mem, int64_t value, bool is_neg);

/** Clear MEM and set it to UNSIGNED. */
void
mem_set_uint(struct Mem *mem, uint64_t value);

/** Clear MEM and set it to BOOLEAN. */
void
mem_set_bool(struct Mem *mem, bool value);

/** Clear MEM and set it to DOUBLE. */
void
mem_set_double(struct Mem *mem, double value);

/** Clear MEM and set it to STRING. The string belongs to another object. */
void
mem_set_str_ephemeral(struct Mem *mem, char *value, uint32_t len);

/** Clear MEM and set it to STRING. The string is static. */
void
mem_set_str_static(struct Mem *mem, char *value, uint32_t len);

/**
 * Clear MEM and set it to STRING. The string was allocated by another object
 * and passed to MEM. MEMs with this allocation type must free given memory
 * whenever the MEM changes.
 */
void
mem_set_str_dynamic(struct Mem *mem, char *value, uint32_t len);

/**
 * Clear MEM and set it to STRING. The string was allocated by another object
 * and passed to MEM. MEMs with this allocation type only deallocate the string
 * on destruction. Also, the memory may be reallocated if MEM is set to a
 * different value of this allocation type.
 */
void
mem_set_str_allocated(struct Mem *mem, char *value, uint32_t len);

/**
 * Clear MEM and set it to NULL-terminated STRING. The string belongs to
 * another object.
 */
void
mem_set_str0_ephemeral(struct Mem *mem, char *value);

/** Clear MEM and set it to NULL-terminated STRING. The string is static. */
void
mem_set_str0_static(struct Mem *mem, char *value);

/**
 * Clear MEM and set it to NULL-terminated STRING. The string was allocated by
 * another object and passed to MEM. MEMs with this allocation type must free
 * given memory whenever the MEM changes.
 */
void
mem_set_str0_dynamic(struct Mem *mem, char *value);

/**
 * Clear MEM and set it to NULL-terminated STRING. The string was allocated by
 * another object and passed to MEM. MEMs with this allocation type only
 * deallocate the string on destruction. Also, the memory may be reallocated if
 * MEM is set to a different value of this allocation type.
 */
void
mem_set_str0_allocated(struct Mem *mem, char *value);

static inline void
mem_set_strl_ephemeral(struct Mem *mem, char *value, int len_hint)
{
	if (len_hint < 0)
		mem_set_str0_ephemeral(mem, value);
	else
		mem_set_str_ephemeral(mem, value, len_hint);
}

static inline void
mem_set_strl_static(struct Mem *mem, char *value, int len_hint)
{
	if (len_hint < 0)
		mem_set_str0_static(mem, value);
	else
		mem_set_str_static(mem, value, len_hint);
}

static inline void
mem_set_strl_dynamic(struct Mem *mem, char *value, int len_hint)
{
	if (len_hint < 0)
		mem_set_str0_dynamic(mem, value);
	else
		mem_set_str_dynamic(mem, value, len_hint);
}

static inline void
mem_set_strl_allocated(struct Mem *mem, char *value, int len_hint)
{
	if (len_hint < 0)
		mem_set_str0_allocated(mem, value);
	else
		mem_set_str_allocated(mem, value, len_hint);
}

static inline void
mem_set_strl(struct Mem *mem, char *value, int len_hint,
	     void (*custom_free)(void *))
{
	if (custom_free == SQL_STATIC)
		return mem_set_strl_static(mem, value, len_hint);
	if (custom_free == SQL_DYNAMIC)
		return mem_set_strl_allocated(mem, value, len_hint);
	if (custom_free != SQL_TRANSIENT)
		return mem_set_strl_dynamic(mem, value, len_hint);
}

/** Copy string to a newly allocated memory. The MEM type becomes STRING. */
int
mem_copy_str(struct Mem *mem, const char *value, uint32_t len);

/**
 * Copy NULL-terminated string to a newly allocated memory. The MEM type becomes
 * STRING.
 */
int
mem_copy_str0(struct Mem *mem, const char *value);

static inline int
mem_copy_strl(struct Mem *mem, const char *value, int len_hint)
{
	if (len_hint < 0)
		return mem_copy_str0(mem, value);
	return mem_copy_str(mem, value, len_hint);
}

/**
 * Clear MEM and set it to VARBINARY. The binary value belongs to another
 * object.
 */
void
mem_set_bin_ephemeral(struct Mem *mem, char *value, uint32_t size);

/** Clear MEM and set it to VARBINARY. The binary value is static. */
void
mem_set_bin_static(struct Mem *mem, char *value, uint32_t size);

/**
 * Clear MEM and set it to VARBINARY. The binary value was allocated by another
 * object and passed to MEM. MEMs with this allocation type must free given
 * memory whenever the MEM changes.
 */
void
mem_set_bin_dynamic(struct Mem *mem, char *value, uint32_t size);

/**
 * Clear MEM and set it to VARBINARY. The binary value was allocated by another
 * object and passed to MEM. MEMs with this allocation type only deallocate the
 * string on destruction. Also, the memory may be reallocated if MEM is set to a
 * different value of this allocation type.
 */
void
mem_set_bin_allocated(struct Mem *mem, char *value, uint32_t size);

static inline void
mem_set_binl(struct Mem *mem, char *value, uint32_t size,
	     void (*custom_free)(void *))
{
	if (custom_free == SQL_STATIC)
		return mem_set_bin_static(mem, value, size);
	if (custom_free == SQL_DYNAMIC)
		return mem_set_bin_allocated(mem, value, size);
	if (custom_free != SQL_TRANSIENT)
		return mem_set_bin_dynamic(mem, value, size);
}

/**
 * Clear MEM and set it to VARBINARY. The binary value consist of n zero bytes.
 */
void
mem_set_zerobin(struct Mem *mem, int n);

/**
 * Copy binary value to a newly allocated memory. The MEM type becomes
 * VARBINARY.
 */
int
mem_copy_bin(struct Mem *mem, const char *value, uint32_t size);

/**
 * Clear MEM and set it to MAP. The binary value belongs to another object. The
 * binary value must be msgpack of MAP type.
 */
void
mem_set_map_ephemeral(struct Mem *mem, char *value, uint32_t size);

/**
 * Clear MEM and set it to MAP. The binary value is static. The binary value
 * must be msgpack of MAP type.
 */
void
mem_set_map_static(struct Mem *mem, char *value, uint32_t size);

/**
 * Clear MEM and set it to MAP. The binary value was allocated by another object
 * and passed to MEM. The binary value must be msgpack of MAP type. MEMs with
 * this allocation type must free given memory whenever the MEM changes.
 */
void
mem_set_map_dynamic(struct Mem *mem, char *value, uint32_t size);

/**
 * Clear MEM and set it to MAP. The binary value was allocated by another object
 * and passed to MEM. The binary value must be msgpack of MAP type. MEMs with
 * this allocation type only deallocate the string on destruction. Also, the
 * memory may be reallocated if MEM is set to a different value of this
 * allocation type.
 */
void
mem_set_map_allocated(struct Mem *mem, char *value, uint32_t size);

/**
 * Clear MEM and set it to ARRAY. The binary value belongs to another object.
 * The binary value must be msgpack of ARRAY type.
 */
void
mem_set_array_ephemeral(struct Mem *mem, char *value, uint32_t size);

/**
 * Clear MEM and set it to ARRAY. The binary value is static. The binary value
 * must be msgpack of ARRAY type.
 */
void
mem_set_array_static(struct Mem *mem, char *value, uint32_t size);

/**
 * Clear MEM and set it to ARRAY. The binary value was allocated by another
 * object and passed to MEM. The binary value must be msgpack of ARRAY type.
 * MEMs with this allocation type must free given memory whenever the MEM
 * changes.
 */
void
mem_set_array_dynamic(struct Mem *mem, char *value, uint32_t size);

/**
 * Clear MEM and set it to ARRAY. The binary value was allocated by another
 * object and passed to MEM. The binary value must be msgpack of ARRAY type.
 * MEMs with this allocation type only deallocate the string on destruction.
 * Also, the memory may be reallocated if MEM is set to a different value of
 * this allocation type.
 */
void
mem_set_array_allocated(struct Mem *mem, char *value, uint32_t size);

/** Clear MEM and set it to invalid state. */
void
mem_set_invalid(struct Mem *mem);

/** Clear MEM and set pointer to be its value. */
void
mem_set_ptr(struct Mem *mem, void *ptr);

/** Clear MEM and set frame to be its value. */
void
mem_set_frame(struct Mem *mem, struct VdbeFrame *frame);

/**
 * Clear the MEM, set the function as its value, and allocate enough memory to
 * hold the accumulation structure for the aggregate function.
 */
int
mem_set_agg(struct Mem *mem, struct func *func, int size);

/** Clear MEM and set it to special, "cleared", NULL. */
void
mem_set_null_clear(struct Mem *mem);

/**
 * Copy content of MEM from one MEM to another. In case source MEM contains
 * string or binary and allocation type is not STATIC, this value is copied to
 * newly allocated by destination MEM memory.
 */
int
mem_copy(struct Mem *to, const struct Mem *from);

/**
 * Copy content of MEM from one MEM to another. In case source MEM contains
 * string or binary and allocation type is not STATIC, this value is copied as
 * value with ephemeral allocation type.
 */
void
mem_copy_as_ephemeral(struct Mem *to, const struct Mem *from);

/**
 * Move all content of source MEM to destination MEM. Source MEM is set to NULL.
 */
void
mem_move(struct Mem *to, struct Mem *from);

/**
 * Concatenate strings or binaries from the first and the second MEMs and write
 * to the result MEM. In case the first MEM or the second MEM is NULL, the
 * result MEM is set to NULL even if the result MEM is actually the first MEM.
 */
int
mem_concat(struct Mem *left, struct Mem *right, struct Mem *result);

/**
 * Add the first MEM to the second MEM and write the result to the third MEM.
 */
int
mem_add(const struct Mem *left, const struct Mem *right, struct Mem *result);

/**
 * Subtract the second MEM from the first MEM and write the result to the third
 * MEM.
 */
int
mem_sub(const struct Mem *left, const struct Mem *right, struct Mem *result);

/**
 * Multiply the first MEM by the second MEM and write the result to the third
 * MEM.
 */
int
mem_mul(const struct Mem *left, const struct Mem *right, struct Mem *result);

/**
 * Divide the first MEM by the second MEM and write the result to the third
 * MEM.
 */
int
mem_div(const struct Mem *left, const struct Mem *right, struct Mem *result);

/**
 * Divide the first MEM by the second MEM and write integer part of the result
 * to the third MEM.
 */
int
mem_rem(const struct Mem *left, const struct Mem *right, struct Mem *result);

/** Perform a bitwise AND for two MEMs and write the result to the third MEM. */
int
mem_bit_and(const struct Mem *left, const struct Mem *right,
	    struct Mem *result);

/** Perform a bitwise OR for two MEMs and write the result to the third MEM. */
int
mem_bit_or(const struct Mem *left, const struct Mem *right, struct Mem *result);

/**
 * Perform a bitwise left shift for the first MEM by value from the second MEM
 * and write the result to the third MEM.
 */
int
mem_shift_left(const struct Mem *left, const struct Mem *right,
	       struct Mem *result);

/**
 * Perform a bitwise right shift for the first MEM by value from the second MEM
 * and write the result to the third MEM.
 */
int
mem_shift_right(const struct Mem *left, const struct Mem *right,
		struct Mem *result);

/** Perform a bitwise NOT to the MEM and write the result to the second MEM. */
int
mem_bit_not(const struct Mem *mem, struct Mem *result);

/**
 * Compare two MEMs and return the result of comparison. MEMs should be of
 * BOOLEAN type or their values are converted to VARBINARY according to implicit
 * cast rules. Original MEMs are not changed.
 */
int
mem_cmp_bool(const struct Mem *a, const struct Mem *b, int *result);

/**
 * Compare two MEMs and return the result of comparison. MEMs should be of
 * VARBINARY type or their values are converted to VARBINARY according to
 * implicit cast rules. Original MEMs are not changed.
 */
int
mem_cmp_bin(const struct Mem *a, const struct Mem *b, int *result);

/**
 * Compare two MEMs and return the result of comparison. MEMs should be of
 * STRING type or their values are converted to VARBINARY according to
 * implicit cast rules. Original MEMs are not changed.
 */
int
mem_cmp_str(const struct Mem *left, const struct Mem *right, int *result,
	    const struct coll *coll);

/**
 * Compare two MEMs and return the result of comparison. MEMs should be of
 * NUMBER type or their values are converted to NUMBER according to
 * implicit cast rules. Original MEMs are not changed.
 */
int
mem_cmp_num(const struct Mem *a, const struct Mem *b, int *result);

/**
 * Convert the given MEM to INTEGER. This function and the function below define
 * the rules that are used to convert values of all other types to INTEGER. In
 * this function, the conversion from double to integer may result in loss of
 * precision.
 */
int
mem_to_int(struct Mem *mem);

/**
 * Convert the given MEM to INTEGER. This function and the function above define
 * the rules that are used to convert values of all other types to INTEGER. In
 * this function, the conversion from double to integer is only possible if it
 * is lossless.
 */
int
mem_to_int_precise(struct Mem *mem);

/**
 * Convert the given MEM to DOUBLE. This function defines the rules that are
 * used to convert values of all other types to DOUBLE.
 */
int
mem_to_double(struct Mem *mem);

/**
 * Convert the given MEM to NUMBER. This function defines the rules that are
 * used to convert values of all other types to NUMBER.
 */
int
mem_to_number(struct Mem *mem);

/**
 * Convert the given MEM to STRING. This function and the function below define
 * the rules that are used to convert values of all other types to STRING. In
 * this function, the string received after the convertion may be not
 * NULL-terminated.
 */
int
mem_to_str(struct Mem *mem);

/**
 * Convert the given MEM to STRING. This function and the function above define
 * the rules that are used to convert values of all other types to STRING. In
 * this function, the string received after convertion is NULL-terminated.
 */
int
mem_to_str0(struct Mem *mem);

/** Convert the given MEM to given type according to explicit cast rules. */
int
mem_cast_explicit(struct Mem *mem, enum field_type type);

/** Convert the given MEM to given type according to implicit cast rules. */
int
mem_cast_implicit(struct Mem *mem, enum field_type type);

/**
 * Convert the given MEM to given type according to legacy implicit cast rules.
 */
int
mem_cast_implicit_old(struct Mem *mem, enum field_type type);

/**
 * Return value for MEM of INTEGER type. For MEM of all other types convert
 * value of the MEM to INTEGER if possible and return converted value. Original
 * MEM is not changed.
 */
int
mem_get_int(const struct Mem *mem, int64_t *i, bool *is_neg);

/**
 * Return value of MEM converted to int64_t. This function is not safe, since it
 * returns 0 if mem_get_int() fails. There is no proper handling for this case.
 * Also it works incorrectly with integer values that are more than INT64_MAX.
 */
static inline int64_t
mem_get_int_unsafe(const struct Mem *mem)
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
mem_get_uint(const struct Mem *mem, uint64_t *u);

/**
 * Return value of MEM converted to uint64_t. This function is not safe, since it
 * returns 0 if mem_get_uint() fails. There is no proper handling for this case.
 */
static inline uint64_t
mem_get_uint_unsafe(const struct Mem *mem)
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
mem_get_double(const struct Mem *mem, double *d);

/**
 * Return value of MEM converted to double. This function is not safe since
 * there is no proper processing in case mem_get_double() return an error. In
 * this case this functions returns 0.
 */
static inline double
mem_get_double_unsafe(const struct Mem *mem)
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
mem_get_bool(const struct Mem *mem, bool *b);

/**
 * Return value of MEM converted to boolean. This function is not safe since
 * there is no proper processing in case mem_get_bool() return an error. In
 * this case this function returns FALSE.
 */
static inline bool
mem_get_bool_unsafe(const struct Mem *mem)
{
	bool b;
	if (mem_get_bool(mem, &b) != 0)
		return false;
	return b;
}

/**
 * Return value for MEM of STRING type if MEM contains a NULL-terminated string.
 * Otherwise convert value of the MEM to NULL-terminated string if possible and
 * return converted value. Original MEM is not changed.
 */
int
mem_get_str0(const struct Mem *mem, const char **s);

/**
 * Return value for MEM of STRING type if MEM contains NULL-terminated string.
 * Otherwise convert MEM to MEM of string type that contains NULL-terminated
 * string and return its value. Return NULL if conversion is impossible.
 */
static inline const char *
mem_as_str0(struct Mem *mem)
{
	if (mem_to_str0(mem) != 0)
		return NULL;
	return mem->z;
}

/**
 * Return value for MEM of VARBINARY type. For MEM of all other types convert
 * value of the MEM to VARBINARY if possible and return converted value.
 * Original MEM is not changed.
 */
int
mem_get_bin(const struct Mem *mem, const char **s);

/**
 * Return length of value for MEM of STRING or VARBINARY type. Original MEM is
 * not changed.
 */
int
mem_len(const struct Mem *mem, uint32_t *len);

/**
 * Return length of value for MEM of STRING or VARBINARY type. This function is
 * not safe since there is no proper processing in case mem_len() return an
 * error. In this case this function returns 0.
 */
static inline int
mem_len_unsafe(const struct Mem *mem)
{
	uint32_t len;
	if (mem_len(mem, &len) != 0)
		return 0;
	return len;
}

/**
 * Return address of memory allocated for accumulation structure of the
 * aggregate function.
 */
int
mem_get_agg(const struct Mem *mem, void **accum);

/**
 * Simple type to str convertor. It is used to simplify
 * error reporting.
 */
char *
mem_type_to_str(const struct Mem *p);

/*
 * Return the MP_type of the value of the MEM.
 * Analogue of sql_value_type() but operates directly on
 * transparent memory cell.
 */
enum mp_type
mem_mp_type(struct Mem *mem);

enum mp_type
sql_value_type(struct Mem *);

#ifdef SQL_DEBUG
int sqlVdbeCheckMemInvariants(struct Mem *);
void sqlVdbeMemPrettyPrint(Mem * pMem, char *zBuf);
void
registerTrace(int iReg, Mem *p);

/*
 * Return true if a memory cell is not marked as invalid.  This macro
 * is for use inside assert() statements only.
 */
#define memIsValid(M)  ((M)->type != MEM_TYPE_INVALID)
#endif

int sqlVdbeMemExpandBlob(struct Mem *);
#define ExpandBlob(P) (((P)->flags&MEM_Zero)?sqlVdbeMemExpandBlob(P):0)

/** Setters = Change MEM value. */

int sqlVdbeMemClearAndResize(struct Mem * pMem, int n);

void sqlValueFree(struct Mem *);
struct Mem *sqlValueNew(struct sql *);

/*
 * Release an array of N Mem elements
 */
void
releaseMemArray(Mem * p, int N);

#define VdbeFrameMem(p) ((Mem *)&((u8 *)p)[ROUND8(sizeof(VdbeFrame))])

int sqlVdbeMemTooBig(Mem *);

/* Return TRUE if Mem X contains dynamically allocated content - anything
 * that needs to be deallocated to avoid a leak.
 */
#define VdbeMemDynamic(X) (((X)->flags & MEM_Dyn) != 0 ||\
			   ((X)->type & (MEM_TYPE_AGG | MEM_TYPE_FRAME)) != 0)

int sqlMemCompare(const Mem *, const Mem *, const struct coll *);

/** MEM manipulate functions. */

/**
 * Memory cell mem contains the context of an aggregate function.
 * This routine calls the finalize method for that function. The
 * result of the aggregate is stored back into mem.
 *
 * Returns -1 if the finalizer reports an error. 0 otherwise.
 */
int
sql_vdbemem_finalize(struct Mem *mem, struct func *func);

/** MEM and msgpack functions. */

/**
 * Perform comparison of two keys: one is packed and one is not.
 *
 * @param key1 Pointer to pointer to first key.
 * @param unpacked Pointer to unpacked tuple.
 * @param key2_idx index of key in umpacked record to compare.
 *
 * @retval +1 if key1 > pUnpacked[iKey2], -1 ptherwise.
 */
int sqlVdbeCompareMsgpack(const char **key1,
			      struct UnpackedRecord *unpacked, int key2_idx);

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
mem_from_mp_ephemeral(struct Mem *mem, const char *buf, uint32_t *len);

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
mem_from_mp(struct Mem *mem, const char *buf, uint32_t *len);

/**
 * Perform encoding memory variable to stream.
 * @param stream Initialized mpstream encoder object.
 * @param var Vdbe memory variable to encode with stream.
 */
void
mpstream_encode_vdbe_mem(struct mpstream *stream, struct Mem *var);

/**
 * Perform encoding field_count Vdbe memory fields on region as
 * msgpack array.
 * @param fields The first Vdbe memory field to encode.
 * @param field_count Count of fields to encode.
 * @param[out] tuple_size Size of encoded tuple.
 * @param region Region to use.
 * @retval NULL on error, diag message is set.
 * @retval Pointer to valid tuple on success.
 */
char *
sql_vdbe_mem_encode_tuple(struct Mem *fields, uint32_t field_count,
			  uint32_t *tuple_size, struct region *region);
