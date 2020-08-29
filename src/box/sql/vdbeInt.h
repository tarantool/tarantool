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
 * calls to mem_destroy() when the memory cells belonging to the
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

int sqlVdbeExec(Vdbe *);
int sqlVdbeList(Vdbe *);

int sqlVdbeHalt(Vdbe *);

const char *sqlOpcodeName(int);
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

int sqlVdbeCheckFk(Vdbe *, int);

int sqlVdbeMemTranslate(Mem *, u8);
#ifdef SQL_DEBUG
void sqlVdbePrintSql(Vdbe *);
#endif
int sqlVdbeMemHandleBom(Mem * pMem);

struct mpstream;
struct region;

/** Callback to forward and error from mpstream methods. */
static inline void
set_encode_error(void *error_ctx)
{
	*(bool *)error_ctx = true;
}

#endif				/* !defined(SQL_VDBEINT_H) */
