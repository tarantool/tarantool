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
	u32 flags;		/* Some combination of MEM_Null, MEM_Str, MEM_Dyn, etc. */
	/** Subtype for this value. */
	enum sql_subtype subtype;
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

/*
 * Size of struct Mem not including the Mem.zMalloc member or anything that
 * follows.
 */
#define MEMCELLSIZE offsetof(Mem,zMalloc)

/* One or more of the following flags are set to indicate the validOK
 * representations of the value stored in the Mem struct.
 *
 * If the MEM_Null flag is set, then the value is an SQL NULL value.
 * No other flags may be set in this case.
 *
 * If the MEM_Str flag is set then Mem.z points at a string representation.
 * Usually this is encoded in the same unicode encoding as the main
 * database (see below for exceptions). If the MEM_Term flag is also
 * set, then the string is nul terminated. The MEM_Int and MEM_Real
 * flags may coexist with the MEM_Str flag.
 */
#define MEM_Null      0x0001	/* Value is NULL */
#define MEM_Str       0x0002	/* Value is a string */
#define MEM_Int       0x0004	/* Value is an integer */
#define MEM_Real      0x0008	/* Value is a real number */
#define MEM_Blob      0x0010	/* Value is a BLOB */
#define MEM_Bool      0x0020    /* Value is a bool */
#define MEM_UInt      0x0040	/* Value is an unsigned integer */
#define MEM_Frame     0x0080	/* Value is a VdbeFrame object */
#define MEM_Undefined 0x0100	/* Value is undefined */
#define MEM_Cleared   0x0200	/* NULL set by OP_Null, not from data */
#define MEM_TypeMask  0x83ff	/* Mask of type bits */

/* Whenever Mem contains a valid string or blob representation, one of
 * the following flags must be set to determine the memory management
 * policy for Mem.z.  The MEM_Term flag tells us whether or not the
 * string is \000 or \u0000 terminated
 */
#define MEM_Term      0x0400	/* String rep is nul terminated */
#define MEM_Dyn       0x0800	/* Need to call Mem.xDel() on Mem.z */
#define MEM_Static    0x1000	/* Mem.z points to a static string */
#define MEM_Ephem     0x2000	/* Mem.z points to an ephemeral string */
#define MEM_Agg       0x4000	/* Mem.z points to an agg function context */
#define MEM_Zero      0x8000	/* Mem.i contains count of 0s appended to blob */
#define MEM_Subtype   0x10000	/* Mem.eSubtype is valid */
#define MEM_Ptr       0x20000	/* Value is a generic pointer */

/**
 * In contrast to Mem_TypeMask, this one allows to get
 * pure type of memory cell, i.e. without _Dyn/_Zero and other
 * auxiliary flags.
 */
enum {
	MEM_PURE_TYPE_MASK = 0x7f
};

static_assert(MEM_PURE_TYPE_MASK == (MEM_Null | MEM_Str | MEM_Int | MEM_Real |
				     MEM_Blob | MEM_Bool | MEM_UInt),
	      "value of type mask must consist of corresponding to memory "\
	      "type bits");

static inline bool
mem_is_null(const struct Mem *mem)
{
	return (mem->flags & MEM_Null) != 0;
}

static inline bool
mem_is_uint(const struct Mem *mem)
{
	return (mem->flags & MEM_UInt) != 0;
}

static inline bool
mem_is_nint(const struct Mem *mem)
{
	return (mem->flags & MEM_Int) != 0;
}

static inline bool
mem_is_str(const struct Mem *mem)
{
	return (mem->flags & MEM_Str) != 0;
}

static inline bool
mem_is_num(const struct Mem *mem)
{
	return (mem->flags & (MEM_Real | MEM_Int | MEM_UInt)) != 0;
}

static inline bool
mem_is_double(const struct Mem *mem)
{
	return (mem->flags & MEM_Real) != 0;
}

static inline bool
mem_is_int(const struct Mem *mem)
{
	return (mem->flags & (MEM_Int | MEM_UInt)) != 0;
}

static inline bool
mem_is_bool(const struct Mem *mem)
{
	return (mem->flags & MEM_Bool) != 0;
}

static inline bool
mem_is_bin(const struct Mem *mem)
{
	return (mem->flags & MEM_Blob) != 0;
}

static inline bool
mem_is_map(const struct Mem *mem)
{
	assert((mem->flags & MEM_Subtype) == 0 || (mem->flags & MEM_Blob) != 0);
	assert((mem->flags & MEM_Subtype) == 0 ||
	       mem->subtype == SQL_SUBTYPE_MSGPACK);
	return (mem->flags & MEM_Subtype) != 0 && mp_typeof(*mem->z) == MP_MAP;
}

static inline bool
mem_is_array(const struct Mem *mem)
{
	assert((mem->flags & MEM_Subtype) == 0 || (mem->flags & MEM_Blob) != 0);
	assert((mem->flags & MEM_Subtype) == 0 ||
	       mem->subtype == SQL_SUBTYPE_MSGPACK);
	return (mem->flags & MEM_Subtype) != 0 &&
	       mp_typeof(*mem->z) == MP_ARRAY;
}

static inline bool
mem_is_agg(const struct Mem *mem)
{
	return (mem->flags & MEM_Agg) != 0;
}

static inline bool
mem_is_bytes(const struct Mem *mem)
{
	return (mem->flags & (MEM_Blob | MEM_Str)) != 0;
}

static inline bool
mem_is_frame(const struct Mem *mem)
{
	return (mem->flags & MEM_Frame) != 0;
}

static inline bool
mem_is_invalid(const struct Mem *mem)
{
	return (mem->flags & MEM_Undefined) != 0;
}

static inline bool
mem_is_static(const struct Mem *mem)
{
	assert((mem->flags & (MEM_Str | MEM_Blob)) != 0);
	return (mem->flags & MEM_Static) != 0;
}

static inline bool
mem_is_ephemeral(const struct Mem *mem)
{
	assert((mem->flags & (MEM_Str | MEM_Blob)) != 0);
	return (mem->flags & MEM_Ephem) != 0;
}

static inline bool
mem_is_dynamic(const struct Mem *mem)
{
	assert((mem->flags & (MEM_Str | MEM_Blob)) != 0);
	return (mem->flags & MEM_Dyn) != 0;
}

static inline bool
mem_is_allocated(const struct Mem *mem)
{
	return (mem->flags & (MEM_Str | MEM_Blob)) != 0 &&
	       mem->z == mem->zMalloc;
}

static inline bool
mem_is_cleared(const struct Mem *mem)
{
	assert((mem->flags & MEM_Cleared) == 0 || (mem->flags & MEM_Null) != 0);
	return (mem->flags & MEM_Cleared) != 0;
}

static inline bool
mem_is_zerobin(const struct Mem *mem)
{
	assert((mem->flags & MEM_Zero) == 0 || (mem->flags & MEM_Blob) != 0);
	return (mem->flags & MEM_Zero) != 0;
}

static inline bool
mem_is_same_type(const struct Mem *mem1, const struct Mem *mem2)
{
	return (mem1->flags & MEM_PURE_TYPE_MASK) ==
	       (mem2->flags & MEM_PURE_TYPE_MASK);
}

static inline bool
mem_is_any_null(const struct Mem *mem1, const struct Mem *mem2)
{
	return ((mem1->flags | mem2->flags) & MEM_Null) != 0;
}

/**
 * Return a string that represent content of MEM. String is either allocated
 * using static_alloc() of just a static variable.
 */
const char *
mem_str(const struct Mem *mem);

/** Initialize MEM and set NULL. */
void
mem_create(struct Mem *mem);

/** Free all allocated memory in MEM and set MEM to NULL. */
void
mem_destroy(struct Mem *mem);

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
u16
numericType(Mem *pMem);

int sqlValueBytes(struct Mem *);

#ifdef SQL_DEBUG
int sqlVdbeCheckMemInvariants(struct Mem *);
void sqlVdbeMemPrettyPrint(Mem * pMem, char *zBuf);
void
registerTrace(int iReg, Mem *p);

/*
 * Return true if a memory cell is not marked as invalid.  This macro
 * is for use inside assert() statements only.
 */
#define memIsValid(M)  ((M)->flags & MEM_Undefined)==0
#endif

/**
 * Try to convert a string value into a numeric representation
 * if we can do so without loss of information. Firstly, value
 * is attempted to be converted to integer, and in case of fail -
 * to floating point number. Note that function is assumed to be
 * called on memory cell containing string, i.e. mem->type == MEM_Str.
 *
 * @param record Memory cell containing value to be converted.
 * @retval 0 If value can be converted to integer or number.
 * @retval -1 Otherwise.
 */
int
mem_apply_numeric_type(struct Mem *record);
int sqlVdbeMemRealify(struct Mem *);

/**
 * Convert @a mem to NUMBER type, so that after conversion it has
 * one of types MEM_Real, MEM_Int or MEM_UInt. If conversion is
 * not possible, function returns -1.
 *
 * Beware - this function changes value and type of @a mem
 * argument.
 */
int
vdbe_mem_numerify(struct Mem *mem);

int sqlVdbeMemCast(struct Mem *, enum field_type type);
int mem_apply_integer_type(struct Mem *);
int sqlVdbeMemStringify(struct Mem *);
int sqlVdbeMemNulTerminate(struct Mem *);
int sqlVdbeMemExpandBlob(struct Mem *);
#define ExpandBlob(P) (((P)->flags&MEM_Zero)?sqlVdbeMemExpandBlob(P):0)
void sql_value_apply_type(struct Mem *val, enum field_type type);


/**
 * Processing is determined by the field type parameter:
 *
 * INTEGER:
 *    If memory holds floating point value and it can be
 *    converted without loss (2.0 - > 2), it's type is
 *    changed to INT. Otherwise, simply return success status.
 *
 * NUMBER:
 *    If memory holds INT or floating point value,
 *    no actions take place.
 *
 * STRING:
 *    Convert mem to a string representation.
 *
 * SCALAR:
 *    Mem is unchanged, but flag is set to BLOB in case of
 *    scalar-like type. Otherwise, (MAP, ARRAY) conversion
 *    is impossible.
 *
 * BOOLEAN:
 *    If memory holds BOOLEAN no actions take place.
 *
 * ANY:
 *    Mem is unchanged, no actions take place.
 *
 * MAP/ARRAY:
 *    These types can't be casted to scalar ones, or to each
 *    other. So the only valid conversion is to type itself.
 *
 * @param record The value to apply type to.
 * @param type The type to be applied.
 */
int
mem_apply_type(struct Mem *record, enum field_type type);

/**
 * Convert the numeric value contained in MEM to another numeric
 * type.
 *
 * @param mem The MEM that contains the numeric value.
 * @param type The type to convert to.
 * @retval 0 if the conversion was successful, -1 otherwise.
 */
int
mem_convert_to_numeric(struct Mem *mem, enum field_type type);

/** Setters = Change MEM value. */

int sqlVdbeMemGrow(struct Mem * pMem, int n, int preserve);
int sqlVdbeMemClearAndResize(struct Mem * pMem, int n);

void
mem_set_bool(struct Mem *mem, bool value);

/**
 * Set VDBE memory register with given pointer as a data.
 * @param mem VDBE memory register to update.
 * @param ptr Pointer to use.
 */
void
mem_set_ptr(struct Mem *mem, void *ptr);

/**
 * Set integer value. Depending on its sign MEM_Int (in case
 * of negative value) or MEM_UInt flag is set.
 */
void
mem_set_i64(struct Mem *mem, int64_t value);

/** Set unsigned value and MEM_UInt flag. */
void
mem_set_u64(struct Mem *mem, uint64_t value);

/**
 * Set integer value. According to is_neg flag value is considered
 * to be signed or unsigned.
 */
void
mem_set_int(struct Mem *mem, int64_t value, bool is_neg);

/** Set double value and MEM_Real flag. */
void
mem_set_double(struct Mem *mem, double value);

int
sqlVdbeMemSetStr(struct Mem *, const char *, int, u8, void (*)(void *));
void
sqlVdbeMemSetNull(struct Mem *);
void
sqlVdbeMemSetZeroBlob(struct Mem *, int);
void sqlValueSetStr(struct Mem *, int, const void *,
			void (*)(void *));
void sqlValueSetNull(struct Mem *);
void sqlValueFree(struct Mem *);
struct Mem *sqlValueNew(struct sql *);

/*
 * Release an array of N Mem elements
 */
void
releaseMemArray(Mem * p, int N);

/*
 * Clear any existing type flags from a Mem and replace them with f
 */
#define MemSetTypeFlag(p, f) \
   ((p)->flags = ((p)->flags&~(MEM_TypeMask|MEM_Zero))|f)

/** Getters. */

int
mem_value_bool(const struct Mem *mem, bool *b);
int sqlVdbeIntValue(struct Mem *, int64_t *, bool *is_neg);
int sqlVdbeRealValue(struct Mem *, double *);
const void *
sql_value_blob(struct Mem *);

int
sql_value_bytes(struct Mem *);

double
sql_value_double(struct Mem *);

bool
sql_value_boolean(struct Mem *val);

int
sql_value_int(struct Mem *);

sql_int64
sql_value_int64(struct Mem *);

uint64_t
sql_value_uint64(struct Mem *val);

const unsigned char *
sql_value_text(struct Mem *);

const void *sqlValueText(struct Mem *);

#define VdbeFrameMem(p) ((Mem *)&((u8 *)p)[ROUND8(sizeof(VdbeFrame))])

enum sql_subtype
sql_value_subtype(sql_value * pVal);

const Mem *
columnNullValue(void);

/** Checkers. */

int sqlVdbeMemTooBig(Mem *);

/* Return TRUE if Mem X contains dynamically allocated content - anything
 * that needs to be deallocated to avoid a leak.
 */
#define VdbeMemDynamic(X)  \
  (((X)->flags&(MEM_Agg|MEM_Dyn|MEM_Frame))!=0)


int sqlMemCompare(const Mem *, const Mem *, const struct coll *);

/**
 * Check that MEM_type of the mem is compatible with given type.
 *
 * @param mem The MEM that contains the value to check.
 * @param type The type to check.
 * @retval TRUE if the MEM_type of the value and the given type
 *         are compatible, FALSE otherwise.
 */
bool
mem_is_type_compatible(struct Mem *mem, enum field_type type);

/** MEM manipulate functions. */

int
vdbe_mem_alloc_blob_region(struct Mem *vdbe_mem, uint32_t size);
int sqlVdbeMemCopy(Mem *, const Mem *);
void sqlVdbeMemShallowCopy(Mem *, const Mem *, int);
void sqlVdbeMemMove(Mem *, Mem *);
int sqlVdbeMemMakeWriteable(Mem *);

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
 * Decode msgpack and save value into VDBE memory cell.
 *
 * @param buf Buffer to deserialize msgpack from.
 * @param mem Memory cell to write value into.
 * @param len[out] Length of decoded part.
 * @retval Return code: < 0 in case of error.
 * @retval 0 on success.
 */
int
vdbe_decode_msgpack_into_mem(const char *buf, struct Mem *mem, uint32_t *len);

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
