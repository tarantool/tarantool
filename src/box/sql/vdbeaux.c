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
 * This file contains code used for creating, destroying, and populating
 * a VDBE (or an "sql_stmt" as it is known to the outside world.)
 */
#include "fiber.h"
#include "coll/coll.h"
#include "box/session.h"
#include "box/schema.h"
#include "box/tuple_format.h"
#include "box/txn.h"
#include "msgpuck/msgpuck.h"
#include "sqlInt.h"
#include "vdbeInt.h"
#include "tarantoolInt.h"
#include "box/execute.h"

/*
 * Create a new virtual database engine.
 */
Vdbe *
sqlVdbeCreate(Parse * pParse)
{
	sql *db = pParse->db;
	Vdbe *p;
	p = sqlDbMallocRawNN(db, sizeof(Vdbe));
	if (p == 0)
		return 0;
	memset(p, 0, sizeof(Vdbe));
	p->db = db;
	stailq_create(&p->autoinc_id_list);
	if (db->pVdbe) {
		db->pVdbe->pPrev = p;
	}

	p->pNext = db->pVdbe;
	p->pPrev = 0;
	db->pVdbe = p;
	p->magic = VDBE_MAGIC_INIT;
	p->pParse = pParse;
	p->schema_ver = box_schema_version();
	assert(pParse->aLabel == 0);
	assert(pParse->nLabel == 0);
	assert(pParse->nOpAlloc == 0);
	assert(pParse->szOpAlloc == 0);
	return p;
}

int
sql_vdbe_prepare(struct Vdbe *vdbe)
{
	assert(vdbe != NULL);
	struct txn *txn = in_txn();
	vdbe->auto_commit = txn == NULL;
	return 0;
}

/*
 * Remember the SQL string for a prepared statement.
 */
void
sqlVdbeSetSql(Vdbe * p, const char *z, int n)
{
	if (p == 0)
		return;
	assert(p->zSql == 0);
	p->zSql = sqlDbStrNDup(p->db, z, n);
}

/*
 * Swap all content between two VDBE structures.
 */
void
sqlVdbeSwap(Vdbe * pA, Vdbe * pB)
{
	Vdbe tmp, *pTmp;
	char *zTmp;
	assert(pA->db == pB->db);
	tmp = *pA;
	*pA = *pB;
	*pB = tmp;
	pTmp = pA->pNext;
	pA->pNext = pB->pNext;
	pB->pNext = pTmp;
	pTmp = pA->pPrev;
	pA->pPrev = pB->pPrev;
	pB->pPrev = pTmp;
	zTmp = pA->zSql;
	pA->zSql = pB->zSql;
	pB->zSql = zTmp;
}

/*
 * Resize the Vdbe.aOp array so that it is at least nOp elements larger
 * than its current size. nOp is guaranteed to be less than or equal
 * to 1024/sizeof(Op).
 *
 * If an out-of-memory error occurs while resizing the array, return
 * -1. In this case Vdbe.aOp and Parse.nOpAlloc remain
 * unchanged (this is so that any opcodes already allocated can be
 * correctly deallocated along with the rest of the Vdbe).
 */
static int
growOpArray(Vdbe * v, int nOp)
{
	VdbeOp *pNew;
	Parse *p = v->pParse;

	/* The SQL_TEST_REALLOC_STRESS compile-time option is designed to force
	 * more frequent reallocs and hence provide more opportunities for
	 * simulated OOM faults.  SQL_TEST_REALLOC_STRESS is generally used
	 * during testing only.  With SQL_TEST_REALLOC_STRESS grow the op array
	 * by the minimum* amount required until the size reaches 512.  Normal
	 * operation (without SQL_TEST_REALLOC_STRESS) is to double the current
	 * size of the op array or add 1KB of space, whichever is smaller.
	 */
#ifdef SQL_TEST_REALLOC_STRESS
	int nNew = (p->nOpAlloc >= 512 ? p->nOpAlloc * 2 : p->nOpAlloc + nOp);
#else
	int nNew = (p->nOpAlloc ? p->nOpAlloc * 2 : (int)(1024 / sizeof(Op)));
	UNUSED_PARAMETER(nOp);
#endif

	assert((unsigned)nOp <= (1024 / sizeof(Op)));
	assert(nNew >= (p->nOpAlloc + nOp));
	pNew = sqlDbRealloc(p->db, v->aOp, nNew * sizeof(Op));
	if (pNew) {
		p->szOpAlloc = sqlDbMallocSize(p->db, pNew);
		p->nOpAlloc = p->szOpAlloc / sizeof(Op);
		v->aOp = pNew;
		return 0;
	}
	return -1;
}

#ifdef SQL_DEBUG
/*
 * This routine is just a convenient place to set a breakpoint
 * that will fire after each opcode is inserted in debug build.
 */
static void
test_addop_breakpoint(void)
{
	static int n = 0;
	n++;
}
#endif

/*
 * Add a new instruction to the list of instructions current in the
 * VDBE.  Return the address of the new instruction.
 *
 * Parameters:
 *
 *    p               Pointer to the VDBE
 *
 *    op              The opcode for this instruction
 *
 *    p1, p2, p3      Operands
 *
 * Use the sqlVdbeResolveLabel() function to fix an address and
 * the sqlVdbeChangeP4() function to change the value of the P4
 * operand.
 */
static SQL_NOINLINE int
growOp3(Vdbe * p, int op, int p1, int p2, int p3)
{
	assert(p->pParse->nOpAlloc <= p->nOp);
	if (growOpArray(p, 1))
		return 1;
	assert(p->pParse->nOpAlloc > p->nOp);
	return sqlVdbeAddOp3(p, op, p1, p2, p3);
}

int
sqlVdbeAddOp3(Vdbe * p, int op, int p1, int p2, int p3)
{
	int i;
	VdbeOp *pOp;
	struct session MAYBE_UNUSED *user_session;
	user_session = current_session();

	i = p->nOp;
	assert(p->magic == VDBE_MAGIC_INIT);
	assert(op >= 0 && op < 0xff);
	if (p->pParse->nOpAlloc <= i) {
		return growOp3(p, op, p1, p2, p3);
	}
	p->nOp++;
	pOp = &p->aOp[i];
	pOp->opcode = (u8) op;
	pOp->p5 = 0;
	pOp->p1 = p1;
	pOp->p2 = p2;
	pOp->p3 = p3;
	pOp->p4.p = 0;
	pOp->p4type = P4_NOTUSED;
#ifdef SQL_ENABLE_EXPLAIN_COMMENTS
	pOp->zComment = 0;
#endif
#ifdef SQL_DEBUG
	test_addop_breakpoint();
#endif
#ifdef VDBE_PROFILE
	pOp->cycles = 0;
	pOp->cnt = 0;
#endif
#ifdef SQL_VDBE_COVERAGE
	pOp->iSrcLine = 0;
#endif
	return i;
}

int
sqlVdbeAddOp0(Vdbe * p, int op)
{
	return sqlVdbeAddOp3(p, op, 0, 0, 0);
}

int
sqlVdbeAddOp1(Vdbe * p, int op, int p1)
{
	return sqlVdbeAddOp3(p, op, p1, 0, 0);
}

int
sqlVdbeAddOp2(Vdbe * p, int op, int p1, int p2)
{
	return sqlVdbeAddOp3(p, op, p1, p2, 0);
}

/* Generate code for an unconditional jump to instruction iDest
 */
int
sqlVdbeGoto(Vdbe * p, int iDest)
{
	return sqlVdbeAddOp3(p, OP_Goto, 0, iDest, 0);
}

/* Generate code to cause the string zStr to be loaded into
 * register iDest
 */
int
sqlVdbeLoadString(Vdbe * p, int iDest, const char *zStr)
{
	return sqlVdbeAddOp4(p, OP_String8, 0, iDest, 0, zStr, 0);
}

/*
 * Generate code that initializes multiple registers to string or integer
 * constants.  The registers begin with iDest and increase consecutively.
 * One register is initialized for each character in zTypes[].  For each
 * "s" character in zTypes[], the register is a string if the argument is
 * not NULL, or OP_Null if the value is a null pointer.  For each "i" character
 * in zTypes[], the register is initialized to an integer.
 */
void
sqlVdbeMultiLoad(Vdbe * p, int iDest, const char *zTypes, ...)
{
	va_list ap;
	int i;
	char c;
	va_start(ap, zTypes);
	for (i = 0; (c = zTypes[i]) != 0; i++) {
		if (c == 's') {
			const char *z = va_arg(ap, const char *);
			sqlVdbeAddOp4(p, z == 0 ? OP_Null : OP_String8, 0,
					  iDest++, 0, z, 0);
		} else {
			assert(c == 'i');
			sqlVdbeAddOp2(p, OP_Integer, va_arg(ap, int),
					  iDest++);
		}
	}
	va_end(ap);
}

/*
 * Add an opcode that includes the p4 value as a pointer.
 */
int
sqlVdbeAddOp4(Vdbe * p,	/* Add the opcode to this VM */
		  int op,	/* The new opcode */
		  int p1,	/* The P1 operand */
		  int p2,	/* The P2 operand */
		  int p3,	/* The P3 operand */
		  const char *zP4,	/* The P4 operand */
		  int p4type)	/* P4 operand type */

{
	int addr = sqlVdbeAddOp3(p, op, p1, p2, p3);
	sqlVdbeChangeP4(p, addr, zP4, p4type);
	return addr;
}

/*
 * Add an opcode that includes the p4 value with a P4_INT64/UINT64
 * or P4_REAL type.
 */
int
sqlVdbeAddOp4Dup8(Vdbe * p,	/* Add the opcode to this VM */
		      int op,	/* The new opcode */
		      int p1,	/* The P1 operand */
		      int p2,	/* The P2 operand */
		      int p3,	/* The P3 operand */
		      const u8 * zP4,	/* The P4 operand */
		      int p4type	/* P4 operand type */
    )
{
	char *p4copy = sqlDbMallocRawNN(sqlVdbeDb(p), 8);
	if (p4copy)
		memcpy(p4copy, zP4, 8);
	return sqlVdbeAddOp4(p, op, p1, p2, p3, p4copy, p4type);
}

/*
 * Add an opcode that includes the p4 value as an integer.
 */
int
sqlVdbeAddOp4Int(Vdbe * p,	/* Add the opcode to this VM */
		     int op,	/* The new opcode */
		     int p1,	/* The P1 operand */
		     int p2,	/* The P2 operand */
		     int p3,	/* The P3 operand */
		     int p4)	/* The P4 operand as an integer */
{
	int addr = sqlVdbeAddOp3(p, op, p1, p2, p3);
	if (p->db->mallocFailed == 0) {
		VdbeOp *pOp = &p->aOp[addr];
		pOp->p4type = P4_INT32;
		pOp->p4.i = p4;
	}
	return addr;
}

/* Insert the end of a co-routine
 */
void
sqlVdbeEndCoroutine(Vdbe * v, int regYield)
{
	sqlVdbeAddOp1(v, OP_EndCoroutine, regYield);

	/* Clear the temporary register cache, thereby ensuring that each
	 * co-routine has its own independent set of registers, because co-routines
	 * might expect their registers to be preserved across an OP_Yield, and
	 * that could cause problems if two or more co-routines are using the same
	 * temporary register.
	 */
	v->pParse->nTempReg = 0;
	v->pParse->nRangeReg = 0;
}

/*
 * Create a new symbolic label for an instruction that has yet to be
 * coded.  The symbolic label is really just a negative number.  The
 * label can be used as the P2 value of an operation.  Later, when
 * the label is resolved to a specific address, the VDBE will scan
 * through its operation list and change all values of P2 which match
 * the label into the resolved address.
 *
 * The VDBE knows that a P2 value is a label because labels are
 * always negative and P2 values are suppose to be non-negative.
 * Hence, a negative P2 value is a label that has yet to be resolved.
 *
 * Zero is returned if a malloc() fails.
 */
int
sqlVdbeMakeLabel(Vdbe * v)
{
	Parse *p = v->pParse;
	int i = p->nLabel++;
	assert(v->magic == VDBE_MAGIC_INIT);
	if ((i & (i - 1)) == 0) {
		p->aLabel = sqlDbReallocOrFree(p->db, p->aLabel,
						   (i * 2 +
						    1) * sizeof(p->aLabel[0]));
	}
	if (p->aLabel) {
		p->aLabel[i] = -1;
	}
	return ADDR(i);
}

/*
 * Resolve label "x" to be the address of the next instruction to
 * be inserted.  The parameter "x" must have been obtained from
 * a prior call to sqlVdbeMakeLabel().
 */
void
sqlVdbeResolveLabel(Vdbe * v, int x)
{
	Parse *p = v->pParse;
	int j = ADDR(x);
	assert(v->magic == VDBE_MAGIC_INIT);
	assert(j < p->nLabel);
	assert(j >= 0);
	if (p->aLabel) {
		p->aLabel[j] = v->nOp;
	}
}

/*
 * Mark the VDBE as one that can only be run one time.
 */
void
sqlVdbeRunOnlyOnce(Vdbe * p)
{
	p->runOnlyOnce = 1;
}

/*
 * This routine is called after all opcodes have been inserted.  It loops
 * through all the opcodes and fixes up some details.
 *
 * (1) For each jump instruction with a negative P2 value (a label)
 *     resolve the P2 value to an actual address.
 *
 * (2) Compute the maximum number of arguments used by any SQL function
 *     and store that value in *pMaxFuncArgs.
 *
 * (3) Initialize the p4.xAdvance pointer on opcodes that use it.
 *
 * (4) Reclaim the memory allocated for storing labels.
 *
 * This routine will only function correctly if the mkopcodeh.sh generator
 * script numbers the opcodes correctly.  Changes to this routine must be
 * coordinated with changes to mkopcodeh.sh.
 */
static void
resolveP2Values(Vdbe * p, int *pMaxFuncArgs)
{
	int nMaxArgs = *pMaxFuncArgs;
	Op *pOp;
	Parse *pParse = p->pParse;
	int *aLabel = pParse->aLabel;
	pOp = &p->aOp[p->nOp - 1];
	while (1) {

		/* Only JUMP opcodes and the short list of special opcodes in the switch
		 * below need to be considered.  The mkopcodeh.sh generator script groups
		 * all these opcodes together near the front of the opcode list.  Skip
		 * any opcode that does not need processing by virtual of the fact that
		 * it is larger than SQL_MX_JUMP_OPCODE, as a performance optimization.
		 */
		if (pOp->opcode <= SQL_MX_JUMP_OPCODE) {
			/* NOTE: Be sure to update mkopcodeh.sh when adding or removing
			 * cases from this switch!
			 */
			switch (pOp->opcode) {
			case OP_Next:
			case OP_NextIfOpen:
			case OP_SorterNext:{
					pOp->p4.xAdvance = sqlCursorNext;
					pOp->p4type = P4_ADVANCE;
					break;
				}
			case OP_Prev:
			case OP_PrevIfOpen:{
					pOp->p4.xAdvance = sqlCursorPrevious;
					pOp->p4type = P4_ADVANCE;
					break;
				}
			}
			if ((sqlOpcodeProperty[pOp->opcode] & OPFLG_JUMP) !=
			    0 && pOp->p2 < 0) {
				assert(ADDR(pOp->p2) < pParse->nLabel);
				pOp->p2 = aLabel[ADDR(pOp->p2)];
			}
		}
		if (pOp == p->aOp)
			break;
		pOp--;
	}
	sqlDbFree(p->db, pParse->aLabel);
	pParse->aLabel = 0;
	pParse->nLabel = 0;
	*pMaxFuncArgs = nMaxArgs;
}

/*
 * Return the address of the next instruction to be inserted.
 */
int
sqlVdbeCurrentAddr(Vdbe * p)
{
	assert(p->magic == VDBE_MAGIC_INIT);
	return p->nOp;
}

/*
 * This function returns a pointer to the array of opcodes associated with
 * the Vdbe passed as the first argument. It is the callers responsibility
 * to arrange for the returned array to be eventually freed using the
 * vdbeFreeOpArray() function.
 *
 * Before returning, *pnOp is set to the number of entries in the returned
 * array. Also, *pnMaxArg is set to the larger of its current value and
 * the number of entries in the Vdbe.apArg[] array required to execute the
 * returned program.
 */
VdbeOp *
sqlVdbeTakeOpArray(Vdbe * p, int *pnOp, int *pnMaxArg)
{
	VdbeOp *aOp = p->aOp;
	assert(aOp && !p->db->mallocFailed);

	resolveP2Values(p, pnMaxArg);
	*pnOp = p->nOp;
	p->aOp = 0;
	return aOp;
}

/*
 * Change the value of the opcode, or P1, P2, P3, or P5 operands
 * for a specific instruction.
 */
void
sqlVdbeChangeOpcode(Vdbe * p, u32 addr, u8 iNewOpcode)
{
	sqlVdbeGetOp(p, addr)->opcode = iNewOpcode;
}

void
sqlVdbeChangeP1(Vdbe * p, u32 addr, int val)
{
	sqlVdbeGetOp(p, addr)->p1 = val;
}

void
sqlVdbeChangeP2(Vdbe * p, u32 addr, int val)
{
	sqlVdbeGetOp(p, addr)->p2 = val;
}

void
sqlVdbeChangeP3(Vdbe * p, u32 addr, int val)
{
	sqlVdbeGetOp(p, addr)->p3 = val;
}

void
sqlVdbeChangeP5(Vdbe * p, int p5)
{
	assert(p->nOp > 0 || p->db->mallocFailed);
	if (p->nOp > 0)
		p->aOp[p->nOp - 1].p5 = p5;
}

/*
 * Change the P2 operand of instruction addr so that it points to
 * the address of the next instruction to be coded.
 */
void
sqlVdbeJumpHere(Vdbe * p, int addr)
{
	sqlVdbeChangeP2(p, addr, p->nOp);
}

static void vdbeFreeOpArray(sql *, Op *, int);

static SQL_NOINLINE void
freeP4FuncCtx(sql * db, sql_context * p)
{
	sqlDbFree(db, p);
}

static void
freeP4(sql * db, int p4type, void *p4)
{
	assert(db);
	switch (p4type) {
	case P4_FUNCCTX:{
			freeP4FuncCtx(db, (sql_context *) p4);
			break;
		}
	case P4_REAL:
	case P4_INT64:
	case P4_UINT64:
	case P4_DYNAMIC:
	case P4_INTARRAY:{
			sqlDbFree(db, p4);
			break;
		}
	case P4_KEYINFO:
		sql_key_info_unref(p4);
		break;
	case P4_MEM:
		sqlValueFree((sql_value *) p4);
		break;
	default:
		break;
	}
}

/*
 * Free the space allocated for aOp and any p4 values allocated for the
 * opcodes contained within. If aOp is not NULL it is assumed to contain
 * nOp entries.
 */
static void
vdbeFreeOpArray(sql * db, Op * aOp, int nOp)
{
	if (aOp) {
		Op *pOp;
		for (pOp = aOp; pOp < &aOp[nOp]; pOp++) {
			if (pOp->p4type)
				freeP4(db, pOp->p4type, pOp->p4.p);
#ifdef SQL_ENABLE_EXPLAIN_COMMENTS
			sqlDbFree(db, pOp->zComment);
#endif
		}
	}
	sqlDbFree(db, aOp);
}

/*
 * Link the SubProgram object passed as the second argument into the linked
 * list at Vdbe.pSubProgram. This list is used to delete all sub-program
 * objects when the VM is no longer required.
 */
void
sqlVdbeLinkSubProgram(Vdbe * pVdbe, SubProgram * p)
{
	p->pNext = pVdbe->pProgram;
	pVdbe->pProgram = p;
}

/*
 * Change the opcode at addr into OP_Noop
 */
int
sqlVdbeChangeToNoop(Vdbe * p, int addr)
{
	VdbeOp *pOp;
	if (p->db->mallocFailed)
		return 0;
	assert(addr >= 0 && addr < p->nOp);
	pOp = &p->aOp[addr];
	freeP4(p->db, pOp->p4type, pOp->p4.p);
	pOp->p4type = P4_NOTUSED;
	pOp->p4.z = 0;
	pOp->opcode = OP_Noop;
	return 1;
}

/*
 * If the last opcode is "op" and it is not a jump destination,
 * then remove it.  Return true if and only if an opcode was removed.
 */
int
sqlVdbeDeletePriorOpcode(Vdbe * p, u8 op)
{
	if (p->nOp > 0 && p->aOp[p->nOp - 1].opcode == op) {
		return sqlVdbeChangeToNoop(p, p->nOp - 1);
	} else {
		return 0;
	}
}

/*
 * Change the value of the P4 operand for a specific instruction.
 *
 * If n>=0 then the P4 operand is dynamic, meaning that a copy of
 * the string is made into memory obtained from sql_malloc().
 * A value of n==0 means copy bytes of zP4 up to and including the
 * first null byte.  If n>0 then copy n+1 bytes of zP4.
 *
 * Other values of n (P4_STATIC, P4_COLLSEQ etc.) indicate that zP4 points
 * to a string or structure that is guaranteed to exist for the lifetime of
 * the Vdbe. In these cases we can just copy the pointer.
 *
 * If addr<0 then change P4 on the most recently inserted instruction.
 */
static void SQL_NOINLINE
vdbeChangeP4Full(Vdbe * p, Op * pOp, const char *zP4, int n)
{
	if (pOp->p4type) {
		freeP4(p->db, pOp->p4type, pOp->p4.p);
		pOp->p4type = 0;
		pOp->p4.p = 0;
	}
	if (n < 0) {
		sqlVdbeChangeP4(p, (int)(pOp - p->aOp), zP4, n);
	} else {
		if (n == 0)
			n = sqlStrlen30(zP4);
		pOp->p4.z = sqlDbStrNDup(p->db, zP4, n);
		pOp->p4type = P4_DYNAMIC;
	}
}

void
sqlVdbeChangeP4(Vdbe * p, int addr, const char *zP4, int n)
{
	Op *pOp;
	sql *db;
	assert(p != 0);
	db = p->db;
	assert(p->magic == VDBE_MAGIC_INIT);
	assert(p->aOp != 0 || db->mallocFailed);
	if (db->mallocFailed) {
		freeP4(db, n, (void *)*(char **)&zP4);
		return;
	}
	assert(p->nOp > 0);
	assert(addr < p->nOp);
	if (addr < 0) {
		addr = p->nOp - 1;
	}
	pOp = &p->aOp[addr];
	if (n >= 0 || pOp->p4type) {
		vdbeChangeP4Full(p, pOp, zP4, n);
		return;
	}
	if (n == P4_INT32) {
		/* Note: this cast is safe, because the origin data point was an int
		 * that was cast to a (const char *).
		 */
		pOp->p4.i = SQL_PTR_TO_INT(zP4);
		pOp->p4type = P4_INT32;
	} if (n == P4_BOOL) {
		pOp->p4.b = *(bool*)zP4;
		pOp->p4type = P4_BOOL;
	} else {
		assert(n < 0);
		pOp->p4.p = (void *)zP4;
		pOp->p4type = (signed char)n;
	}
}

/*
 * Change the P4 operand of the most recently coded instruction
 * to the value defined by the arguments.  This is a high-speed
 * version of sqlVdbeChangeP4().
 *
 * The P4 operand must not have been previously defined.  And the new
 * P4 must not be P4_INT32.  Use sqlVdbeChangeP4() in either of
 * those cases.
 */
void
sqlVdbeAppendP4(Vdbe * p, void *pP4, int n)
{
	VdbeOp *pOp;
	assert(n != P4_INT32);
	assert(n <= 0);
	if (p->db->mallocFailed) {
		freeP4(p->db, n, pP4);
	} else {
		assert(pP4 != 0);
		assert(p->nOp > 0);
		pOp = &p->aOp[p->nOp - 1];
		assert(pOp->p4type == P4_NOTUSED);
		pOp->p4type = n;
		pOp->p4.p = pP4;
	}
}

void
sql_vdbe_set_p4_key_def(struct Parse *parse, struct key_def *key_def)
{
	struct Vdbe *v = parse->pVdbe;
	assert(v != NULL);
	assert(key_def != NULL);
	struct sql_key_info *key_info =
		sql_key_info_new_from_key_def(parse->db, key_def);
	if (key_info != NULL)
		sqlVdbeAppendP4(v, key_info, P4_KEYINFO);
}

#ifdef SQL_ENABLE_EXPLAIN_COMMENTS
/*
 * Change the comment on the most recently coded instruction.  Or
 * insert a No-op and add the comment to that new instruction.  This
 * makes the code easier to read during debugging.  None of this happens
 * in a production build.
 */
static void
vdbeVComment(Vdbe * p, const char *zFormat, va_list ap)
{
	assert(p->nOp > 0 || p->aOp == 0);
	assert(p->aOp == 0 || p->aOp[p->nOp - 1].zComment == 0
	       || p->db->mallocFailed);
	if (p->nOp) {
		assert(p->aOp);
		sqlDbFree(p->db, p->aOp[p->nOp - 1].zComment);
		p->aOp[p->nOp - 1].zComment =
		    sqlVMPrintf(p->db, zFormat, ap);
	}
}

void
sqlVdbeComment(Vdbe * p, const char *zFormat, ...)
{
	va_list ap;
	if (p) {
		va_start(ap, zFormat);
		vdbeVComment(p, zFormat, ap);
		va_end(ap);
	}
}

void
sqlVdbeNoopComment(Vdbe * p, const char *zFormat, ...)
{
	va_list ap;
	if (p) {
		sqlVdbeAddOp0(p, OP_Noop);
		va_start(ap, zFormat);
		vdbeVComment(p, zFormat, ap);
		va_end(ap);
	}
}
#endif				/* NDEBUG */

#ifdef SQL_VDBE_COVERAGE
/*
 * Set the value if the iSrcLine field for the previously coded instruction.
 */
void
sqlVdbeSetLineNumber(Vdbe * v, int iLine)
{
	sqlVdbeGetOp(v, -1)->iSrcLine = iLine;
}
#endif				/* SQL_VDBE_COVERAGE */

/*
 * Return the opcode for a given address.  If the address is -1, then
 * return the most recently inserted opcode.
 *
 * If a memory allocation error has occurred prior to the calling of this
 * routine, then a pointer to a dummy VdbeOp will be returned.  That opcode
 * is readable but not writable, though it is cast to a writable value.
 * The return of a dummy opcode allows the call to continue functioning
 * after an OOM fault without having to check to see if the return from
 * this routine is a valid pointer.  But because the dummy.opcode is 0,
 * dummy will never be written to.  This is verified by code inspection and
 * by running with Valgrind.
 */
VdbeOp *
sqlVdbeGetOp(Vdbe * p, int addr)
{
	/* C89 specifies that the constant "dummy" will be initialized to all
	 * zeros, which is correct.
	 */
	static VdbeOp dummy;
	assert(p->magic == VDBE_MAGIC_INIT);
	if (addr < 0) {
		addr = p->nOp - 1;
	}
	assert((addr >= 0 && addr < p->nOp) || p->db->mallocFailed);
	if (p->db->mallocFailed) {
		return (VdbeOp *) & dummy;
	} else {
		return &p->aOp[addr];
	}
}

#if defined(SQL_ENABLE_EXPLAIN_COMMENTS)
/*
 * Return an integer value for one of the parameters to the opcode pOp
 * determined by character c.
 */
static int
translateP(char c, const Op * pOp)
{
	if (c == '1')
		return pOp->p1;
	if (c == '2')
		return pOp->p2;
	if (c == '3')
		return pOp->p3;
	if (c == '4')
		return pOp->p4.i;
	return pOp->p5;
}

/*
 * Compute a string for the "comment" field of a VDBE opcode listing.
 *
 * The Synopsis: field in comments in the vdbe.c source file gets converted
 * to an extra string that is appended to the sqlOpcodeName().  In the
 * absence of other comments, this synopsis becomes the comment on the opcode.
 * Some translation occurs:
 *
 *       "PX"      ->  "r[X]"
 *       "PX@PY"   ->  "r[X..X+Y-1]"  or "r[x]" if y is 0 or 1
 *       "PX@PY+1" ->  "r[X..X+Y]"    or "r[x]" if y is 0
 *       "PY..PY"  ->  "r[X..Y]"      or "r[x]" if y<=x
 */
static int
displayComment(const Op * pOp,	/* The opcode to be commented */
	       const char *zP4,	/* Previously obtained value for P4 */
	       char *zTemp,	/* Write result here */
	       int nTemp)	/* Space available in zTemp[] */
{
	const char *zOpName;
	const char *zSynopsis;
	int nOpName;
	int ii, jj;
	char zAlt[50];
	zOpName = sqlOpcodeName(pOp->opcode);
	nOpName = sqlStrlen30(zOpName);
	if (zOpName[nOpName + 1]) {
		int seenCom = 0;
		char c;
		zSynopsis = zOpName += nOpName + 1;
		if (strncmp(zSynopsis, "IF ", 3) == 0) {
			if (pOp->p5 & SQL_STOREP2) {
				sql_snprintf(sizeof(zAlt), zAlt,
						 "r[P2] = (%s)", zSynopsis + 3);
			} else {
				sql_snprintf(sizeof(zAlt), zAlt,
						 "if %s goto P2",
						 zSynopsis + 3);
			}
			zSynopsis = zAlt;
		}
		for (ii = jj = 0; jj < nTemp - 1 && (c = zSynopsis[ii]) != 0;
		     ii++) {
			if (c == 'P') {
				c = zSynopsis[++ii];
				if (c == '4') {
					sql_snprintf(nTemp - jj, zTemp + jj,
							 "%s", zP4);
				} else if (c == 'X') {
					sql_snprintf(nTemp - jj, zTemp + jj,
							 "%s", pOp->zComment);
					seenCom = 1;
				} else {
					int v1 = translateP(c, pOp);
					int v2;
					sql_snprintf(nTemp - jj, zTemp + jj,
							 "%d", v1);
					if (strncmp(zSynopsis + ii + 1, "@P", 2)
					    == 0) {
						ii += 3;
						jj +=
						    sqlStrlen30(zTemp + jj);
						v2 = translateP(zSynopsis[ii],
								pOp);
						if (strncmp
						    (zSynopsis + ii + 1, "+1",
						     2) == 0) {
							ii += 2;
							v2++;
						}
						if (v2 > 1) {
							sql_snprintf(nTemp -
									 jj,
									 zTemp +
									 jj,
									 "..%d",
									 v1 +
									 v2 -
									 1);
						}
					} else
					    if (strncmp
						(zSynopsis + ii + 1, "..P3",
						 4) == 0 && pOp->p3 == 0) {
						ii += 4;
					}
				}
				jj += sqlStrlen30(zTemp + jj);
			} else {
				zTemp[jj++] = c;
			}
		}
		if (!seenCom && jj < nTemp - 5 && pOp->zComment) {
			sql_snprintf(nTemp - jj, zTemp + jj, "; %s",
					 pOp->zComment);
			jj += sqlStrlen30(zTemp + jj);
		}
		if (jj < nTemp)
			zTemp[jj] = 0;
	} else if (pOp->zComment) {
		sql_snprintf(nTemp, zTemp, "%s", pOp->zComment);
		jj = sqlStrlen30(zTemp);
	} else {
		zTemp[0] = 0;
		jj = 0;
	}
	return jj;
}
#endif				/* SQL_DEBUG */

/*
 * Compute a string that describes the P4 parameter for an opcode.
 * Use zTemp for any required temporary buffer space.
 */
static char *
displayP4(Op * pOp, char *zTemp, int nTemp)
{
	/*
	 * Msgpack is subtype, not type of P4, so lets consider
	 * it as special case. We should decode msgpack to display
	 * it in a readable form.
	 */
	if (pOp->opcode == OP_Blob && pOp->p3 == SQL_SUBTYPE_MSGPACK) {
		mp_snprint(zTemp, nTemp, pOp->p4.z);
		return zTemp;
	}
	char *zP4 = zTemp;
	StrAccum x;
	assert(nTemp >= 20);
	sqlStrAccumInit(&x, 0, zTemp, nTemp, 0);
	switch (pOp->p4type) {
	case P4_KEYINFO:{
			struct key_def *def = NULL;
			if (pOp->p4.key_info != NULL)
				def = sql_key_info_to_key_def(pOp->p4.key_info);
			if (def == NULL) {
				sqlXPrintf(&x, "k[NULL]");
			} else {
				sqlXPrintf(&x, "k(%d", def->part_count);
				for (int j = 0; j < (int)def->part_count; j++) {
					struct coll *coll = def->parts[j].coll;
					const char *coll_str;
					if (coll == NULL)
						coll_str = "B";
					else
						coll_str = coll->fingerprint;
					const char *sort_order = "";
					if (def->parts[j].sort_order ==
					    SORT_ORDER_DESC) {
						sort_order = "-";
					}
					sqlXPrintf(&x, ",%s%s",
						       sort_order,
						       coll_str);
				}
				sqlStrAccumAppend(&x, ")", 1);
			}
			break;
		}
	case P4_COLLSEQ:{
			struct coll *pColl = pOp->p4.pColl;
			if (pColl != NULL)
				sqlXPrintf(&x, "(%.100s)",
					       pColl->fingerprint);
			else
				sqlXPrintf(&x, "(binary)");
			break;
		}
	case P4_FUNC:{
			struct func *func = pOp->p4.func;
			sqlXPrintf(&x, "%s(%d)", func->def->name,
				   func->def->param_count);
			break;
		}
#if defined(SQL_DEBUG) || defined(VDBE_PROFILE)
	case P4_FUNCCTX:{
			struct func *func = pOp->p4.func;
			sqlXPrintf(&x, "%s(%d)", func->def->name,
				   func->def->param_count);
			break;
		}
#endif
	case P4_BOOL:
			sqlXPrintf(&x, "%d", pOp->p4.b);
			break;
	case P4_INT64:{
			sqlXPrintf(&x, "%lld", *pOp->p4.pI64);
			break;
		}
	case P4_UINT64: {
		sqlXPrintf(&x, "%llu", (uint64_t)*pOp->p4.pI64);
			break;
	}
	case P4_INT32:{
			sqlXPrintf(&x, "%d", pOp->p4.i);
			break;
		}
	case P4_REAL:{
			sqlXPrintf(&x, "%.16g", *pOp->p4.pReal);
			break;
		}
	case P4_MEM:{
			Mem *pMem = pOp->p4.pMem;
			if (pMem->flags & MEM_Str) {
				zP4 = pMem->z;
			} else if (pMem->flags & MEM_Int) {
				sqlXPrintf(&x, "%lld", pMem->u.i);
			} else if (pMem->flags & MEM_UInt) {
				sqlXPrintf(&x, "%llu", pMem->u.u);
			} else if (pMem->flags & MEM_Real) {
				sqlXPrintf(&x, "%.16g", pMem->u.r);
			} else if (pMem->flags & MEM_Null) {
				zP4 = "NULL";
			} else {
				assert(pMem->flags & MEM_Blob);
				zP4 = "(binary string)";
			}
			break;
		}
	case P4_INTARRAY:{
			int i;
			int *ai = pOp->p4.ai;
			int n = ai[0];	/* The first element of an INTARRAY is always the
					 * count of the number of elements to follow
					 */
			for (i = 1; i < n; i++) {
				sqlXPrintf(&x, ",%d", ai[i]);
			}
			zTemp[0] = '[';
			sqlStrAccumAppend(&x, "]", 1);
			break;
		}
	case P4_SUBPROGRAM:{
			sqlXPrintf(&x, "program");
			break;
		}
	case P4_ADVANCE:{
			zTemp[0] = 0;
			break;
		}
	case P4_SPACEPTR: {
		sqlXPrintf(&x, "space<name=%s>", space_name(pOp->p4.space));
		break;
	}
	default:{
			zP4 = pOp->p4.z;
			if (zP4 == 0) {
				zP4 = zTemp;
				zTemp[0] = 0;
			}
		}
	}
	sqlStrAccumFinish(&x);
	assert(zP4 != 0);
	return zP4;
}


#if defined(VDBE_PROFILE) || defined(SQL_DEBUG)
/*
 * Print a single opcode.  This routine is used for debugging only.
 */
void
sqlVdbePrintOp(FILE * pOut, int pc, Op * pOp)
{
	char *zP4;
	char zPtr[256];
	char zCom[256];
	static const char *zFormat1 =
	    "%4d> %4d %-13s %4d %4d %4d %-13s %.2X %s\n";
	if (pOut == 0)
		pOut = stdout;
	zP4 = displayP4(pOp, zPtr, sizeof(zPtr));
#ifdef SQL_ENABLE_EXPLAIN_COMMENTS
	displayComment(pOp, zP4, zCom, sizeof(zCom));
#else
	zCom[0] = 0;
#endif
	/* NB:  The sqlOpcodeName() function is implemented by code created
	 * by the mkopcodeh.awk and mkopcodec.awk scripts which extract the
	 * information from the vdbe.c source text
	 */
	fprintf(pOut, zFormat1, fiber_self()->fid, pc,
		sqlOpcodeName(pOp->opcode), pOp->p1, pOp->p2, pOp->p3, zP4,
		pOp->p5, zCom);
	fflush(pOut);
}
#endif

/*
 * Initialize an array of N Mem element.
 */
static void
initMemArray(Mem * p, int N, sql * db, u32 flags)
{
	while ((N--) > 0) {
		p->db = db;
		p->flags = flags;
		p->szMalloc = 0;
		p->field_type = field_type_MAX;
#ifdef SQL_DEBUG
		p->pScopyFrom = 0;
#endif
		p++;
	}
}

/*
 * Release an array of N Mem elements
 */
static void
releaseMemArray(Mem * p, int N)
{
	if (p && N) {
		Mem *pEnd = &p[N];
		sql *db = p->db;
		do {
			assert((&p[1]) == pEnd || p[0].db == p[1].db);
			assert(sqlVdbeCheckMemInvariants(p));

			/* This block is really an inlined version of sqlVdbeMemRelease()
			 * that takes advantage of the fact that the memory cell value is
			 * being set to NULL after releasing any dynamic resources.
			 *
			 * The justification for duplicating code is that according to
			 * callgrind, this causes a certain test case to hit the CPU 4.7
			 * percent less (x86 linux, gcc version 4.1.2, -O6) than if
			 * sqlMemRelease() were called from here. With -O2, this jumps
			 * to 6.6 percent. The test case is inserting 1000 rows into a table
			 * with no indexes using a single prepared INSERT statement, bind()
			 * and reset(). Inserts are grouped into a transaction.
			 */
			testcase(p->flags & MEM_Agg);
			testcase(p->flags & MEM_Dyn);
			testcase(p->flags & MEM_Frame);
			if (p->
			    flags & (MEM_Agg | MEM_Dyn | MEM_Frame)) {
				sqlVdbeMemRelease(p);
			} else if (p->szMalloc) {
				sqlDbFree(db, p->zMalloc);
				p->szMalloc = 0;
			}

			p->flags = MEM_Undefined;
		} while ((++p) < pEnd);
	}
}

/*
 * Delete a VdbeFrame object and its contents. VdbeFrame objects are
 * allocated by the OP_Program opcode in sqlVdbeExec().
 */
void
sqlVdbeFrameDelete(VdbeFrame * p)
{
	int i;
	Mem *aMem = VdbeFrameMem(p);
	VdbeCursor **apCsr = (VdbeCursor **) & aMem[p->nChildMem];
	for (i = 0; i < p->nChildCsr; i++) {
		sqlVdbeFreeCursor(p->v, apCsr[i]);
	}
	releaseMemArray(aMem, p->nChildMem);
	sqlDbFree(p->v->db, p);
}

/*
 * Give a listing of the program in the virtual machine.
 *
 * The interface is the same as sqlVdbeExec().  But instead of
 * running the code, it invokes the callback once for each instruction.
 * This feature is used to implement "EXPLAIN".
 *
 * When p->explain==1, each instruction is listed.  When
 * p->explain==2, only OP_Explain instructions are listed and these
 * are shown in a different format.  p->explain==2 is used to implement
 * EXPLAIN QUERY PLAN.
 *
 * When p->explain==1, first the main program is listed, then each of
 * the trigger subprograms are listed one by one.
 */
int
sqlVdbeList(Vdbe * p)
{
	int nRow;		/* Stop when row count reaches this */
	int nSub = 0;		/* Number of sub-vdbes seen so far */
	SubProgram **apSub = 0;	/* Array of sub-vdbes */
	Mem *pSub = 0;		/* Memory cell hold array of subprogs */
	int i;			/* Loop counter */
	int rc = 0;	/* Return code */
	Mem *pMem = &p->aMem[1];	/* First Mem of result set */

	assert(p->explain);
	assert(p->magic == VDBE_MAGIC_RUN);

	/* Even though this opcode does not use dynamic strings for
	 * the result, result columns may become dynamic if the user calls
	 * sql_column_text16(), causing a translation to UTF-16 encoding.
	 */
	releaseMemArray(pMem, 8);
	p->pResultSet = 0;

	/* When the number of output rows reaches nRow, that means the
	 * listing has finished and sql_step() should return SQL_DONE.
	 * nRow is the sum of the number of rows in the main program, plus
	 * the sum of the number of rows in all trigger subprograms encountered
	 * so far.  The nRow value will increase as new trigger subprograms are
	 * encountered, but p->pc will eventually catch up to nRow.
	 */
	nRow = p->nOp;
	if (p->explain == 1) {
		/* The first 8 memory cells are used for the result set.  So we will
		 * commandeer the 9th cell to use as storage for an array of pointers
		 * to trigger subprograms.  The VDBE is guaranteed to have at least 9
		 * cells.
		 */
		assert(p->nMem > 9);
		pSub = &p->aMem[9];
		if (pSub->flags & MEM_Blob) {
			/* On the first call to sql_step(), pSub will hold a NULL.  It is
			 * initialized to a BLOB by the P4_SUBPROGRAM processing logic below
			 */
			nSub = pSub->n / sizeof(Vdbe *);
			apSub = (SubProgram **) pSub->z;
		}
		for (i = 0; i < nSub; i++) {
			nRow += apSub[i]->nOp;
		}
	}

	do {
		i = p->pc++;
	} while (i < nRow && p->explain == 2 && p->aOp[i].opcode != OP_Explain);
	if (i >= nRow) {
		rc = SQL_DONE;
	} else {
		char *zP4;
		Op *pOp;
		if (i < p->nOp) {
			/* The output line number is small enough that we are still in the
			 * main program.
			 */
			pOp = &p->aOp[i];
		} else {
			/* We are currently listing subprograms.  Figure out which one and
			 * pick up the appropriate opcode.
			 */
			int j;
			i -= p->nOp;
			for (j = 0; i >= apSub[j]->nOp; j++) {
				i -= apSub[j]->nOp;
			}
			pOp = &apSub[j]->aOp[i];
		}
		if (p->explain == 1) {
			assert(i >= 0);
			mem_set_u64(pMem, i);

			pMem++;

			pMem->flags = MEM_Static | MEM_Str | MEM_Term;
			pMem->z = (char *)sqlOpcodeName(pOp->opcode);	/* Opcode */
			assert(pMem->z != 0);
			pMem->n = sqlStrlen30(pMem->z);
			pMem++;

			/* When an OP_Program opcode is encounter (the only opcode that has
			 * a P4_SUBPROGRAM argument), expand the size of the array of subprograms
			 * kept in p->aMem[9].z to hold the new program - assuming this subprogram
			 * has not already been seen.
			 */
			if (pOp->p4type == P4_SUBPROGRAM) {
				int nByte = (nSub + 1) * sizeof(SubProgram *);
				int j;
				for (j = 0; j < nSub; j++) {
					if (apSub[j] == pOp->p4.pProgram)
						break;
				}
				if (j == nSub &&
				    sqlVdbeMemGrow(pSub, nByte,
						   nSub != 0) == 0) {
					apSub = (SubProgram **) pSub->z;
					apSub[nSub++] = pOp->p4.pProgram;
					pSub->flags |= MEM_Blob;
					pSub->n = nSub * sizeof(SubProgram *);
				}
			}
		}

		mem_set_i64(pMem, pOp->p1);
		pMem++;

		mem_set_i64(pMem, pOp->p2);
		pMem++;

		mem_set_i64(pMem, pOp->p3);
		pMem++;

		if (sqlVdbeMemClearAndResize(pMem, 256)) {
			assert(p->db->mallocFailed);
			return -1;
		}
		pMem->flags = MEM_Str | MEM_Term;
		zP4 = displayP4(pOp, pMem->z, pMem->szMalloc);

		if (zP4 != pMem->z) {
			pMem->n = 0;
			sqlVdbeMemSetStr(pMem, zP4, -1, 1, 0);
		} else {
			assert(pMem->z != 0);
			pMem->n = sqlStrlen30(pMem->z);
		}
		pMem++;

		if (p->explain == 1) {
			if (sqlVdbeMemClearAndResize(pMem, 4)) {
				assert(p->db->mallocFailed);
				return -1;
			}
			pMem->flags = MEM_Str | MEM_Term;
			pMem->n = 2;
			sql_snprintf(3, pMem->z, "%.2x", pOp->p5);	/* P5 */
			pMem++;

#ifdef SQL_ENABLE_EXPLAIN_COMMENTS
			if (sqlVdbeMemClearAndResize(pMem, 500)) {
				assert(p->db->mallocFailed);
				return -1;
			}
			pMem->flags = MEM_Str | MEM_Term;
			pMem->n = displayComment(pOp, zP4, pMem->z, 500);
#else
			pMem->flags = MEM_Null;	/* Comment */
#endif
		}

		p->nResColumn = 8 - 4 * (p->explain - 1);
		p->pResultSet = &p->aMem[1];
		rc = SQL_ROW;
	}
	return rc;
}

#ifdef SQL_DEBUG
/*
 * Print the SQL that was used to generate a VDBE program.
 */
void
sqlVdbePrintSql(Vdbe * p)
{
	const char *z = 0;
	if (p->zSql) {
		z = p->zSql;
	} else if (p->nOp >= 1) {
		const VdbeOp *pOp = &p->aOp[0];
		if (pOp->opcode == OP_Init && pOp->p4.z != 0) {
			z = pOp->p4.z;
			while (sqlIsspace(*z))
				z++;
		}
	}
	if (z)
		printf("SQL: [%s]\n", z);
}
#endif


/* An instance of this object describes bulk memory available for use
 * by subcomponents of a prepared statement.  Space is allocated out
 * of a ReusableSpace object by the allocSpace() routine below.
 */
struct ReusableSpace {
	u8 *pSpace;		/* Available memory */
	int nFree;		/* Bytes of available memory */
	int nNeeded;		/* Total bytes that could not be allocated */
};

/* Try to allocate nByte bytes of 8-byte aligned bulk memory for pBuf
 * from the ReusableSpace object.  Return a pointer to the allocated
 * memory on success.  If insufficient memory is available in the
 * ReusableSpace object, increase the ReusableSpace.nNeeded
 * value by the amount needed and return NULL.
 *
 * If pBuf is not initially NULL, that means that the memory has already
 * been allocated by a prior call to this routine, so just return a copy
 * of pBuf and leave ReusableSpace unchanged.
 *
 * This allocator is employed to repurpose unused slots at the end of the
 * opcode array of prepared state for other memory needs of the prepared
 * statement.
 */
static void *
allocSpace(struct ReusableSpace *p,	/* Bulk memory available for allocation */
	   void *pBuf,		/* Pointer to a prior allocation */
	   int nByte		/* Bytes of memory needed */
    )
{
	assert(EIGHT_BYTE_ALIGNMENT(p->pSpace));
	if (pBuf == 0) {
		nByte = ROUND8(nByte);
		if (nByte <= p->nFree) {
			p->nFree -= nByte;
			pBuf = &p->pSpace[p->nFree];
		} else {
			p->nNeeded += nByte;
		}
	}
	assert(EIGHT_BYTE_ALIGNMENT(pBuf));
	return pBuf;
}

/*
 * Rewind the VDBE back to the beginning in preparation for
 * running it.
 */
void
sqlVdbeRewind(Vdbe * p)
{
#if defined(SQL_DEBUG) || defined(VDBE_PROFILE)
	int i;
#endif
	assert(p != 0);
	assert(p->magic == VDBE_MAGIC_INIT || p->magic == VDBE_MAGIC_RESET);

	/* There should be at least one opcode.
	 */
	assert(p->nOp > 0);

	/* Set the magic to VDBE_MAGIC_RUN sooner rather than later. */
	p->magic = VDBE_MAGIC_RUN;

#ifdef SQL_DEBUG
	for (i = 0; i < p->nMem; i++) {
		assert(p->aMem[i].db == p->db);
	}
#endif
	p->pc = -1;
	p->is_aborted = false;
	p->ignoreRaised = 0;
	p->errorAction = ON_CONFLICT_ACTION_ABORT;
	p->nChange = 0;
	p->cacheCtr = 1;
	p->iStatement = 0;
	p->nFkConstraint = 0;
#ifdef VDBE_PROFILE
	for (i = 0; i < p->nOp; i++) {
		p->aOp[i].cnt = 0;
		p->aOp[i].cycles = 0;
	}
#endif
}

/*
 * Prepare a virtual machine for execution for the first time after
 * creating the virtual machine.  This involves things such
 * as allocating registers and initializing the program counter.
 * After the VDBE has be prepped, it can be executed by one or more
 * calls to sqlVdbeExec().
 *
 * This function may be called exactly once on each virtual machine.
 * After this routine is called the VM has been "packaged" and is ready
 * to run.  After this routine is called, further calls to
 * sqlVdbeAddOp() functions are prohibited.  This routine disconnects
 * the Vdbe from the Parse object that helped generate it so that the
 * the Vdbe becomes an independent entity and the Parse object can be
 * destroyed.
 *
 * Use the sqlVdbeRewind() procedure to restore a virtual machine back
 * to its initial state after it has been run.
 */
void
sqlVdbeMakeReady(Vdbe * p,	/* The VDBE */
		     Parse * pParse	/* Parsing context */
    )
{
	sql *db;		/* The database connection */
	int nVar;		/* Number of parameters */
	int nMem;		/* Number of VM memory registers */
	int nCursor;		/* Number of cursors required */
	int nArg;		/* Number of arguments in subprograms */
	int n;			/* Loop counter */
	struct ReusableSpace x;	/* Reusable bulk memory */

	assert(p != 0);
	assert(p->nOp > 0);
	assert(pParse != 0);
	assert(p->magic == VDBE_MAGIC_INIT);
	assert(pParse == p->pParse);
	db = p->db;
	assert(db->mallocFailed == 0);
	nVar = pParse->nVar;
	nMem = pParse->nMem;
	nCursor = pParse->nTab;
	nArg = pParse->nMaxArg;

	/* Each cursor uses a memory cell.  The first cursor (cursor 0) can
	 * use aMem[0] which is not otherwise used by the VDBE program.  Allocate
	 * space at the end of aMem[] for cursors 1 and greater.
	 * See also: allocateCursor().
	 */
	nMem += nCursor;
	if (nCursor == 0 && nMem > 0)
		nMem++;		/* Space for aMem[0] even if not used */

	/* Figure out how much reusable memory is available at the end of the
	 * opcode array.  This extra memory will be reallocated for other elements
	 * of the prepared statement.
	 */
	n = ROUND8(sizeof(Op) * p->nOp);	/* Bytes of opcode memory used */
	x.pSpace = &((u8 *) p->aOp)[n];	/* Unused opcode memory */
	assert(EIGHT_BYTE_ALIGNMENT(x.pSpace));
	x.nFree = ROUNDDOWN8(pParse->szOpAlloc - n);	/* Bytes of unused memory */
	assert(x.nFree >= 0);
	assert(EIGHT_BYTE_ALIGNMENT(&x.pSpace[x.nFree]));

	resolveP2Values(p, &nArg);
	if (pParse->explain && nMem < 10) {
		nMem = 10;
	}
	p->expired = 0;

	/* Memory for registers, parameters, cursor, etc, is allocated in one or two
	 * passes.  On the first pass, we try to reuse unused memory at the
	 * end of the opcode array.  If we are unable to satisfy all memory
	 * requirements by reusing the opcode array tail, then the second
	 * pass will fill in the remainder using a fresh memory allocation.
	 *
	 * This two-pass approach that reuses as much memory as possible from
	 * the leftover memory at the end of the opcode array.  This can significantly
	 * reduce the amount of memory held by a prepared statement.
	 */
	do {
		x.nNeeded = 0;
		p->aMem = allocSpace(&x, p->aMem, nMem * sizeof(Mem));
		p->aVar = allocSpace(&x, p->aVar, nVar * sizeof(Mem));
		p->apArg = allocSpace(&x, p->apArg, nArg * sizeof(Mem *));
		p->apCsr =
		    allocSpace(&x, p->apCsr, nCursor * sizeof(VdbeCursor *));
		if (x.nNeeded == 0)
			break;
		x.pSpace = p->pFree = sqlDbMallocRawNN(db, x.nNeeded);
		x.nFree = x.nNeeded;
	} while (!db->mallocFailed);

	p->pVList = pParse->pVList;
	pParse->pVList = 0;
	p->explain = pParse->explain;
	if (db->mallocFailed) {
		p->nVar = 0;
		p->nCursor = 0;
		p->nMem = 0;
	} else {
		p->nCursor = nCursor;
		p->nVar = (ynVar) nVar;
		initMemArray(p->aVar, nVar, db, MEM_Null);
		p->nMem = nMem;
		initMemArray(p->aMem, nMem, db, MEM_Undefined);
		memset(p->apCsr, 0, nCursor * sizeof(VdbeCursor *));
	}
	sqlVdbeRewind(p);
}

/*
 * Close a VDBE cursor and release all the resources that cursor
 * happens to hold.
 */
void
sqlVdbeFreeCursor(Vdbe * p, VdbeCursor * pCx)
{
	if (pCx == 0) {
		return;
	}
	switch (pCx->eCurType) {
	case CURTYPE_SORTER:{
			sqlVdbeSorterClose(p->db, pCx);
			break;
		}
	case CURTYPE_TARANTOOL:{
		assert(pCx->uc.pCursor != 0);
		sql_cursor_close(pCx->uc.pCursor);
			break;
		}
	}
}

/*
 * Close all cursors in the current frame.
 */
static void
closeCursorsInFrame(Vdbe * p)
{
	if (p->apCsr) {
		int i;
		for (i = 0; i < p->nCursor; i++) {
			VdbeCursor *pC = p->apCsr[i];
			if (pC) {
				sqlVdbeFreeCursor(p, pC);
				p->apCsr[i] = 0;
			}
		}
	}
}

/*
 * Copy the values stored in the VdbeFrame structure to its Vdbe. This
 * is used, for example, when a trigger sub-program is halted to restore
 * control to the main program.
 */
int
sqlVdbeFrameRestore(VdbeFrame * pFrame)
{
	Vdbe *v = pFrame->v;
	closeCursorsInFrame(v);
	v->aOp = pFrame->aOp;
	v->nOp = pFrame->nOp;
	v->aMem = pFrame->aMem;
	v->nMem = pFrame->nMem;
	v->apCsr = pFrame->apCsr;
	v->nCursor = pFrame->nCursor;
	v->nChange = pFrame->nChange;
	v->db->nChange = pFrame->nDbChange;
	return pFrame->pc;
}

/*
 * Close top frame cursors.
 *
 */
static void
closeTopFrameCursors(Vdbe * p)
{
	if (p->pFrame) {
		VdbeFrame *pFrame;
		for (pFrame = p->pFrame; pFrame->pParent;
		     pFrame = pFrame->pParent) ;
		sqlVdbeFrameRestore(pFrame);
		p->pFrame = 0;
		p->nFrame = 0;
	}
	assert(p->nFrame == 0);
	closeCursorsInFrame(p);
}

/*
 * Close cursors in frames marked for deletetion and free memory
 *
 * Delete all frames marked for deletion, which in turn will cause in-frame
 * cursors to be closed.
 * Also release any dynamic memory held by the VM in the Vdbe.aMem memory
 * cell array. This is necessary as the memory cell array may contain
 * pointers to VdbeFrame objects, which may in turn contain pointers to
 * open cursors.
 */
static void
closeCursorsAndFree(Vdbe * p)
{
	if (p->aMem) {
		releaseMemArray(p->aMem, p->nMem);
	}
	while (p->pDelFrame) {
		VdbeFrame *pDel = p->pDelFrame;
		p->pDelFrame = pDel->pParent;
		sqlVdbeFrameDelete(pDel);
	}
}

/*
 * Clean up the VM after a single run.
 */
static void
Cleanup(Vdbe * p)
{

#ifdef SQL_DEBUG
	/* Execute assert() statements to ensure that the Vdbe.apCsr[] and
	 * Vdbe.aMem[] arrays have already been cleaned up.
	 */
	int i;
	if (p->apCsr)
		for (i = 0; i < p->nCursor; i++)
			assert(p->apCsr[i] == 0);
	if (p->aMem) {
		for (i = 0; i < p->nMem; i++)
			assert(p->aMem[i].flags == MEM_Undefined);
	}
#endif

	p->pResultSet = 0;
}

void
vdbe_metadata_delete(struct Vdbe *v)
{
	if (v->metadata != NULL) {
		for (int i = 0; i < v->nResColumn; ++i) {
			free(v->metadata[i].name);
			free(v->metadata[i].type);
			free(v->metadata[i].collation);
			free(v->metadata[i].span);
		}
		free(v->metadata);
	}
}

/*
 * Set the number of result columns that will be returned by this SQL
 * statement. This is now set at compile time, rather than during
 * execution of the vdbe program so that sql_column_count() can
 * be called on an SQL statement before sql_step().
 */
void
sqlVdbeSetNumCols(Vdbe * p, int nResColumn)
{
	vdbe_metadata_delete(p);
	p->nResColumn = (u16) nResColumn;
	p->metadata = (struct sql_column_metadata *)
		calloc(nResColumn, sizeof(struct sql_column_metadata));
	if (p->metadata == NULL) {
		diag_set(OutOfMemory,
			 nResColumn * sizeof(struct sql_column_metadata),
			 "calloc", "metadata");
		return;
	}
	for (int i = 0; i < nResColumn; ++i)
		p->metadata[i].nullable = -1;

}

int
vdbe_metadata_set_col_name(struct Vdbe *p, int idx, const char *name)
{
	assert(idx < p->nResColumn);
	if (p->metadata[idx].name != NULL)
		free(p->metadata[idx].name);
	p->metadata[idx].name = strdup(name);
	if (p->metadata[idx].name == NULL) {
		diag_set(OutOfMemory, strlen(name) + 1, "strdup", "name");
		return -1;
	}
	return 0;
}

int
vdbe_metadata_set_col_type(struct Vdbe *p, int idx, const char *type)
{
	assert(idx < p->nResColumn);
	if (p->metadata[idx].type != NULL)
		free(p->metadata[idx].type);
	p->metadata[idx].type = strdup(type);
	if (p->metadata[idx].type == NULL) {
		diag_set(OutOfMemory, strlen(type) + 1, "strdup", "type");
		return -1;
	}
	return 0;
}

int
vdbe_metadata_set_col_collation(struct Vdbe *p, int idx, const char *coll,
				size_t coll_len)
{
	assert(idx < p->nResColumn);
	if (p->metadata[idx].collation != NULL)
		free(p->metadata[idx].collation);
	p->metadata[idx].collation = strndup(coll, coll_len);
	if (p->metadata[idx].collation == NULL) {
		diag_set(OutOfMemory, coll_len + 1, "strndup", "collation");
		return -1;
	}
	return 0;
}

void
vdbe_metadata_set_col_nullability(struct Vdbe *p, int idx, int nullable)
{
	assert(idx < p->nResColumn);
	p->metadata[idx].nullable = nullable;
}

void
vdbe_metadata_set_col_autoincrement(struct Vdbe *p, int idx)
{
	assert(idx < p->nResColumn);
	p->metadata[idx].is_actoincrement = true;
}

int
vdbe_metadata_set_col_span(struct Vdbe *p, int idx, const char *span)
{
	assert(idx < p->nResColumn);
	if (p->metadata[idx].span != NULL)
		free((void *)p->metadata[idx].span);
	if (span == NULL) {
		p->metadata[idx].span = NULL;
		return 0;
	}
	p->metadata[idx].span = strdup(span);
	if (p->metadata[idx].span == NULL) {
		diag_set(OutOfMemory, strlen(span) + 1, "strdup", "span");
		return -1;
	}
	return 0;
}

/*
 * This routine checks that the sql.nVdbeActive count variable
 * matches the number of vdbe's in the list sql.pVdbe that are
 * currently active. An assertion fails if the two counts do not match.
 * This is an internal self-check only - it is not an essential processing
 * step.
 *
 * This is a no-op if NDEBUG is defined.
 */
#ifndef NDEBUG
static void
checkActiveVdbeCnt(sql * db)
{
	Vdbe *p;
	int cnt = 0;
	p = db->pVdbe;
	while (p) {
		if (sql_stmt_busy((sql_stmt *) p)) {
			cnt++;
		}
		p = p->pNext;
	}
	assert(cnt == db->nVdbeActive);
}
#else
#define checkActiveVdbeCnt(x)
#endif

/*
 * If the Vdbe passed as the first argument opened a statement-transaction,
 * close it now. Argument eOp must be either SAVEPOINT_ROLLBACK or
 * SAVEPOINT_RELEASE. If it is SAVEPOINT_ROLLBACK, then the statement
 * transaction is rolled back. If eOp is SAVEPOINT_RELEASE, then the
 * statement transaction is committed.
 *
 * If an IO error occurs, -1 is returned.
 * Otherwise 0.
 */
int
sqlVdbeCloseStatement(Vdbe * p, int eOp)
{
	int rc = 0;
	struct txn_savepoint *savepoint = p->anonymous_savepoint;
	/*
	 * If we have an anonymous transaction opened -> perform eOp.
	 */
	if (savepoint && eOp == SAVEPOINT_ROLLBACK)
		rc = box_txn_rollback_to_savepoint(savepoint);
	p->anonymous_savepoint = NULL;
	return rc;
}

/*
 * This function is called when a transaction opened by the database
 * handle associated with the VM passed as an argument is about to be
 * committed. If there are outstanding deferred foreign key constraint
 * violations, return -1. Otherwise, 0.
 *
 * If there are outstanding FK violations and this function returns
 * -1 and set an error.
 */
int
sqlVdbeCheckFk(Vdbe * p, int deferred)
{
	struct txn *txn = in_txn();
	if ((deferred && txn != NULL && txn->fk_deferred_count > 0) ||
	    (!deferred && p->nFkConstraint > 0)) {
		p->is_aborted = true;
		p->errorAction = ON_CONFLICT_ACTION_ABORT;
		diag_set(ClientError, ER_SQL_EXECUTE, "FOREIGN KEY constraint "\
			 "failed");
		return -1;
	}
	return 0;
}

/*
 * This routine is called the when a VDBE tries to halt.  If the VDBE
 * has made changes and is in autocommit mode, then commit those
 * changes.  If a rollback is needed, then do the rollback.
 *
 * This routine is the only way to move the state of a VM from
 * SQL_MAGIC_RUN to SQL_MAGIC_HALT.  It is harmless to
 * call this on a VM that is in the SQL_MAGIC_HALT state.
 *
 * Return an error code.
 */
int
sqlVdbeHalt(Vdbe * p)
{
	int rc;			/* Used to store transient return codes */
	sql *db = p->db;

	/* This function contains the logic that determines if a statement or
	 * transaction will be committed or rolled back as a result of the
	 * execution of this virtual machine.
	 */

	if (db->mallocFailed) {
		p->is_aborted = true;
	}
	closeTopFrameCursors(p);
	if (p->magic != VDBE_MAGIC_RUN) {
		return 0;
	}
	checkActiveVdbeCnt(db);

	/* No commit or rollback needed if the program never started or if the
	 * SQL statement does not read or write a database file.
	 */
	if (p->pc >= 0) {
		int eStatementOp = 0;

		/* Check for immediate foreign key violations. */
		if (!p->is_aborted)
			sqlVdbeCheckFk(p, 0);

		/* If the auto-commit flag is set and this is the only active writer
		 * VM, then we do either a commit or rollback of the current transaction.
		 *
		 * Note: This block also runs if one of the special errors handled
		 * above has occurred.
		 */
		if (p->auto_commit) {
			if (!p->is_aborted
			    || (p->errorAction == ON_CONFLICT_ACTION_FAIL)) {
				rc = sqlVdbeCheckFk(p, 1);
				if (rc != 0) {
					/* Close all opened cursors if
					 * they exist and free all
					 * VDBE frames.
					 */
					if (NEVER(p->pDelFrame)) {
						closeCursorsAndFree(p);
						return -1;
					}
				} else {
					/* The auto-commit flag is true, the vdbe program was successful
					 * or hit an 'OR FAIL' constraint and there are no deferred foreign
					 * key constraints to hold up the transaction. This means a commit
					 * is required.
					 */
					rc = (in_txn() == NULL ||
					      txn_commit(in_txn()) == 0) ?
					      0 : -1;
					closeCursorsAndFree(p);
				}
				if (rc != 0) {
					p->is_aborted = true;
					box_txn_rollback();
					closeCursorsAndFree(p);
					sqlRollbackAll(p);
					p->nChange = 0;
				}
			} else {
				box_txn_rollback();
				closeCursorsAndFree(p);
				sqlRollbackAll(p);
				p->nChange = 0;
			}
			p->anonymous_savepoint = NULL;
		} else if (eStatementOp == 0) {
			if (!p->is_aborted ||
			    p->errorAction == ON_CONFLICT_ACTION_FAIL) {
				eStatementOp = SAVEPOINT_RELEASE;
			} else if (p->errorAction == ON_CONFLICT_ACTION_ABORT) {
				eStatementOp = SAVEPOINT_ROLLBACK;
			} else {
				box_txn_rollback();
				closeCursorsAndFree(p);
				sqlRollbackAll(p);
				sqlCloseSavepoints(p);
				p->nChange = 0;
			}
		}

		/* If eStatementOp is non-zero, then a statement transaction needs to
		 * be committed or rolled back. Call sqlVdbeCloseStatement() to
		 * do so. If this operation returns an error, and the current statement
		 * error code is 0 or -1, then promote the
		 * current statement error code.
		 */
		if (eStatementOp) {
			rc = sqlVdbeCloseStatement(p, eStatementOp);
			if (rc) {
				box_txn_rollback();
				p->is_aborted = true;
				closeCursorsAndFree(p);
				sqlRollbackAll(p);
				sqlCloseSavepoints(p);
				p->nChange = 0;
			}
		}

		/*
		 * If this was an INSERT, UPDATE or DELETE and
		 * statement transaction has been rolled back,
		 * update the database connection change-counter.
		 * Other statements should return 0 (zero).
		 */
		if (p->changeCntOn) {
			sqlVdbeSetChanges(db, p->nChange);
			p->nChange = 0;
		} else {
			db->nChange = 0;
		}
	}

	closeCursorsAndFree(p);

	/* We have successfully halted and closed the VM.  Record this fact. */
	if (p->pc >= 0) {
		db->nVdbeActive--;
	}
	p->magic = VDBE_MAGIC_HALT;
	checkActiveVdbeCnt(db);
	if (db->mallocFailed)
		p->is_aborted = true;

	assert(db->nVdbeActive > 0 || box_txn() ||
	       p->anonymous_savepoint == NULL);
	return 0;
}

/*
 * This routine sets is_aborted of VDBE to false.
 */
void
sqlVdbeResetStepResult(Vdbe * p)
{
	p->is_aborted = false;
}

/*
 * Clean up a VDBE after execution but do not delete the VDBE just yet.
 * Return the result code.
 *
 * After this routine is run, the VDBE should be ready to be executed
 * again.
 *
 * To look at it another way, this routine resets the state of the
 * virtual machine from VDBE_MAGIC_RUN or VDBE_MAGIC_HALT back to
 * VDBE_MAGIC_INIT.
 */
int
sqlVdbeReset(Vdbe * p)
{
	/* If the VM did not run to completion or if it encountered an
	 * error, then it might not have been halted properly.  So halt
	 * it now.
	 */
	sqlVdbeHalt(p);

	/* If the VDBE has be run even partially, then transfer the error code
	 * and error message from the VDBE into the main database structure.  But
	 * if the VDBE has just been set to run but has not actually executed any
	 * instructions yet, leave the main database error information unchanged.
	 */
	if (p->pc >= 0) {
		if (p->runOnlyOnce)
			p->expired = 1;
	} else {
		/*
		 * An error should be thrown here if the expired
		 * flag is set on the VDBE flag with the first
		 * call to sql_step(). However, the expired flag
		 * is currently disabled, so this error has been
		 * replaced with assert.
		 */
		assert(!p->is_aborted || p->expired == 0);
	}

	/* Reclaim all memory used by the VDBE
	 */
	Cleanup(p);

	/* Save profiling information from this VDBE run.
	 */
#ifdef VDBE_PROFILE
	{
		FILE *out = fopen("vdbe_profile.out", "a");
		if (out) {
			int i;
			fprintf(out, "---- ");
			for (i = 0; i < p->nOp; i++) {
				fprintf(out, "%02x", p->aOp[i].opcode);
			}
			fprintf(out, "\n");
			if (p->zSql) {
				char c, pc = 0;
				fprintf(out, "-- ");
				for (i = 0; (c = p->zSql[i]) != 0; i++) {
					if (pc == '\n')
						fprintf(out, "-- ");
					putc(c, out);
					pc = c;
				}
				if (pc != '\n')
					fprintf(out, "\n");
			}
			for (i = 0; i < p->nOp; i++) {
				char zHdr[100];
				sql_snprintf(sizeof(zHdr), zHdr,
						 "%6u %12llu %8llu ",
						 p->aOp[i].cnt,
						 p->aOp[i].cycles,
						 p->aOp[i].cnt >
						 0 ? p->aOp[i].cycles /
						 p->aOp[i].cnt : 0);
				fprintf(out, "%s", zHdr);
				sqlVdbePrintOp(out, i, &p->aOp[i]);
			}
			fclose(out);
		}
	}
#endif
	p->iCurrentTime = 0;
	p->magic = VDBE_MAGIC_RESET;
	return p->is_aborted ? -1 : 0;
}

/*
 * Clean up and delete a VDBE after execution.  Return an integer which is
 * the result code.
 */
int
sqlVdbeFinalize(Vdbe * p)
{
	if (p == NULL)
		return 0;
	int rc = 0;
	if (p->magic == VDBE_MAGIC_RUN || p->magic == VDBE_MAGIC_HALT)
		rc = sqlVdbeReset(p);
	sqlVdbeDelete(p);
	return rc;
}

/*
 * Free all memory associated with the Vdbe passed as the second argument,
 * except for object itself, which is preserved.
 *
 * The difference between this function and sqlVdbeDelete() is that
 * VdbeDelete() also unlinks the Vdbe from the list of VMs associated with
 * the database connection and frees the object itself.
 */
void
sqlVdbeClearObject(sql * db, Vdbe * p)
{
	SubProgram *pSub, *pNext;
	assert(p->db == 0 || p->db == db);
	vdbe_metadata_delete(p);
	for (pSub = p->pProgram; pSub; pSub = pNext) {
		pNext = pSub->pNext;
		vdbeFreeOpArray(db, pSub->aOp, pSub->nOp);
		sqlDbFree(db, pSub);
	}
	if (p->magic != VDBE_MAGIC_INIT) {
		releaseMemArray(p->aVar, p->nVar);
		sqlDbFree(db, p->pVList);
		sqlDbFree(db, p->pFree);
	}
	vdbeFreeOpArray(db, p->aOp, p->nOp);
	sqlDbFree(db, p->zSql);
}

/*
 * Delete an entire VDBE.
 */
void
sqlVdbeDelete(Vdbe * p)
{
	sql *db;

	if (NEVER(p == 0))
		return;
	db = p->db;
	sqlVdbeClearObject(db, p);
	if (p->pPrev) {
		p->pPrev->pNext = p->pNext;
	} else {
		assert(db->pVdbe == p);
		db->pVdbe = p->pNext;
	}
	if (p->pNext) {
		p->pNext->pPrev = p->pPrev;
	}
	p->magic = VDBE_MAGIC_DEAD;
	p->db = 0;
	free(p->var_pos);
	/*
	 * VDBE is responsible for releasing region after txn
	 * was commited.
	 */
	if (in_txn() == NULL)
		fiber_gc();
	sqlDbFree(db, p);
}

/*
 * This routine is used to allocate sufficient space for an UnpackedRecord
 * structure large enough to be used with sqlVdbeRecordUnpack() if
 * the first argument is a pointer to key_def structure.
 *
 * The space is either allocated using sqlDbMallocRaw() or from within
 * the unaligned buffer passed via the second and third arguments (presumably
 * stack space). If the former, then *ppFree is set to a pointer that should
 * be eventually freed by the caller using sqlDbFree(). Or, if the
 * allocation comes from the pSpace/szSpace buffer, *ppFree is set to NULL
 * before returning.
 *
 * If an OOM error occurs, NULL is returned.
 */
UnpackedRecord *
sqlVdbeAllocUnpackedRecord(struct sql *db, struct key_def *key_def)
{
	UnpackedRecord *p;	/* Unpacked record to return */
	int nByte;		/* Number of bytes required for *p */
	nByte =
	    ROUND8(sizeof(UnpackedRecord)) + sizeof(Mem) * (key_def->part_count +
							    1);
	p = (UnpackedRecord *) sqlDbMallocRaw(db, nByte);
	if (!p)
		return 0;
	p->aMem = (Mem *) & ((char *)p)[ROUND8(sizeof(UnpackedRecord))];
	p->key_def = key_def;
	p->nField = key_def->part_count + 1;
	return p;
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

/*
 * Compare two blobs.  Return negative, zero, or positive if the first
 * is less than, equal to, or greater than the second, respectively.
 * If one blob is a prefix of the other, then the shorter is the lessor.
 */
static SQL_NOINLINE int
sqlBlobCompare(const Mem * pB1, const Mem * pB2)
{
	int c;
	int n1 = pB1->n;
	int n2 = pB2->n;

	/* It is possible to have a Blob value that has some non-zero content
	 * followed by zero content.  But that only comes up for Blobs formed
	 * by the OP_MakeRecord opcode, and such Blobs never get passed into
	 * sqlMemCompare().
	 */
	assert((pB1->flags & MEM_Zero) == 0 || n1 == 0);
	assert((pB2->flags & MEM_Zero) == 0 || n2 == 0);

	if ((pB1->flags | pB2->flags) & MEM_Zero) {
		if (pB1->flags & pB2->flags & MEM_Zero) {
			return pB1->u.nZero - pB2->u.nZero;
		} else if (pB1->flags & MEM_Zero) {
			if (!isAllZero(pB2->z, pB2->n))
				return -1;
			return pB1->u.nZero - n2;
		} else {
			if (!isAllZero(pB1->z, pB1->n))
				return +1;
			return n1 - pB2->u.nZero;
		}
	}
	c = memcmp(pB1->z, pB2->z, n1 > n2 ? n2 : n1);
	if (c)
		return c;
	return n1 - n2;
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
		if ((f1 & f2 & MEM_Int) != 0) {
			if (pMem1->u.i < pMem2->u.i)
				return -1;
			if (pMem1->u.i > pMem2->u.i)
				return +1;
			return 0;
		}
		if ((f1 & f2 & MEM_UInt) != 0) {
			if (pMem1->u.u < pMem2->u.u)
				return -1;
			if (pMem1->u.u > pMem2->u.u)
				return +1;
			return 0;
		}
		if ((f1 & f2 & MEM_Real) != 0) {
			if (pMem1->u.r < pMem2->u.r)
				return -1;
			if (pMem1->u.r > pMem2->u.r)
				return +1;
			return 0;
		}
		if ((f1 & MEM_Int) != 0) {
			if ((f2 & MEM_Real) != 0) {
				return double_compare_nint64(pMem2->u.r,
							     pMem1->u.i, -1);
			} else {
				return -1;
			}
		}
		if ((f1 & MEM_UInt) != 0) {
			if ((f2 & MEM_Real) != 0) {
				return double_compare_uint64(pMem2->u.r,
							     pMem1->u.u, -1);
			} else if ((f2 & MEM_Int) != 0) {
				return +1;
			} else {
				return -1;
			}
		}
		if ((f1 & MEM_Real) != 0) {
			if ((f2 & MEM_Int) != 0) {
				return double_compare_nint64(pMem1->u.r,
							     pMem2->u.i, 1);
			} else if ((f2 & MEM_UInt) != 0) {
				return double_compare_uint64(pMem1->u.r,
							     pMem2->u.u, 1);
			} else {
				return -1;
			}
		}
		return +1;
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
		/* The collation sequence must be defined at this point, even if
		 * the user deletes the collation sequence after the vdbe program is
		 * compiled (this was not always the case).
		 */
		if (pColl) {
			return vdbeCompareMemString(pMem1, pMem2, pColl);
		} else {
			size_t n = pMem1->n < pMem2->n ? pMem1->n : pMem2->n;
			int res;
			res = memcmp(pMem1->z, pMem2->z, n);
			if (res == 0)
				res = (int)pMem1->n - (int)pMem2->n;
			return res;
		}
		/* If a NULL pointer was passed as the collate function, fall through
		 * to the blob case and use memcmp().
		 */
	}

	/* Both values must be blobs.  Compare using memcmp().  */
	return sqlBlobCompare(pMem1, pMem2);
}

/*
 * This routine sets the value to be returned by subsequent calls to
 * sql_changes() on the database handle 'db'.
 */
void
sqlVdbeSetChanges(sql * db, int nChange)
{
	db->nChange = nChange;
}

/*
 * Set a flag in the vdbe to update the change counter when it is finalised
 * or reset.
 */
void
sqlVdbeCountChanges(Vdbe * v)
{
	v->changeCntOn = 1;
}

/*
 * Mark every prepared statement associated with a database connection
 * as expired.
 *
 * An expired statement means that recompilation of the statement is
 * recommend.  Statements expire when things happen that make their
 * programs obsolete.  Removing user-defined functions or collating
 * sequences, or changing an authorization function are the types of
 * things that make prepared statements obsolete.
 */
void
sqlExpirePreparedStatements(sql * db)
{
	Vdbe *p;
	for (p = db->pVdbe; p; p = p->pNext) {
		p->expired = 1;
	}
}

/*
 * Return the database associated with the Vdbe.
 */
sql *
sqlVdbeDb(Vdbe * v)
{
	return v->db;
}

/*
 * Return a pointer to an sql_value structure containing the value bound
 * parameter iVar of VM v. Except, if the value is an SQL NULL, return
 * 0 instead. Unless it is NULL, apply type to the value before returning it.
 *
 * The returned value must be freed by the caller using sqlValueFree().
 */
sql_value *
sqlVdbeGetBoundValue(Vdbe * v, int iVar, u8 aff)
{
	assert(iVar > 0);
	if (v) {
		Mem *pMem = &v->aVar[iVar - 1];
		if (0 == (pMem->flags & MEM_Null)) {
			sql_value *pRet = sqlValueNew(v->db);
			if (pRet) {
				sqlVdbeMemCopy((Mem *) pRet, pMem);
				sql_value_apply_type(pRet, aff);
			}
			return pRet;
		}
	}
	return 0;
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
vdbe_decode_msgpack_into_mem(const char *buf, struct Mem *mem, uint32_t *len)
{
	const char *start_buf = buf;
	switch (mp_typeof(*buf)) {
	case MP_ARRAY:
	case MP_MAP:
	case MP_EXT:
	default: {
		mem->flags = 0;
		break;
	}
	case MP_NIL: {
		mp_decode_nil(&buf);
		mem->flags = MEM_Null;
		break;
	}
	case MP_BOOL: {
		mem->u.b = mp_decode_bool(&buf);
		mem->flags = MEM_Bool;
		break;
	}
	case MP_UINT: {
		uint64_t v = mp_decode_uint(&buf);
		mem->u.u = v;
		mem->flags = MEM_UInt;
		break;
	}
	case MP_INT: {
		mem->u.i = mp_decode_int(&buf);
		mem->flags = MEM_Int;
		break;
	}
	case MP_STR: {
		/* XXX u32->int */
		mem->n = (int) mp_decode_strl(&buf);
		mem->flags = MEM_Str | MEM_Ephem;
install_blob:
		mem->z = (char *)buf;
		buf += mem->n;
		break;
	}
	case MP_BIN: {
		/* XXX u32->int */
		mem->n = (int) mp_decode_binl(&buf);
		mem->flags = MEM_Blob | MEM_Ephem;
		goto install_blob;
	}
	case MP_FLOAT: {
		mem->u.r = mp_decode_float(&buf);
		mem->flags = sqlIsNaN(mem->u.r) ? MEM_Null : MEM_Real;
		break;
	}
	case MP_DOUBLE: {
		mem->u.r = mp_decode_double(&buf);
		mem->flags = sqlIsNaN(mem->u.r) ? MEM_Null : MEM_Real;
		break;
	}
	}
	*len = (uint32_t)(buf - start_buf);
	return 0;
}

void
sqlVdbeRecordUnpackMsgpack(struct key_def *key_def,	/* Information about the record format */
			       const void *pKey,	/* The binary record */
			       UnpackedRecord * p)	/* Populate this structure before returning. */
{
	uint32_t n;
	const char *zParse = pKey;
	Mem *pMem = p->aMem;
	n = mp_decode_array(&zParse);
	n = p->nField = MIN(n, key_def->part_count);
	p->default_rc = 0;
	p->key_def = key_def;
	while (n--) {
		pMem->szMalloc = 0;
		pMem->z = 0;
		uint32_t sz = 0;
		vdbe_decode_msgpack_into_mem(zParse, pMem, &sz);
		if (sz == 0) {
			/* MsgPack array, map or ext. Treat as blob. */
			pMem->z = (char *)zParse;
			mp_next(&zParse);
			pMem->n = zParse - pMem->z;
			pMem->flags = MEM_Blob | MEM_Ephem;
		} else {
			zParse += sz;
		}
		pMem++;
	}
}
