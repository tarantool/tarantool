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
 * Header file for the Virtual DataBase Engine (VDBE)
 *
 * This header defines the interface to the virtual database engine
 * or VDBE.  The VDBE implements an abstract machine that runs a
 * simple program to access and modify the underlying database.
 */
#ifndef SQL_VDBE_H
#define SQL_VDBE_H
#include <stdio.h>

/*
 * A single VDBE is an opaque structure named "Vdbe".  Only routines
 * in the source file sqlVdbe.c are allowed to see the insides
 * of this structure.
 */
typedef struct Vdbe Vdbe;

/*
 * The names of the following types declared in vdbeInt.h are required
 * for the VdbeOp definition.
 */
typedef struct SubProgram SubProgram;

/*
 * A single instruction of the virtual machine has an opcode
 * and as many as three operands.  The instruction is recorded
 * as an instance of the following structure:
 */
struct VdbeOp {
	u8 opcode;		/* What operation to perform */
	signed char p4type;	/* One of the P4_xxx constants for p4 */
	u16 p5;			/* Fifth parameter is an unsigned character */
	int p1;			/* First operand */
	int p2;			/* Second parameter (often the jump destination) */
	int p3;			/* The third parameter */
	union p4union {		/* fourth parameter */
		int i;		/* Integer value if p4type==P4_INT32 */
		void *p;	/* Generic pointer */
		char *z;	/* Pointer to data for string (char array) types */
		i64 *pI64;	/* Used when p4type is P4_INT64/UINT64 */
		double *pReal;	/* Used when p4type is P4_REAL */
		/**
		 * A pointer to function implementation.
		 * Used when p4type is P4_FUNC.
		 */
		struct func *func;
		sql_context *pCtx;	/* Used when p4type is P4_FUNCCTX */
		struct coll *pColl;	/* Used when p4type is P4_COLLSEQ */
		struct Mem *pMem;	/* Used when p4type is P4_MEM */
		bool b;         /* Used when p4type is P4_BOOL */
		int *ai;	/* Used when p4type is P4_INTARRAY */
		SubProgram *pProgram;	/* Used when p4type is P4_SUBPROGRAM */
		int (*xAdvance) (BtCursor *, int *);
		/** Used when p4type is P4_KEYINFO. */
		struct sql_key_info *key_info;
		/**
		 * Used to apply types when making a record, or
		 * doing a cast.
		 */
		enum field_type *types;
		/**
		 * Information about ephemeral space field types and key parts.
		 */
		struct sql_space_info *space_info;
		/** P4 contains address of decimal. */
		decimal_t *dec;
	} p4;
#ifdef SQL_ENABLE_EXPLAIN_COMMENTS
	char *zComment;		/* Comment to improve readability */
#endif
};
typedef struct VdbeOp VdbeOp;

/*
 * A sub-routine used to implement a trigger program.
 */
struct SubProgram {
	VdbeOp *aOp;		/* Array of opcodes for sub-program */
	int nOp;		/* Elements in aOp[] */
	int nMem;		/* Number of memory cells required */
	int nCsr;		/* Number of cursors required */
	void *token;		/* id that may be used to recursive triggers */
	SubProgram *pNext;	/* Next sub-program already visited */
};

/*
 * Allowed values of VdbeOp.p4type
 */
#define P4_NOTUSED    0		/* The P4 parameter is not used */
/* Pointer to a string obtained from sql_xmalloc(). */
#define P4_DYNAMIC  (-1)
#define P4_STATIC   (-2)	/* Pointer to a static string */
#define P4_COLLSEQ  (-3)	/* P4 is a pointer to a CollSeq structure */
/** P4 is a pointer to a func structure. */
#define P4_FUNC     (-4)
/** P4 is a pointer to a decimal. */
#define P4_DEC      (-5)
#define P4_MEM      (-7)	/* P4 is a pointer to a Mem*    structure */
#define P4_TRANSIENT  0		/* P4 is a pointer to a transient string */
#define P4_REAL     (-9)	/* P4 is a 64-bit floating point value */
#define P4_INT64    (-10)	/* P4 is a 64-bit signed integer */
#define P4_UINT64   (-8)	/* P4 is a 64-bit signed integer */
#define P4_INT32    (-11)	/* P4 is a 32-bit signed integer */
#define P4_INTARRAY (-12)	/* P4 is a vector of 32-bit integers */
#define P4_SUBPROGRAM  (-13)	/* P4 is a pointer to a SubProgram structure */
#define P4_ADVANCE  (-14)	/* P4 is a pointer to BtreeNext() or BtreePrev() */
#define P4_FUNCCTX  (-16)	/* P4 is a pointer to an sql_context object */
#define P4_BOOL     (-17)	/* P4 is a bool value */
#define P4_PTR      (-18)	/* P4 is a generic pointer */
#define P4_KEYINFO  (-19)       /* P4 is a pointer to sql_key_info structure. */

/* Error message codes for OP_Halt */
#define P5_ConstraintNotNull 1
#define P5_ConstraintUnique  2
#define P5_ConstraintFK      4

/*
 * The following macro converts a relative address in the p2 field
 * of a VdbeOp structure into a negative number.
 */
#define ADDR(X)  (-1-(X))

/*
 * The makefile scans the vdbe.c source file and creates the "opcodes.h"
 * header file that defines a number for each opcode used by the VDBE.
 */
#include "opcodes.h"

/*
 * Prototypes for the VDBE interface.  See comments on the implementation
 * for a description of what each of these routines does.
 */
Vdbe *sqlVdbeCreate(Parse *);

/**
 * Prepare given VDBE to execution: initialize structs connected
 * with transaction routine: autocommit mode, deferred foreign
 * keys counter, struct representing SQL savepoint.
 * If execution context is already within active transaction,
 * just transfer transaction data to VDBE.
 *
 * @param vdbe VDBE to be prepared.
 * @retval -1 on out of memory, 0 otherwise.
 */
int
sql_vdbe_prepare(struct Vdbe *vdbe);

/**
 * Return pointer to list of generated ids.
 *
 * @param vdbe VDBE to get list of generated ids from.
 * @retval List of generated ids.
 */
struct stailq *
vdbe_autoinc_id_list(struct Vdbe *vdbe);

int sqlVdbeAddOp0(Vdbe *, int);
int sqlVdbeAddOp1(Vdbe *, int, int);
int sqlVdbeAddOp2(Vdbe *, int, int, int);
int sqlVdbeGoto(Vdbe *, int);
int sqlVdbeLoadString(Vdbe *, int, const char *);
void sqlVdbeMultiLoad(Vdbe *, int, const char *, ...);
int sqlVdbeAddOp3(Vdbe *, int, int, int, int);
int sqlVdbeAddOp4(Vdbe *, int, int, int, int, const char *zP4, int);
int sqlVdbeAddOp4Dup8(Vdbe *, int, int, int, int, const u8 *, int);
int sqlVdbeAddOp4Int(Vdbe *, int, int, int, int, int);
void sqlVdbeEndCoroutine(Vdbe *, int);
void sqlVdbeChangeOpcode(Vdbe *, u32 addr, u8);
void sqlVdbeChangeP1(Vdbe *, u32 addr, int P1);
void sqlVdbeChangeP2(Vdbe *, u32 addr, int P2);
void sqlVdbeChangeP3(Vdbe *, u32 addr, int P3);
void sqlVdbeChangeP5(Vdbe *, int P5);
void sqlVdbeJumpHere(Vdbe *, int addr);
int sqlVdbeChangeToNoop(Vdbe *, int addr);
int sqlVdbeDeletePriorOpcode(Vdbe *, u8 op);
void sqlVdbeChangeP4(Vdbe *, int addr, const char *zP4, int N);
void sqlVdbeAppendP4(Vdbe *, void *pP4, int p4type);

VdbeOp *sqlVdbeGetOp(Vdbe *, int);
int sqlVdbeMakeLabel(Vdbe *);
void sqlVdbeRunOnlyOnce(Vdbe *);

void
vdbe_metadata_delete(struct Vdbe *v);

void sqlVdbeDelete(Vdbe *);
void sqlVdbeMakeReady(Vdbe *, Parse *);
int sqlVdbeFinalize(Vdbe *);
void sqlVdbeResolveLabel(Vdbe *, int);
int sqlVdbeCurrentAddr(Vdbe *);
void sqlVdbeResetStepResult(Vdbe *);
void sqlVdbeRewind(Vdbe *);
int sqlVdbeReset(Vdbe *);
void sqlVdbeSetNumCols(Vdbe *, int);

/**
 * Set the name of the idx'th column to be returned by the SQL
 * statement. @name must be a pointer to a nul terminated string.
 * This call must be made after a call to sqlVdbeSetNumCols().
 */
int
vdbe_metadata_set_col_name(struct Vdbe *v, int col_idx, const char *name);

int
vdbe_metadata_set_col_type(struct Vdbe *v, int col_idx, const char *type);

int
vdbe_metadata_set_col_collation(struct Vdbe *p, int idx, const char *coll,
				size_t coll_len);

void
vdbe_metadata_set_col_nullability(struct Vdbe *p, int idx, int nullable);

void
vdbe_metadata_set_col_autoincrement(struct Vdbe *p, int idx);

int
vdbe_metadata_set_col_span(struct Vdbe *p, int idx, const char *span);

const struct Mem *
vdbe_get_bound_value(struct Vdbe *vdbe, int id);

void sqlVdbeCountChanges(Vdbe *);
void sqlVdbeSetSql(Vdbe *, const char *z, int n);
void sqlVdbeSwap(Vdbe *, Vdbe *);

struct VdbeOp *
sqlVdbeTakeOpArray(struct Vdbe *p, int *pnOp);

char *sqlVdbeExpandSql(Vdbe *, const char *);

/**
 * Perform unpacking of provided message pack.
 *
 * @param key_def Information about the record format
 * @param key The binary record
 * @param dest Populate this structure before returning.
 */
void sqlVdbeRecordUnpackMsgpack(struct key_def *key_def,
				    const void *msgpack,
				    struct UnpackedRecord *dest);

/**
 * This routine is used to allocate sufficient space for an UnpackedRecord
 * structure large enough to be used with sqlVdbeRecordUnpack() if
 * the first argument is a pointer to key_def structure.
 *
 * The space is either allocated using sql_xmalloc() or from within
 * the unaligned buffer passed via the second and third arguments (presumably
 * stack space). If the former, then *ppFree is set to a pointer that should
 * be eventually freed by the caller using sql_xfree(). Or, if the
 * allocation comes from the pSpace/szSpace buffer, *ppFree is set to NULL
 * before returning.
 *
 * Does not return NULL.
 */
struct UnpackedRecord *
sqlVdbeAllocUnpackedRecord(struct key_def *key_def);

void sqlVdbeLinkSubProgram(Vdbe *, SubProgram *);

/* Use SQL_ENABLE_COMMENTS to enable generation of extra comments on
 * each VDBE opcode.
 */
#ifdef SQL_ENABLE_EXPLAIN_COMMENTS
void sqlVdbeComment(Vdbe *, const char *, ...);
#define VdbeComment(X)  sqlVdbeComment X
void sqlVdbeNoopComment(Vdbe *, const char *, ...);
#define VdbeNoopComment(X)  sqlVdbeNoopComment X
#else
#define VdbeComment(X) (void) 0
#define VdbeNoopComment(X) (void) 0
#endif
#endif				/* SQL_VDBE_H */
