/*
 * Copyright 2010-2017, Tarantool AUTHORS, please see AUTHORS file.
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

/*
 * This is the header file for information that is private to the
 * VDBE.  This information used to all be at the top of the single
 * source code file "vdbe.c".  When that file became too big (over
 * 6000 lines long) it was split up into several smaller files and
 * this header information was factored out.
 */
#ifndef SQL_VDBEINT_H
#define SQL_VDBEINT_H

/*
 * SQL is translated into a sequence of instructions to be
 * executed by a virtual machine.  Each instruction is an instance
 * of the following structure.
 */
typedef struct VdbeOp Op;

struct func;

/*
 * Boolean values
 */
typedef unsigned Bool;

/* Opaque type used by code in vdbesort.c */
typedef struct VdbeSorter VdbeSorter;

/* Types of VDBE cursors */
#define CURTYPE_TARANTOOL   0
#define CURTYPE_SORTER      1
#define CURTYPE_PSEUDO      2

/*
 * A VdbeCursor is an superclass (a wrapper) for various cursor objects:
 *
 *      * A Tarantool cursor
 *          -  On either an ephemeral or ordinary space
 *      * A sorter
 *      * A one-row "pseudotable" stored in a single register
 */
typedef struct VdbeCursor VdbeCursor;
struct VdbeCursor {
	u8 eCurType;		/* One of the CURTYPE_* values above */
	u8 nullRow;		/* True if pointing to a row with no data */
#ifdef SQL_DEBUG
	u8 seekOp;		/* Most recent seek operation on this cursor */
#endif
	i64 seqCount;		/* Sequence counter */

	/* Cached OP_Column parse information is only valid if cacheStatus matches
	 * Vdbe.cacheCtr.  Vdbe.cacheCtr will never take on the value of
	 * CACHE_STALE (0) and so setting cacheStatus=CACHE_STALE guarantees that
	 * the cache is out of date.
	 */
	u32 cacheStatus;	/* Cache is valid if this matches Vdbe.cacheCtr */
	int seekResult;		/* Result of previous sqlCursorMoveto() or 0
				 * if there have been no prior seeks on the cursor.
				 */
	/* NB: seekResult does not distinguish between "no seeks have ever occurred
	 * on this cursor" and "the most recent seek was an exact match".
	 */

	/* When a new VdbeCursor is allocated, only the fields above are zeroed.
	 * The fields that follow are uninitialized, and must be individually
	 * initialized prior to first use.
	 */
	union {
		BtCursor *pCursor;	/* CURTYPE_TARANTOOL */
		int pseudoTableReg;	/* CURTYPE_PSEUDO. Reg holding content. */
		VdbeSorter *pSorter;	/* CURTYPE_SORTER. Sorter object */
	} uc;
	/** Info about keys needed by index cursors. */
	struct key_def *key_def;
	i16 nField;		/* Number of fields in the header */
	/**
	 * Auxiliary VDBE structure to speed-up tuple data
	 * field access.
	 */
	struct vdbe_field_ref field_ref;
};

/*
 * A value for VdbeCursor.cacheStatus that means the cache is always invalid.
 */
#define CACHE_STALE 0

/*
 * When a sub-program is executed (OP_Program), a structure of this type
 * is allocated to store the current value of the program counter, as
 * well as the current memory cell array and various other frame specific
 * values stored in the Vdbe struct. When the sub-program is finished,
 * these values are copied back to the Vdbe from the VdbeFrame structure,
 * restoring the state of the VM to as it was before the sub-program
 * began executing.
 *
 * The memory for a VdbeFrame object is allocated and managed by a memory
 * cell in the parent (calling) frame. When the memory cell is deleted or
 * overwritten, the VdbeFrame object is not freed immediately. Instead, it
 * is linked into the Vdbe.pDelFrame list. The contents of the Vdbe.pDelFrame
 * list is deleted when the VM is reset in VdbeHalt(). The reason for doing
 * this instead of deleting the VdbeFrame immediately is to avoid recursive
 * calls to sqlVdbeMemRelease() when the memory cells belonging to the
 * child frame are released.
 *
 * The currently executing frame is stored in Vdbe.pFrame. Vdbe.pFrame is
 * set to NULL if the currently executing frame is the main program.
 */
typedef struct VdbeFrame VdbeFrame;
struct VdbeFrame {
	Vdbe *v;		/* VM this frame belongs to */
	VdbeFrame *pParent;	/* Parent of this frame, or NULL if parent is main */
	Op *aOp;		/* Program instructions for parent frame */
	i64 *anExec;		/* Event counters from parent frame */
	Mem *aMem;		/* Array of memory cells for parent frame */
	VdbeCursor **apCsr;	/* Array of Vdbe cursors for parent frame */
	void *token;		/* Copy of SubProgram.token */
	int nCursor;		/* Number of entries in apCsr */
	int pc;			/* Program Counter in parent (calling) frame */
	int nOp;		/* Size of aOp array */
	int nMem;		/* Number of entries in aMem */
	int nChildMem;		/* Number of memory cells for child frame */
	int nChildCsr;		/* Number of cursors for child frame */
	int nChange;		/* Statement changes (Vdbe.nChange)     */
	int nDbChange;		/* Value of db->nChange */
};

#define VdbeFrameMem(p) ((Mem *)&((u8 *)p)[ROUND8(sizeof(VdbeFrame))])

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
		VdbeFrame *pFrame;	/* Used when flags==MEM_Frame */
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

/**
 * Simple type to str convertor. It is used to simplify
 * error reporting.
 */
char *
mem_type_to_str(const struct Mem *p);

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

/* Return TRUE if Mem X contains dynamically allocated content - anything
 * that needs to be deallocated to avoid a leak.
 */
#define VdbeMemDynamic(X)  \
  (((X)->flags&(MEM_Agg|MEM_Dyn|MEM_Frame))!=0)

/*
 * Clear any existing type flags from a Mem and replace them with f
 */
#define MemSetTypeFlag(p, f) \
   ((p)->flags = ((p)->flags&~(MEM_TypeMask|MEM_Zero))|f)

/*
 * Return true if a memory cell is not marked as invalid.  This macro
 * is for use inside assert() statements only.
 */
#ifdef SQL_DEBUG
#define memIsValid(M)  ((M)->flags & MEM_Undefined)==0
#endif

/*
 * The "context" argument for an installable function.  A pointer to an
 * instance of this structure is the first argument to the routines used
 * implement the SQL functions.
 *
 * There is a typedef for this structure in sql.h.  So all routines,
 * even the public interface to sql, can use a pointer to this structure.
 * But this file is the only place where the internal details of this
 * structure are known.
 *
 * This structure is defined inside of vdbeInt.h because it uses substructures
 * (Mem) which are only defined there.
 */
struct sql_context {
	Mem *pOut;		/* The return value is stored here */
	/* A pointer to function implementation. */
	struct func *func;
	Mem *pMem;		/* Memory cell used to store aggregate context */
	Vdbe *pVdbe;		/* The VM that owns this context */
	/** Instruction number of OP_BuiltinFunction0. */
	int iOp;
	/*
	 * True, if an error occurred during the execution of the
	 * function.
	 */
	bool is_aborted;
	u8 skipFlag;		/* Skip accumulator loading if true */
	u8 argc;		/* Number of arguments */
	sql_value *argv[1];	/* Argument set */
};

/* A bitfield type for use inside of structures.  Always follow with :N where
 * N is the number of bits.
 */
typedef unsigned bft;		/* Bit Field Type */

typedef struct ScanStatus ScanStatus;
struct ScanStatus {
	int addrExplain;	/* OP_Explain for loop */
	int addrLoop;		/* Address of "loops" counter */
	int addrVisit;		/* Address of "rows visited" counter */
	int iSelectID;		/* The "Select-ID" for this loop */
	LogEst nEst;		/* Estimated output rows per loop */
	char *zName;		/* Name of table or index */
};

struct sql_column_metadata {
	char *name;
	char *type;
	char *collation;
	/**
	 * -1 is for any member of result set except for pure
	 * columns: all other expressions are nullable by default.
	 */
	int8_t nullable;
	/** True if column features autoincrement property. */
	bool is_actoincrement;
	/**
	 * Span is an original expression forming result set
	 * column. In most cases it is the same as name; it is
	 * different only in case of presence of AS clause.
	 */
	char *span;
};

/*
 * An instance of the virtual machine.  This structure contains the complete
 * state of the virtual machine.
 *
 * The "sql_stmt" structure pointer that is returned by sql_prepare()
 * is really a pointer to an instance of this structure.
 */
struct Vdbe {
	sql *db;		/* The database connection that owns this statement */
	Vdbe *pPrev, *pNext;	/* Linked list of VDBEs with the same Vdbe.db */
	Parse *pParse;		/* Parsing context used to create this Vdbe */
	ynVar nVar;		/* Number of entries in aVar[] */
	u32 magic;		/* Magic number for sanity checking */
	int nMem;		/* Number of memory locations currently allocated */
	int nCursor;		/* Number of slots in apCsr[] */
	u32 cacheCtr;		/* VdbeCursor row cache generation counter */
	int pc;			/* The program counter */
	/** True, if error occured during VDBE execution. */
	bool is_aborted;
	int nChange;		/* Number of db changes made since last reset */
	int iStatement;		/* Statement number (or 0 if has not opened stmt) */
	i64 iCurrentTime;	/* Value of julianday('now') for this statement */
	i64 nFkConstraint;	/* Number of imm. FK constraints this VM */
	uint32_t schema_ver;	/* Schema version at the moment of VDBE creation. */

	/*
	 * In recursive triggers we can execute INSERT/UPDATE OR IGNORE
	 * statements. If IGNORE error action happened inside a trigger,
	 * an IgnoreRaised exception is being generated and recursion stops.
	 * But now INSERT OR IGNORE query bytecode has been optimized and
	 * ignoreRaised variable helps to track such situations
	 */
	u8 ignoreRaised;	/* Flag for ON CONFLICT IGNORE for triggers */
	/** The auto-commit flag. */
	bool auto_commit;
	/**
	 * List of ids generated in current VDBE. It is returned
	 * as metadata of SQL response.
	 */
	struct stailq autoinc_id_list;

	/* When allocating a new Vdbe object, all of the fields below should be
	 * initialized to zero or NULL
	 */

	Op *aOp;		/* Space to hold the virtual machine's program */
	Mem *aMem;		/* The memory locations */
	Mem **apArg;		/* Arguments to currently executing user function */
	/** SQL metadata for DML/DQL queries. */
	struct sql_column_metadata *metadata;
	Mem *pResultSet;	/* Pointer to an array of results */
	VdbeCursor **apCsr;	/* One element of this array for each open cursor */
	Mem *aVar;		/* Values for the OP_Variable opcode. */
	/**
	 * Array which contains positions of variables to be
	 * bound in resulting set of SELECT.
	 **/
	uint32_t *var_pos;
	/**
	 * Number of variables to be bound in result set.
	 * In other words - size of @var_pos array.
	 * For example:
	 * SELECT ?, ? WHERE id = ?;
	 * Result set consists of two binding variables.
	 */
	uint32_t res_var_count;
	VList *pVList;		/* Name of variables */
	i64 startTime;		/* Time when query started - used for profiling */
	int nOp;		/* Number of instructions in the program */
	u16 nResColumn;		/* Number of columns in one row of the result set */
	u8 errorAction;		/* Recovery action to do in case of an error */
	bft expired:1;		/* True if the VM needs to be recompiled */
	bft doingRerun:1;	/* True if rerunning after an auto-reprepare */
	bft explain:2;		/* True if EXPLAIN present on SQL command */
	bft changeCntOn:1;	/* True to update the change-counter */
	bft runOnlyOnce:1;	/* Automatically expire on reset */
	u32 aCounter[5];	/* Counters used by sql_stmt_status() */
	char *zSql;		/* Text of the SQL statement that generated this */
	void *pFree;		/* Free this when deleting the vdbe */
	VdbeFrame *pFrame;	/* Parent frame */
	VdbeFrame *pDelFrame;	/* List of frame objects to free on VM reset */
	int nFrame;		/* Number of frames in pFrame list */
	SubProgram *pProgram;	/* Linked list of all sub-programs used by VM */
	/** Parser flags with which this object was built. */
	uint32_t sql_flags;
	/* Anonymous savepoint for aborts only */
	struct txn_savepoint *anonymous_savepoint;
};

/*
 * The following are allowed values for Vdbe.magic
 */
#define VDBE_MAGIC_INIT     0x16bceaa5	/* Building a VDBE program */
#define VDBE_MAGIC_RUN      0x2df20da3	/* VDBE is ready to execute */
#define VDBE_MAGIC_HALT     0x319c2973	/* VDBE has completed execution */
#define VDBE_MAGIC_RESET    0x48fa9f76	/* Reset and ready to run again */
#define VDBE_MAGIC_DEAD     0x5606c3c8	/* The VDBE has been deallocated */

/*
 * Function prototypes
 */
void sqlVdbeFreeCursor(Vdbe *, VdbeCursor *);
void sqlVdbePopStack(Vdbe *, int);
int sqlVdbeCursorRestore(VdbeCursor *);
#if defined(SQL_DEBUG) || defined(VDBE_PROFILE)
void sqlVdbePrintOp(FILE *, int, Op *);
#endif
u32 sqlVdbeSerialTypeLen(u32);
u32 sqlVdbeSerialType(Mem *, int, u32 *);
u32 sqlVdbeSerialPut(unsigned char *, Mem *, u32);
u32 sqlVdbeSerialGet(const unsigned char *, u32, Mem *);

int sqlVdbeExec(Vdbe *);
int sqlVdbeList(Vdbe *);

int sqlVdbeHalt(Vdbe *);
int sqlVdbeMemTooBig(Mem *);
int sqlVdbeMemCopy(Mem *, const Mem *);
void sqlVdbeMemShallowCopy(Mem *, const Mem *, int);
void sqlVdbeMemMove(Mem *, Mem *);
int sqlVdbeMemNulTerminate(Mem *);
int sqlVdbeMemSetStr(Mem *, const char *, int, u8, void (*)(void *));

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

void sqlVdbeMemInit(Mem *, sql *, u32);
void sqlVdbeMemSetNull(Mem *);
void sqlVdbeMemSetZeroBlob(Mem *, int);
int sqlVdbeMemMakeWriteable(Mem *);
int sqlVdbeMemStringify(Mem *);
int sqlVdbeIntValue(Mem *, int64_t *, bool *is_neg);

int sqlVdbeRealValue(Mem *, double *);

int
mem_value_bool(const struct Mem *mem, bool *b);

int mem_apply_integer_type(Mem *);
int sqlVdbeMemRealify(Mem *);

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

int sqlVdbeMemCast(Mem *, enum field_type type);
int sqlVdbeMemFromBtree(BtCursor *, u32, u32, Mem *);
void sqlVdbeMemRelease(Mem * p);

/**
 * Memory cell mem contains the context of an aggregate function.
 * This routine calls the finalize method for that function. The
 * result of the aggregate is stored back into mem.
 *
 * Returns -1 if the finalizer reports an error. 0 otherwise.
 */
int
sql_vdbemem_finalize(struct Mem *mem, struct func *func);

const char *sqlOpcodeName(int);
int sqlVdbeMemGrow(Mem * pMem, int n, int preserve);
int sqlVdbeMemClearAndResize(Mem * pMem, int n);
int sqlVdbeCloseStatement(Vdbe *, int);
void sqlVdbeFrameDelete(VdbeFrame *);
int sqlVdbeFrameRestore(VdbeFrame *);

int sqlVdbeSorterInit(struct sql *db, struct VdbeCursor *cursor);
void sqlVdbeSorterReset(sql *, VdbeSorter *);

enum field_type
vdbe_sorter_get_field_type(struct VdbeSorter *sorter, uint32_t field_no);

void sqlVdbeSorterClose(sql *, VdbeCursor *);
int sqlVdbeSorterRowkey(const VdbeCursor *, Mem *);
int sqlVdbeSorterNext(sql *, const VdbeCursor *, int *);
int sqlVdbeSorterRewind(const VdbeCursor *, int *);
int sqlVdbeSorterWrite(const VdbeCursor *, Mem *);
int sqlVdbeSorterCompare(const VdbeCursor *, Mem *, int, int *);

#ifdef SQL_DEBUG
void sqlVdbeMemAboutToChange(Vdbe *, Mem *);
int sqlVdbeCheckMemInvariants(Mem *);
#endif

int sqlVdbeCheckFk(Vdbe *, int);

int sqlVdbeMemTranslate(Mem *, u8);
#ifdef SQL_DEBUG
void sqlVdbePrintSql(Vdbe *);
void sqlVdbeMemPrettyPrint(Mem * pMem, char *zBuf);
#endif
int sqlVdbeMemHandleBom(Mem * pMem);

int sqlVdbeMemExpandBlob(Mem *);
#define ExpandBlob(P) (((P)->flags&MEM_Zero)?sqlVdbeMemExpandBlob(P):0)

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

struct mpstream;
struct region;

/** Callback to forward and error from mpstream methods. */
static inline void
set_encode_error(void *error_ctx)
{
	*(bool *)error_ctx = true;
}

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

#endif				/* !defined(SQL_VDBEINT_H) */
