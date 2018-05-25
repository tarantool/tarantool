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
 * The code in this file implements the function that runs the
 * bytecode of a prepared statement.
 *
 * Various scripts scan this source file in order to generate HTML
 * documentation, headers files, or other derived files.  The formatting
 * of the code in this file is, therefore, important.  See other comments
 * in this file for details.  If in doubt, do not deviate from existing
 * commenting and indentation practices when changing or adding code.
 */
#include "box/txn.h"
#include "box/session.h"
#include "sqliteInt.h"
#include "vdbeInt.h"
#include "tarantoolInt.h"

#include "msgpuck/msgpuck.h"

#include "box/schema.h"
#include "box/space.h"
#include "box/sequence.h"

/*
 * Invoke this macro on memory cells just prior to changing the
 * value of the cell.  This macro verifies that shallow copies are
 * not misused.  A shallow copy of a string or blob just copies a
 * pointer to the string or blob, not the content.  If the original
 * is changed while the copy is still in use, the string or blob might
 * be changed out from under the copy.  This macro verifies that nothing
 * like that ever happens.
 */
#ifdef SQLITE_DEBUG
# define memAboutToChange(P,M) sqlite3VdbeMemAboutToChange(P,M)
#else
# define memAboutToChange(P,M)
#endif

/*
 * The following global variable is incremented every time a cursor
 * moves, either by the OP_SeekXX, OP_Next, or OP_Prev opcodes.  The test
 * procedures use this information to make sure that indices are
 * working correctly.  This variable has no function other than to
 * help verify the correct operation of the library.
 */
#ifdef SQLITE_TEST
int sql_search_count = 0;
#endif

/*
 * When this global variable is positive, it gets decremented once before
 * each instruction in the VDBE.  When it reaches zero, the u1.isInterrupted
 * field of the sqlite3 structure is set in order to simulate an interrupt.
 *
 * This facility is used for testing purposes only.  It does not function
 * in an ordinary build.
 */
#ifdef SQLITE_TEST
int sqlite3_interrupt_count = 0;
#endif

/*
 * The next global variable is incremented each type the OP_Sort opcode
 * is executed.  The test procedures use this information to make sure that
 * sorting is occurring or not occurring at appropriate times.   This variable
 * has no function other than to help verify the correct operation of the
 * library.
 */
#ifdef SQLITE_TEST
int sql_sort_count = 0;
#endif

/*
 * The next global variable records the size of the largest MEM_Blob
 * or MEM_Str that has been used by a VDBE opcode.  The test procedures
 * use this information to make sure that the zero-blob functionality
 * is working correctly.   This variable has no function other than to
 * help verify the correct operation of the library.
 */
#ifdef SQLITE_TEST
int sqlite3_max_blobsize = 0;
static void
updateMaxBlobsize(Mem *p)
{
	if ((p->flags & (MEM_Str|MEM_Blob))!=0 && p->n>sqlite3_max_blobsize) {
		sqlite3_max_blobsize = p->n;
	}
}
#endif

/*
 * This macro evaluates to true if either the update hook or the preupdate
 * hook are enabled for database connect DB.
 */
#ifdef SQLITE_ENABLE_PREUPDATE_HOOK
# define HAS_UPDATE_HOOK(DB) ((DB)->xPreUpdateCallback||(DB)->xUpdateCallback)
#else
# define HAS_UPDATE_HOOK(DB) ((DB)->xUpdateCallback)
#endif

/*
 * The next global variable is incremented each time the OP_Found opcode
 * is executed. This is used to test whether or not the foreign key
 * operation implemented using OP_FkIsZero is working. This variable
 * has no function other than to help verify the correct operation of the
 * library.
 */
#ifdef SQLITE_TEST
int sql_found_count = 0;
#endif

/*
 * Test a register to see if it exceeds the current maximum blob size.
 * If it does, record the new maximum blob size.
 */
#if defined(SQLITE_TEST) && !defined(SQLITE_UNTESTABLE)
# define UPDATE_MAX_BLOBSIZE(P)  updateMaxBlobsize(P)
#else
# define UPDATE_MAX_BLOBSIZE(P)
#endif

/*
 * Invoke the VDBE coverage callback, if that callback is defined.  This
 * feature is used for test suite validation only and does not appear an
 * production builds.
 *
 * M is an integer, 2 or 3, that indices how many different ways the
 * branch can go.  It is usually 2.  "I" is the direction the branch
 * goes.  0 means falls through.  1 means branch is taken.  2 means the
 * second alternative branch is taken.
 *
 * iSrcLine is the source code line (from the __LINE__ macro) that
 * generated the VDBE instruction.  This instrumentation assumes that all
 * source code is in a single file (the amalgamation).  Special values 1
 * and 2 for the iSrcLine parameter mean that this particular branch is
 * always taken or never taken, respectively.
 */
#if !defined(SQLITE_VDBE_COVERAGE)
# define VdbeBranchTaken(I,M)
#else
# define VdbeBranchTaken(I,M) vdbeTakeBranch(pOp->iSrcLine,I,M)
static void
vdbeTakeBranch(int iSrcLine, u8 I, u8 M)
{
	if (iSrcLine<=2 && ALWAYS(iSrcLine>0)) {
		M = iSrcLine;
		/* Assert the truth of VdbeCoverageAlwaysTaken() and
		 * VdbeCoverageNeverTaken()
		 */
		assert((M & I)==I);
	} else {
		if (sqlite3GlobalConfig.xVdbeBranch==0) return;  /*NO_TEST*/
		sqlite3GlobalConfig.xVdbeBranch(sqlite3GlobalConfig.pVdbeBranchArg,
						iSrcLine,I,M);
	}
}
#endif

/*
 * Convert the given register into a string if it isn't one
 * already. Return non-zero if a malloc() fails.
 */
#define Stringify(P)						\
	if(((P)->flags&(MEM_Str|MEM_Blob))==0 && sqlite3VdbeMemStringify(P,0)) \
	{ goto no_mem; }

/*
 * An ephemeral string value (signified by the MEM_Ephem flag) contains
 * a pointer to a dynamically allocated string where some other entity
 * is responsible for deallocating that string.  Because the register
 * does not control the string, it might be deleted without the register
 * knowing it.
 *
 * This routine converts an ephemeral string into a dynamically allocated
 * string that the register itself controls.  In other words, it
 * converts an MEM_Ephem string into a string with P.z==P.zMalloc.
 */
#define Deephemeralize(P)					\
	if (((P)->flags&MEM_Ephem)!=0				\
	    && sqlite3VdbeMemMakeWriteable(P)) { goto no_mem;}

/* Return true if the cursor was opened using the OP_OpenSorter opcode. */
#define isSorter(x) ((x)->eCurType==CURTYPE_SORTER)

/*
 * Allocate VdbeCursor number iCur.  Return a pointer to it.  Return NULL
 * if we run out of memory.
 */
static VdbeCursor *
allocateCursor(
	Vdbe *p,              /* The virtual machine */
	int iCur,             /* Index of the new VdbeCursor */
	int nField,           /* Number of fields in the table or index */
	u8 eCurType           /* Type of the new cursor */
	)
{
	/* Find the memory cell that will be used to store the blob of memory
	 * required for this VdbeCursor structure. It is convenient to use a
	 * vdbe memory cell to manage the memory allocation required for a
	 * VdbeCursor structure for the following reasons:
	 *
	 *   * Sometimes cursor numbers are used for a couple of different
	 *     purposes in a vdbe program. The different uses might require
	 *     different sized allocations. Memory cells provide growable
	 *     allocations.
	 *
	 *   * When using ENABLE_MEMORY_MANAGEMENT, memory cell buffers can
	 *     be freed lazily via the sqlite3_release_memory() API. This
	 *     minimizes the number of malloc calls made by the system.
	 *
	 * The memory cell for cursor 0 is aMem[0]. The rest are allocated from
	 * the top of the register space.  Cursor 1 is at Mem[p->nMem-1].
	 * Cursor 2 is at Mem[p->nMem-2]. And so forth.
	 */
	Mem *pMem = iCur>0 ? &p->aMem[p->nMem-iCur] : p->aMem;

	int nByte;
	VdbeCursor *pCx = 0;
	nByte =
		ROUND8(sizeof(VdbeCursor)) + sizeof(u32)*nField +
		(eCurType==CURTYPE_TARANTOOL ? ROUND8(sizeof(BtCursor)) : 0);

	assert(iCur>=0 && iCur<p->nCursor);
	if (p->apCsr[iCur]) { /*OPTIMIZATION-IF-FALSE*/
		sqlite3VdbeFreeCursor(p, p->apCsr[iCur]);
		p->apCsr[iCur] = 0;
	}
	if (SQLITE_OK==sqlite3VdbeMemClearAndResize(pMem, nByte)) {
		p->apCsr[iCur] = pCx = (VdbeCursor*)pMem->z;
		memset(pCx, 0, offsetof(VdbeCursor,uc));
		pCx->eCurType = eCurType;
		pCx->nField = nField;
		if (eCurType==CURTYPE_TARANTOOL) {
			pCx->uc.pCursor = (BtCursor*)
				&pMem->z[ROUND8(sizeof(VdbeCursor))+sizeof(u32)*nField];
			sqlite3CursorZero(pCx->uc.pCursor);
		}
	}
	return pCx;
}

/*
 * Try to convert a value into a numeric representation if we can
 * do so without loss of information.  In other words, if the string
 * looks like a number, convert it into a number.  If it does not
 * look like a number, leave it alone.
 *
 * If the bTryForInt flag is true, then extra effort is made to give
 * an integer representation.  Strings that look like floating point
 * values but which have no fractional component (example: '48.00')
 * will have a MEM_Int representation when bTryForInt is true.
 *
 * If bTryForInt is false, then if the input string contains a decimal
 * point or exponential notation, the result is only MEM_Real, even
 * if there is an exact integer representation of the quantity.
 */
static void
applyNumericAffinity(Mem *pRec, int bTryForInt)
{
	double rValue;
	i64 iValue;
	assert((pRec->flags & (MEM_Str|MEM_Int|MEM_Real))==MEM_Str);
	if (sqlite3AtoF(pRec->z, &rValue, pRec->n)==0) return;
	if (0==sqlite3Atoi64(pRec->z, &iValue, pRec->n)) {
		pRec->u.i = iValue;
		pRec->flags |= MEM_Int;
	} else {
		pRec->u.r = rValue;
		pRec->flags |= MEM_Real;
		if (bTryForInt) sqlite3VdbeIntegerAffinity(pRec);
	}
}

/*
 * Processing is determine by the affinity parameter:
 *
 * AFFINITY_INTEGER:
 * AFFINITY_REAL:
 * AFFINITY_NUMERIC:
 *    Try to convert pRec to an integer representation or a
 *    floating-point representation if an integer representation
 *    is not possible.  Note that the integer representation is
 *    always preferred, even if the affinity is REAL, because
 *    an integer representation is more space efficient on disk.
 *
 * AFFINITY_TEXT:
 *    Convert pRec to a text representation.
 *
 * AFFINITY_BLOB:
 *    No-op.  pRec is unchanged.
 */
static void
applyAffinity(
	Mem *pRec,          /* The value to apply affinity to */
	char affinity       /* The affinity to be applied */
	)
{
	if (affinity>=AFFINITY_NUMERIC) {
		assert(affinity==AFFINITY_INTEGER || affinity==AFFINITY_REAL
			|| affinity==AFFINITY_NUMERIC);
		if ((pRec->flags & MEM_Int)==0) { /*OPTIMIZATION-IF-FALSE*/
			if ((pRec->flags & MEM_Real)==0) {
				if (pRec->flags & MEM_Str) applyNumericAffinity(pRec,1);
			} else {
				sqlite3VdbeIntegerAffinity(pRec);
			}
		}
	} else if (affinity==AFFINITY_TEXT) {
		/* Only attempt the conversion to TEXT if there is an integer or real
		 * representation (blob and NULL do not get converted) but no string
		 * representation.  It would be harmless to repeat the conversion if
		 * there is already a string rep, but it is pointless to waste those
		 * CPU cycles.
		 */
		if (0==(pRec->flags&MEM_Str)) { /*OPTIMIZATION-IF-FALSE*/
			if ((pRec->flags&(MEM_Real|MEM_Int))) {
				sqlite3VdbeMemStringify(pRec, 1);
			}
		}
		pRec->flags &= ~(MEM_Real|MEM_Int);
	}
}

/*
 * Try to convert the type of a function argument or a result column
 * into a numeric representation.  Use either INTEGER or REAL whichever
 * is appropriate.  But only do the conversion if it is possible without
 * loss of information and return the revised type of the argument.
 */
int sqlite3_value_numeric_type(sqlite3_value *pVal) {
	int eType = sqlite3_value_type(pVal);
	if (eType==SQLITE_TEXT) {
		Mem *pMem = (Mem*)pVal;
		applyNumericAffinity(pMem, 0);
		eType = sqlite3_value_type(pVal);
	}
	return eType;
}

/*
 * Exported version of applyAffinity(). This one works on sqlite3_value*,
 * not the internal Mem* type.
 */
void
sqlite3ValueApplyAffinity(
	sqlite3_value *pVal,
	u8 affinity)
{
	applyAffinity((Mem *)pVal, affinity);
}

/*
 * pMem currently only holds a string type (or maybe a BLOB that we can
 * interpret as a string if we want to).  Compute its corresponding
 * numeric type, if has one.  Set the pMem->u.r and pMem->u.i fields
 * accordingly.
 */
static u16 SQLITE_NOINLINE computeNumericType(Mem *pMem)
{
	assert((pMem->flags & (MEM_Int|MEM_Real))==0);
	assert((pMem->flags & (MEM_Str|MEM_Blob))!=0);
	if (sqlite3AtoF(pMem->z, &pMem->u.r, pMem->n)==0) {
		return 0;
	}
	if (sqlite3Atoi64(pMem->z, &pMem->u.i, pMem->n)==SQLITE_OK) {
		return MEM_Int;
	}
	return MEM_Real;
}

/*
 * Return the numeric type for pMem, either MEM_Int or MEM_Real or both or
 * none.
 *
 * Unlike applyNumericAffinity(), this routine does not modify pMem->flags.
 * But it does set pMem->u.r and pMem->u.i appropriately.
 */
static u16 numericType(Mem *pMem)
{
	if (pMem->flags & (MEM_Int|MEM_Real)) {
		return pMem->flags & (MEM_Int|MEM_Real);
	}
	if (pMem->flags & (MEM_Str|MEM_Blob)) {
		return computeNumericType(pMem);
	}
	return 0;
}

#ifdef SQLITE_DEBUG
/*
 * Write a nice string representation of the contents of cell pMem
 * into buffer zBuf, length nBuf.
 */
void
sqlite3VdbeMemPrettyPrint(Mem *pMem, char *zBuf)
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

		sqlite3_snprintf(100, zCsr, "%c", c);
		zCsr += sqlite3Strlen30(zCsr);
		sqlite3_snprintf(100, zCsr, "%d[", pMem->n);
		zCsr += sqlite3Strlen30(zCsr);
		for(i=0; i<16 && i<pMem->n; i++) {
			sqlite3_snprintf(100, zCsr, "%02X", ((int)pMem->z[i] & 0xFF));
			zCsr += sqlite3Strlen30(zCsr);
		}
		for(i=0; i<16 && i<pMem->n; i++) {
			char z = pMem->z[i];
			if (z<32 || z>126) *zCsr++ = '.';
			else *zCsr++ = z;
		}
		sqlite3_snprintf(100, zCsr, "]%s", "(8)");
		zCsr += sqlite3Strlen30(zCsr);
		if (f & MEM_Zero) {
			sqlite3_snprintf(100, zCsr,"+%dz",pMem->u.nZero);
			zCsr += sqlite3Strlen30(zCsr);
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
		sqlite3_snprintf(100, &zBuf[k], "%d", pMem->n);
		k += sqlite3Strlen30(&zBuf[k]);
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
		sqlite3_snprintf(100,&zBuf[k],"(8)");
		k += sqlite3Strlen30(&zBuf[k]);
		zBuf[k++] = 0;
	}
}
#endif

#ifdef SQLITE_DEBUG
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
#ifndef SQLITE_OMIT_FLOATING_POINT
	} else if (p->flags & MEM_Real) {
		printf(" r:%g", p->u.r);
#endif
	} else {
		char zBuf[200];
		sqlite3VdbeMemPrettyPrint(p, zBuf);
		printf(" %s", zBuf);
	}
	if (p->flags & MEM_Subtype) printf(" subtype=0x%02x", p->eSubtype);
}
static void
registerTrace(int iReg, Mem *p) {
	printf("REG[%d] = ", iReg);
	memTracePrint(p);
	printf("\n");
}
#endif

#ifdef SQLITE_DEBUG
#  define REGISTER_TRACE(R,M)						\
	if(user_session->sql_flags&SQLITE_VdbeTrace) registerTrace(R,M);
#else
#  define REGISTER_TRACE(R,M)
#endif


#ifdef VDBE_PROFILE

/*
 * hwtime.h contains inline assembler code for implementing
 * high-performance timing routines.
 */
#include "hwtime.h"

#endif

/*
 * Return the register of pOp->p2 after first preparing it to be
 * overwritten with an integer value.
 */
static SQLITE_NOINLINE Mem *
out2PrereleaseWithClear(Mem *pOut)
{
	sqlite3VdbeMemSetNull(pOut);
	pOut->flags = MEM_Int;
	return pOut;
}

static Mem *
out2Prerelease(Vdbe *p, VdbeOp *pOp)
{
	Mem *pOut;
	assert(pOp->p2>0);
	assert(pOp->p2<=(p->nMem+1 - p->nCursor));
	pOut = &p->aMem[pOp->p2];
	memAboutToChange(p, pOut);
	if (VdbeMemDynamic(pOut)) { /*OPTIMIZATION-IF-FALSE*/
		return out2PrereleaseWithClear(pOut);
	} else {
		pOut->flags = MEM_Int;
		return pOut;
	}
}

/*
 * Execute as much of a VDBE program as we can.
 * This is the core of sqlite3_step().
 */
int sqlite3VdbeExec(Vdbe *p)
{
	Op *aOp = p->aOp;          /* Copy of p->aOp */
	Op *pOp = aOp;             /* Current operation */
#if defined(SQLITE_DEBUG) || defined(VDBE_PROFILE)
	Op *pOrigOp;               /* Value of pOp at the top of the loop */
#endif
#ifdef SQLITE_DEBUG
	int nExtraDelete = 0;      /* Verifies FORDELETE and AUXDELETE flags */
#endif
	int rc = SQLITE_OK;        /* Value to return */
	sqlite3 *db = p->db;       /* The database */
	u8 resetSchemaOnFault = 0; /* Reset schema after an error if positive */
	int iCompare = 0;          /* Result of last comparison */
	unsigned nVmStep = 0;      /* Number of virtual machine steps */
#ifndef SQLITE_OMIT_PROGRESS_CALLBACK
	unsigned nProgressLimit = 0;/* Invoke xProgress() when nVmStep reaches this */
#endif
	Mem *aMem = p->aMem;       /* Copy of p->aMem */
	Mem *pIn1 = 0;             /* 1st input operand */
	Mem *pIn2 = 0;             /* 2nd input operand */
	Mem *pIn3 = 0;             /* 3rd input operand */
	Mem *pOut = 0;             /* Output operand */
	int *aPermute = 0;         /* Permutation of columns for OP_Compare */
#ifdef VDBE_PROFILE
	u64 start;                 /* CPU clock count at start of opcode */
#endif
	struct session *user_session = current_session();
	/*** INSERT STACK UNION HERE ***/

	assert(p->magic==VDBE_MAGIC_RUN);  /* sqlite3_step() verifies this */
	if (p->rc==SQLITE_NOMEM) {
		/* This happens if a malloc() inside a call to sqlite3_column_text() or
		 * sqlite3_column_text16() failed.
		 */
		goto no_mem;
	}
	assert(p->rc==SQLITE_OK || (p->rc&0xff)==SQLITE_BUSY);
	p->rc = SQLITE_OK;
	p->iCurrentTime = 0;
	assert(p->explain==0);
	p->pResultSet = 0;
	db->busyHandler.nBusy = 0;
	if (db->u1.isInterrupted) goto abort_due_to_interrupt;
	sqlite3VdbeIOTraceSql(p);
#ifndef SQLITE_OMIT_PROGRESS_CALLBACK
	if (db->xProgress) {
		u32 iPrior = p->aCounter[SQLITE_STMTSTATUS_VM_STEP];
		assert(0 < db->nProgressOps);
		nProgressLimit = db->nProgressOps - (iPrior % db->nProgressOps);
	}
#endif
#ifdef SQLITE_DEBUG
	sqlite3BeginBenignMalloc();
	if (p->pc==0
	    && (user_session->sql_flags&
		(SQLITE_VdbeListing|SQLITE_VdbeEQP|SQLITE_VdbeTrace))!=0
		) {
		int i;
		int once = 1;
		sqlite3VdbePrintSql(p);
		if (user_session->sql_flags & SQLITE_VdbeListing) {
			printf("VDBE Program Listing:\n");
			for(i=0; i<p->nOp; i++) {
				sqlite3VdbePrintOp(stdout, i, &aOp[i]);
			}
		}
		if (user_session->sql_flags & SQLITE_VdbeEQP) {
			for(i=0; i<p->nOp; i++) {
				if (aOp[i].opcode==OP_Explain) {
					if (once) printf("VDBE Query Plan:\n");
					printf("%s\n", aOp[i].p4.z);
					once = 0;
				}
			}
		}
		if (user_session->sql_flags & SQLITE_VdbeTrace)  printf("VDBE Trace:\n");
	}
	sqlite3EndBenignMalloc();
#endif
	for(pOp=&aOp[p->pc]; 1; pOp++) {
		/* Errors are detected by individual opcodes, with an immediate
		 * jumps to abort_due_to_error.
		 */
		assert(rc==SQLITE_OK);

		assert(pOp>=aOp && pOp<&aOp[p->nOp]);
#ifdef VDBE_PROFILE
		start = sqlite3Hwtime();
#endif
		nVmStep++;
#ifdef SQLITE_ENABLE_STMT_SCANSTATUS
		if (p->anExec) p->anExec[(int)(pOp-aOp)]++;
#endif

		/* Only allow tracing if SQLITE_DEBUG is defined.
		 */
#ifdef SQLITE_DEBUG
		if (user_session->sql_flags & SQLITE_VdbeTrace) {
			sqlite3VdbePrintOp(stdout, (int)(pOp - aOp), pOp);
		}
#endif


		/* Check to see if we need to simulate an interrupt.  This only happens
		 * if we have a special test build.
		 */
#ifdef SQLITE_TEST
		if (sqlite3_interrupt_count>0) {
			sqlite3_interrupt_count--;
			if (sqlite3_interrupt_count==0) {
				sqlite3_interrupt(db);
			}
		}
#endif

		/* Sanity checking on other operands */
#ifdef SQLITE_DEBUG
		{
			u8 opProperty = sqlite3OpcodeProperty[pOp->opcode];
			if ((opProperty & OPFLG_IN1)!=0) {
				assert(pOp->p1>0);
				assert(pOp->p1<=(p->nMem+1 - p->nCursor));
				assert(memIsValid(&aMem[pOp->p1]));
				assert(sqlite3VdbeCheckMemInvariants(&aMem[pOp->p1]));
				REGISTER_TRACE(pOp->p1, &aMem[pOp->p1]);
			}
			if ((opProperty & OPFLG_IN2)!=0) {
				assert(pOp->p2>0);
				assert(pOp->p2<=(p->nMem+1 - p->nCursor));
				assert(memIsValid(&aMem[pOp->p2]));
				assert(sqlite3VdbeCheckMemInvariants(&aMem[pOp->p2]));
				REGISTER_TRACE(pOp->p2, &aMem[pOp->p2]);
			}
			if ((opProperty & OPFLG_IN3)!=0) {
				assert(pOp->p3>0);
				assert(pOp->p3<=(p->nMem+1 - p->nCursor));
				assert(memIsValid(&aMem[pOp->p3]));
				assert(sqlite3VdbeCheckMemInvariants(&aMem[pOp->p3]));
				REGISTER_TRACE(pOp->p3, &aMem[pOp->p3]);
			}
			if ((opProperty & OPFLG_OUT2)!=0) {
				assert(pOp->p2>0);
				assert(pOp->p2<=(p->nMem+1 - p->nCursor));
				memAboutToChange(p, &aMem[pOp->p2]);
			}
			if ((opProperty & OPFLG_OUT3)!=0) {
				assert(pOp->p3>0);
				assert(pOp->p3<=(p->nMem+1 - p->nCursor));
				memAboutToChange(p, &aMem[pOp->p3]);
			}
		}
#endif
#if defined(SQLITE_DEBUG) || defined(VDBE_PROFILE)
		pOrigOp = pOp;
#endif

		switch( pOp->opcode) {

/*****************************************************************************
 * What follows is a massive switch statement where each case implements a
 * separate instruction in the virtual machine.  If we follow the usual
 * indentation conventions, each case should be indented by 6 spaces.  But
 * that is a lot of wasted space on the left margin.  So the code within
 * the switch statement will break with convention and be flush-left. Another
 * big comment (similar to this one) will mark the point in the code where
 * we transition back to normal indentation.
 *
 * The formatting of each case is important.  The makefile for SQLite
 * generates two C files "opcodes.h" and "opcodes.c" by scanning this
 * file looking for lines that begin with "case OP_".  The opcodes.h files
 * will be filled with #defines that give unique integer values to each
 * opcode and the opcodes.c file is filled with an array of strings where
 * each string is the symbolic name for the corresponding opcode.  If the
 * case statement is followed by a comment of the form "/# same as ... #/"
 * that comment is used to determine the particular value of the opcode.
 *
 * Other keywords in the comment that follows each case are used to
 * construct the OPFLG_INITIALIZER value that initializes opcodeProperty[].
 * Keywords include: in1, in2, in3, out2, out3.  See
 * the mkopcodeh.awk script for additional information.
 *
 * Documentation about VDBE opcodes is generated by scanning this file
 * for lines of that contain "Opcode:".  That line and all subsequent
 * comment lines are used in the generation of the opcode.html documentation
 * file.
 *
 * SUMMARY:
 *
 *     Formatting is important to scripts that scan this file.
 *     Do not deviate from the formatting style currently in use.
 *
 ****************************************************************************/

/* Opcode:  Goto * P2 * * *
 *
 * An unconditional jump to address P2.
 * The next instruction executed will be
 * the one at index P2 from the beginning of
 * the program.
 *
 * The P1 parameter is not actually used by this opcode.  However, it
 * is sometimes set to 1 instead of 0 as a hint to the command-line shell
 * that this Goto is the bottom of a loop and that the lines from P2 down
 * to the current line should be indented for EXPLAIN output.
 */
case OP_Goto: {             /* jump */
			jump_to_p2_and_check_for_interrupt:
	pOp = &aOp[pOp->p2 - 1];

	/* Opcodes that are used as the bottom of a loop (OP_Next, OP_Prev,
	 * OP_RowSetNext, or OP_SorterNext) all jump here upon
	 * completion.  Check to see if sqlite3_interrupt() has been called
	 * or if the progress callback needs to be invoked.
	 *
	 * This code uses unstructured "goto" statements and does not look clean.
	 * But that is not due to sloppy coding habits. The code is written this
	 * way for performance, to avoid having to run the interrupt and progress
	 * checks on every opcode.  This helps sqlite3_step() to run about 1.5%
	 * faster according to "valgrind --tool=cachegrind"
	 */
			check_for_interrupt:
	if (db->u1.isInterrupted) goto abort_due_to_interrupt;
#ifndef SQLITE_OMIT_PROGRESS_CALLBACK
	/* Call the progress callback if it is configured and the required number
	 * of VDBE ops have been executed (either since this invocation of
	 * sqlite3VdbeExec() or since last time the progress callback was called).
	 * If the progress callback returns non-zero, exit the virtual machine with
	 * a return code SQLITE_ABORT.
	 */
	if (db->xProgress!=0 && nVmStep>=nProgressLimit) {
		assert(db->nProgressOps!=0);
		nProgressLimit = nVmStep + db->nProgressOps - (nVmStep%db->nProgressOps);
		if (db->xProgress(db->pProgressArg)) {
			rc = SQLITE_INTERRUPT;
			goto abort_due_to_error;
		}
	}
#endif

	break;
}

/* Opcode:  Gosub P1 P2 * * *
 *
 * Write the current address onto register P1
 * and then jump to address P2.
 */
case OP_Gosub: {            /* jump */
	assert(pOp->p1>0 && pOp->p1<=(p->nMem+1 - p->nCursor));
	pIn1 = &aMem[pOp->p1];
	assert(VdbeMemDynamic(pIn1)==0);
	memAboutToChange(p, pIn1);
	pIn1->flags = MEM_Int;
	pIn1->u.i = (int)(pOp-aOp);
	REGISTER_TRACE(pOp->p1, pIn1);

	/* Most jump operations do a goto to this spot in order to update
	 * the pOp pointer.
	 */
			jump_to_p2:
	pOp = &aOp[pOp->p2 - 1];
	break;
}

/* Opcode:  Return P1 * * * *
 *
 * Jump to the next instruction after the address in register P1.  After
 * the jump, register P1 becomes undefined.
 */
case OP_Return: {           /* in1 */
	pIn1 = &aMem[pOp->p1];
	assert(pIn1->flags==MEM_Int);
	pOp = &aOp[pIn1->u.i];
	pIn1->flags = MEM_Undefined;
	break;
}

/* Opcode: InitCoroutine P1 P2 P3 * *
 *
 * Set up register P1 so that it will Yield to the coroutine
 * located at address P3.
 *
 * If P2!=0 then the coroutine implementation immediately follows
 * this opcode.  So jump over the coroutine implementation to
 * address P2.
 *
 * See also: EndCoroutine
 */
case OP_InitCoroutine: {     /* jump */
	assert(pOp->p1>0 &&  pOp->p1<=(p->nMem+1 - p->nCursor));
	assert(pOp->p2>=0 && pOp->p2<p->nOp);
	assert(pOp->p3>=0 && pOp->p3<p->nOp);
	pOut = &aMem[pOp->p1];
	assert(!VdbeMemDynamic(pOut));
	pOut->u.i = pOp->p3 - 1;
	pOut->flags = MEM_Int;
	if (pOp->p2) goto jump_to_p2;
	break;
}

/* Opcode:  EndCoroutine P1 * * * *
 *
 * The instruction at the address in register P1 is a Yield.
 * Jump to the P2 parameter of that Yield.
 * After the jump, register P1 becomes undefined.
 *
 * See also: InitCoroutine
 */
case OP_EndCoroutine: {           /* in1 */
	VdbeOp *pCaller;
	pIn1 = &aMem[pOp->p1];
	assert(pIn1->flags==MEM_Int);
	assert(pIn1->u.i>=0 && pIn1->u.i<p->nOp);
	pCaller = &aOp[pIn1->u.i];
	assert(pCaller->opcode==OP_Yield);
	assert(pCaller->p2>=0 && pCaller->p2<p->nOp);
	pOp = &aOp[pCaller->p2 - 1];
	pIn1->flags = MEM_Undefined;
	break;
}

/* Opcode:  Yield P1 P2 * * *
 *
 * Swap the program counter with the value in register P1.  This
 * has the effect of yielding to a coroutine.
 *
 * If the coroutine that is launched by this instruction ends with
 * Yield or Return then continue to the next instruction.  But if
 * the coroutine launched by this instruction ends with
 * EndCoroutine, then jump to P2 rather than continuing with the
 * next instruction.
 *
 * See also: InitCoroutine
 */
case OP_Yield: {            /* in1, jump */
	int pcDest;
	pIn1 = &aMem[pOp->p1];
	assert(VdbeMemDynamic(pIn1)==0);
	pIn1->flags = MEM_Int;
	pcDest = (int)pIn1->u.i;
	pIn1->u.i = (int)(pOp - aOp);
	REGISTER_TRACE(pOp->p1, pIn1);
	pOp = &aOp[pcDest];
	break;
}

/* Opcode:  HaltIfNull  P1 P2 P3 P4 P5
 * Synopsis: if r[P3]=null halt
 *
 * Check the value in register P3.  If it is NULL then Halt using
 * parameter P1, P2, and P4 as if this were a Halt instruction.  If the
 * value in register P3 is not NULL, then this routine is a no-op.
 * The P5 parameter should be 1.
 */
case OP_HaltIfNull: {      /* in3 */
	pIn3 = &aMem[pOp->p3];
	if ((pIn3->flags & MEM_Null)==0) break;
	/* Fall through into OP_Halt */
	FALLTHROUGH;
}

/* Opcode:  Halt P1 P2 * P4 P5
 *
 * Exit immediately.  All open cursors, etc are closed
 * automatically.
 *
 * P1 is the result code returned by sqlite3_exec(), sqlite3_reset(),
 * or sqlite3_finalize().  For a normal halt, this should be SQLITE_OK (0).
 * For errors, it can be some other value.  If P1!=0 then P2 will determine
 * whether or not to rollback the current transaction.  Do not rollback
 * if P2==ON_CONFLICT_ACTION_FAIL. Do the rollback if
 * P2==ON_CONFLICT_ACTION_ROLLBACK.  If P2==ON_CONFLICT_ACTION_ABORT,
 * then back out all changes that have occurred during this execution of the
 * VDBE, but do not rollback the transaction.
 *
 * If P4 is not null then it is an error message string.
 *
 * P5 is a value between 0 and 4, inclusive, that modifies the P4 string.
 *
 *    0:  (no change)
 *    1:  NOT NULL contraint failed: P4
 *    2:  UNIQUE constraint failed: P4
 *    3:  CHECK constraint failed: P4
 *    4:  FOREIGN KEY constraint failed: P4
 *
 * If P5 is not zero and P4 is NULL, then everything after the ":" is
 * omitted.
 *
 * There is an implied "Halt 0 0 0" instruction inserted at the very end of
 * every program.  So a jump past the last instruction of the program
 * is the same as executing Halt.
 */
case OP_Halt: {
	VdbeFrame *pFrame;
	int pcx;

	pcx = (int)(pOp - aOp);
	if (pOp->p1==SQLITE_OK && p->pFrame) {
		/* Halt the sub-program. Return control to the parent frame. */
		pFrame = p->pFrame;
		p->pFrame = pFrame->pParent;
		p->nFrame--;
		sqlite3VdbeSetChanges(db, p->nChange);
		pcx = sqlite3VdbeFrameRestore(pFrame);
		if (pOp->p2 == ON_CONFLICT_ACTION_IGNORE) {
			/* Instruction pcx is the OP_Program that invoked the sub-program
			 * currently being halted. If the p2 instruction of this OP_Halt
			 * instruction is set to ON_CONFLICT_ACTION_IGNORE, then
			 * the sub-program is throwing
			 * an IGNORE exception. In this case jump to the address specified
			 * as the p2 of the calling OP_Program.
			 */
			pcx = p->aOp[pcx].p2-1;
		}
		aOp = p->aOp;
		aMem = p->aMem;
		pOp = &aOp[pcx];
		break;
	}
	p->rc = pOp->p1;
	p->errorAction = (u8)pOp->p2;
	p->pc = pcx;
	assert(pOp->p5<=4);
	if (p->rc) {
		if (pOp->p5) {
			static const char * const azType[] = { "NOT NULL", "UNIQUE", "CHECK",
							       "FOREIGN KEY" };
			testcase( pOp->p5==1);
			testcase( pOp->p5==2);
			testcase( pOp->p5==3);
			testcase( pOp->p5==4);
			sqlite3VdbeError(p, "%s constraint failed", azType[pOp->p5-1]);
			if (pOp->p4.z) {
				p->zErrMsg = sqlite3MPrintf(db, "%z: %s", p->zErrMsg, pOp->p4.z);
			}
		} else {
			sqlite3VdbeError(p, "%s", pOp->p4.z);
		}
		sqlite3_log(pOp->p1, "abort at %d in [%s]: %s", pcx, p->zSql, p->zErrMsg);
	}
	rc = sqlite3VdbeHalt(p);
	assert(rc==SQLITE_BUSY || rc==SQLITE_OK || rc==SQLITE_ERROR);
	if (rc==SQLITE_BUSY) {
		p->rc = SQLITE_BUSY;
	} else {
		assert(rc==SQLITE_OK || (p->rc&0xff)==SQLITE_CONSTRAINT);
		rc = p->rc ? SQLITE_ERROR : SQLITE_DONE;
	}
	goto vdbe_return;
}

/* Opcode: Integer P1 P2 * * *
 * Synopsis: r[P2]=P1
 *
 * The 32-bit integer value P1 is written into register P2.
 */
case OP_Integer: {         /* out2 */
	pOut = out2Prerelease(p, pOp);
	pOut->u.i = pOp->p1;
	break;
}

/* Opcode: Bool P1 P2 * * *
 * Synopsis: r[P2]=P1
 *
 * The boolean value P1 is written into register P2.
 */
case OP_Bool: {         /* out2 */
	pOut = out2Prerelease(p, pOp);
	assert(pOp->p4type == P4_BOOL);
	pOut->flags = MEM_Bool;
	pOut->u.b = pOp->p4.p;
	break;
}

/* Opcode: Int64 * P2 * P4 *
 * Synopsis: r[P2]=P4
 *
 * P4 is a pointer to a 64-bit integer value.
 * Write that value into register P2.
 */
case OP_Int64: {           /* out2 */
	pOut = out2Prerelease(p, pOp);
	assert(pOp->p4.pI64!=0);
	pOut->u.i = *pOp->p4.pI64;
	break;
}

/* Opcode: LoadPtr * P2 * P4 *
 * Synopsis: r[P2] = P4
 *
 * P4 is a generic or space pointer. Copy it into register P2.
 */
case OP_LoadPtr: {
	pOut = out2Prerelease(p, pOp);
	assert(pOp->p4type == P4_PTR || pOp->p4type == P4_SPACEPTR );
	pOut->u.p = pOp->p4.space;
	pOut->flags = MEM_Ptr;
	break;
}

#ifndef SQLITE_OMIT_FLOATING_POINT
/* Opcode: Real * P2 * P4 *
 * Synopsis: r[P2]=P4
 *
 * P4 is a pointer to a 64-bit floating point value.
 * Write that value into register P2.
 */
case OP_Real: {            /* same as TK_FLOAT, out2 */
	pOut = out2Prerelease(p, pOp);
	pOut->flags = MEM_Real;
	assert(!sqlite3IsNaN(*pOp->p4.pReal));
	pOut->u.r = *pOp->p4.pReal;
	break;
}
#endif

/* Opcode: String8 * P2 * P4 *
 * Synopsis: r[P2]='P4'
 *
 * P4 points to a nul terminated UTF-8 string. This opcode is transformed
 * into a String opcode before it is executed for the first time.  During
 * this transformation, the length of string P4 is computed and stored
 * as the P1 parameter.
 */
case OP_String8: {         /* same as TK_STRING, out2 */
	assert(pOp->p4.z!=0);
	pOut = out2Prerelease(p, pOp);
	pOp->opcode = OP_String;
	pOp->p1 = sqlite3Strlen30(pOp->p4.z);

	if (pOp->p1>db->aLimit[SQLITE_LIMIT_LENGTH]) {
		goto too_big;
	}
	assert(rc==SQLITE_OK);
	/* Fall through to the next case, OP_String */
	FALLTHROUGH;
}

/* Opcode: String P1 P2 P3 P4 P5
 * Synopsis: r[P2]='P4' (len=P1)
 *
 * The string value P4 of length P1 (bytes) is stored in register P2.
 *
 * If P3 is not zero and the content of register P3 is equal to P5, then
 * the datatype of the register P2 is converted to BLOB.  The content is
 * the same sequence of bytes, it is merely interpreted as a BLOB instead
 * of a string, as if it had been CAST.  In other words:
 *
 * if (P3!=0 and reg[P3]==P5) reg[P2] := CAST(reg[P2] as BLOB)
 */
case OP_String: {          /* out2 */
	assert(pOp->p4.z!=0);
	pOut = out2Prerelease(p, pOp);
	pOut->flags = MEM_Str|MEM_Static|MEM_Term;
	pOut->z = pOp->p4.z;
	pOut->n = pOp->p1;
	UPDATE_MAX_BLOBSIZE(pOut);
#ifndef SQLITE_LIKE_DOESNT_MATCH_BLOBS
	if (pOp->p3>0) {
		assert(pOp->p3<=(p->nMem+1 - p->nCursor));
		pIn3 = &aMem[pOp->p3];
		assert(pIn3->flags & MEM_Int);
		if (pIn3->u.i==pOp->p5) pOut->flags = MEM_Blob|MEM_Static|MEM_Term;
	}
#endif
	break;
}

/* Opcode: NextAutoincValue P1 P2 * * *
 * Synopsis: r[P2] = next value from space sequence, which pageno is r[P1]
 *
 * Get next value from space sequence, which pageno is written into register
 * P1, write this value into register P2. If space doesn't exists (invalid
 * space_id or something else), raise an error. If space with
 * specified space_id doesn't have attached sequence, also raise an error.
 */
case OP_NextAutoincValue: {
	assert(pOp->p1 > 0);
	assert(pOp->p2 > 0);

	int64_t value;
	uint32_t space_id = SQLITE_PAGENO_TO_SPACEID(pOp->p1);

	struct space *space = space_by_id(space_id);
	if (space == NULL) {
		rc = SQL_TARANTOOL_ERROR;
		goto abort_due_to_error;
	}

	struct sequence *sequence = space->sequence;
	if (sequence == NULL || sequence_next(sequence, &value) != 0) {
		rc = SQL_TARANTOOL_ERROR;
		goto abort_due_to_error;
	}

	pOut = out2Prerelease(p, pOp);
	pOut->flags = MEM_Int;
	pOut->u.i = value;

	break;
}

/* Opcode: Null P1 P2 P3 * *
 * Synopsis: r[P2..P3]=NULL
 *
 * Write a NULL into registers P2.  If P3 greater than P2, then also write
 * NULL into register P3 and every register in between P2 and P3.  If P3
 * is less than P2 (typically P3 is zero) then only register P2 is
 * set to NULL.
 *
 * If the P1 value is non-zero, then also set the MEM_Cleared flag so that
 * NULL values will not compare equal even if SQLITE_NULLEQ is set on
 * OP_Ne or OP_Eq.
 */
case OP_Null: {           /* out2 */
	int cnt;
	u16 nullFlag;
	pOut = out2Prerelease(p, pOp);
	cnt = pOp->p3-pOp->p2;
	assert(pOp->p3<=(p->nMem+1 - p->nCursor));
	pOut->flags = nullFlag = pOp->p1 ? (MEM_Null|MEM_Cleared) : MEM_Null;
	pOut->n = 0;
	while( cnt>0) {
		pOut++;
		memAboutToChange(p, pOut);
		sqlite3VdbeMemSetNull(pOut);
		pOut->flags = nullFlag;
		pOut->n = 0;
		cnt--;
	}
	break;
}

/* Opcode: SoftNull P1 * * * *
 * Synopsis: r[P1]=NULL
 *
 * Set register P1 to have the value NULL as seen by the OP_MakeRecord
 * instruction, but do not free any string or blob memory associated with
 * the register, so that if the value was a string or blob that was
 * previously copied using OP_SCopy, the copies will continue to be valid.
 */
case OP_SoftNull: {
	assert(pOp->p1>0 && pOp->p1<=(p->nMem+1 - p->nCursor));
	pOut = &aMem[pOp->p1];
	pOut->flags = (pOut->flags|MEM_Null)&~MEM_Undefined;
	break;
}

/* Opcode: Blob P1 P2 P3 P4 *
 * Synopsis: r[P2]=P4 (len=P1, subtype=P3)
 *
 * P4 points to a blob of data P1 bytes long.  Store this
 * blob in register P2.  Set subtype to P3.
 */
case OP_Blob: {                /* out2 */
	assert(pOp->p1 <= SQLITE_MAX_LENGTH);
	pOut = out2Prerelease(p, pOp);
	sqlite3VdbeMemSetStr(pOut, pOp->p4.z, pOp->p1, 0, 0);
	if (pOp->p3!=0) {
		pOut->flags |= MEM_Subtype;
		pOut->eSubtype = pOp->p3;
	}
	UPDATE_MAX_BLOBSIZE(pOut);
	break;
}

/* Opcode: Variable P1 P2 * P4 *
 * Synopsis: r[P2]=parameter(P1,P4)
 *
 * Transfer the values of bound parameter P1 into register P2
 *
 * If the parameter is named, then its name appears in P4.
 * The P4 value is used by sqlite3_bind_parameter_name().
 */
case OP_Variable: {            /* out2 */
	Mem *pVar;       /* Value being transferred */

	assert(pOp->p1>0 && pOp->p1<=p->nVar);
	assert(pOp->p4.z==0 || pOp->p4.z==sqlite3VListNumToName(p->pVList,pOp->p1));
	pVar = &p->aVar[pOp->p1 - 1];
	if (sqlite3VdbeMemTooBig(pVar)) {
		goto too_big;
	}
	pOut = out2Prerelease(p, pOp);
	sqlite3VdbeMemShallowCopy(pOut, pVar, MEM_Static);
	UPDATE_MAX_BLOBSIZE(pOut);
	break;
}

/* Opcode: Move P1 P2 P3 * *
 * Synopsis: r[P2@P3]=r[P1@P3]
 *
 * Move the P3 values in register P1..P1+P3-1 over into
 * registers P2..P2+P3-1.  Registers P1..P1+P3-1 are
 * left holding a NULL.  It is an error for register ranges
 * P1..P1+P3-1 and P2..P2+P3-1 to overlap.  It is an error
 * for P3 to be less than 1.
 */
case OP_Move: {
	int n;           /* Number of registers left to copy */
	int p1;          /* Register to copy from */
	int p2;          /* Register to copy to */

	n = pOp->p3;
	p1 = pOp->p1;
	p2 = pOp->p2;
	assert(n>0 && p1>0 && p2>0);
	assert(p1+n<=p2 || p2+n<=p1);

	pIn1 = &aMem[p1];
	pOut = &aMem[p2];
	do{
		assert(pOut<=&aMem[(p->nMem+1 - p->nCursor)]);
		assert(pIn1<=&aMem[(p->nMem+1 - p->nCursor)]);
		assert(memIsValid(pIn1));
		memAboutToChange(p, pOut);
		sqlite3VdbeMemMove(pOut, pIn1);
#ifdef SQLITE_DEBUG
		if (pOut->pScopyFrom>=&aMem[p1] && pOut->pScopyFrom<pOut) {
			pOut->pScopyFrom += pOp->p2 - p1;
		}
#endif
		Deephemeralize(pOut);
		REGISTER_TRACE(p2++, pOut);
		pIn1++;
		pOut++;
	}while( --n);
	break;
}

/* Opcode: Copy P1 P2 P3 * *
 * Synopsis: r[P2@P3+1]=r[P1@P3+1]
 *
 * Make a copy of registers P1..P1+P3 into registers P2..P2+P3.
 *
 * This instruction makes a deep copy of the value.  A duplicate
 * is made of any string or blob constant.  See also OP_SCopy.
 */
case OP_Copy: {
	int n;

	n = pOp->p3;
	pIn1 = &aMem[pOp->p1];
	pOut = &aMem[pOp->p2];
	assert(pOut!=pIn1);
	while( 1) {
		sqlite3VdbeMemShallowCopy(pOut, pIn1, MEM_Ephem);
		Deephemeralize(pOut);
#ifdef SQLITE_DEBUG
		pOut->pScopyFrom = 0;
#endif
		REGISTER_TRACE(pOp->p2+pOp->p3-n, pOut);
		if ((n--)==0) break;
		pOut++;
		pIn1++;
	}
	break;
}

/* Opcode: SCopy P1 P2 * * *
 * Synopsis: r[P2]=r[P1]
 *
 * Make a shallow copy of register P1 into register P2.
 *
 * This instruction makes a shallow copy of the value.  If the value
 * is a string or blob, then the copy is only a pointer to the
 * original and hence if the original changes so will the copy.
 * Worse, if the original is deallocated, the copy becomes invalid.
 * Thus the program must guarantee that the original will not change
 * during the lifetime of the copy.  Use OP_Copy to make a complete
 * copy.
 */
case OP_SCopy: {            /* out2 */
	pIn1 = &aMem[pOp->p1];
	pOut = &aMem[pOp->p2];
	assert(pOut!=pIn1);
	sqlite3VdbeMemShallowCopy(pOut, pIn1, MEM_Ephem);
#ifdef SQLITE_DEBUG
	if (pOut->pScopyFrom==0) pOut->pScopyFrom = pIn1;
#endif
	break;
}

/* Opcode: IntCopy P1 P2 * * *
 * Synopsis: r[P2]=r[P1]
 *
 * Transfer the integer value held in register P1 into register P2.
 *
 * This is an optimized version of SCopy that works only for integer
 * values.
 */
case OP_IntCopy: {            /* out2 */
	pIn1 = &aMem[pOp->p1];
	assert((pIn1->flags & MEM_Int)!=0);
	pOut = &aMem[pOp->p2];
	sqlite3VdbeMemSetInt64(pOut, pIn1->u.i);
	break;
}

/* Opcode: ResultRow P1 P2 * * *
 * Synopsis: output=r[P1@P2]
 *
 * The registers P1 through P1+P2-1 contain a single row of
 * results. This opcode causes the sqlite3_step() call to terminate
 * with an SQLITE_ROW return code and it sets up the sqlite3_stmt
 * structure to provide access to the r(P1)..r(P1+P2-1) values as
 * the result row.
 */
case OP_ResultRow: {
	Mem *pMem;
	int i;

	assert(p->nResColumn==pOp->p2);
	assert(pOp->p1>0);
	assert(pOp->p1+pOp->p2<=(p->nMem+1 - p->nCursor)+1);

#ifndef SQLITE_OMIT_PROGRESS_CALLBACK
	/* Run the progress counter just before returning.
	 */
	if (db->xProgress!=0
	    && nVmStep>=nProgressLimit
	    && db->xProgress(db->pProgressArg)!=0
		) {
		rc = SQLITE_INTERRUPT;
		goto abort_due_to_error;
	}
#endif

	/* If this statement has violated immediate foreign key constraints, do
	 * not return the number of rows modified. And do not RELEASE the statement
	 * transaction. It needs to be rolled back.
	 */
	if (SQLITE_OK!=(rc = sqlite3VdbeCheckFk(p, 0))) {
		assert(user_session->sql_flags&SQLITE_CountRows);
		goto abort_due_to_error;
	}

	/* If the SQLITE_CountRows flag is set in sqlite3.flags mask, then
	 * DML statements invoke this opcode to return the number of rows
	 * modified to the user. This is the only way that a VM that
	 * opens a statement transaction may invoke this opcode.
	 *
	 * In case this is such a statement, close any statement transaction
	 * opened by this VM before returning control to the user. This is to
	 * ensure that statement-transactions are always nested, not overlapping.
	 * If the open statement-transaction is not closed here, then the user
	 * may step another VM that opens its own statement transaction. This
	 * may lead to overlapping statement transactions.
	 *
	 * The statement transaction is never a top-level transaction.  Hence
	 * the RELEASE call below can never fail.
	 */
	assert(p->iStatement==0 || user_session->sql_flags&SQLITE_CountRows);
	rc = sqlite3VdbeCloseStatement(p, SAVEPOINT_RELEASE);
	assert(rc==SQLITE_OK);

	/* Invalidate all ephemeral cursor row caches */
	p->cacheCtr = (p->cacheCtr + 2)|1;

	/* Make sure the results of the current row are \000 terminated
	 * and have an assigned type.  The results are de-ephemeralized as
	 * a side effect.
	 */
	pMem = p->pResultSet = &aMem[pOp->p1];
	for(i=0; i<pOp->p2; i++) {
		assert(memIsValid(&pMem[i]));
		Deephemeralize(&pMem[i]);
		assert((pMem[i].flags & MEM_Ephem)==0
		       || (pMem[i].flags & (MEM_Str|MEM_Blob))==0);
		sqlite3VdbeMemNulTerminate(&pMem[i]);
		REGISTER_TRACE(pOp->p1+i, &pMem[i]);
	}
	if (db->mallocFailed) goto no_mem;

	if (db->mTrace & SQLITE_TRACE_ROW) {
		db->xTrace(SQLITE_TRACE_ROW, db->pTraceArg, p, 0);
	}

	/* Return SQLITE_ROW
	 */
	p->pc = (int)(pOp - aOp) + 1;
	rc = SQLITE_ROW;
	goto vdbe_return;
}

/* Opcode: Concat P1 P2 P3 * *
 * Synopsis: r[P3]=r[P2]+r[P1]
 *
 * Add the text in register P1 onto the end of the text in
 * register P2 and store the result in register P3.
 * If either the P1 or P2 text are NULL then store NULL in P3.
 *
 *   P3 = P2 || P1
 *
 * It is illegal for P1 and P3 to be the same register. Sometimes,
 * if P3 is the same register as P2, the implementation is able
 * to avoid a memcpy().
 */
case OP_Concat: {           /* same as TK_CONCAT, in1, in2, out3 */
	i64 nByte;

	pIn1 = &aMem[pOp->p1];
	pIn2 = &aMem[pOp->p2];
	pOut = &aMem[pOp->p3];
	assert(pIn1!=pOut);
	if ((pIn1->flags | pIn2->flags) & MEM_Null) {
		sqlite3VdbeMemSetNull(pOut);
		break;
	}
	if (ExpandBlob(pIn1) || ExpandBlob(pIn2)) goto no_mem;
	Stringify(pIn1);
	Stringify(pIn2);
	nByte = pIn1->n + pIn2->n;
	if (nByte>db->aLimit[SQLITE_LIMIT_LENGTH]) {
		goto too_big;
	}
	if (sqlite3VdbeMemGrow(pOut, (int)nByte+2, pOut==pIn2)) {
		goto no_mem;
	}
	MemSetTypeFlag(pOut, MEM_Str);
	if (pOut!=pIn2) {
		memcpy(pOut->z, pIn2->z, pIn2->n);
	}
	memcpy(&pOut->z[pIn2->n], pIn1->z, pIn1->n);
	pOut->z[nByte]=0;
	pOut->z[nByte+1] = 0;
	pOut->flags |= MEM_Term;
	pOut->n = (int)nByte;
	UPDATE_MAX_BLOBSIZE(pOut);
	break;
}

/* Opcode: Add P1 P2 P3 * *
 * Synopsis: r[P3]=r[P1]+r[P2]
 *
 * Add the value in register P1 to the value in register P2
 * and store the result in register P3.
 * If either input is NULL, the result is NULL.
 */
/* Opcode: Multiply P1 P2 P3 * *
 * Synopsis: r[P3]=r[P1]*r[P2]
 *
 *
 * Multiply the value in register P1 by the value in register P2
 * and store the result in register P3.
 * If either input is NULL, the result is NULL.
 */
/* Opcode: Subtract P1 P2 P3 * *
 * Synopsis: r[P3]=r[P2]-r[P1]
 *
 * Subtract the value in register P1 from the value in register P2
 * and store the result in register P3.
 * If either input is NULL, the result is NULL.
 */
/* Opcode: Divide P1 P2 P3 * *
 * Synopsis: r[P3]=r[P2]/r[P1]
 *
 * Divide the value in register P1 by the value in register P2
 * and store the result in register P3 (P3=P2/P1). If the value in
 * register P1 is zero, then the result is NULL. If either input is
 * NULL, the result is NULL.
 */
/* Opcode: Remainder P1 P2 P3 * *
 * Synopsis: r[P3]=r[P2]%r[P1]
 *
 * Compute the remainder after integer register P2 is divided by
 * register P1 and store the result in register P3.
 * If the value in register P1 is zero the result is NULL.
 * If either operand is NULL, the result is NULL.
 */
case OP_Add:                   /* same as TK_PLUS, in1, in2, out3 */
case OP_Subtract:              /* same as TK_MINUS, in1, in2, out3 */
case OP_Multiply:              /* same as TK_STAR, in1, in2, out3 */
case OP_Divide:                /* same as TK_SLASH, in1, in2, out3 */
case OP_Remainder: {           /* same as TK_REM, in1, in2, out3 */
	char bIntint;   /* Started out as two integer operands */
	u32 flags;      /* Combined MEM_* flags from both inputs */
	u16 type1;      /* Numeric type of left operand */
	u16 type2;      /* Numeric type of right operand */
	i64 iA;         /* Integer value of left operand */
	i64 iB;         /* Integer value of right operand */
	double rA;      /* Real value of left operand */
	double rB;      /* Real value of right operand */

	pIn1 = &aMem[pOp->p1];
	type1 = numericType(pIn1);
	pIn2 = &aMem[pOp->p2];
	type2 = numericType(pIn2);
	pOut = &aMem[pOp->p3];
	flags = pIn1->flags | pIn2->flags;
	if ((flags & MEM_Null)!=0) goto arithmetic_result_is_null;
	if ((type1 & type2 & MEM_Int)!=0) {
		iA = pIn1->u.i;
		iB = pIn2->u.i;
		bIntint = 1;
		switch( pOp->opcode) {
		case OP_Add:       if (sqlite3AddInt64(&iB,iA)) goto fp_math;  break;
		case OP_Subtract:  if (sqlite3SubInt64(&iB,iA)) goto fp_math;  break;
		case OP_Multiply:  if (sqlite3MulInt64(&iB,iA)) goto fp_math;  break;
		case OP_Divide: {
			if (iA==0) goto arithmetic_result_is_null;
			if (iA==-1 && iB==SMALLEST_INT64) goto fp_math;
			iB /= iA;
			break;
		}
		default: {
			if (iA==0) goto arithmetic_result_is_null;
			if (iA==-1) iA = 1;
			iB %= iA;
			break;
		}
		}
		pOut->u.i = iB;
		MemSetTypeFlag(pOut, MEM_Int);
	} else {
		bIntint = 0;
	fp_math:
		rA = sqlite3VdbeRealValue(pIn1);
		rB = sqlite3VdbeRealValue(pIn2);
		switch( pOp->opcode) {
		case OP_Add:         rB += rA;       break;
		case OP_Subtract:    rB -= rA;       break;
		case OP_Multiply:    rB *= rA;       break;
		case OP_Divide: {
			/* (double)0 In case of SQLITE_OMIT_FLOATING_POINT... */
			if (rA==(double)0) goto arithmetic_result_is_null;
			rB /= rA;
			break;
		}
		default: {
			iA = (i64)rA;
			iB = (i64)rB;
			if (iA==0) goto arithmetic_result_is_null;
			if (iA==-1) iA = 1;
			rB = (double)(iB % iA);
			break;
		}
		}
#ifdef SQLITE_OMIT_FLOATING_POINT
		pOut->u.i = rB;
		MemSetTypeFlag(pOut, MEM_Int);
#else
		if (sqlite3IsNaN(rB)) {
			goto arithmetic_result_is_null;
		}
		pOut->u.r = rB;
		MemSetTypeFlag(pOut, MEM_Real);
		if (((type1|type2)&MEM_Real)==0 && !bIntint) {
			sqlite3VdbeIntegerAffinity(pOut);
		}
#endif
	}
	break;

			arithmetic_result_is_null:
	sqlite3VdbeMemSetNull(pOut);
	break;
}

/* Opcode: CollSeq P1 * * P4
 *
 * P4 is a pointer to a CollSeq struct. If the next call to a user function
 * or aggregate calls sqlite3GetFuncCollSeq(), this collation sequence will
 * be returned. This is used by the built-in min(), max() and nullif()
 * functions.
 *
 * If P1 is not zero, then it is a register that a subsequent min() or
 * max() aggregate will set to 1 if the current row is not the minimum or
 * maximum.  The P1 register is initialized to 0 by this instruction.
 *
 * The interface used by the implementation of the aforementioned functions
 * to retrieve the collation sequence set by this opcode is not available
 * publicly.  Only built-in functions have access to this feature.
 */
case OP_CollSeq: {
	assert(pOp->p4type==P4_COLLSEQ || pOp->p4.pColl == NULL);
	if (pOp->p1) {
		sqlite3VdbeMemSetInt64(&aMem[pOp->p1], 0);
	}
	break;
}

/* Opcode: Function0 P1 P2 P3 P4 P5
 * Synopsis: r[P3]=func(r[P2@P5])
 *
 * Invoke a user function (P4 is a pointer to a FuncDef object that
 * defines the function) with P5 arguments taken from register P2 and
 * successors.  The result of the function is stored in register P3.
 * Register P3 must not be one of the function inputs.
 *
 * P1 is a 32-bit bitmask indicating whether or not each argument to the
 * function was determined to be constant at compile time. If the first
 * argument was constant then bit 0 of P1 is set. This is used to determine
 * whether meta data associated with a user function argument using the
 * sqlite3_set_auxdata() API may be safely retained until the next
 * invocation of this opcode.
 *
 * See also: Function, AggStep, AggFinal
 */
/* Opcode: Function P1 P2 P3 P4 P5
 * Synopsis: r[P3]=func(r[P2@P5])
 *
 * Invoke a user function (P4 is a pointer to an sqlite3_context object that
 * contains a pointer to the function to be run) with P5 arguments taken
 * from register P2 and successors.  The result of the function is stored
 * in register P3.  Register P3 must not be one of the function inputs.
 *
 * P1 is a 32-bit bitmask indicating whether or not each argument to the
 * function was determined to be constant at compile time. If the first
 * argument was constant then bit 0 of P1 is set. This is used to determine
 * whether meta data associated with a user function argument using the
 * sqlite3_set_auxdata() API may be safely retained until the next
 * invocation of this opcode.
 *
 * SQL functions are initially coded as OP_Function0 with P4 pointing
 * to a FuncDef object.  But on first evaluation, the P4 operand is
 * automatically converted into an sqlite3_context object and the operation
 * changed to this OP_Function opcode.  In this way, the initialization of
 * the sqlite3_context object occurs only once, rather than once for each
 * evaluation of the function.
 *
 * See also: Function0, AggStep, AggFinal
 */
case OP_Function0: {
	int n;
	sqlite3_context *pCtx;

	assert(pOp->p4type==P4_FUNCDEF);
	n = pOp->p5;
	assert(pOp->p3>0 && pOp->p3<=(p->nMem+1 - p->nCursor));
	assert(n==0 || (pOp->p2>0 && pOp->p2+n<=(p->nMem+1 - p->nCursor)+1));
	assert(pOp->p3<pOp->p2 || pOp->p3>=pOp->p2+n);
	pCtx = sqlite3DbMallocRawNN(db, sizeof(*pCtx) + (n-1)*sizeof(sqlite3_value*));
	if (pCtx==0) goto no_mem;
	pCtx->pOut = 0;
	pCtx->pFunc = pOp->p4.pFunc;
	pCtx->iOp = (int)(pOp - aOp);
	pCtx->pVdbe = p;
	pCtx->argc = n;
	pOp->p4type = P4_FUNCCTX;
	pOp->p4.pCtx = pCtx;
	pOp->opcode = OP_Function;
	/* Fall through into OP_Function */
	FALLTHROUGH;
}
case OP_Function: {
	int i;
	sqlite3_context *pCtx;

	assert(pOp->p4type==P4_FUNCCTX);
	pCtx = pOp->p4.pCtx;

	/* If this function is inside of a trigger, the register array in aMem[]
	 * might change from one evaluation to the next.  The next block of code
	 * checks to see if the register array has changed, and if so it
	 * reinitializes the relavant parts of the sqlite3_context object
	 */
	pOut = &aMem[pOp->p3];
	if (pCtx->pOut != pOut) {
		pCtx->pOut = pOut;
		for(i=pCtx->argc-1; i>=0; i--) pCtx->argv[i] = &aMem[pOp->p2+i];
	}

	memAboutToChange(p, pCtx->pOut);
#ifdef SQLITE_DEBUG
	for(i=0; i<pCtx->argc; i++) {
		assert(memIsValid(pCtx->argv[i]));
		REGISTER_TRACE(pOp->p2+i, pCtx->argv[i]);
	}
#endif
	MemSetTypeFlag(pCtx->pOut, MEM_Null);
	pCtx->fErrorOrAux = 0;
	(*pCtx->pFunc->xSFunc)(pCtx, pCtx->argc, pCtx->argv);/* IMP: R-24505-23230 */

	/* If the function returned an error, throw an exception */
	if (pCtx->fErrorOrAux) {
		if (pCtx->isError) {
			sqlite3VdbeError(p, "%s", sqlite3_value_text(pCtx->pOut));
			rc = pCtx->isError;
		}
		sqlite3VdbeDeleteAuxData(db, &p->pAuxData, pCtx->iOp, pOp->p1);
		if (rc) goto abort_due_to_error;
	}

	/* Copy the result of the function into register P3 */
	if (pOut->flags & (MEM_Str|MEM_Blob)) {
		if (sqlite3VdbeMemTooBig(pCtx->pOut)) goto too_big;
	}

	REGISTER_TRACE(pOp->p3, pCtx->pOut);
	UPDATE_MAX_BLOBSIZE(pCtx->pOut);
	break;
}

/* Opcode: BitAnd P1 P2 P3 * *
 * Synopsis: r[P3]=r[P1]&r[P2]
 *
 * Take the bit-wise AND of the values in register P1 and P2 and
 * store the result in register P3.
 * If either input is NULL, the result is NULL.
 */
/* Opcode: BitOr P1 P2 P3 * *
 * Synopsis: r[P3]=r[P1]|r[P2]
 *
 * Take the bit-wise OR of the values in register P1 and P2 and
 * store the result in register P3.
 * If either input is NULL, the result is NULL.
 */
/* Opcode: ShiftLeft P1 P2 P3 * *
 * Synopsis: r[P3]=r[P2]<<r[P1]
 *
 * Shift the integer value in register P2 to the left by the
 * number of bits specified by the integer in register P1.
 * Store the result in register P3.
 * If either input is NULL, the result is NULL.
 */
/* Opcode: ShiftRight P1 P2 P3 * *
 * Synopsis: r[P3]=r[P2]>>r[P1]
 *
 * Shift the integer value in register P2 to the right by the
 * number of bits specified by the integer in register P1.
 * Store the result in register P3.
 * If either input is NULL, the result is NULL.
 */
case OP_BitAnd:                 /* same as TK_BITAND, in1, in2, out3 */
case OP_BitOr:                  /* same as TK_BITOR, in1, in2, out3 */
case OP_ShiftLeft:              /* same as TK_LSHIFT, in1, in2, out3 */
case OP_ShiftRight: {           /* same as TK_RSHIFT, in1, in2, out3 */
	i64 iA;
	u64 uA;
	i64 iB;
	u8 op;

	pIn1 = &aMem[pOp->p1];
	pIn2 = &aMem[pOp->p2];
	pOut = &aMem[pOp->p3];
	if ((pIn1->flags | pIn2->flags) & MEM_Null) {
		sqlite3VdbeMemSetNull(pOut);
		break;
	}
	iA = sqlite3VdbeIntValue(pIn2);
	iB = sqlite3VdbeIntValue(pIn1);
	op = pOp->opcode;
	if (op==OP_BitAnd) {
		iA &= iB;
	} else if (op==OP_BitOr) {
		iA |= iB;
	} else if (iB!=0) {
		assert(op==OP_ShiftRight || op==OP_ShiftLeft);

		/* If shifting by a negative amount, shift in the other direction */
		if (iB<0) {
			assert(OP_ShiftRight==OP_ShiftLeft+1);
			op = 2*OP_ShiftLeft + 1 - op;
			iB = iB>(-64) ? -iB : 64;
		}

		if (iB>=64) {
			iA = (iA>=0 || op==OP_ShiftLeft) ? 0 : -1;
		} else {
			memcpy(&uA, &iA, sizeof(uA));
			if (op==OP_ShiftLeft) {
				uA <<= iB;
			} else {
				uA >>= iB;
				/* Sign-extend on a right shift of a negative number */
				if (iA<0) uA |= ((((u64)0xffffffff)<<32)|0xffffffff) << (64-iB);
			}
			memcpy(&iA, &uA, sizeof(iA));
		}
	}
	pOut->u.i = iA;
	MemSetTypeFlag(pOut, MEM_Int);
	break;
}

/* Opcode: AddImm  P1 P2 * * *
 * Synopsis: r[P1]=r[P1]+P2
 *
 * Add the constant P2 to the value in register P1.
 * The result is always an integer.
 *
 * To force any register to be an integer, just add 0.
 */
case OP_AddImm: {            /* in1 */
	pIn1 = &aMem[pOp->p1];
	memAboutToChange(p, pIn1);
	sqlite3VdbeMemIntegerify(pIn1);
	pIn1->u.i += pOp->p2;
	break;
}

/* Opcode: MustBeInt P1 P2 * * *
 *
 * Force the value in register P1 to be an integer.  If the value
 * in P1 is not an integer and cannot be converted into an integer
 * without data loss, then jump immediately to P2, or if P2==0
 * raise an SQLITE_MISMATCH exception.
 */
case OP_MustBeInt: {            /* jump, in1 */
	pIn1 = &aMem[pOp->p1];
	if ((pIn1->flags & MEM_Int)==0) {
		applyAffinity(pIn1, AFFINITY_NUMERIC);
		VdbeBranchTaken((pIn1->flags&MEM_Int)==0, 2);
		if ((pIn1->flags & MEM_Int)==0) {
			if (pOp->p2==0) {
				rc = SQLITE_MISMATCH;
				goto abort_due_to_error;
			} else {
				goto jump_to_p2;
			}
		}
	}
	MemSetTypeFlag(pIn1, MEM_Int);
	break;
}

#ifndef SQLITE_OMIT_FLOATING_POINT
/* Opcode: RealAffinity P1 * * * *
 *
 * If register P1 holds an integer convert it to a real value.
 *
 * This opcode is used when extracting information from a column that
 * has REAL affinity.  Such column values may still be stored as
 * integers, for space efficiency, but after extraction we want them
 * to have only a real value.
 */
case OP_RealAffinity: {                  /* in1 */
	pIn1 = &aMem[pOp->p1];
	if (pIn1->flags & MEM_Int) {
		sqlite3VdbeMemRealify(pIn1);
	}
	break;
}
#endif

#ifndef SQLITE_OMIT_CAST
/* Opcode: Cast P1 P2 * * *
 * Synopsis: affinity(r[P1])
 *
 * Force the value in register P1 to be the type defined by P2.
 *
 * <ul>
 * <li value="97"> TEXT
 * <li value="98"> BLOB
 * <li value="99"> NUMERIC
 * <li value="100"> INTEGER
 * <li value="101"> REAL
 * </ul>
 *
 * A NULL value is not changed by this routine.  It remains NULL.
 */
case OP_Cast: {                  /* in1 */
	assert(pOp->p2>=AFFINITY_BLOB && pOp->p2<=AFFINITY_REAL);
	testcase( pOp->p2==AFFINITY_TEXT);
	testcase( pOp->p2==AFFINITY_BLOB);
	testcase( pOp->p2==AFFINITY_NUMERIC);
	testcase( pOp->p2==AFFINITY_INTEGER);
	testcase( pOp->p2==AFFINITY_REAL);
	pIn1 = &aMem[pOp->p1];
	memAboutToChange(p, pIn1);
	rc = ExpandBlob(pIn1);
	sqlite3VdbeMemCast(pIn1, pOp->p2);
	UPDATE_MAX_BLOBSIZE(pIn1);
	if (rc) goto abort_due_to_error;
	break;
}
#endif /* SQLITE_OMIT_CAST */

/* Opcode: Eq P1 P2 P3 P4 P5
 * Synopsis: IF r[P3]==r[P1]
 *
 * Compare the values in register P1 and P3.  If reg(P3)==reg(P1) then
 * jump to address P2.  Or if the SQLITE_STOREP2 flag is set in P5, then
 * store the result of comparison in register P2.
 *
 * The AFFINITY_MASK portion of P5 must be an affinity character -
 * AFFINITY_TEXT, AFFINITY_INTEGER, and so forth. An attempt is made
 * to coerce both inputs according to this affinity before the
 * comparison is made. If the AFFINITY_MASK is 0x00, then numeric
 * affinity is used. Note that the affinity conversions are stored
 * back into the input registers P1 and P3.  So this opcode can cause
 * persistent changes to registers P1 and P3.
 *
 * Once any conversions have taken place, and neither value is NULL,
 * the values are compared. If both values are blobs then memcmp() is
 * used to determine the results of the comparison.  If both values
 * are text, then the appropriate collating function specified in
 * P4 is used to do the comparison.  If P4 is not specified then
 * memcmp() is used to compare text string.  If both values are
 * numeric, then a numeric comparison is used. If the two values
 * are of different types, then numbers are considered less than
 * strings and strings are considered less than blobs.
 *
 * If SQLITE_NULLEQ is set in P5 then the result of comparison is always either
 * true or false and is never NULL.  If both operands are NULL then the result
 * of comparison is true.  If either operand is NULL then the result is false.
 * If neither operand is NULL the result is the same as it would be if
 * the SQLITE_NULLEQ flag were omitted from P5.
 *
 * If both SQLITE_STOREP2 and SQLITE_KEEPNULL flags are set then the
 * content of r[P2] is only changed if the new value is NULL or 0 (false).
 * In other words, a prior r[P2] value will not be overwritten by 1 (true).
 */
/* Opcode: Ne P1 P2 P3 P4 P5
 * Synopsis: IF r[P3]!=r[P1]
 *
 * This works just like the Eq opcode except that the jump is taken if
 * the operands in registers P1 and P3 are not equal.  See the Eq opcode for
 * additional information.
 *
 * If both SQLITE_STOREP2 and SQLITE_KEEPNULL flags are set then the
 * content of r[P2] is only changed if the new value is NULL or 1 (true).
 * In other words, a prior r[P2] value will not be overwritten by 0 (false).
 */
/* Opcode: Lt P1 P2 P3 P4 P5
 * Synopsis: IF r[P3]<r[P1]
 *
 * Compare the values in register P1 and P3.  If reg(P3)<reg(P1) then
 * jump to address P2.  Or if the SQLITE_STOREP2 flag is set in P5 store
 * the result of comparison (0 or 1 or NULL) into register P2.
 *
 * If the SQLITE_JUMPIFNULL bit of P5 is set and either reg(P1) or
 * reg(P3) is NULL then the take the jump.  If the SQLITE_JUMPIFNULL
 * bit is clear then fall through if either operand is NULL.
 *
 * The AFFINITY_MASK portion of P5 must be an affinity character -
 * AFFINITY_TEXT, AFFINITY_INTEGER, and so forth. An attempt is made
 * to coerce both inputs according to this affinity before the
 * comparison is made. If the AFFINITY_MASK is 0x00, then numeric
 * affinity is used. Note that the affinity conversions are stored
 * back into the input registers P1 and P3.  So this opcode can cause
 * persistent changes to registers P1 and P3.
 *
 * Once any conversions have taken place, and neither value is NULL,
 * the values are compared. If both values are blobs then memcmp() is
 * used to determine the results of the comparison.  If both values
 * are text, then the appropriate collating function specified in
 * P4 is  used to do the comparison.  If P4 is not specified then
 * memcmp() is used to compare text string.  If both values are
 * numeric, then a numeric comparison is used. If the two values
 * are of different types, then numbers are considered less than
 * strings and strings are considered less than blobs.
 */
/* Opcode: Le P1 P2 P3 P4 P5
 * Synopsis: IF r[P3]<=r[P1]
 *
 * This works just like the Lt opcode except that the jump is taken if
 * the content of register P3 is less than or equal to the content of
 * register P1.  See the Lt opcode for additional information.
 */
/* Opcode: Gt P1 P2 P3 P4 P5
 * Synopsis: IF r[P3]>r[P1]
 *
 * This works just like the Lt opcode except that the jump is taken if
 * the content of register P3 is greater than the content of
 * register P1.  See the Lt opcode for additional information.
 */
/* Opcode: Ge P1 P2 P3 P4 P5
 * Synopsis: IF r[P3]>=r[P1]
 *
 * This works just like the Lt opcode except that the jump is taken if
 * the content of register P3 is greater than or equal to the content of
 * register P1.  See the Lt opcode for additional information.
 */
case OP_Eq:               /* same as TK_EQ, jump, in1, in3 */
case OP_Ne:               /* same as TK_NE, jump, in1, in3 */
case OP_Lt:               /* same as TK_LT, jump, in1, in3 */
case OP_Le:               /* same as TK_LE, jump, in1, in3 */
case OP_Gt:               /* same as TK_GT, jump, in1, in3 */
case OP_Ge: {             /* same as TK_GE, jump, in1, in3 */
	int res, res2;      /* Result of the comparison of pIn1 against pIn3 */
	char affinity;      /* Affinity to use for comparison */
	u32 flags1;         /* Copy of initial value of pIn1->flags */
	u32 flags3;         /* Copy of initial value of pIn3->flags */

	pIn1 = &aMem[pOp->p1];
	pIn3 = &aMem[pOp->p3];
	flags1 = pIn1->flags;
	flags3 = pIn3->flags;
	if ((flags1 | flags3)&MEM_Null) {
		/* One or both operands are NULL */
		if (pOp->p5 & SQLITE_NULLEQ) {
			/* If SQLITE_NULLEQ is set (which will only happen if the operator is
			 * OP_Eq or OP_Ne) then take the jump or not depending on whether
			 * or not both operands are null.
			 */
			assert(pOp->opcode==OP_Eq || pOp->opcode==OP_Ne);
			assert((flags1 & MEM_Cleared)==0);
			assert((pOp->p5 & SQLITE_JUMPIFNULL)==0);
			if ((flags1&flags3&MEM_Null)!=0
			    && (flags3&MEM_Cleared)==0
				) {
				res = 0;  /* Operands are equal */
			} else {
				res = 1;  /* Operands are not equal */
			}
		} else {
			/* SQLITE_NULLEQ is clear and at least one operand is NULL,
			 * then the result is always NULL.
			 * The jump is taken if the SQLITE_JUMPIFNULL bit is set.
			 */
			if (pOp->p5 & SQLITE_STOREP2) {
				pOut = &aMem[pOp->p2];
				iCompare = 1;    /* Operands are not equal */
				memAboutToChange(p, pOut);
				MemSetTypeFlag(pOut, MEM_Null);
				REGISTER_TRACE(pOp->p2, pOut);
			} else {
				VdbeBranchTaken(2,3);
				if (pOp->p5 & SQLITE_JUMPIFNULL) {
					goto jump_to_p2;
				}
			}
			break;
		}
	} else {
		/* Neither operand is NULL.  Do a comparison. */
		affinity = pOp->p5 & AFFINITY_MASK;
		if (affinity>=AFFINITY_NUMERIC) {
			if ((flags1 | flags3)&MEM_Str) {
				if ((flags1 & (MEM_Int|MEM_Real|MEM_Str))==MEM_Str) {
					applyNumericAffinity(pIn1,0);
					testcase( flags3!=pIn3->flags); /* Possible if pIn1==pIn3 */
					flags3 = pIn3->flags;
				}
				if ((flags3 & (MEM_Int|MEM_Real|MEM_Str))==MEM_Str) {
					applyNumericAffinity(pIn3,0);
				}
			}
			/* Handle the common case of integer comparison here, as an
			 * optimization, to avoid a call to sqlite3MemCompare()
			 */
			if ((pIn1->flags & pIn3->flags & MEM_Int)!=0) {
				if (pIn3->u.i > pIn1->u.i) { res = +1; goto compare_op; }
				if (pIn3->u.i < pIn1->u.i) { res = -1; goto compare_op; }
				res = 0;
				goto compare_op;
			}
		} else if (affinity==AFFINITY_TEXT) {
			if ((flags1 & MEM_Str)==0 && (flags1 & (MEM_Int|MEM_Real))!=0) {
				testcase( pIn1->flags & MEM_Int);
				testcase( pIn1->flags & MEM_Real);
				sqlite3VdbeMemStringify(pIn1, 1);
				testcase( (flags1&MEM_Dyn) != (pIn1->flags&MEM_Dyn));
				flags1 = (pIn1->flags & ~MEM_TypeMask) | (flags1 & MEM_TypeMask);
				assert(pIn1!=pIn3);
			}
			if ((flags3 & MEM_Str)==0 && (flags3 & (MEM_Int|MEM_Real))!=0) {
				testcase( pIn3->flags & MEM_Int);
				testcase( pIn3->flags & MEM_Real);
				sqlite3VdbeMemStringify(pIn3, 1);
				testcase( (flags3&MEM_Dyn) != (pIn3->flags&MEM_Dyn));
				flags3 = (pIn3->flags & ~MEM_TypeMask) | (flags3 & MEM_TypeMask);
			}
		}
		assert(pOp->p4type==P4_COLLSEQ || pOp->p4.pColl==0);
		res = sqlite3MemCompare(pIn3, pIn1, pOp->p4.pColl);
	}
			compare_op:
	switch( pOp->opcode) {
	case OP_Eq:    res2 = res==0;     break;
	case OP_Ne:    res2 = res;        break;
	case OP_Lt:    res2 = res<0;      break;
	case OP_Le:    res2 = res<=0;     break;
	case OP_Gt:    res2 = res>0;      break;
	default:       res2 = res>=0;     break;
	}

	/* Undo any changes made by applyAffinity() to the input registers. */
	assert((pIn1->flags & MEM_Dyn) == (flags1 & MEM_Dyn));
	pIn1->flags = flags1;
	assert((pIn3->flags & MEM_Dyn) == (flags3 & MEM_Dyn));
	pIn3->flags = flags3;

	if (pOp->p5 & SQLITE_STOREP2) {
		pOut = &aMem[pOp->p2];
		iCompare = res;
		res2 = res2!=0;  /* For this path res2 must be exactly 0 or 1 */
		if ((pOp->p5 & SQLITE_KEEPNULL)!=0) {
			/* The KEEPNULL flag prevents OP_Eq from overwriting a NULL with 1
			 * and prevents OP_Ne from overwriting NULL with 0.  This flag
			 * is only used in contexts where either:
			 *   (1) op==OP_Eq && (r[P2]==NULL || r[P2]==0)
			 *   (2) op==OP_Ne && (r[P2]==NULL || r[P2]==1)
			 * Therefore it is not necessary to check the content of r[P2] for
			 * NULL.
			 */
			assert(pOp->opcode==OP_Ne || pOp->opcode==OP_Eq);
			assert(res2==0 || res2==1);
			testcase( res2==0 && pOp->opcode==OP_Eq);
			testcase( res2==1 && pOp->opcode==OP_Eq);
			testcase( res2==0 && pOp->opcode==OP_Ne);
			testcase( res2==1 && pOp->opcode==OP_Ne);
			if ((pOp->opcode==OP_Eq)==res2) break;
		}
		memAboutToChange(p, pOut);
		MemSetTypeFlag(pOut, MEM_Int);
		pOut->u.i = res2;
		REGISTER_TRACE(pOp->p2, pOut);
	} else {
		VdbeBranchTaken(res!=0, (pOp->p5 & SQLITE_NULLEQ)?2:3);
		if (res2) {
			goto jump_to_p2;
		}
	}
	break;
}

/* Opcode: ElseNotEq * P2 * * *
 *
 * This opcode must immediately follow an OP_Lt or OP_Gt comparison operator.
 * If result of an OP_Eq comparison on the same two operands
 * would have be NULL or false (0), then then jump to P2.
 * If the result of an OP_Eq comparison on the two previous operands
 * would have been true (1), then fall through.
 */
case OP_ElseNotEq: {       /* same as TK_ESCAPE, jump */
	assert(pOp>aOp);
	assert(pOp[-1].opcode==OP_Lt || pOp[-1].opcode==OP_Gt);
	assert(pOp[-1].p5 & SQLITE_STOREP2);
	VdbeBranchTaken(iCompare!=0, 2);
	if (iCompare!=0) goto jump_to_p2;
	break;
}


/* Opcode: Permutation * * * P4 *
 *
 * Set the permutation used by the OP_Compare operator to be the array
 * of integers in P4.
 *
 * The permutation is only valid until the next OP_Compare that has
 * the OPFLAG_PERMUTE bit set in P5. Typically the OP_Permutation should
 * occur immediately prior to the OP_Compare.
 *
 * The first integer in the P4 integer array is the length of the array
 * and does not become part of the permutation.
 */
case OP_Permutation: {
			assert(pOp->p4type==P4_INTARRAY);
			assert(pOp->p4.ai);
			aPermute = pOp->p4.ai + 1;
			break;
		}

/* Opcode: Compare P1 P2 P3 P4 P5
 * Synopsis: r[P1@P3] <-> r[P2@P3]
 *
 * Compare two vectors of registers in reg(P1)..reg(P1+P3-1) (call this
 * vector "A") and in reg(P2)..reg(P2+P3-1) ("B").  Save the result of
 * the comparison for use by the next OP_Jump instruct.
 *
 * If P5 has the OPFLAG_PERMUTE bit set, then the order of comparison is
 * determined by the most recent OP_Permutation operator.  If the
 * OPFLAG_PERMUTE bit is clear, then register are compared in sequential
 * order.
 *
 * P4 is a key_def structure that defines collating sequences and sort
 * orders for the comparison.  The permutation applies to registers
 * only.  The key_def elements are used sequentially.
 *
 * The comparison is a sort comparison, so NULLs compare equal,
 * NULLs are less than numbers, numbers are less than strings,
 * and strings are less than blobs.
 */
case OP_Compare: {
	int p1;
	int p2;
	int idx;

	if ((pOp->p5 & OPFLAG_PERMUTE) == 0)
		aPermute = 0;

	int n = pOp->p3;

	assert(pOp->p4type == P4_KEYDEF);
	assert(n>0);
	p1 = pOp->p1;
	p2 = pOp->p2;

	struct key_def *def = pOp->p4.key_def;
#if SQLITE_DEBUG
	if (aPermute) {
		int mx = 0;
		for(uint32_t k = 0; k < (uint32_t)n; k++)
			if (aPermute[k] > mx)
				mx = aPermute[k];
		assert(p1>0 && p1+mx<=(p->nMem+1 - p->nCursor)+1);
		assert(p2>0 && p2+mx<=(p->nMem+1 - p->nCursor)+1);
	} else {
		assert(p1>0 && p1+n<=(p->nMem+1 - p->nCursor)+1);
		assert(p2>0 && p2+n<=(p->nMem+1 - p->nCursor)+1);
	}
#endif /* SQLITE_DEBUG */
	for(int i = 0; i < n; i++) {
		idx = aPermute ? aPermute[i] : i;
		assert(memIsValid(&aMem[p1+idx]));
		assert(memIsValid(&aMem[p2+idx]));
		REGISTER_TRACE(p1+idx, &aMem[p1+idx]);
		REGISTER_TRACE(p2+idx, &aMem[p2+idx]);
		assert(i < (int)def->part_count);
		struct coll *coll = def->parts[i].coll;
		bool is_rev = def->parts[i].sort_order == SORT_ORDER_DESC;
		iCompare = sqlite3MemCompare(&aMem[p1+idx], &aMem[p2+idx], coll);
		if (iCompare) {
			if (is_rev)
				iCompare = -iCompare;
			break;
		}
	}
	aPermute = 0;
	break;
}

/* Opcode: Jump P1 P2 P3 * *
 *
 * Jump to the instruction at address P1, P2, or P3 depending on whether
 * in the most recent OP_Compare instruction the P1 vector was less than
 * equal to, or greater than the P2 vector, respectively.
 */
case OP_Jump: {             /* jump */
	if (iCompare<0) {
		VdbeBranchTaken(0,3); pOp = &aOp[pOp->p1 - 1];
	} else if (iCompare==0) {
		VdbeBranchTaken(1,3); pOp = &aOp[pOp->p2 - 1];
	} else {
		VdbeBranchTaken(2,3); pOp = &aOp[pOp->p3 - 1];
	}
	break;
}

/* Opcode: And P1 P2 P3 * *
 * Synopsis: r[P3]=(r[P1] && r[P2])
 *
 * Take the logical AND of the values in registers P1 and P2 and
 * write the result into register P3.
 *
 * If either P1 or P2 is 0 (false) then the result is 0 even if
 * the other input is NULL.  A NULL and true or two NULLs give
 * a NULL output.
 */
/* Opcode: Or P1 P2 P3 * *
 * Synopsis: r[P3]=(r[P1] || r[P2])
 *
 * Take the logical OR of the values in register P1 and P2 and
 * store the answer in register P3.
 *
 * If either P1 or P2 is nonzero (true) then the result is 1 (true)
 * even if the other input is NULL.  A NULL and false or two NULLs
 * give a NULL output.
 */
case OP_And:              /* same as TK_AND, in1, in2, out3 */
case OP_Or: {             /* same as TK_OR, in1, in2, out3 */
	int v1;    /* Left operand:  0==FALSE, 1==TRUE, 2==UNKNOWN or NULL */
	int v2;    /* Right operand: 0==FALSE, 1==TRUE, 2==UNKNOWN or NULL */

	pIn1 = &aMem[pOp->p1];
	if (pIn1->flags & MEM_Null) {
		v1 = 2;
	} else {
		v1 = sqlite3VdbeIntValue(pIn1)!=0;
	}
	pIn2 = &aMem[pOp->p2];
	if (pIn2->flags & MEM_Null) {
		v2 = 2;
	} else {
		v2 = sqlite3VdbeIntValue(pIn2)!=0;
	}
	if (pOp->opcode==OP_And) {
		static const unsigned char and_logic[] = { 0, 0, 0, 0, 1, 2, 0, 2, 2 };
		v1 = and_logic[v1*3+v2];
	} else {
		static const unsigned char or_logic[] = { 0, 1, 2, 1, 1, 1, 2, 1, 2 };
		v1 = or_logic[v1*3+v2];
	}
	pOut = &aMem[pOp->p3];
	if (v1==2) {
		MemSetTypeFlag(pOut, MEM_Null);
	} else {
		pOut->u.i = v1;
		MemSetTypeFlag(pOut, MEM_Int);
	}
	break;
}

/* Opcode: Not P1 P2 * * *
 * Synopsis: r[P2]= !r[P1]
 *
 * Interpret the value in register P1 as a boolean value.  Store the
 * boolean complement in register P2.  If the value in register P1 is
 * NULL, then a NULL is stored in P2.
 */
case OP_Not: {                /* same as TK_NOT, in1, out2 */
	pIn1 = &aMem[pOp->p1];
	pOut = &aMem[pOp->p2];
	sqlite3VdbeMemSetNull(pOut);
	if ((pIn1->flags & MEM_Null)==0) {
		pOut->flags = MEM_Int;
		pOut->u.i = !sqlite3VdbeIntValue(pIn1);
	}
	break;
}

/* Opcode: BitNot P1 P2 * * *
 * Synopsis: r[P1]= ~r[P1]
 *
 * Interpret the content of register P1 as an integer.  Store the
 * ones-complement of the P1 value into register P2.  If P1 holds
 * a NULL then store a NULL in P2.
 */
case OP_BitNot: {             /* same as TK_BITNOT, in1, out2 */
	pIn1 = &aMem[pOp->p1];
	pOut = &aMem[pOp->p2];
	sqlite3VdbeMemSetNull(pOut);
	if ((pIn1->flags & MEM_Null)==0) {
		pOut->flags = MEM_Int;
		pOut->u.i = ~sqlite3VdbeIntValue(pIn1);
	}
	break;
}

/* Opcode: Once P1 P2 * * *
 *
 * If the P1 value is equal to the P1 value on the OP_Init opcode at
 * instruction 0, then jump to P2.  If the two P1 values differ, then
 * set the P1 value on this opcode to equal the P1 value on the OP_Init
 * and fall through.
 */
case OP_Once: {             /* jump */
	assert(p->aOp[0].opcode==OP_Init);
	VdbeBranchTaken(p->aOp[0].p1==pOp->p1, 2);
	if (p->aOp[0].p1==pOp->p1) {
		goto jump_to_p2;
	} else {
		pOp->p1 = p->aOp[0].p1;
	}
	break;
}

/* Opcode: If P1 P2 P3 * *
 *
 * Jump to P2 if the value in register P1 is true.  The value
 * is considered true if it is numeric and non-zero.  If the value
 * in P1 is NULL then take the jump if and only if P3 is non-zero.
 */
/* Opcode: IfNot P1 P2 P3 * *
 *
 * Jump to P2 if the value in register P1 is False.  The value
 * is considered false if it has a numeric value of zero.  If the value
 * in P1 is NULL then take the jump if and only if P3 is non-zero.
 */
case OP_If:                 /* jump, in1 */
case OP_IfNot: {            /* jump, in1 */
	int c;
	pIn1 = &aMem[pOp->p1];
	if (pIn1->flags & MEM_Null) {
		c = pOp->p3;
	} else {
#ifdef SQLITE_OMIT_FLOATING_POINT
		c = sqlite3VdbeIntValue(pIn1)!=0;
#else
		c = sqlite3VdbeRealValue(pIn1)!=0.0;
#endif
		if (pOp->opcode==OP_IfNot) c = !c;
	}
	VdbeBranchTaken(c!=0, 2);
	if (c) {
		goto jump_to_p2;
	}
	break;
}

/* Opcode: IsNull P1 P2 * * *
 * Synopsis: if r[P1]==NULL goto P2
 *
 * Jump to P2 if the value in register P1 is NULL.
 */
case OP_IsNull: {            /* same as TK_ISNULL, jump, in1 */
	pIn1 = &aMem[pOp->p1];
	VdbeBranchTaken( (pIn1->flags & MEM_Null)!=0, 2);
	if ((pIn1->flags & MEM_Null)!=0) {
		goto jump_to_p2;
	}
	break;
}

/* Opcode: NotNull P1 P2 * * *
 * Synopsis: if r[P1]!=NULL goto P2
 *
 * Jump to P2 if the value in register P1 is not NULL.
 */
case OP_NotNull: {            /* same as TK_NOTNULL, jump, in1 */
	pIn1 = &aMem[pOp->p1];
	VdbeBranchTaken( (pIn1->flags & MEM_Null)==0, 2);
	if ((pIn1->flags & MEM_Null)==0) {
		goto jump_to_p2;
	}
	break;
}

/* Opcode: Column P1 P2 P3 P4 P5
 * Synopsis: r[P3]=PX
 *
 * Interpret the data that cursor P1 points to as a structure built using
 * the MakeRecord instruction.  (See the MakeRecord opcode for additional
 * information about the format of the data.)  Extract the P2-th column
 * from this record.  If there are less that (P2+1)
 * values in the record, extract a NULL.
 *
 * The value extracted is stored in register P3.
 *
 * If the column contains fewer than P2 fields, then extract a NULL.  Or,
 * if the P4 argument is a P4_MEM use the value of the P4 argument as
 * the result.
 *
 * If the OPFLAG_CLEARCACHE bit is set on P5 and P1 is a pseudo-table cursor,
 * then the cache of the cursor is reset prior to extracting the column.
 * The first OP_Column against a pseudo-table after the value of the content
 * register has changed should have this bit set.
 *
 * If the OPFLAG_LENGTHARG and OPFLAG_TYPEOFARG bits are set on P5 when
 * the result is guaranteed to only be used as the argument of a length()
 * or typeof() function, respectively.  The loading of large blobs can be
 * skipped for length() and all content loading can be skipped for typeof().
 */
case OP_Column: {
	int p2;            /* column number to retrieve */
	VdbeCursor *pC;    /* The VDBE cursor */
	BtCursor *pCrsr = NULL; /* The BTree cursor */
	u32 *aOffset;      /* aOffset[i] is offset to start of data for i-th column */
	int i;             /* Loop counter */
	Mem *pDest;        /* Where to write the extracted value */
	Mem sMem;          /* For storing the record being decoded */
	const u8 *zData;   /* Part of the record being decoded */
	const u8 MAYBE_UNUSED *zEnd;    /* Data end */
	const u8 *zParse;  /* Next unparsed byte of the row */
	Mem *pReg;         /* PseudoTable input register */

	pC = p->apCsr[pOp->p1];
	p2 = pOp->p2;

	assert(pOp->p3>0 && pOp->p3<=(p->nMem+1 - p->nCursor));
	pDest = &aMem[pOp->p3];
	memAboutToChange(p, pDest);
	assert(pOp->p1>=0 && pOp->p1<p->nCursor);
	assert(pC!=0);
	assert(p2<pC->nField);
	aOffset = pC->aOffset;
	assert(pC->eCurType!=CURTYPE_PSEUDO || pC->nullRow);
	assert(pC->eCurType!=CURTYPE_SORTER);

	if (pC->cacheStatus!=p->cacheCtr) {                /*OPTIMIZATION-IF-FALSE*/
		if (pC->nullRow) {
			if (pC->eCurType==CURTYPE_PSEUDO) {
				assert(pC->uc.pseudoTableReg>0);
				pReg = &aMem[pC->uc.pseudoTableReg];
				assert(pReg->flags & MEM_Blob);
				assert(memIsValid(pReg));
				pC->payloadSize = pC->szRow = pReg->n;
				pC->aRow = (u8*)pReg->z;
			} else {
				sqlite3VdbeMemSetNull(pDest);
				goto op_column_out;
			}
		} else {
			pCrsr = pC->uc.pCursor;
			assert(pC->eCurType==CURTYPE_TARANTOOL);
			assert(pCrsr);
			assert(sqlite3CursorIsValid(pCrsr));
			assert(pCrsr->curFlags & BTCF_TaCursor ||
			       pCrsr->curFlags & BTCF_TEphemCursor);
			pC->aRow = tarantoolSqlite3PayloadFetch(pCrsr,
								&pC->payloadSize);
			pC->szRow = pC->payloadSize;

		}
		pC->cacheStatus = p->cacheCtr;
		zParse = pC->aRow;
		pC->nRowField = mp_decode_array((const char **)&zParse); /* # of fields */
		aOffset[0] = (u32)(zParse - pC->aRow);
		pC->nHdrParsed = 0;
	}

	if ( (u32)p2>=pC->nRowField) {
		if (pOp->p4type==P4_MEM) {
			sqlite3VdbeMemShallowCopy(pDest, pOp->p4.pMem, MEM_Static);
		} else {
			sqlite3VdbeMemSetNull(pDest);
		}
		goto op_column_out;
	}

	/* Sometimes the data is too large and overflow pages come into play.
	 * In the later case allocate a buffer and reassamble the row.
	 * Stock SQLite utilized several clever techniques to optimize here.
	 * The techniques we ripped out to simplify the code.
	 */
	if (pC->szRow==pC->payloadSize) {
		zData = pC->aRow;
		zEnd = zData + pC->payloadSize;
	} else {
		memset(&sMem, 0, sizeof(sMem));
		rc = sqlite3VdbeMemFromBtree(pC->uc.pCursor, 0, pC->payloadSize, &sMem);
		if (rc!=SQLITE_OK) goto abort_due_to_error;
		zData = (u8*)sMem.z;
		zEnd = zData + pC->payloadSize;
	}

	/*
	 * Make sure at least the first p2+1 entries of the header
	 * have been parsed and valid information is in aOffset[].
	 * If there is more header available for parsing in the
	 * record, try to extract additional fields up through the
	 * p2+1-th field.
	 */
	if (pC->nHdrParsed <= p2) {
		u32 size;
		if (pC->eCurType == CURTYPE_TARANTOOL &&
		    pCrsr != NULL && ((pCrsr->curFlags & BTCF_TaCursor) != 0 ||
		    (pCrsr->curFlags & BTCF_TEphemCursor)) &&
		    (zParse = tarantoolSqlite3TupleColumnFast(pCrsr, p2,
							      &size)) != NULL) {
			/*
			 * Special case for tarantool spaces: for
			 * indexed fields a tuple field map can be
			 * used. Else there is no sense in
			 * tuple_field usage, because it makes
			 * foreach field { mp_next(); } to find
			 * a field. In such a case sqlite is
			 * better - it saves offsets to all fields
			 * visited in mp_next() cycle.
			 */
			aOffset[p2] = zParse - zData;
			aOffset[p2 + 1] = aOffset[p2] + size;
		} else {
			i = pC->nHdrParsed;
			zParse = zData+aOffset[i];

			/*
			 * Fill in aOffset[i] values through the
			 * p2-th field.
			 */
			do{
				mp_next((const char **) &zParse);
				aOffset[++i] = (u32)(zParse-zData);
			}while( i<=p2);
			assert((u32)p2 != pC->nRowField || zParse == zEnd);
			pC->nHdrParsed = i;
		}
	}

	/* Extract the content for the p2+1-th column.  Control can only
	 * reach this point if aOffset[p2], aOffset[p2+1] are
	 * all valid.
	 */
	assert(rc==SQLITE_OK);
	assert(sqlite3VdbeCheckMemInvariants(pDest));
	if (VdbeMemDynamic(pDest)) {
		sqlite3VdbeMemSetNull(pDest);
	}

	sqlite3VdbeMsgpackGet(zData+aOffset[p2], pDest);
	/* MsgPack map, array or extension (unsupported in sqlite).
	 * Wrap it in a blob verbatim.
	 */

	if (pDest->flags == 0) {
		pDest->n = aOffset[p2+1]-aOffset[p2];
		pDest->z = (char *)zData+aOffset[p2];
		pDest->flags = MEM_Blob|MEM_Ephem|MEM_Subtype;
		pDest->eSubtype = MSGPACK_SUBTYPE;
	}
	/*
	 * Add 0 termination (at most for strings)
	 * Not sure why do we check MEM_Ephem
	 */
	if ((pDest->flags & (MEM_Ephem | MEM_Str)) == (MEM_Ephem | MEM_Str)) {
		int len = pDest->n;
		if (pDest->szMalloc<len+1) {
			if (sqlite3VdbeMemGrow(pDest, len+1, 1)) goto op_column_error;
		} else {
			pDest->z = memcpy(pDest->zMalloc, pDest->z, len);
			pDest->flags &= ~MEM_Ephem;
		}
		pDest->z[len] = 0;
		pDest->flags |= MEM_Term;
	}

	if (zData!=pC->aRow) sqlite3VdbeMemRelease(&sMem);
			op_column_out:
	UPDATE_MAX_BLOBSIZE(pDest);
	REGISTER_TRACE(pOp->p3, pDest);
	break;

			op_column_error:
	if (zData!=pC->aRow) sqlite3VdbeMemRelease(&sMem);
	goto abort_due_to_error;
}

/* Opcode: Affinity P1 P2 * P4 *
 * Synopsis: affinity(r[P1@P2])
 *
 * Apply affinities to a range of P2 registers starting with P1.
 *
 * P4 is a string that is P2 characters long. The nth character of the
 * string indicates the column affinity that should be used for the nth
 * memory cell in the range.
 */
case OP_Affinity: {
	const char *zAffinity;   /* The affinity to be applied */
	char cAff;               /* A single character of affinity */

	zAffinity = pOp->p4.z;
	assert(zAffinity!=0);
	assert(zAffinity[pOp->p2]==0);
	pIn1 = &aMem[pOp->p1];
	while( (cAff = *(zAffinity++))!=0) {
		assert(pIn1 <= &p->aMem[(p->nMem+1 - p->nCursor)]);
		assert(memIsValid(pIn1));
		applyAffinity(pIn1, cAff);
		pIn1++;
	}
	break;
}

/* Opcode: MakeRecord P1 P2 P3 P4 P5
 * Synopsis: r[P3]=mkrec(r[P1@P2])
 *
 * Convert P2 registers beginning with P1 into the [record format]
 * use as a data record in a database table or as a key
 * in an index.  The OP_Column opcode can decode the record later.
 *
 * P4 may be a string that is P2 characters long.  The nth character of the
 * string indicates the column affinity that should be used for the nth
 * field of the index key.
 *
 * The mapping from character to affinity is given by the AFFINITY_
 * macros defined in sqliteInt.h.
 *
 * If P4 is NULL then all index fields have the affinity BLOB.
 *
 * If P5 is not NULL then record under construction is intended to be inserted
 * into ephemeral space. Thus, sort of memory optimization can be performed.
 */
case OP_MakeRecord: {
	Mem *pRec;             /* The new record */
	i64 nByte;             /* Data space required for this record */
	Mem *pData0;           /* First field to be combined into the record */
	Mem MAYBE_UNUSED *pLast;  /* Last field of the record */
	int nField;            /* Number of fields in the record */
	char *zAffinity;       /* The affinity string for the record */
	u8 bIsEphemeral;

	/* Assuming the record contains N fields, the record format looks
	 * like this:
	 *
	 * ------------------------------------------------------------------------
	 * | hdr-size | type 0 | type 1 | ... | type N-1 | data0 | ... | data N-1 |
	 * ------------------------------------------------------------------------
	 *
	 * Data(0) is taken from register P1.  Data(1) comes from register P1+1
	 * and so forth.
	 *
	 * Each type field is a varint representing the serial type of the
	 * corresponding data element (see sqlite3VdbeSerialType()). The
	 * hdr-size field is also a varint which is the offset from the beginning
	 * of the record to data0.
	 */
	nField = pOp->p1;
	zAffinity = pOp->p4.z;
	bIsEphemeral = pOp->p5;
	assert(nField>0 && pOp->p2>0 && pOp->p2+nField<=(p->nMem+1 - p->nCursor)+1);
	pData0 = &aMem[nField];
	nField = pOp->p2;
	pLast = &pData0[nField-1];

	/* Identify the output register */
	assert(pOp->p3<pOp->p1 || pOp->p3>=pOp->p1+pOp->p2);
	pOut = &aMem[pOp->p3];
	memAboutToChange(p, pOut);

	/* Apply the requested affinity to all inputs
	 */
	assert(pData0<=pLast);
	if (zAffinity) {
		pRec = pData0;
		do{
			applyAffinity(pRec++, *(zAffinity++));
			assert(zAffinity[0]==0 || pRec<=pLast);
		}while( zAffinity[0]);
	}

	/* Loop through the elements that will make up the record to figure
	 * out how much space is required for the new record.
	 */
	nByte = sqlite3VdbeMsgpackRecordLen(pData0, nField);

	if (nByte>db->aLimit[SQLITE_LIMIT_LENGTH]) {
		goto too_big;
	}

	/* In case of ephemeral space, it is possible to save some memory
	 * allocating one by ordinary malloc: instead of cutting pieces
	 * from region and waiting while they will be freed after
	 * statement commitment, it is better to reuse the same chunk.
	 * Such optimization is prohibited for ordinary spaces, since
	 * memory shouldn't be reused until it is written into WAL.
	 *
	 * However, if memory for ephemeral space is allocated
	 * on region, it will be freed only in vdbeHalt() routine.
	 * It is the only way to free this region memory,
	 * since ephemeral spaces don't have nothing in common
	 * with txn routine and region memory won't be released
	 * after txn_commit() or txn_rollback() as it happens
	 * with ordinary spaces.
	 */
	if (bIsEphemeral) {
		rc = sqlite3VdbeMemClearAndResize(pOut, nByte);
		pOut->flags = MEM_Blob;
	} else {
		/* Allocate memory on the region for the tuple
		 * to be passed to Tarantool. Before that, make
		 * sure previously allocated memory has gone.
		 */
		sqlite3VdbeMemRelease(pOut);
		rc = sql_vdbe_mem_alloc_region(pOut, nByte);
	}
	if (rc)
		goto no_mem;
	/* Write the record */
	assert(pOp->p3>0 && pOp->p3<=(p->nMem+1 - p->nCursor));
	pOut->n = sqlite3VdbeMsgpackRecordPut((u8 *)pOut->z, pData0, nField);
	REGISTER_TRACE(pOp->p3, pOut);
	UPDATE_MAX_BLOBSIZE(pOut);
	break;
}

/* Opcode: Count P1 P2 * * *
 * Synopsis: r[P2]=count()
 *
 * Store the number of entries (an integer value) in the table or index
 * opened by cursor P1 in register P2
 */
case OP_Count: {         /* out2 */
	i64 nEntry;
	BtCursor *pCrsr;

	assert(p->apCsr[pOp->p1]->eCurType==CURTYPE_TARANTOOL);
	pCrsr = p->apCsr[pOp->p1]->uc.pCursor;
	assert(pCrsr);
	nEntry = 0;  /* Not needed.  Only used to silence a warning. */
	if (pCrsr->curFlags & BTCF_TaCursor) {
		rc = tarantoolSqlite3Count(pCrsr, &nEntry);
	} else if (pCrsr->curFlags & BTCF_TEphemCursor) {
		rc = tarantoolSqlite3EphemeralCount(pCrsr, &nEntry);
	} else {
		unreachable();
	}
	if (rc) goto abort_due_to_error;
	pOut = out2Prerelease(p, pOp);
	pOut->u.i = nEntry;
	break;
}

/* Opcode: Savepoint P1 * * P4 *
 *
 * Open, release or rollback the savepoint named by parameter P4, depending
 * on the value of P1. To open a new savepoint, P1==0. To release (commit) an
 * existing savepoint, P1==1, or to rollback an existing savepoint P1==2.
 */
case OP_Savepoint: {
	int p1;                         /* Value of P1 operand */
	char *zName;                    /* Name of savepoint */
	Savepoint *pNew;
	Savepoint *pSavepoint;
	Savepoint *pTmp;
	struct sql_txn *psql_txn = p->psql_txn;

	if (psql_txn == NULL) {
		assert(!box_txn());
		diag_set(ClientError, ER_SAVEPOINT_NO_TRANSACTION);
		rc = SQL_TARANTOOL_ERROR;
		goto abort_due_to_error;
	}
	p1 = pOp->p1;
	zName = pOp->p4.z;

	/* Assert that the p1 parameter is valid. Also that if there is no open
	 * transaction, then there cannot be any savepoints.
	 */
	assert(psql_txn->pSavepoint == NULL || box_txn());
	assert(p1==SAVEPOINT_BEGIN||p1==SAVEPOINT_RELEASE||p1==SAVEPOINT_ROLLBACK);

	if (p1==SAVEPOINT_BEGIN) {
		/* Create a new savepoint structure. */
		pNew = sql_savepoint(p, zName);
		/* Link the new savepoint into the database handle's list. */
		pNew->pNext = psql_txn->pSavepoint;
		psql_txn->pSavepoint = pNew;
	} else {
		/* Find the named savepoint. If there is no such savepoint, then an
		 * an error is returned to the user.
		 */
		for(
			pSavepoint = psql_txn->pSavepoint;
			pSavepoint && sqlite3StrICmp(pSavepoint->zName, zName);
			pSavepoint = pSavepoint->pNext
			);
		if (!pSavepoint) {
			sqlite3VdbeError(p, "no such savepoint: %s", zName);
			rc = SQLITE_ERROR;
		} else {

			/* Determine whether or not this is a transaction savepoint. If so,
			 * and this is a RELEASE command, then the current transaction
			 * is committed.
			 */
			int isTransaction = pSavepoint->pNext == 0;
			if (isTransaction && p1==SAVEPOINT_RELEASE) {
				if ((rc = sqlite3VdbeCheckFk(p, 1))!=SQLITE_OK) {
					goto vdbe_return;
				}
				if (sqlite3VdbeHalt(p)==SQLITE_BUSY) {
					p->pc = (int)(pOp - aOp);
					p->rc = rc = SQLITE_BUSY;
					goto vdbe_return;
				}
				rc = p->rc;
			} else {
				if (p1==SAVEPOINT_ROLLBACK) {
					box_txn_rollback_to_savepoint(pSavepoint->tnt_savepoint);
					if ((user_session->sql_flags &
					     SQLITE_InternChanges) != 0)
						sqlite3ExpirePreparedStatements(db);
				}
			}

			/* Regardless of whether this is a RELEASE or ROLLBACK, destroy all
			 * savepoints nested inside of the savepoint being operated on.
			 */
			while (psql_txn->pSavepoint != pSavepoint) {
				pTmp = psql_txn->pSavepoint;
				psql_txn->pSavepoint = pTmp->pNext;
				/*
				 * Since savepoints are stored in region, we do not
				 * have to destroy them
				 */
			}

			/* If it is a RELEASE, then destroy the savepoint being operated on
			 * too. If it is a ROLLBACK TO, then set the number of deferred
			 * constraint violations present in the database to the value stored
			 * when the savepoint was created.
			 */
			if (p1==SAVEPOINT_RELEASE) {
				assert(pSavepoint == psql_txn->pSavepoint);
				psql_txn->pSavepoint = pSavepoint->pNext;
			} else {
				p->psql_txn->fk_deferred_count =
					pSavepoint->tnt_savepoint->fk_deferred_count;
			}
		}
	}
	if (rc) goto abort_due_to_error;

	break;
}

/* Opcode: FkCheckCommit * * * * *
 *
 * This opcode is used and required by DROP TABLE statement,
 * since deleted rows should be rollbacked in case of foreign keys
 * constraint violations. In case of rollback, instruction
 * also causes the VM to halt, because it makes no sense to continue
 * execution with FK violations. If there is no FK violations, then
 * just commit changes - deleted rows.
 *
 * Do not use this instruction in any statement implementation
 * except for DROP TABLE!
 */
case OP_FkCheckCommit: {
	if (!box_txn()) {
		sqlite3VdbeError(p, "cannot commit or rollback - " \
			"no transaction is active");
		rc = SQLITE_ERROR;
		goto abort_due_to_error;
	}
	if ((rc = sqlite3VdbeCheckFk(p, 0) != SQLITE_OK)) {
		box_txn_rollback();
		sqlite3VdbeHalt(p);
		goto vdbe_return;
	} else {
		rc = box_txn_commit() == 0 ? SQLITE_OK : SQL_TARANTOOL_ERROR;
		if (rc) goto abort_due_to_error;
	}
	break;
}

/* Opcode: TransactionBegin * * * * *
 *
 * Start Tarantool's transaction.
 * Only do that if there is no other active transactions.
 * Otherwise, raise an error with appropriate error message.
 */
case OP_TransactionBegin: {
	if (sql_txn_begin(p) != 0) {
		rc = SQL_TARANTOOL_ERROR;
		goto abort_due_to_error;
	}
	p->auto_commit = false;
	break;
}

/* Opcode: TransactionCommit * * * * *
 *
 * Commit Tarantool's transaction.
 * If there is no active transaction, raise an error.
 */
case OP_TransactionCommit: {
	if (box_txn()) {
		if (box_txn_commit() != 0) {
			rc = SQL_TARANTOOL_ERROR;
			goto abort_due_to_error;
		}
	} else {
		sqlite3VdbeError(p, "cannot commit - no transaction is active");
		rc = SQLITE_ERROR;
		goto abort_due_to_error;
	}
	break;
}

/* Opcode: TransactionRollback * * * * *
 *
 * Rollback Tarantool's transaction.
 * If there is no active transaction, raise an error.
 */
case OP_TransactionRollback: {
	if (box_txn()) {
		if (box_txn_rollback() != 0) {
			rc = SQL_TARANTOOL_ERROR;
			goto abort_due_to_error;
		}
	} else {
		sqlite3VdbeError(p, "cannot rollback - no "
				    "transaction is active");
		rc = SQLITE_ERROR;
		goto abort_due_to_error;
	}
	break;
}

/* Opcode: TTransaction * * * * *
 *
 * Start Tarantool's transaction, if there is no active
 * transactions. Otherwise, create anonymous savepoint,
 * which is used to correctly process ABORT statement inside
 * outer transaction.
 *
 * In contrast to OP_TransactionBegin, this is service opcode,
 * generated automatically alongside with DML routine.
 */
case OP_TTransaction: {
	if (!box_txn()) {
		if (box_txn_begin() != 0) {
			rc = SQL_TARANTOOL_ERROR;
			goto abort_due_to_error;
		}
	} else {
		p->anonymous_savepoint = sql_savepoint(p, NULL);
		if (p->anonymous_savepoint == NULL) {
			rc = SQL_TARANTOOL_ERROR;
			goto abort_due_to_error;
		}
	}
	break;
}

/* Opcode: ReadCookie P1 P2 P3 * *
 *
 * Read cookie number P3 from database P1 and write it into register P2.
 * P3==1 is the schema version.  P3==2 is the database format.
 * P3==3 is the recommended pager cache size, and so forth.  P1==0 is
 * the main database file and P1==1 is the database file used to store
 * temporary tables.
 *
 * There must be a read-lock on the database (either a transaction
 * must be started or there must be an open cursor) before
 * executing this instruction.
 */
case OP_ReadCookie: {               /* out2 */
	pOut = out2Prerelease(p, pOp);
	pOut->u.i = 0;
	break;
}

/* Opcode: SetCookie P1 P2 P3 * *
 *
 * Write the integer value P3 into the schema version.
 * P2==3 is the recommended pager cache
 * size, and so forth.  P1==0 is the main database file and P1==1 is the
 * database file used to store temporary tables.
 *
 * A transaction must be started before executing this opcode.
 */
case OP_SetCookie: {
	assert(pOp->p1==0);
	/* See note about index shifting on OP_ReadCookie */
	/* When the schema cookie changes, record the new cookie internally */
	user_session->sql_flags |= SQLITE_InternChanges;
	if (pOp->p1==1) {
		/* Invalidate all prepared statements whenever the TEMP database
		 * schema is changed.  Ticket #1644
		 */
		sqlite3ExpirePreparedStatements(db);
		p->expired = 0;
	}
	if (rc) goto abort_due_to_error;
	break;
}

/* Opcode: OpenRead P1 P2 P3 P4 P5
 * Synopsis: index id = P2, space ptr = P3
 *
 * Open a cursor for a space specified by pointer in P3 and index
 * id in P2. Give the new cursor an identifier of P1. The P1
 * values need not be contiguous but all P1 values should be
 * small integers. It is an error for P1 to be negative.
 *
 * The P4 value may be a pointer to a key_def structure.
 * If it is a pointer to a key_def structure, then said structure
 * defines the content and collatining sequence of the index
 * being opened. Otherwise, P4 is NULL.
 *
 * If schema has changed since compile time, VDBE ends execution
 * with appropriate error message. The only exception is
 * when P5 is set to OPFLAG_FRESH_PTR, which means that
 * space pointer has been fetched in runtime right before
 * this opcode.
 */
/* Opcode: ReopenIdx P1 P2 P3 P4 P5
 * Synopsis: index id = P2, space ptr = P3
 *
 * The ReopenIdx opcode works exactly like OpenRead except that
 * it first checks to see if the cursor on P1 is already open
 * with the same index and if it is this opcode becomes a no-op.
 * In other words, if the cursor is already open, do not reopen it.
 *
 * The ReopenIdx opcode may only be used with P5 == 0.
 */
/* Opcode: OpenWrite P1 P2 P3 P4 P5
 * Synopsis: index id = P2, space ptr = P3
 *
 * For now, OpenWrite is an alias for OpenRead.
 * It exists just due legacy reasons and should be removed:
 * it isn't neccessary to open cursor to make insertion or
 * deletion.
 */
case OP_ReopenIdx: {
	int nField;
	int p2;
	VdbeCursor *pCur;
	BtCursor *pBtCur;

	assert(pOp->p5==0 || pOp->p5==OPFLAG_SEEKEQ);
	pCur = p->apCsr[pOp->p1];
	p2 = pOp->p2;
	pIn3 = &aMem[pOp->p3];
	assert(pIn3->flags & MEM_Ptr);
	if (pCur && pCur->uc.pCursor->space == (struct space *) pIn3->u.p &&
	    pCur->uc.pCursor->index->def->iid == SQLITE_PAGENO_TO_INDEXID(p2)) {
		goto open_cursor_set_hints;
	}
	/* If the cursor is not currently open or is open on a different
	 * index, then fall through into OP_OpenRead to force a reopen
	 */
case OP_OpenRead:
case OP_OpenWrite:

	assert(pOp->opcode==OP_OpenWrite || pOp->p5==0 || pOp->p5==OPFLAG_SEEKEQ);
	/*
	 * Even if schema has changed, pointer can come from
	 * OP_SIDtoPtr opcode, which converts space id to pointer
	 * during runtime.
	 */
	if (box_schema_version() != p->schema_ver &&
	    (pOp->p5 & OPFLAG_FRESH_PTR) == 0) {
		p->expired = 1;
		rc = SQLITE_ERROR;
		sqlite3VdbeError(p, "schema version has changed: " \
				    "need to re-compile SQL statement");
		goto abort_due_to_error;
	}
	p2 = pOp->p2;
	pIn3 = &aMem[pOp->p3];
	assert(pIn3->flags & MEM_Ptr);
	struct space *space = ((struct space *) pIn3->u.p);
	assert(space != NULL);
	struct index *index = space_index(space, SQLITE_PAGENO_TO_INDEXID(p2));
	assert(index != NULL);
	/*
	 * Since Tarantool iterator provides the full tuple,
	 * we need a number of fields as wide as the table itself.
	 * Otherwise, not enough slots for row parser cache are
	 * allocated in VdbeCursor object.
	 */
	nField = space->def->field_count;
	assert(pOp->p1>=0);
	assert(nField>=0);
	pCur = allocateCursor(p, pOp->p1, nField, CURTYPE_TARANTOOL);
	if (pCur==0) goto no_mem;
	pCur->nullRow = 1;
	pBtCur = pCur->uc.pCursor;
	pBtCur->curFlags |= BTCF_TaCursor;
	pBtCur->space = space;
	pBtCur->index = index;
	pBtCur->eState = CURSOR_INVALID;
	/* Key info still contains sorter order and collation. */
	pCur->key_def = index->def->key_def;

open_cursor_set_hints:
	assert(OPFLAG_BULKCSR==BTREE_BULKLOAD);
	assert(OPFLAG_SEEKEQ==BTREE_SEEK_EQ);
	testcase( pOp->p5 & OPFLAG_BULKCSR);
#ifdef SQLITE_ENABLE_CURSOR_HINTS
	testcase( pOp->p2 & OPFLAG_SEEKEQ);
#endif
	sqlite3CursorHintFlags(pCur->uc.pCursor,
				    (pOp->p5 & (OPFLAG_BULKCSR|OPFLAG_SEEKEQ)));
	if (rc) goto abort_due_to_error;
	break;
}

/**
 * Opcode: OpenTEphemeral P1 P2 * P4 *
 * Synopsis:
 * @param P1 index of new cursor to be created.
 * @param P2 number of columns in a new table.
 * @param P4 key def for new table, NULL is allowed.
 *
 * This opcode creates Tarantool's ephemeral table and sets cursor P1 to it.
 */
case OP_OpenTEphemeral: {
	VdbeCursor *pCx;
	BtCursor *pBtCur;
	assert(pOp->p1 >= 0);
	assert(pOp->p2 > 0);
	assert(pOp->p4type != P4_KEYDEF || pOp->p4.key_def != NULL);

	pCx = allocateCursor(p, pOp->p1, pOp->p2, CURTYPE_TARANTOOL);
	if (pCx == 0) goto no_mem;
	pCx->nullRow = 1;

	pBtCur = pCx->uc.pCursor;
	/* Ephemeral spaces don't have space_id */
	pBtCur->eState = CURSOR_INVALID;
	pBtCur->curFlags = BTCF_TEphemCursor;

	rc = tarantoolSqlite3EphemeralCreate(pCx->uc.pCursor, pOp->p2,
					     pOp->p4.key_def);
	pCx->key_def = pCx->uc.pCursor->index->def->key_def;
	if (rc) goto abort_due_to_error;
	break;
}

/* Opcode: SorterOpen P1 P2 P3 P4 *
 *
 * This opcode works like OP_OpenEphemeral except that it opens
 * a transient index that is specifically designed to sort large
 * tables using an external merge-sort algorithm.
 *
 * If argument P3 is non-zero, then it indicates that the sorter may
 * assume that a stable sort considering the first P3 fields of each
 * key is sufficient to produce the required results.
 */
case OP_SorterOpen: {
	VdbeCursor *pCx;

	assert(pOp->p1>=0);
	assert(pOp->p2>=0);
	pCx = allocateCursor(p, pOp->p1, pOp->p2, CURTYPE_SORTER);
	if (pCx==0) goto no_mem;
	pCx->key_def = pOp->p4.key_def;
	rc = sqlite3VdbeSorterInit(db, pCx);
	if (rc) goto abort_due_to_error;
	break;
}

/* Opcode: SequenceTest P1 P2 * * *
 * Synopsis: if (cursor[P1].ctr++) pc = P2
 *
 * P1 is a sorter cursor. If the sequence counter is currently zero, jump
 * to P2. Regardless of whether or not the jump is taken, increment the
 * the sequence value.
 */
case OP_SequenceTest: {
	VdbeCursor *pC;
	assert(pOp->p1>=0 && pOp->p1<p->nCursor);
	pC = p->apCsr[pOp->p1];
	assert(isSorter(pC));
	if ((pC->seqCount++)==0) {
		goto jump_to_p2;
	}
	break;
}

/* Opcode: OpenPseudo P1 P2 P3 * *
 * Synopsis: P3 columns in r[P2]
 *
 * Open a new cursor that points to a fake table that contains a single
 * row of data.  The content of that one row is the content of memory
 * register P2.  In other words, cursor P1 becomes an alias for the
 * MEM_Blob content contained in register P2.
 *
 * A pseudo-table created by this opcode is used to hold a single
 * row output from the sorter so that the row can be decomposed into
 * individual columns using the OP_Column opcode.  The OP_Column opcode
 * is the only cursor opcode that works with a pseudo-table.
 *
 * P3 is the number of fields in the records that will be stored by
 * the pseudo-table.
 */
case OP_OpenPseudo: {
	VdbeCursor *pCx;

	assert(pOp->p1>=0);
	assert(pOp->p3>=0);
	pCx = allocateCursor(p, pOp->p1, pOp->p3, CURTYPE_PSEUDO);
	if (pCx==0) goto no_mem;
	pCx->nullRow = 1;
	pCx->uc.pseudoTableReg = pOp->p2;
	assert(pOp->p5==0);
	break;
}

/* Opcode: Close P1 * * * *
 *
 * Close a cursor previously opened as P1.  If P1 is not
 * currently open, this instruction is a no-op.
 */
case OP_Close: {
	assert(pOp->p1>=0 && pOp->p1<p->nCursor);
	sqlite3VdbeFreeCursor(p, p->apCsr[pOp->p1]);
	p->apCsr[pOp->p1] = 0;
	break;
}

#ifdef SQLITE_ENABLE_COLUMN_USED_MASK
/* Opcode: ColumnsUsed P1 * * P4 *
 *
 * This opcode (which only exists if SQLite was compiled with
 * SQLITE_ENABLE_COLUMN_USED_MASK) identifies which columns of the
 * table or index for cursor P1 are used.  P4 is a 64-bit integer
 * (P4_INT64) in which the first 63 bits are one for each of the
 * first 63 columns of the table or index that are actually used
 * by the cursor.  The high-order bit is set if any column after
 * the 64th is used.
 */
case OP_ColumnsUsed: {
	VdbeCursor *pC;
	pC = p->apCsr[pOp->p1];
	assert(pC->eCurType==CURTYPE_TARANTOOL);
	pC->maskUsed = *(u64*)pOp->p4.pI64;
	break;
}
#endif

/* Opcode: SeekGE P1 P2 P3 P4 P5
 * Synopsis: key=r[P3@P4]
 *
 * If cursor P1 refers to an SQL table (B-Tree that uses integer keys),
 * use the value in register P3 as the key.  If cursor P1 refers
 * to an SQL index, then P3 is the first in an array of P4 registers
 * that are used as an unpacked index key.
 *
 * Reposition cursor P1 so that  it points to the smallest entry that
 * is greater than or equal to the key value. If there are no records
 * greater than or equal to the key and P2 is not zero, then jump to P2.
 *
 * If the cursor P1 was opened using the OPFLAG_SEEKEQ flag, then this
 * opcode will always land on a record that equally equals the key, or
 * else jump immediately to P2.  When the cursor is OPFLAG_SEEKEQ, this
 * opcode must be followed by an IdxLE opcode with the same arguments.
 * The IdxLE opcode will be skipped if this opcode succeeds, but the
 * IdxLE opcode will be used on subsequent loop iterations.
 *
 * This opcode leaves the cursor configured to move in forward order,
 * from the beginning toward the end.  In other words, the cursor is
 * configured to use Next, not Prev.
 *
 * If P5 is not zero, than it is offset of IPK in input vector. Force
 * corresponding value to be INTEGER.
 *
 * See also: Found, NotFound, SeekLt, SeekGt, SeekLe
 */
/* Opcode: SeekGT P1 P2 P3 P4 P5
 * Synopsis: key=r[P3@P4]
 *
 * If cursor P1 refers to an SQL table (B-Tree that uses integer keys),
 * use the value in register P3 as a key. If cursor P1 refers
 * to an SQL index, then P3 is the first in an array of P4 registers
 * that are used as an unpacked index key.
 *
 * Reposition cursor P1 so that  it points to the smallest entry that
 * is greater than the key value. If there are no records greater than
 * the key and P2 is not zero, then jump to P2.
 *
 * This opcode leaves the cursor configured to move in forward order,
 * from the beginning toward the end.  In other words, the cursor is
 * configured to use Next, not Prev.
 *
 * If P5 is not zero, than it is offset of IPK in input vector. Force
 * corresponding value to be INTEGER.
 *
 * See also: Found, NotFound, SeekLt, SeekGe, SeekLe
 */
/* Opcode: SeekLT P1 P2 P3 P4 P5
 * Synopsis: key=r[P3@P4]
 *
 * If cursor P1 refers to an SQL table (B-Tree that uses integer keys),
 * use the value in register P3 as a key. If cursor P1 refers
 * to an SQL index, then P3 is the first in an array of P4 registers
 * that are used as an unpacked index key.
 *
 * Reposition cursor P1 so that  it points to the largest entry that
 * is less than the key value. If there are no records less than
 * the key and P2 is not zero, then jump to P2.
 *
 * This opcode leaves the cursor configured to move in reverse order,
 * from the end toward the beginning.  In other words, the cursor is
 * configured to use Prev, not Next.
 *
 * If P5 is not zero, than it is offset of IPK in input vector. Force
 * corresponding value to be INTEGER.
 *
 * See also: Found, NotFound, SeekGt, SeekGe, SeekLe
 */
/* Opcode: SeekLE P1 P2 P3 P4 P5
 * Synopsis: key=r[P3@P4]
 *
 * If cursor P1 refers to an SQL table (B-Tree that uses integer keys),
 * use the value in register P3 as a key. If cursor P1 refers
 * to an SQL index, then P3 is the first in an array of P4 registers
 * that are used as an unpacked index key.
 *
 * Reposition cursor P1 so that it points to the largest entry that
 * is less than or equal to the key value. If there are no records
 * less than or equal to the key and P2 is not zero, then jump to P2.
 *
 * This opcode leaves the cursor configured to move in reverse order,
 * from the end toward the beginning.  In other words, the cursor is
 * configured to use Prev, not Next.
 *
 * If the cursor P1 was opened using the OPFLAG_SEEKEQ flag, then this
 * opcode will always land on a record that equally equals the key, or
 * else jump immediately to P2.  When the cursor is OPFLAG_SEEKEQ, this
 * opcode must be followed by an IdxGE opcode with the same arguments.
 * The IdxGE opcode will be skipped if this opcode succeeds, but the
 * IdxGE opcode will be used on subsequent loop iterations.
 *
 * See also: Found, NotFound, SeekGt, SeekGe, SeekLt
 */
case OP_SeekLT:         /* jump, in3 */
case OP_SeekLE:         /* jump, in3 */
case OP_SeekGE:         /* jump, in3 */
case OP_SeekGT: {       /* jump, in3 */
	int res;           /* Comparison result */
	int oc;            /* Opcode */
	VdbeCursor *pC;    /* The cursor to seek */
	UnpackedRecord r;  /* The key to seek for */
	int nField;        /* Number of columns or fields in the key */
	i64 iKey;          /* The id we are to seek to */
	int eqOnly;        /* Only interested in == results */
	int reg_ipk=0;     /* Register number which holds IPK. */

	assert(pOp->p1>=0 && pOp->p1<p->nCursor);
	assert(pOp->p2!=0);
	pC = p->apCsr[pOp->p1];
	assert(pC!=0);
	assert(pC->eCurType==CURTYPE_TARANTOOL);
	assert(OP_SeekLE == OP_SeekLT+1);
	assert(OP_SeekGE == OP_SeekLT+2);
	assert(OP_SeekGT == OP_SeekLT+3);
	assert(pC->uc.pCursor!=0);
	oc = pOp->opcode;
	eqOnly = 0;
	pC->nullRow = 0;
#ifdef SQLITE_DEBUG
	pC->seekOp = pOp->opcode;
#endif
	iKey = 0;
	reg_ipk = pOp->p5;

	if (reg_ipk > 0) {

		/* The input value in P3 might be of any type: integer, real, string,
		 * blob, or NULL.  But it needs to be an integer before we can do
		 * the seek, so convert it.
		 */
		pIn3 = &aMem[reg_ipk];
		if ((pIn3->flags & (MEM_Int|MEM_Real|MEM_Str))==MEM_Str) {
			applyNumericAffinity(pIn3, 0);
		}
		iKey = sqlite3VdbeIntValue(pIn3);

		/* If the P3 value could not be converted into an integer without
		 * loss of information, then special processing is required...
		 */
		if ((pIn3->flags & MEM_Int)==0) {
			if ((pIn3->flags & MEM_Real)==0) {
				/* If the P3 value cannot be converted into any kind of a number,
				 * then the seek is not possible, so jump to P2
				 */
				VdbeBranchTaken(1,2); goto jump_to_p2;
				break;
			}

			/* If the approximation iKey is larger than the actual real search
			 * term, substitute >= for > and < for <=. e.g. if the search term
			 * is 4.9 and the integer approximation 5:
			 *
			 *        (x >  4.9)    ->     (x >= 5)
			 *        (x <= 4.9)    ->     (x <  5)
			 */
			if (pIn3->u.r<(double)iKey) {
				assert(OP_SeekGE==(OP_SeekGT-1));
				assert(OP_SeekLT==(OP_SeekLE-1));
				assert((OP_SeekLE & 0x0001)==(OP_SeekGT & 0x0001));
				if ((oc & 0x0001)==(OP_SeekGT & 0x0001)) oc--;
			}

			/* If the approximation iKey is smaller than the actual real search
			 * term, substitute <= for < and > for >=.
			 */
			else if (pIn3->u.r>(double)iKey) {
				assert(OP_SeekLE==(OP_SeekLT+1));
				assert(OP_SeekGT==(OP_SeekGE+1));
				assert((OP_SeekLT & 0x0001)==(OP_SeekGE & 0x0001));
				if ((oc & 0x0001)==(OP_SeekLT & 0x0001)) oc++;
			}
		}
	}
	/* For a cursor with the BTREE_SEEK_EQ hint, only the OP_SeekGE and
	 * OP_SeekLE opcodes are allowed, and these must be immediately followed
	 * by an OP_IdxGT or OP_IdxLT opcode, respectively, with the same key.
	 */
	if (sqlite3CursorHasHint(pC->uc.pCursor, BTREE_SEEK_EQ)) {
		eqOnly = 1;
		assert(pOp->opcode==OP_SeekGE || pOp->opcode==OP_SeekLE);
		assert(pOp[1].opcode==OP_IdxLT || pOp[1].opcode==OP_IdxGT);
		assert(pOp[1].p1==pOp[0].p1);
		assert(pOp[1].p2==pOp[0].p2);
		assert(pOp[1].p3==pOp[0].p3);
		assert(pOp[1].p4.i==pOp[0].p4.i);
	}

	nField = pOp->p4.i;
	assert(pOp->p4type==P4_INT32);
	assert(nField>0);
	r.key_def = pC->key_def;
	r.nField = (u16)nField;

	if (reg_ipk > 0) {
		aMem[reg_ipk].u.i = iKey;
		aMem[reg_ipk].flags = MEM_Int;
	}

	r.default_rc = ((1 & (oc - OP_SeekLT)) ? -1 : +1);
	assert(oc!=OP_SeekGT || r.default_rc==-1);
	assert(oc!=OP_SeekLE || r.default_rc==-1);
	assert(oc!=OP_SeekGE || r.default_rc==+1);
	assert(oc!=OP_SeekLT || r.default_rc==+1);

	r.aMem = &aMem[pOp->p3];
#ifdef SQLITE_DEBUG
	{ int i; for(i=0; i<r.nField; i++) assert(memIsValid(&r.aMem[i])); }
#endif
	r.eqSeen = 0;
	r.opcode = oc;
	rc = sqlite3CursorMovetoUnpacked(pC->uc.pCursor, &r, &res);
	if (rc!=SQLITE_OK) {
		goto abort_due_to_error;
	}
	if (eqOnly && r.eqSeen==0) {
		assert(res!=0);
		goto seek_not_found;
	}
	pC->cacheStatus = CACHE_STALE;
#ifdef SQLITE_TEST
	sql_search_count++;
#endif
	if (oc>=OP_SeekGE) {  assert(oc==OP_SeekGE || oc==OP_SeekGT);
		if (res<0 || (res==0 && oc==OP_SeekGT)) {
			res = 0;
			rc = sqlite3CursorNext(pC->uc.pCursor, &res);
			if (rc!=SQLITE_OK) goto abort_due_to_error;
		} else {
			res = 0;
		}
	} else {
		assert(oc==OP_SeekLT || oc==OP_SeekLE);
		if (res>0 || (res==0 && oc==OP_SeekLT)) {
			res = 0;
			rc = sqlite3CursorPrevious(pC->uc.pCursor, &res);
			if (rc!=SQLITE_OK) goto abort_due_to_error;
		} else {
			/* res might be negative because the table is empty.  Check to
			 * see if this is the case.
			 */
			res = (CURSOR_VALID != pC->uc.pCursor->eState);
		}
	}
			seek_not_found:
	assert(pOp->p2>0);
	VdbeBranchTaken(res!=0,2);
	if (res) {
		goto jump_to_p2;
	} else if (eqOnly) {
		assert(pOp[1].opcode==OP_IdxLT || pOp[1].opcode==OP_IdxGT);
		pOp++; /* Skip the OP_IdxLt or OP_IdxGT that follows */
	}
	break;
}

/* Opcode: Found P1 P2 P3 P4 *
 * Synopsis: key=r[P3@P4]
 *
 * If P4==0 then register P3 holds a blob constructed by MakeRecord.  If
 * P4>0 then register P3 is the first of P4 registers that form an unpacked
 * record.
 *
 * Cursor P1 is on an index btree.  If the record identified by P3 and P4
 * is a prefix of any entry in P1 then a jump is made to P2 and
 * P1 is left pointing at the matching entry.
 *
 * This operation leaves the cursor in a state where it can be
 * advanced in the forward direction.  The Next instruction will work,
 * but not the Prev instruction.
 *
 * See also: NotFound, NoConflict, NotExists. SeekGe
 */
/* Opcode: NotFound P1 P2 P3 P4 *
 * Synopsis: key=r[P3@P4]
 *
 * If P4==0 then register P3 holds a blob constructed by MakeRecord.  If
 * P4>0 then register P3 is the first of P4 registers that form an unpacked
 * record.
 *
 * Cursor P1 is on an index btree.  If the record identified by P3 and P4
 * is not the prefix of any entry in P1 then a jump is made to P2.  If P1
 * does contain an entry whose prefix matches the P3/P4 record then control
 * falls through to the next instruction and P1 is left pointing at the
 * matching entry.
 *
 * This operation leaves the cursor in a state where it cannot be
 * advanced in either direction.  In other words, the Next and Prev
 * opcodes do not work after this operation.
 *
 * See also: Found, NotExists, NoConflict
 */
/* Opcode: NoConflict P1 P2 P3 P4 *
 * Synopsis: key=r[P3@P4]
 *
 * If P4==0 then register P3 holds a blob constructed by MakeRecord.  If
 * P4>0 then register P3 is the first of P4 registers that form an unpacked
 * record.
 *
 * Cursor P1 is on an index btree.  If the record identified by P3 and P4
 * contains any NULL value, jump immediately to P2.  If all terms of the
 * record are not-NULL then a check is done to determine if any row in the
 * P1 index btree has a matching key prefix.  If there are no matches, jump
 * immediately to P2.  If there is a match, fall through and leave the P1
 * cursor pointing to the matching row.
 *
 * This opcode is similar to OP_NotFound with the exceptions that the
 * branch is always taken if any part of the search key input is NULL.
 *
 * This operation leaves the cursor in a state where it cannot be
 * advanced in either direction.  In other words, the Next and Prev
 * opcodes do not work after this operation.
 *
 * See also: NotFound, Found, NotExists
 */
case OP_NoConflict:     /* jump, in3 */
case OP_NotFound:       /* jump, in3 */
case OP_Found: {        /* jump, in3 */
	int alreadyExists;
	int takeJump;
	int ii;
	VdbeCursor *pC;
	int res;
	UnpackedRecord *pFree;
	UnpackedRecord *pIdxKey;
	UnpackedRecord r;

#ifdef SQLITE_TEST
	if (pOp->opcode!=OP_NoConflict) sql_found_count++;
#endif

	assert(pOp->p1>=0 && pOp->p1<p->nCursor);
	assert(pOp->p4type==P4_INT32);
	pC = p->apCsr[pOp->p1];
	assert(pC!=0);
#ifdef SQLITE_DEBUG
	pC->seekOp = pOp->opcode;
#endif
	pIn3 = &aMem[pOp->p3];
	assert(pC->eCurType==CURTYPE_TARANTOOL);
	assert(pC->uc.pCursor!=0);
	if (pOp->p4.i>0) {
		r.key_def = pC->key_def;
		r.nField = (u16)pOp->p4.i;
		r.aMem = pIn3;
#ifdef SQLITE_DEBUG
		for(ii=0; ii<r.nField; ii++) {
			assert(memIsValid(&r.aMem[ii]));
			assert((r.aMem[ii].flags & MEM_Zero)==0 || r.aMem[ii].n==0);
			if (ii) REGISTER_TRACE(pOp->p3+ii, &r.aMem[ii]);
		}
#endif
		pIdxKey = &r;
		pFree = 0;
	} else {
		pFree = pIdxKey = sqlite3VdbeAllocUnpackedRecord(db, pC->key_def);
		if (pIdxKey==0) goto no_mem;
		assert(pIn3->flags & MEM_Blob );
		(void)ExpandBlob(pIn3);
		sqlite3VdbeRecordUnpackMsgpack(pC->key_def,
					       pIn3->z, pIdxKey);
	}
	pIdxKey->default_rc = 0;
	pIdxKey->opcode = pOp->opcode;
	takeJump = 0;
	if (pOp->opcode==OP_NoConflict) {
		/* For the OP_NoConflict opcode, take the jump if any of the
		 * input fields are NULL, since any key with a NULL will not
		 * conflict
		 */
		for(ii=0; ii<pIdxKey->nField; ii++) {
			if (pIdxKey->aMem[ii].flags & MEM_Null) {
				takeJump = 1;
				break;
			}
		}
	}
	rc = sqlite3CursorMovetoUnpacked(pC->uc.pCursor, pIdxKey, &res);
	if (pFree) sqlite3DbFree(db, pFree);
	if (rc!=SQLITE_OK) {
		goto abort_due_to_error;
	}
	pC->seekResult = res;
	alreadyExists = (res==0);
	pC->nullRow = 1-alreadyExists;
	pC->cacheStatus = CACHE_STALE;
	if (pOp->opcode==OP_Found) {
		VdbeBranchTaken(alreadyExists!=0,2);
		if (alreadyExists) goto jump_to_p2;
	} else {
		VdbeBranchTaken(takeJump||alreadyExists==0,2);
		if (takeJump || !alreadyExists) goto jump_to_p2;
	}
	break;
}

/* Opcode: Sequence P1 P2 * * *
 * Synopsis: r[P2]=cursor[P1].ctr++
 *
 * Find the next available sequence number for cursor P1.
 * Write the sequence number into register P2.
 * The sequence number on the cursor is incremented after this
 * instruction.
 */
case OP_Sequence: {           /* out2 */
	assert(pOp->p1>=0 && pOp->p1<p->nCursor);
	assert(p->apCsr[pOp->p1]!=0);
	pOut = out2Prerelease(p, pOp);
	pOut->u.i = p->apCsr[pOp->p1]->seqCount++;
	break;
}

/* Opcode: NextSequenceId * P2 * * *
 * Synopsis: r[P2]=get_max(_sequence)
 *
 * Get next Id of the _sequence space.
 * Return in P2 maximum id found in _sequence,
 * incremented by one.
 */
case OP_NextSequenceId: {
	pOut = &aMem[pOp->p2];
	tarantoolSqlNextSeqId((uint64_t *) &pOut->u.i);

	pOut->u.i += 1;
	pOut->flags = MEM_Int;
	break;
}

/* Opcode: NextIdEphemeral P1 P2 P3 * *
 * Synopsis: r[P3]=get_max(space_index[P1]{Column[P2]})
 *
 * This opcode works in the same way as OP_NextId does, except it is
 * only applied for ephemeral tables. The difference is in the fact that
 * all ephemeral tables don't have space_id (to be more precise it equals to zero).
 */
case OP_NextIdEphemeral: {
	VdbeCursor *pC;
	int p2;
	pC = p->apCsr[pOp->p1];
	p2 = pOp->p2;
	pOut = &aMem[pOp->p3];

	assert(pC->uc.pCursor->curFlags & BTCF_TEphemCursor);

	rc = tarantoolSqlite3EphemeralGetMaxId(pC->uc.pCursor, p2,
					       (uint64_t *) &pOut->u.i);
	if (rc) goto abort_due_to_error;

	pOut->u.i += 1;
	pOut->flags = MEM_Int;
	break;
}

/* Opcode: FCopy P1 P2 P3 * *
 * Synopsis: reg[P2@cur_frame]= reg[P1@root_frame(OPFLAG_SAME_FRAME)]
 *
 * Copy integer value of register P1 in root frame in to register P2 of current
 * frame. If current frame is topmost - copy within signle frame.
 * Source register must hold integer value.
 *
 * If P3's flag OPFLAG_SAME_FRAME is set, do shallow copy of register within
 * same frame, still making sure the value is integer.
 *
 * If P3's flag OPFLAG_NOOP_IF_NULL is set, then do nothing if reg[P1] is NULL
 */
case OP_FCopy: {     /* out2 */
	VdbeFrame *pFrame;
	Mem *pIn1, *pOut;
	if (p->pFrame && ((pOp->p3 & OPFLAG_SAME_FRAME) == 0)) {
		for(pFrame=p->pFrame; pFrame->pParent; pFrame=pFrame->pParent);
		pIn1 = &pFrame->aMem[pOp->p1];
	} else {
		pIn1 = &aMem[pOp->p1];
	}

	if ((pOp->p3 & OPFLAG_NOOP_IF_NULL) && (pIn1->flags & MEM_Null)) {
		pOut = &aMem[pOp->p2];
		if (pOut->flags & MEM_Undefined) pOut->flags = MEM_Null;
		/* Flag is set and register is NULL -> do nothing  */
	} else {
		assert(memIsValid(pIn1));
		assert(pIn1->flags &  MEM_Int);

		pOut = &aMem[pOp->p2];
		MemSetTypeFlag(pOut, MEM_Int);

		pOut->u.i = pIn1->u.i;
	}
	break;
}

/* Opcode: Delete P1 P2 P3 P4 P5
 *
 * Delete the record at which the P1 cursor is currently pointing.
 *
 * If the OPFLAG_SAVEPOSITION bit of the P5 parameter is set, then
 * the cursor will be left pointing at  either the next or the previous
 * record in the table. If it is left pointing at the next record, then
 * the next Next instruction will be a no-op. As a result, in this case
 * it is ok to delete a record from within a Next loop. If
 * OPFLAG_SAVEPOSITION bit of P5 is clear, then the cursor will be
 * left in an undefined state.
 *
 * If the OPFLAG_AUXDELETE bit is set on P5, that indicates that this
 * delete one of several associated with deleting a table row and all its
 * associated index entries.  Exactly one of those deletes is the "primary"
 * delete.  The others are all on OPFLAG_FORDELETE cursors or else are
 * marked with the AUXDELETE flag.
 *
 * If the OPFLAG_NCHANGE flag of P2 (NB: P2 not P5) is set, then the row
 * change count is incremented (otherwise not).
 *
 * P1 must not be pseudo-table.  It has to be a real table with
 * multiple rows.
 *
 * If P4 is not NULL then it points to a Table object. In this case either
 * the update or pre-update hook, or both, may be invoked. The P1 cursor must
 * have been positioned using OP_NotFound prior to invoking this opcode in
 * this case. Specifically, if one is configured, the pre-update hook is
 * invoked if P4 is not NULL. The update-hook is invoked if one is configured,
 * P4 is not NULL, and the OPFLAG_NCHANGE flag is set in P2.
 */
case OP_Delete: {
	VdbeCursor *pC;
	int opflags;

	opflags = pOp->p2;
	assert(pOp->p1>=0 && pOp->p1<p->nCursor);
	pC = p->apCsr[pOp->p1];
	BtCursor *pBtCur = pC->uc.pCursor;
	assert(pC!=0);
	assert(pC->eCurType==CURTYPE_TARANTOOL);
	assert(pC->uc.pCursor!=0);
	assert(pBtCur->eState == CURSOR_VALID);

	if (pBtCur->curFlags & BTCF_TaCursor) {
		rc = tarantoolSqlite3Delete(pBtCur, 0);
	} else if (pBtCur->curFlags & BTCF_TEphemCursor) {
		rc = tarantoolSqlite3EphemeralDelete(pBtCur);
	} else {
		unreachable();
	}
	pC->cacheStatus = CACHE_STALE;
	pC->seekResult = 0;
	if (rc) goto abort_due_to_error;

	if (opflags & OPFLAG_NCHANGE)
		p->nChange++;

	break;
}
/* Opcode: ResetCount * * * * *
 *
 * The value of the change counter is copied to the database handle
 * change counter (returned by subsequent calls to sqlite3_changes()).
 * Then the VMs internal change counter resets to 0.
 * This is used by trigger programs.
 */
case OP_ResetCount: {
	sqlite3VdbeSetChanges(db, p->nChange);
	p->nChange = 0;
	p->ignoreRaised = 0;
	break;
}

/* Opcode: SorterCompare P1 P2 P3 P4
 * Synopsis: if key(P1)!=trim(r[P3],P4) goto P2
 *
 * P1 is a sorter cursor. This instruction compares a prefix of the
 * record blob in register P3 against a prefix of the entry that
 * the sorter cursor currently points to.  Only the first P4 fields
 * of r[P3] and the sorter record are compared.
 *
 * If either P3 or the sorter contains a NULL in one of their significant
 * fields (not counting the P4 fields at the end which are ignored) then
 * the comparison is assumed to be equal.
 *
 * Fall through to next instruction if the two records compare equal to
 * each other.  Jump to P2 if they are different.
 */
case OP_SorterCompare: {
			VdbeCursor *pC;
			int res;
			int nKeyCol;

			pC = p->apCsr[pOp->p1];
			assert(isSorter(pC));
			assert(pOp->p4type==P4_INT32);
			pIn3 = &aMem[pOp->p3];
			nKeyCol = pOp->p4.i;
			res = 0;
			rc = sqlite3VdbeSorterCompare(pC, pIn3, nKeyCol, &res);
			VdbeBranchTaken(res!=0,2);
			if (rc) goto abort_due_to_error;
			if (res) goto jump_to_p2;
			break;
		};

/* Opcode: SorterData P1 P2 P3 * *
 * Synopsis: r[P2]=data
 *
 * Write into register P2 the current sorter data for sorter cursor P1.
 * Then clear the column header cache on cursor P3.
 *
 * This opcode is normally use to move a record out of the sorter and into
 * a register that is the source for a pseudo-table cursor created using
 * OpenPseudo.  That pseudo-table cursor is the one that is identified by
 * parameter P3.  Clearing the P3 column cache as part of this opcode saves
 * us from having to issue a separate NullRow instruction to clear that cache.
 */
case OP_SorterData: {
	VdbeCursor *pC;

	pOut = &aMem[pOp->p2];
	pC = p->apCsr[pOp->p1];
	assert(isSorter(pC));
	rc = sqlite3VdbeSorterRowkey(pC, pOut);
	assert(rc!=SQLITE_OK || (pOut->flags & MEM_Blob));
	assert(pOp->p1>=0 && pOp->p1<p->nCursor);
	if (rc) goto abort_due_to_error;
	p->apCsr[pOp->p3]->cacheStatus = CACHE_STALE;
	break;
}

/* Opcode: RowData P1 P2 * * *
 * Synopsis: r[P2]=data
 *
 * Write into register P2 the complete row content for the row at
 * which cursor P1 is currently pointing.
 * There is no interpretation of the data.
 * It is just copied onto the P2 register exactly as
 * it is found in the database file.
 *
 * If cursor P1 is an index, then the content is the key of the row.
 * If cursor P2 is a table, then the content extracted is the data.
 *
 * If the P1 cursor must be pointing to a valid row (not a NULL row)
 * of a real table, not a pseudo-table.
 */
case OP_RowData: {
	VdbeCursor *pC;
	BtCursor *pCrsr;
	u32 n;

	pOut = &aMem[pOp->p2];
	memAboutToChange(p, pOut);

	assert(pOp->p1>=0 && pOp->p1<p->nCursor);
	pC = p->apCsr[pOp->p1];
	assert(pC!=0);
	assert(pC->eCurType==CURTYPE_TARANTOOL);
	assert(isSorter(pC)==0);
	assert(pC->nullRow==0);
	assert(pC->uc.pCursor!=0);
	pCrsr = pC->uc.pCursor;

	/* The OP_RowData opcodes always follow
	 * OP_Rewind/Op_Next with no intervening instructions
	 * that might invalidate the cursor.
	 * If this where not the case, on of the following assert()s
	 * would fail.
	 */
	assert(sqlite3CursorIsValid(pCrsr));
	assert(pCrsr->eState == CURSOR_VALID);
	assert(pCrsr->curFlags & BTCF_TaCursor ||
	       pCrsr->curFlags & BTCF_TEphemCursor);
	tarantoolSqlite3PayloadFetch(pCrsr, &n);
	if (n>(u32)db->aLimit[SQLITE_LIMIT_LENGTH]) {
		goto too_big;
	}
	testcase( n==0);

	sqlite3VdbeMemRelease(pOut);
	rc = sql_vdbe_mem_alloc_region(pOut, n);
	if (rc)
		goto no_mem;
	rc = sqlite3CursorPayload(pCrsr, 0, n, pOut->z);
	if (rc) goto abort_due_to_error;
	UPDATE_MAX_BLOBSIZE(pOut);
	REGISTER_TRACE(pOp->p2, pOut);
	break;
}

/* Opcode: NullRow P1 * * * *
 *
 * Move the cursor P1 to a null row.  Any OP_Column operations
 * that occur while the cursor is on the null row will always
 * write a NULL.
 */
case OP_NullRow: {
	VdbeCursor *pC;

	assert(pOp->p1>=0 && pOp->p1<p->nCursor);
	pC = p->apCsr[pOp->p1];
	assert(pC!=0);
	pC->nullRow = 1;
	pC->cacheStatus = CACHE_STALE;
	if (pC->eCurType==CURTYPE_TARANTOOL) {
		assert(pC->uc.pCursor!=0);
		sql_cursor_cleanup(pC->uc.pCursor);
	}
	break;
}

/* Opcode: Last P1 P2 P3 * *
 *
 * The next use of the Column or Prev instruction for P1
 * will refer to the last entry in the database table or index.
 * If the table or index is empty and P2>0, then jump immediately to P2.
 * If P2 is 0 or if the table or index is not empty, fall through
 * to the following instruction.
 *
 * This opcode leaves the cursor configured to move in reverse order,
 * from the end toward the beginning.  In other words, the cursor is
 * configured to use Prev, not Next.
 *
 * If P3 is -1, then the cursor is positioned at the end of the btree
 * for the purpose of appending a new entry onto the btree.  In that
 * case P2 must be 0.  It is assumed that the cursor is used only for
 * appending and so if the cursor is valid, then the cursor must already
 * be pointing at the end of the btree and so no changes are made to
 * the cursor.
 */
case OP_Last: {        /* jump */
	VdbeCursor *pC;
	BtCursor *pCrsr;
	int res;

	assert(pOp->p1>=0 && pOp->p1<p->nCursor);
	pC = p->apCsr[pOp->p1];
	assert(pC!=0);
	assert(pC->eCurType==CURTYPE_TARANTOOL);
	pCrsr = pC->uc.pCursor;
	res = 0;
	assert(pCrsr!=0);
	pC->seekResult = pOp->p3;
#ifdef SQLITE_DEBUG
	pC->seekOp = OP_Last;
#endif
	if (pOp->p3==0 || !sqlite3CursorIsValidNN(pCrsr)) {
		rc = tarantoolSqlite3Last(pCrsr, &res);
		pC->nullRow = (u8)res;
		pC->cacheStatus = CACHE_STALE;
		if (rc) goto abort_due_to_error;
		if (pOp->p2>0) {
			VdbeBranchTaken(res!=0,2);
			if (res) goto jump_to_p2;
		}
	} else {
		assert(pOp->p2==0);
	}
	break;
}


/* Opcode: SorterSort P1 P2 * * *
 *
 * After all records have been inserted into the Sorter object
 * identified by P1, invoke this opcode to actually do the sorting.
 * Jump to P2 if there are no records to be sorted.
 *
 * This opcode is an alias for OP_Sort and OP_Rewind that is used
 * for Sorter objects.
 */
/* Opcode: Sort P1 P2 * * *
 *
 * This opcode does exactly the same thing as OP_Rewind except that
 * it increments an undocumented global variable used for testing.
 *
 * Sorting is accomplished by writing records into a sorting index,
 * then rewinding that index and playing it back from beginning to
 * end.  We use the OP_Sort opcode instead of OP_Rewind to do the
 * rewinding so that the global variable will be incremented and
 * regression tests can determine whether or not the optimizer is
 * correctly optimizing out sorts.
 */
case OP_SorterSort:    /* jump */
case OP_Sort: {        /* jump */
#ifdef SQLITE_TEST
			sql_sort_count++;
			sql_search_count--;
#endif
			p->aCounter[SQLITE_STMTSTATUS_SORT]++;
			/* Fall through into OP_Rewind */
			FALLTHROUGH;
		}
/* Opcode: Rewind P1 P2 * * *
 *
 * The next use of the Column or Next instruction for P1
 * will refer to the first entry in the database table or index.
 * If the table or index is empty, jump immediately to P2.
 * If the table or index is not empty, fall through to the following
 * instruction.
 *
 * This opcode leaves the cursor configured to move in forward order,
 * from the beginning toward the end.  In other words, the cursor is
 * configured to use Next, not Prev.
 */
case OP_Rewind: {        /* jump */
	VdbeCursor *pC;
	BtCursor *pCrsr;
	int res;

	assert(pOp->p1>=0 && pOp->p1<p->nCursor);
	pC = p->apCsr[pOp->p1];
	assert(pC!=0);
	assert(isSorter(pC)==(pOp->opcode==OP_SorterSort));
	res = 1;
#ifdef SQLITE_DEBUG
	pC->seekOp = OP_Rewind;
#endif
	if (isSorter(pC)) {
		rc = sqlite3VdbeSorterRewind(pC, &res);
	} else {
		assert(pC->eCurType==CURTYPE_TARANTOOL);
		pCrsr = pC->uc.pCursor;
		assert(pCrsr);
		rc = tarantoolSqlite3First(pCrsr, &res);
		pC->cacheStatus = CACHE_STALE;
	}
	if (rc) goto abort_due_to_error;
	pC->nullRow = (u8)res;
	assert(pOp->p2>0 && pOp->p2<p->nOp);
	VdbeBranchTaken(res!=0,2);
	if (res) goto jump_to_p2;
	break;
}

/* Opcode: Next P1 P2 P3 P4 P5
 *
 * Advance cursor P1 so that it points to the next key/data pair in its
 * table or index.  If there are no more key/value pairs then fall through
 * to the following instruction.  But if the cursor advance was successful,
 * jump immediately to P2.
 *
 * The Next opcode is only valid following an SeekGT, SeekGE, or
 * OP_Rewind opcode used to position the cursor.  Next is not allowed
 * to follow SeekLT, SeekLE, or OP_Last.
 *
 * The P1 cursor must be for a real table, not a pseudo-table.  P1 must have
 * been opened prior to this opcode or the program will segfault.
 *
 * The P3 value is a hint to the btree implementation. If P3==1, that
 * means P1 is an SQL index and that this instruction could have been
 * omitted if that index had been unique.  P3 is usually 0.  P3 is
 * always either 0 or 1.
 *
 * P4 is always of type P4_ADVANCE. The function pointer points to
 * sqlite3BtreeNext().
 *
 * If P5 is positive and the jump is taken, then event counter
 * number P5-1 in the prepared statement is incremented.
 *
 * See also: Prev, NextIfOpen
 */
/* Opcode: NextIfOpen P1 P2 P3 P4 P5
 *
 * This opcode works just like Next except that if cursor P1 is not
 * open it behaves a no-op.
 */
/* Opcode: Prev P1 P2 P3 P4 P5
 *
 * Back up cursor P1 so that it points to the previous key/data pair in its
 * table or index.  If there is no previous key/value pairs then fall through
 * to the following instruction.
 *
 * The Prev opcode is only valid following an SeekLT, SeekLE, or
 * OP_Last opcode used to position the cursor.  Prev is not allowed
 * to follow SeekGT, SeekGE, or OP_Rewind.
 *
 * The P1 cursor must be for a real table, not a pseudo-table.  If P1 is
 * not open then the behavior is undefined.
 *
 * The P3 value is a hint to the btree implementation. If P3==1, that
 * means P1 is an SQL index and that this instruction could have been
 * omitted if that index had been unique.  P3 is usually 0.  P3 is
 * always either 0 or 1.
 *
 * P4 is always of type P4_ADVANCE. The function pointer points to
 * sqlite3BtreePrevious().
 *
 * If P5 is positive and the jump is taken, then event counter
 * number P5-1 in the prepared statement is incremented.
 */
/* Opcode: PrevIfOpen P1 P2 P3 P4 P5
 *
 * This opcode works just like Prev except that if cursor P1 is not
 * open it behaves a no-op.
 */
/* Opcode: SorterNext P1 P2 * * P5
 *
 * This opcode works just like OP_Next except that P1 must be a
 * sorter object for which the OP_SorterSort opcode has been
 * invoked.  This opcode advances the cursor to the next sorted
 * record, or jumps to P2 if there are no more sorted records.
 */
case OP_SorterNext: {  /* jump */
	VdbeCursor *pC;
	int res;

	pC = p->apCsr[pOp->p1];
	assert(isSorter(pC));
	res = 0;
	rc = sqlite3VdbeSorterNext(db, pC, &res);
	goto next_tail;
case OP_PrevIfOpen:    /* jump */
case OP_NextIfOpen:    /* jump */
	if (p->apCsr[pOp->p1]==0) break;
	/* Fall through */
case OP_Prev:          /* jump */
case OP_Next:          /* jump */
	assert(pOp->p1>=0 && pOp->p1<p->nCursor);
	assert(pOp->p5<ArraySize(p->aCounter));
	pC = p->apCsr[pOp->p1];
	res = pOp->p3;
	assert(pC!=0);
	assert(pC->eCurType==CURTYPE_TARANTOOL);
	assert(res==0 || res==1);
	testcase( res==1);
	assert(pOp->opcode!=OP_Next || pOp->p4.xAdvance == sqlite3CursorNext);
	assert(pOp->opcode!=OP_Prev || pOp->p4.xAdvance == sqlite3CursorPrevious);
	assert(pOp->opcode!=OP_NextIfOpen || pOp->p4.xAdvance == sqlite3CursorNext);
	assert(pOp->opcode!=OP_PrevIfOpen || pOp->p4.xAdvance == sqlite3CursorPrevious);

	/* The Next opcode is only used after SeekGT, SeekGE, and Rewind.
	 * The Prev opcode is only used after SeekLT, SeekLE, and Last.
	 */
	assert(pOp->opcode!=OP_Next || pOp->opcode!=OP_NextIfOpen
	       || pC->seekOp==OP_SeekGT || pC->seekOp==OP_SeekGE
	       || pC->seekOp==OP_Rewind || pC->seekOp==OP_Found);
	assert(pOp->opcode!=OP_Prev || pOp->opcode!=OP_PrevIfOpen
	       || pC->seekOp==OP_SeekLT || pC->seekOp==OP_SeekLE
	       || pC->seekOp==OP_Last);

	rc = pOp->p4.xAdvance(pC->uc.pCursor, &res);
			next_tail:
	pC->cacheStatus = CACHE_STALE;
	VdbeBranchTaken(res==0,2);
	if (rc) goto abort_due_to_error;
	if (res==0) {
		pC->nullRow = 0;
		p->aCounter[pOp->p5]++;
#ifdef SQLITE_TEST
		sql_search_count++;
#endif
		goto jump_to_p2_and_check_for_interrupt;
	} else {
		pC->nullRow = 1;
	}
	goto check_for_interrupt;
}

/* Opcode: IdxInsert P1 P2 * * P5
 * Synopsis: key=r[P2]
 *
 * @param P1 Index of a space cursor.
 * @param P2 Index of a register with MessagePack data to insert.
 * @param P5 Flags. If P5 contains OPFLAG_NCHANGE, then VDBE
 *        accounts the change in a case of successful insertion in
 *        nChange counter.
 */
/* Opcode: IdxReplace P1 P2 * * P5
 * Synopsis: key=r[P2]
 *
 * This opcode works exactly as IdxInsert does, but in Tarantool
 * internals it invokes box_replace() instead of box_insert().
 */
/* Opcode: SorterInsert P1 P2 * * *
 * Synopsis: key=r[P2]
 *
 * Register P2 holds an SQL index key made using the
 * MakeRecord instructions.  This opcode writes that key
 * into the sorter P1.  Data for the entry is nil.
 */
case OP_SorterInsert:       /* in2 */
case OP_IdxReplace:
case OP_IdxInsert: {        /* in2 */
	VdbeCursor *pC;

	assert(pOp->p1>=0 && pOp->p1<p->nCursor);
	pC = p->apCsr[pOp->p1];
	assert(pC!=0);
	assert(isSorter(pC)==(pOp->opcode==OP_SorterInsert));
	pIn2 = &aMem[pOp->p2];
	assert(pIn2->flags & MEM_Blob);
	if (pOp->p5 & OPFLAG_NCHANGE) p->nChange++;
	assert(pC->eCurType==CURTYPE_TARANTOOL || pOp->opcode==OP_SorterInsert);
	rc = ExpandBlob(pIn2);
	if (rc) goto abort_due_to_error;
	if (pOp->opcode==OP_SorterInsert) {
		rc = sqlite3VdbeSorterWrite(pC, pIn2);
	} else {
		BtCursor *pBtCur = pC->uc.pCursor;
		if (pBtCur->curFlags & BTCF_TaCursor) {
			/* Make sure that memory has been allocated on region. */
			assert(aMem[pOp->p2].flags & MEM_Ephem);
			if (pOp->opcode == OP_IdxInsert)
				rc = tarantoolSqlite3Insert(pBtCur->space,
							    pIn2->z,
							    pIn2->z + pIn2->n);
			else
				rc = tarantoolSqlite3Replace(pBtCur->space,
							     pIn2->z,
							     pIn2->z + pIn2->n);
		} else if (pBtCur->curFlags & BTCF_TEphemCursor) {
			rc = tarantoolSqlite3EphemeralInsert(pBtCur->space,
							     pIn2->z,
							     pIn2->z + pIn2->n);
		} else {
			unreachable();
		}
		pC->cacheStatus = CACHE_STALE;
	}

	if (pOp->p5 & OPFLAG_OE_IGNORE) {
		/* Ignore any kind of failes and do not raise error message */
		rc = SQLITE_OK;
		/* If we are in trigger, increment ignore raised counter */
		if (p->pFrame) {
			p->ignoreRaised++;
		}
	} else if (pOp->p5 & OPFLAG_OE_FAIL) {
		p->errorAction = ON_CONFLICT_ACTION_FAIL;
	}

	assert(p->errorAction == ON_CONFLICT_ACTION_ABORT ||
	       p->errorAction == ON_CONFLICT_ACTION_FAIL);
	if (rc) goto abort_due_to_error;
	break;
}

/* Opcode: SInsert P1 P2 * * P5
 * Synopsis: space id = P1, key = r[P2]
 *
 * This opcode is used only during DDL routine.
 * In contrast to ordinary insertion, insertion to system spaces
 * such as _space or _index will lead to schema changes.
 * Thus, usage of space pointers is going to be impossible,
 * as far as pointers can be expired since compilation time.
 *
 * If P5 is set to OPFLAG_NCHANGE, account overall changes
 * made to database.
 */
case OP_SInsert: {
	assert(pOp->p1 > 0);
	assert(pOp->p2 >= 0);

	pIn2 = &aMem[pOp->p2];
	struct space *space = space_by_id(pOp->p1);
	assert(space != NULL);
	assert(space_is_system(space));
	rc = tarantoolSqlite3Insert(space, pIn2->z, pIn2->z + pIn2->n);
	if (rc)
		goto abort_due_to_error;
	if (pOp->p5 & OPFLAG_NCHANGE)
		p->nChange++;
	break;
}

/* Opcode: SDelete P1 P2 * * P5
 * Synopsis: space id = P1, key = r[P2]
 *
 * This opcode is used only during DDL routine.
 * Delete entry with given key from system space.
 *
 * If P5 is set to OPFLAG_NCHANGE, account overall changes
 * made to database.
 */
case OP_SDelete: {
	assert(pOp->p1 > 0);
	assert(pOp->p2 >= 0);

	pIn2 = &aMem[pOp->p2];
	struct space *space = space_by_id(pOp->p1);
	assert(space != NULL);
	assert(space_is_system(space));
	rc = sql_delete_by_key(space, pIn2->z, pIn2->n);
	if (rc)
		goto abort_due_to_error;
	if (pOp->p5 & OPFLAG_NCHANGE)
		p->nChange++;
	break;
}

/* Opcode: SIDtoPtr P1 P2 * * *
 * Synopsis: space id = P1, space[out] = r[P2]
 *
 * This opcode makes look up by space id and save found space
 * into register, specified by the content of register P2.
 * Such trick is needed during DLL routine, since schema may
 * change and pointers become expired.
 */
case OP_SIDtoPtr: {
	assert(pOp->p1 > 0);
	assert(pOp->p2 >= 0);

	pIn2 = out2Prerelease(p, pOp);
	struct space *space = space_by_id(pOp->p1);
	assert(space != NULL);
	pIn2->u.p = (void *) space;
	pIn2->flags = MEM_Ptr;
	break;
}

/* Opcode: IdxDelete P1 P2 P3 * *
 * Synopsis: key=r[P2@P3]
 *
 * The content of P3 registers starting at register P2 form
 * an unpacked index key. This opcode removes that entry from the
 * index opened by cursor P1.
 */
case OP_IdxDelete: {
	VdbeCursor *pC;
	BtCursor *pCrsr;
	int res;
	UnpackedRecord r;

	assert(pOp->p3>0);
	assert(pOp->p2>0 && pOp->p2+pOp->p3<=(p->nMem+1 - p->nCursor)+1);
	assert(pOp->p1>=0 && pOp->p1<p->nCursor);
	pC = p->apCsr[pOp->p1];
	assert(pC!=0);
	assert(pC->eCurType==CURTYPE_TARANTOOL);
	pCrsr = pC->uc.pCursor;
	assert(pCrsr!=0);
	assert(pOp->p5==0);
	r.key_def = pC->key_def;
	r.nField = (u16)pOp->p3;
	r.default_rc = 0;
	r.aMem = &aMem[pOp->p2];
	r.opcode = OP_IdxDelete;
	rc = sqlite3CursorMovetoUnpacked(pCrsr, &r, &res);
	if (rc) goto abort_due_to_error;
	if (res==0) {
		assert(pCrsr->eState == CURSOR_VALID);
		if (pCrsr->curFlags & BTCF_TaCursor) {
			rc = tarantoolSqlite3Delete(pCrsr, 0);
		} else if (pCrsr->curFlags & BTCF_TEphemCursor) {
			rc = tarantoolSqlite3EphemeralDelete(pCrsr);
		} else {
			unreachable();
		}
		if (rc) goto abort_due_to_error;
	}
	pC->cacheStatus = CACHE_STALE;
	pC->seekResult = 0;
	break;
}

/* Opcode: IdxGE P1 P2 P3 P4 P5
 * Synopsis: key=r[P3@P4]
 *
 * The P4 register values beginning with P3 form an unpacked index
 * key that omits the PRIMARY KEY.  Compare this key value against the index
 * that P1 is currently pointing to, ignoring the PRIMARY KEY
 * fields at the end.
 *
 * If the P1 index entry is greater than or equal to the key value
 * then jump to P2.  Otherwise fall through to the next instruction.
 */
/* Opcode: IdxGT P1 P2 P3 P4 P5
 * Synopsis: key=r[P3@P4]
 *
 * The P4 register values beginning with P3 form an unpacked index
 * key that omits the PRIMARY KEY.  Compare this key value against the index
 * that P1 is currently pointing to, ignoring the PRIMARY KEY
 * fields at the end.
 *
 * If the P1 index entry is greater than the key value
 * then jump to P2.  Otherwise fall through to the next instruction.
 */
/* Opcode: IdxLT P1 P2 P3 P4 P5
 * Synopsis: key=r[P3@P4]
 *
 * The P4 register values beginning with P3 form an unpacked index
 * key that omits the PRIMARY KEY.  Compare this key value against
 * the index that P1 is currently pointing to, ignoring the PRIMARY KEY
 * on the P1 index.
 *
 * If the P1 index entry is less than the key value then jump to P2.
 * Otherwise fall through to the next instruction.
 */
/* Opcode: IdxLE P1 P2 P3 P4 P5
 * Synopsis: key=r[P3@P4]
 *
 * The P4 register values beginning with P3 form an unpacked index
 * key that omits the PRIMARY KEY.  Compare this key value against
 * the index that P1 is currently pointing to, ignoring the PRIMARY KEY
 * on the P1 index.
 *
 * If the P1 index entry is less than or equal to the key value then jump
 * to P2. Otherwise fall through to the next instruction.
 */
case OP_IdxLE:          /* jump */
case OP_IdxGT:          /* jump */
case OP_IdxLT:          /* jump */
case OP_IdxGE:  {       /* jump */
	VdbeCursor *pC;
	UnpackedRecord r;

	assert(pOp->p1>=0 && pOp->p1<p->nCursor);
	pC = p->apCsr[pOp->p1];
	assert(pC!=0);
	assert(pC->eCurType==CURTYPE_TARANTOOL);
	assert(pC->uc.pCursor!=0);
	assert(pOp->p5==0 || pOp->p5==1);
	assert(pOp->p4type==P4_INT32);
	r.key_def = pC->key_def;
	r.nField = (u16)pOp->p4.i;
	if (pOp->opcode<OP_IdxLT) {
		assert(pOp->opcode==OP_IdxLE || pOp->opcode==OP_IdxGT);
		r.default_rc = -1;
	} else {
		assert(pOp->opcode==OP_IdxGE || pOp->opcode==OP_IdxLT);
		r.default_rc = 0;
	}
	r.aMem = &aMem[pOp->p3];
#ifdef SQLITE_DEBUG
	{ int i; for(i=0; i<r.nField; i++) assert(memIsValid(&r.aMem[i])); }
#endif
	int res =  tarantoolSqlite3IdxKeyCompare(pC->uc.pCursor, &r);
	assert((OP_IdxLE&1)==(OP_IdxLT&1) && (OP_IdxGE&1)==(OP_IdxGT&1));
	if ((pOp->opcode&1)==(OP_IdxLT&1)) {
		assert(pOp->opcode==OP_IdxLE || pOp->opcode==OP_IdxLT);
		res = -res;
	} else {
		assert(pOp->opcode==OP_IdxGE || pOp->opcode==OP_IdxGT);
		res++;
	}
	VdbeBranchTaken(res>0,2);
	if (res>0) goto jump_to_p2;
	break;
}

/* Opcode: Clear P1 * * * *
 * Synopsis: space id = P1
 *
 * Delete all contents of the space, which space id is given
 * in P1 argument. It is worth mentioning, that clearing routine
 * doesn't involve truncating, since it features completely
 * different mechanism under hood.
 */
case OP_Clear: {
	assert(pOp->p1 > 0);
	uint32_t space_id = pOp->p1;
	struct space *space = space_by_id(space_id);
	assert(space != NULL);
	rc = tarantoolSqlite3ClearTable(space);
	if (rc) goto abort_due_to_error;
	break;
}

/* Opcode: ResetSorter P1 * * * *
 *
 * Delete all contents from the ephemeral table or sorter
 * that is open on cursor P1.
 *
 * This opcode only works for cursors used for sorting and
 * opened with OP_OpenEphemeral or OP_SorterOpen.
 */
case OP_ResetSorter: {
	VdbeCursor *pC;

	assert(pOp->p1>=0 && pOp->p1<p->nCursor);
	pC = p->apCsr[pOp->p1];
	assert(pC!=0);
	if (isSorter(pC)) {
		sqlite3VdbeSorterReset(db, pC->uc.pSorter);
	} else {
		assert(pC->eCurType==CURTYPE_TARANTOOL);
		assert(pC->uc.pCursor->curFlags & BTCF_TEphemCursor);
		rc = tarantoolSqlite3EphemeralClearTable(pC->uc.pCursor);
		if (rc) goto abort_due_to_error;
	}
	break;
}

/* Opcode: ParseSchema2 P1 P2 * * *
 * Synopsis: rows=r[P1@P2]
 *
 * For each 4-tuple from r[P1@P2] range convert to following
 * format and update the schema with the resulting entry:
 *  <name, pageno (which is hash(spaceId, indexId)), sql>
 */
case OP_ParseSchema2: {
	InitData initData;
	Mem *pRec, *pRecEnd;
	char *argv[4] = {NULL, NULL, NULL, NULL};


	assert(db->pSchema != NULL);

	initData.db = db;
	initData.pzErrMsg = &p->zErrMsg;

	assert(db->init.busy==0);
	db->init.busy = 1;
	initData.rc = SQLITE_OK;
	assert(!db->mallocFailed);

	pRec = &aMem[pOp->p1];
	pRecEnd = pRec + pOp->p2;

	/*
	 * A register range contains
	 *   name1, spaceId1, indexId1, sql1,
	 *   ...
	 *   nameN, spaceIdN, indexIdN, sqlN.
	 *
	 * Uppdate the schema.
	 */
	for( ; pRecEnd-pRec>=4 && initData.rc==SQLITE_OK; pRec+=4) {
		argv[0] = pRec[0].z;
		int pageNo = SQLITE_PAGENO_FROM_SPACEID_AND_INDEXID(pRec[1].u.i,
								    pRec[2].u.i);
		argv[1] = (char *)&pageNo;
		argv[2] = pRec[3].z;
		sqlite3InitCallback(&initData, 3, argv, NULL);
	}

	rc = initData.rc;
	db->init.busy = 0;

	if (rc) {
		sqlite3ResetAllSchemasOfConnection(db);
		if (rc==SQLITE_NOMEM) {
			goto no_mem;
		}
		goto abort_due_to_error;
	}
	break;
}

/* Opcode: ParseSchema3 P1 * * * *
 * Synopsis: name=r[P1] sql=r[P1+1]
 *
 * Create trigger named r[P1] w/ DDL SQL stored in r[P1+1]
 * in database P2
 */
case OP_ParseSchema3: {
	InitData initData;
	Mem *pRec;
	char zPgnoBuf[16];
	char *argv[4] = {NULL, zPgnoBuf, NULL, NULL};
	assert(db->pSchema != NULL);

	initData.db = db;
	initData.pzErrMsg = &p->zErrMsg;

	assert(db->init.busy==0);
	db->init.busy = 1;
	initData.rc = SQLITE_OK;
	assert(!db->mallocFailed);

	pRec = &aMem[pOp->p1];
	argv[0] = pRec[0].z;
	argv[1] = "0";
	argv[2] = pRec[1].z;
	sqlite3InitCallback(&initData, 3, argv, NULL);

	rc = initData.rc;
	db->init.busy = 0;

	if (rc) {
		sqlite3ResetAllSchemasOfConnection(db);
		if (rc==SQLITE_NOMEM) {
			goto no_mem;
		}
		goto abort_due_to_error;
	}
	break;
}

/* Opcode: RenameTable P1 * * P4 *
 * Synopsis: P1 = root, P4 = name
 *
 * Rename table P1 with name from P4.
 * Invoke tarantoolSqlite3RenameTable, which updates tuple with
 * corresponding space_id in _space: changes string of statement, which creates
 * table and its name. Removes hash of old table name and updates SQL schema
 * by calling sqlite3InitCallback.
 * In presence of triggers or foreign keys, their statements
 * are also updated in _trigger and in parent table.
 *
 */
case OP_RenameTable: {
	unsigned space_id;
	struct space *space;
	const char *zOldTableName;
	const char *zNewTableName;
	Table *pTab;
	FKey *pFKey;
	Trigger *pTrig;
	int iRootPage;
	InitData initData;
	char *argv[4] = {NULL, NULL, NULL, NULL};
	char *zSqlStmt;

	space_id = SQLITE_PAGENO_TO_SPACEID(pOp->p1);
	space = space_by_id(space_id);
	assert(space);
	zOldTableName = space_name(space);
	assert(zOldTableName);
	pTab = sqlite3HashFind(&db->pSchema->tblHash, zOldTableName);
	assert(pTab);
	pTrig = pTab->pTrigger;
	iRootPage = pTab->tnum;
	zNewTableName = pOp->p4.z;
	zOldTableName = sqlite3DbStrNDup(db, zOldTableName,
					 sqlite3Strlen30(zOldTableName));
	rc = tarantoolSqlite3RenameTable(pTab->tnum, zNewTableName,
					 &zSqlStmt);
	if (rc) goto abort_due_to_error;

	/* If it is parent table, all children statements should be updated. */
	for (pFKey = sqlite3FkReferences(pTab); pFKey; pFKey = pFKey->pNextTo) {
		assert(pFKey->zTo);
		assert(pFKey->pFrom);
		rc = tarantoolSqlite3RenameParentTable(pFKey->pFrom->tnum,
						       pFKey->zTo,
						       zNewTableName);
		if (rc) goto abort_due_to_error;
		pFKey->zTo = sqlite3DbStrNDup(db, zNewTableName,
					      sqlite3Strlen30(zNewTableName));
		sqlite3HashInsert(&db->pSchema->fkeyHash, zOldTableName, 0);
		sqlite3HashInsert(&db->pSchema->fkeyHash, zNewTableName, pFKey);
	}

	sqlite3UnlinkAndDeleteTable(db, pTab->def->name);

	initData.db = db;
	initData.pzErrMsg = &p->zErrMsg;
	assert(db->init.busy == 0);
	db->init.busy = 1;
	initData.rc = SQLITE_OK;
	argv[0] = (char*) zNewTableName;
	argv[1] = (char*) &iRootPage;
	argv[2] = zSqlStmt;
	sqlite3InitCallback(&initData, 3, argv, NULL);
	db->init.busy = 0;
	rc = initData.rc;
	if (rc) {
		sqlite3ResetAllSchemasOfConnection(db);
		goto abort_due_to_error;
	}

	pTab = sqlite3HashFind(&db->pSchema->tblHash, zNewTableName);
	pTab->pTrigger = pTrig;

	/* Rename all trigger created on this table.*/
	for (; pTrig; pTrig = pTrig->pNext) {
		sqlite3DbFree(db, pTrig->table);
		pTrig->table = sqlite3DbStrNDup(db, zNewTableName,
						sqlite3Strlen30(zNewTableName));
		pTrig->pTabSchema = pTab->pSchema;
		rc = tarantoolSqlite3RenameTrigger(pTrig->zName,
						   zOldTableName, zNewTableName);
		if (rc) goto abort_due_to_error;
	}
	sqlite3DbFree(db, (void*)zOldTableName);
	sqlite3DbFree(db, (void*)zSqlStmt);
	break;
}

/* Opcode: LoadAnalysis P1 * * * *
 *
 * Read the sql_stat1 table for database P1 and load the content
 * of that table into the internal index hash table.  This will cause
 * the analysis to be used when preparing all subsequent queries.
 */
case OP_LoadAnalysis: {
	assert(pOp->p1==0 );
	rc = sql_analysis_load(db);
	if (rc) goto abort_due_to_error;
	break;
}

/* Opcode: DropTable P1 * * P4 *
 *
 * Remove the internal (in-memory) data structures that describe
 * the table named P4 in database P1.  This is called after a table
 * is dropped from disk (using the Destroy opcode) in order to keep
 * the internal representation of the
 * schema consistent with what is on disk.
 */
case OP_DropTable: {
	sqlite3UnlinkAndDeleteTable(db, pOp->p4.z);
	break;
}

/* Opcode: DropIndex * * *  P4
 *
 * Remove the internal (in-memory) data structures that describe
 * the index named P4 for table.
 * This is called after an index is dropped from disk
 * (using the Destroy opcode) in order to keep
 * the internal representation of the schema consistent with what
 * is on disk.
 */
case OP_DropIndex: {
	sqlite3UnlinkAndDeleteIndex(db, pOp->p4.pIndex);
	break;
}

/* Opcode: DropTrigger P1 * * P4 *
 *
 * Remove the internal (in-memory) data structures that describe
 * the trigger named P4 in database P1.  This is called after a trigger
 * is dropped from disk (using the Destroy opcode) in order to keep
 * the internal representation of the
 * schema consistent with what is on disk.
 */
case OP_DropTrigger: {
	sqlite3UnlinkAndDeleteTrigger(db, pOp->p4.z);
	break;
}
#ifndef SQLITE_OMIT_TRIGGER

/* Opcode: Program P1 P2 P3 P4 P5
 *
 * Execute the trigger program passed as P4 (type P4_SUBPROGRAM).
 *
 * P1 contains the address of the memory cell that contains the first memory
 * cell in an array of values used as arguments to the sub-program. P2
 * contains the address to jump to if the sub-program throws an IGNORE
 * exception using the RAISE() function. Register P3 contains the address
 * of a memory cell in this (the parent) VM that is used to allocate the
 * memory required by the sub-vdbe at runtime.
 *
 * P4 is a pointer to the VM containing the trigger program.
 *
 * If P5 is non-zero, then recursive program invocation is enabled.
 */
case OP_Program: {        /* jump */
	int nMem;               /* Number of memory registers for sub-program */
	int nByte;              /* Bytes of runtime space required for sub-program */
	Mem *pRt;               /* Register to allocate runtime space */
	Mem *pMem;              /* Used to iterate through memory cells */
	Mem *pEnd;              /* Last memory cell in new array */
	VdbeFrame *pFrame;      /* New vdbe frame to execute in */
	SubProgram *pProgram;   /* Sub-program to execute */
	void *t;                /* Token identifying trigger */

	pProgram = pOp->p4.pProgram;
	pRt = &aMem[pOp->p3];
	assert(pProgram->nOp>0);

	/* If the p5 flag is clear, then recursive invocation of triggers is
	 * disabled for backwards compatibility (p5 is set if this sub-program
	 * is really a trigger, not a foreign key action, and the flag set
	 * and cleared by the "PRAGMA recursive_triggers" command is clear).
	 *
	 * It is recursive invocation of triggers, at the SQL level, that is
	 * disabled. In some cases a single trigger may generate more than one
	 * SubProgram (if the trigger may be executed with more than one different
	 * ON CONFLICT algorithm). SubProgram structures associated with a
	 * single trigger all have the same value for the SubProgram.token
	 * variable.
	 */
	if (pOp->p5) {
		t = pProgram->token;
		for(pFrame=p->pFrame; pFrame && pFrame->token!=t; pFrame=pFrame->pParent);
		if (pFrame) break;
	}

	if (p->ignoreRaised > 0) {
		break;
	}

	if (p->nFrame>=db->aLimit[SQLITE_LIMIT_TRIGGER_DEPTH]) {
		rc = SQLITE_ERROR;
		sqlite3VdbeError(p, "too many levels of trigger recursion");
		goto abort_due_to_error;
	}

	/* Register pRt is used to store the memory required to save the state
	 * of the current program, and the memory required at runtime to execute
	 * the trigger program. If this trigger has been fired before, then pRt
	 * is already allocated. Otherwise, it must be initialized.
	 */
	if ((pRt->flags&MEM_Frame)==0) {
		/* SubProgram.nMem is set to the number of memory cells used by the
		 * program stored in SubProgram.aOp. As well as these, one memory
		 * cell is required for each cursor used by the program. Set local
		 * variable nMem (and later, VdbeFrame.nChildMem) to this value.
		 */
		nMem = pProgram->nMem + pProgram->nCsr;
		assert(nMem>0);
		if (pProgram->nCsr==0) nMem++;
		nByte = ROUND8(sizeof(VdbeFrame))
			+ nMem * sizeof(Mem)
			+ pProgram->nCsr * sizeof(VdbeCursor *);
		pFrame = sqlite3DbMallocZero(db, nByte);
		if (!pFrame) {
			goto no_mem;
		}
		sqlite3VdbeMemRelease(pRt);
		pRt->flags = MEM_Frame;
		pRt->u.pFrame = pFrame;

		pFrame->v = p;
		pFrame->nChildMem = nMem;
		pFrame->nChildCsr = pProgram->nCsr;
		pFrame->pc = (int)(pOp - aOp);
		pFrame->aMem = p->aMem;
		pFrame->nMem = p->nMem;
		pFrame->apCsr = p->apCsr;
		pFrame->nCursor = p->nCursor;
		pFrame->aOp = p->aOp;
		pFrame->nOp = p->nOp;
		pFrame->token = pProgram->token;
#ifdef SQLITE_ENABLE_STMT_SCANSTATUS
		pFrame->anExec = p->anExec;
#endif

		pEnd = &VdbeFrameMem(pFrame)[pFrame->nChildMem];
		for(pMem=VdbeFrameMem(pFrame); pMem!=pEnd; pMem++) {
			pMem->flags = MEM_Undefined;
			pMem->db = db;
		}
	} else {
		pFrame = pRt->u.pFrame;
		assert(pProgram->nMem+pProgram->nCsr==pFrame->nChildMem
		       || (pProgram->nCsr==0 && pProgram->nMem+1==pFrame->nChildMem));
		assert(pProgram->nCsr==pFrame->nChildCsr);
		assert((int)(pOp - aOp)==pFrame->pc);
	}

	p->nFrame++;
	pFrame->pParent = p->pFrame;
	pFrame->nChange = p->nChange;
	pFrame->nDbChange = p->db->nChange;
	assert(pFrame->pAuxData==0);
	pFrame->pAuxData = p->pAuxData;
	p->pAuxData = 0;
	p->nChange = 0;
	p->pFrame = pFrame;
	p->aMem = aMem = VdbeFrameMem(pFrame);
	p->nMem = pFrame->nChildMem;
	p->nCursor = (u16)pFrame->nChildCsr;
	p->apCsr = (VdbeCursor **)&aMem[p->nMem];
	p->aOp = aOp = pProgram->aOp;
	p->nOp = pProgram->nOp;
#ifdef SQLITE_ENABLE_STMT_SCANSTATUS
	p->anExec = 0;
#endif
	pOp = &aOp[-1];

	break;
}

/* Opcode: Param P1 P2 * * *
 *
 * This opcode is only ever present in sub-programs called via the
 * OP_Program instruction. Copy a value currently stored in a memory
 * cell of the calling (parent) frame to cell P2 in the current frames
 * address space. This is used by trigger programs to access the new.*
 * and old.* values.
 *
 * The address of the cell in the parent frame is determined by adding
 * the value of the P1 argument to the value of the P1 argument to the
 * calling OP_Program instruction.
 */
case OP_Param: {           /* out2 */
	VdbeFrame *pFrame;
	Mem *pIn;
	pOut = out2Prerelease(p, pOp);
	pFrame = p->pFrame;
	pIn = &pFrame->aMem[pOp->p1 + pFrame->aOp[pFrame->pc].p1];
	sqlite3VdbeMemShallowCopy(pOut, pIn, MEM_Ephem);
	break;
}

#endif /* #ifndef SQLITE_OMIT_TRIGGER */

#ifndef SQLITE_OMIT_FOREIGN_KEY
/* Opcode: FkCounter P1 P2 * * *
 * Synopsis: fkctr[P1]+=P2
 *
 * Increment a "constraint counter" by P2 (P2 may be negative or positive).
 * If P1 is non-zero, the database constraint counter is incremented
 * (deferred foreign key constraints). Otherwise, if P1 is zero, the
 * statement counter is incremented (immediate foreign key constraints).
 */
case OP_FkCounter: {
	if ((user_session->sql_flags & SQLITE_DeferFKs || pOp->p1 != 0) &&
	    !p->auto_commit) {
		assert(p->psql_txn != NULL);
		p->psql_txn->fk_deferred_count += pOp->p2;
	} else {
		p->nFkConstraint += pOp->p2;
	}
	break;
}

/* Opcode: FkIfZero P1 P2 * * *
 * Synopsis: if fkctr[P1]==0 goto P2
 *
 * This opcode tests if a foreign key constraint-counter is currently zero.
 * If so, jump to instruction P2. Otherwise, fall through to the next
 * instruction.
 *
 * If P1 is non-zero, then the jump is taken if the database constraint-counter
 * is zero (the one that counts deferred constraint violations). If P1 is
 * zero, the jump is taken if the statement constraint-counter is zero
 * (immediate foreign key constraint violations).
 */
case OP_FkIfZero: {         /* jump */
	if ((user_session->sql_flags & SQLITE_DeferFKs || pOp->p1) &&
	    !p->auto_commit) {
		assert(p->psql_txn != NULL);
		if (p->psql_txn->fk_deferred_count == 0)
			goto jump_to_p2;
	} else {
		if (p->nFkConstraint == 0)
			goto jump_to_p2;
	}
	break;
}
#endif /* #ifndef SQLITE_OMIT_FOREIGN_KEY */

/* Opcode: IfPos P1 P2 P3 * *
 * Synopsis: if r[P1]>0 then r[P1]-=P3, goto P2
 *
 * Register P1 must contain an integer.
 * If the value of register P1 is 1 or greater, subtract P3 from the
 * value in P1 and jump to P2.
 *
 * If the initial value of register P1 is less than 1, then the
 * value is unchanged and control passes through to the next instruction.
 */
case OP_IfPos: {        /* jump, in1 */
	pIn1 = &aMem[pOp->p1];
	assert(pIn1->flags&MEM_Int);
	VdbeBranchTaken( pIn1->u.i>0, 2);
	if (pIn1->u.i>0) {
		pIn1->u.i -= pOp->p3;
		goto jump_to_p2;
	}
	break;
}

/* Opcode: OffsetLimit P1 P2 P3 * *
 * Synopsis: if r[P1]>0 then r[P2]=r[P1]+max(0,r[P3]) else r[P2]=(-1)
 *
 * This opcode performs a commonly used computation associated with
 * LIMIT and OFFSET process.  r[P1] holds the limit counter.  r[P3]
 * holds the offset counter.  The opcode computes the combined value
 * of the LIMIT and OFFSET and stores that value in r[P2].  The r[P2]
 * value computed is the total number of rows that will need to be
 * visited in order to complete the query.
 *
 * If r[P3] is zero or negative, that means there is no OFFSET
 * and r[P2] is set to be the value of the LIMIT, r[P1].
 *
 * if r[P1] is zero or negative, that means there is no LIMIT
 * and r[P2] is set to -1.
 *
 * Otherwise, r[P2] is set to the sum of r[P1] and r[P3].
 */
case OP_OffsetLimit: {    /* in1, out2, in3 */
	i64 x;
	pIn1 = &aMem[pOp->p1];
	pIn3 = &aMem[pOp->p3];
	pOut = out2Prerelease(p, pOp);
	assert(pIn1->flags & MEM_Int);
	assert(pIn3->flags & MEM_Int);
	x = pIn1->u.i;
	if (x<=0 || sqlite3AddInt64(&x, pIn3->u.i>0?pIn3->u.i:0)) {
		/* If the LIMIT is less than or equal to zero, loop forever.  This
		 * is documented.  But also, if the LIMIT+OFFSET exceeds 2^63 then
		 * also loop forever.  This is undocumented.  In fact, one could argue
		 * that the loop should terminate.  But assuming 1 billion iterations
		 * per second (far exceeding the capabilities of any current hardware)
		 * it would take nearly 300 years to actually reach the limit.  So
		 * looping forever is a reasonable approximation.
		 */
		pOut->u.i = -1;
	} else {
		pOut->u.i = x;
	}
	break;
}

/* Opcode: IfNotZero P1 P2 * * *
 * Synopsis: if r[P1]!=0 then r[P1]--, goto P2
 *
 * Register P1 must contain an integer.  If the content of register P1 is
 * initially greater than zero, then decrement the value in register P1.
 * If it is non-zero (negative or positive) and then also jump to P2.
 * If register P1 is initially zero, leave it unchanged and fall through.
 */
case OP_IfNotZero: {        /* jump, in1 */
	pIn1 = &aMem[pOp->p1];
	assert(pIn1->flags&MEM_Int);
	VdbeBranchTaken(pIn1->u.i<0, 2);
	if (pIn1->u.i) {
		if (pIn1->u.i>0) pIn1->u.i--;
		goto jump_to_p2;
	}
	break;
}

/* Opcode: DecrJumpZero P1 P2 * * *
 * Synopsis: if (--r[P1])==0 goto P2
 *
 * Register P1 must hold an integer.  Decrement the value in P1
 * and jump to P2 if the new value is exactly zero.
 */
case OP_DecrJumpZero: {      /* jump, in1 */
	pIn1 = &aMem[pOp->p1];
	assert(pIn1->flags&MEM_Int);
	if (pIn1->u.i>SMALLEST_INT64) pIn1->u.i--;
	VdbeBranchTaken(pIn1->u.i==0, 2);
	if (pIn1->u.i==0) goto jump_to_p2;
	break;
}


/* Opcode: AggStep0 * P2 P3 P4 P5
 * Synopsis: accum=r[P3] step(r[P2@P5])
 *
 * Execute the step function for an aggregate.  The
 * function has P5 arguments.   P4 is a pointer to the FuncDef
 * structure that specifies the function.  Register P3 is the
 * accumulator.
 *
 * The P5 arguments are taken from register P2 and its
 * successors.
 */
/* Opcode: AggStep * P2 P3 P4 P5
 * Synopsis: accum=r[P3] step(r[P2@P5])
 *
 * Execute the step function for an aggregate.  The
 * function has P5 arguments.   P4 is a pointer to an sqlite3_context
 * object that is used to run the function.  Register P3 is
 * as the accumulator.
 *
 * The P5 arguments are taken from register P2 and its
 * successors.
 *
 * This opcode is initially coded as OP_AggStep0.  On first evaluation,
 * the FuncDef stored in P4 is converted into an sqlite3_context and
 * the opcode is changed.  In this way, the initialization of the
 * sqlite3_context only happens once, instead of on each call to the
 * step function.
 */
case OP_AggStep0: {
	int n;
	sqlite3_context *pCtx;

	assert(pOp->p4type==P4_FUNCDEF);
	n = pOp->p5;
	assert(pOp->p3>0 && pOp->p3<=(p->nMem+1 - p->nCursor));
	assert(n==0 || (pOp->p2>0 && pOp->p2+n<=(p->nMem+1 - p->nCursor)+1));
	assert(pOp->p3<pOp->p2 || pOp->p3>=pOp->p2+n);
	pCtx = sqlite3DbMallocRawNN(db, sizeof(*pCtx) + (n-1)*sizeof(sqlite3_value*));
	if (pCtx==0) goto no_mem;
	pCtx->pMem = 0;
	pCtx->pFunc = pOp->p4.pFunc;
	pCtx->iOp = (int)(pOp - aOp);
	pCtx->pVdbe = p;
	pCtx->argc = n;
	pOp->p4type = P4_FUNCCTX;
	pOp->p4.pCtx = pCtx;
	pOp->opcode = OP_AggStep;
	/* Fall through into OP_AggStep */
	FALLTHROUGH;
}
case OP_AggStep: {
	int i;
	sqlite3_context *pCtx;
	Mem *pMem;
	Mem t;

	assert(pOp->p4type==P4_FUNCCTX);
	pCtx = pOp->p4.pCtx;
	pMem = &aMem[pOp->p3];

	/* If this function is inside of a trigger, the register array in aMem[]
	 * might change from one evaluation to the next.  The next block of code
	 * checks to see if the register array has changed, and if so it
	 * reinitializes the relavant parts of the sqlite3_context object
	 */
	if (pCtx->pMem != pMem) {
		pCtx->pMem = pMem;
		for(i=pCtx->argc-1; i>=0; i--) pCtx->argv[i] = &aMem[pOp->p2+i];
	}

#ifdef SQLITE_DEBUG
	for(i=0; i<pCtx->argc; i++) {
		assert(memIsValid(pCtx->argv[i]));
		REGISTER_TRACE(pOp->p2+i, pCtx->argv[i]);
	}
#endif

	pMem->n++;
	sqlite3VdbeMemInit(&t, db, MEM_Null);
	pCtx->pOut = &t;
	pCtx->fErrorOrAux = 0;
	pCtx->skipFlag = 0;
	(pCtx->pFunc->xSFunc)(pCtx,pCtx->argc,pCtx->argv); /* IMP: R-24505-23230 */
	if (pCtx->fErrorOrAux) {
		if (pCtx->isError) {
			sqlite3VdbeError(p, "%s", sqlite3_value_text(&t));
			rc = pCtx->isError;
		}
		sqlite3VdbeMemRelease(&t);
		if (rc) goto abort_due_to_error;
	} else {
		assert(t.flags==MEM_Null);
	}
	if (pCtx->skipFlag) {
		assert(pOp[-1].opcode==OP_CollSeq);
		i = pOp[-1].p1;
		if (i) sqlite3VdbeMemSetInt64(&aMem[i], 1);
	}
	break;
}

/* Opcode: AggFinal P1 P2 * P4 *
 * Synopsis: accum=r[P1] N=P2
 *
 * Execute the finalizer function for an aggregate.  P1 is
 * the memory location that is the accumulator for the aggregate.
 *
 * P2 is the number of arguments that the step function takes and
 * P4 is a pointer to the FuncDef for this function.  The P2
 * argument is not used by this opcode.  It is only there to disambiguate
 * functions that can take varying numbers of arguments.  The
 * P4 argument is only needed for the degenerate case where
 * the step function was not previously called.
 */
case OP_AggFinal: {
	Mem *pMem;
	assert(pOp->p1>0 && pOp->p1<=(p->nMem+1 - p->nCursor));
	pMem = &aMem[pOp->p1];
	assert((pMem->flags & ~(MEM_Null|MEM_Agg))==0);
	rc = sqlite3VdbeMemFinalize(pMem, pOp->p4.pFunc);
	if (rc) {
		sqlite3VdbeError(p, "%s", sqlite3_value_text(pMem));
		goto abort_due_to_error;
	}
	UPDATE_MAX_BLOBSIZE(pMem);
	if (sqlite3VdbeMemTooBig(pMem)) {
		goto too_big;
	}
	break;
}

/* Opcode: Expire P1 * * * *
 *
 * Cause precompiled statements to expire.  When an expired statement
 * is executed using sqlite3_step() it will either automatically
 * reprepare itself (if it was originally created using sqlite3_prepare_v2())
 * or it will fail with SQLITE_SCHEMA.
 *
 * If P1 is 0, then all SQL statements become expired. If P1 is non-zero,
 * then only the currently executing statement is expired.
 */
case OP_Expire: {
	if (!pOp->p1) {
		sqlite3ExpirePreparedStatements(db);
	} else {
		p->expired = 1;
	}
	break;
}

/* Opcode: Init P1 P2 * P4 *
 * Synopsis: Start at P2
 *
 * Programs contain a single instance of this opcode as the very first
 * opcode.
 *
 * If tracing is enabled (by the sqlite3_trace()) interface, then
 * the UTF-8 string contained in P4 is emitted on the trace callback.
 * Or if P4 is blank, use the string returned by sqlite3_sql().
 *
 * If P2 is not zero, jump to instruction P2.
 *
 * Increment the value of P1 so that OP_Once opcodes will jump the
 * first time they are evaluated for this run.
 */
case OP_Init: {          /* jump */
	char *zTrace;
	int i;

	/* If the P4 argument is not NULL, then it must be an SQL comment string.
	 * The "--" string is broken up to prevent false-positives with srcck1.c.
	 *
	 * This assert() provides evidence for:
	 * EVIDENCE-OF: R-50676-09860 The callback can compute the same text that
	 * would have been returned by the legacy sqlite3_trace() interface by
	 * using the X argument when X begins with "--" and invoking
	 * sqlite3_expanded_sql(P) otherwise.
	 */
	assert(pOp->p4.z==0 || strncmp(pOp->p4.z, "-" "- ", 3)==0);
	assert(pOp==p->aOp);  /* Always instruction 0 */
	/*
	 * Once per execution time prepare the program: detect
	 * autocommit, create SQL specific transaction things. To
	 * guarantee the single call of this function the
	 * preparation is done in the parent frame only. Child
	 * programs like triggers must use the information
	 * received from the parent.
	 */
	if (p->pFrame == NULL && sql_vdbe_prepare(p) != 0) {
		sqlite3DbFree(db, p);
		rc = SQL_TARANTOOL_ERROR;
		break;
	}

#ifndef SQLITE_OMIT_TRACE
	if ((db->mTrace & SQLITE_TRACE_STMT)!=0
	    && !p->doingRerun
	    && (zTrace = (pOp->p4.z ? pOp->p4.z : p->zSql))!=0
		) {
		{
			(void)db->xTrace(SQLITE_TRACE_STMT, db->pTraceArg, p, zTrace);
		}
	}
#ifdef SQLITE_DEBUG
	if ((user_session->sql_flags & SQLITE_SqlTrace)!=0
	    && (zTrace = (pOp->p4.z ? pOp->p4.z : p->zSql))!=0
		) {
		sqlite3DebugPrintf("SQL-trace: %s\n", zTrace);
	}
#endif /* SQLITE_DEBUG */
#endif /* SQLITE_OMIT_TRACE */
	assert(pOp->p2>0);
	if (pOp->p1>=sqlite3GlobalConfig.iOnceResetThreshold) {
		for(i=1; i<p->nOp; i++) {
			if (p->aOp[i].opcode==OP_Once) p->aOp[i].p1 = 0;
		}
		pOp->p1 = 0;
	}
	pOp->p1++;
	goto jump_to_p2;
}

/* Opcode: IncMaxid P1 * * * *
 *
 * Increment the max_id from _schema (max space id)
 * and store updated id in register specified by first operand.
 * It is system opcode and must be used only during DDL routine.
 */
case OP_IncMaxid: {
	assert(pOp->p1 > 0);
	pOut = &aMem[pOp->p1];

	rc = tarantoolSqlite3IncrementMaxid((uint64_t*) &pOut->u.i);
	if (rc!=SQLITE_OK) {
		goto abort_due_to_error;
	}
	pOut->flags = MEM_Int;
	break;
}

/* Opcode: Noop * * * * *
 *
 * Do nothing.  This instruction is often useful as a jump
 * destination.
 */
/*
 * The magic Explain opcode are only inserted when explain==2 (which
 * is to say when the EXPLAIN QUERY PLAN syntax is used.)
 * This opcode records information from the optimizer.  It is the
 * the same as a no-op.  This opcodesnever appears in a real VM program.
 */
default: {          /* This is really OP_Noop and OP_Explain */
	assert(pOp->opcode==OP_Noop || pOp->opcode==OP_Explain);
	break;
}

/*****************************************************************************
 * The cases of the switch statement above this line should all be indented
 * by 6 spaces.  But the left-most 6 spaces have been removed to improve the
 * readability.  From this point on down, the normal indentation rules are
 * restored.
 ****************************************************************************/
		}

#ifdef VDBE_PROFILE
		{
			u64 endTime = sqlite3Hwtime();
			if (endTime>start) pOrigOp->cycles += endTime - start;
			pOrigOp->cnt++;
		}
#endif

		/* The following code adds nothing to the actual functionality
		 * of the program.  It is only here for testing and debugging.
		 * On the other hand, it does burn CPU cycles every time through
		 * the evaluator loop.  So we can leave it out when NDEBUG is defined.
		 */
#ifndef NDEBUG
		assert(pOp>=&aOp[-1] && pOp<&aOp[p->nOp-1]);

#ifdef SQLITE_DEBUG
		if (user_session->sql_flags & SQLITE_VdbeTrace) {
			u8 opProperty = sqlite3OpcodeProperty[pOrigOp->opcode];
			if (rc!=0) printf("rc=%d\n",rc);
			if (opProperty & (OPFLG_OUT2)) {
				registerTrace(pOrigOp->p2, &aMem[pOrigOp->p2]);
			}
			if (opProperty & OPFLG_OUT3) {
				registerTrace(pOrigOp->p3, &aMem[pOrigOp->p3]);
			}
		}
#endif  /* SQLITE_DEBUG */
#endif  /* NDEBUG */
	}  /* The end of the for(;;) loop the loops through opcodes */

	/* If we reach this point, it means that execution is finished with
	 * an error of some kind.
	 */
abort_due_to_error:
	if (db->mallocFailed) rc = SQLITE_NOMEM_BKPT;
	assert(rc);
	if (p->zErrMsg==0 && rc!=SQLITE_IOERR_NOMEM) {
		const char *msg;
		/* Avoiding situation when Tarantool error is set,
		 * but error message isn't.
		 */
		if (is_tarantool_error(rc) && tarantoolErrorMessage()) {
			msg = tarantoolErrorMessage();
		} else {
			msg = sqlite3ErrStr(rc);
		}
		sqlite3VdbeError(p, "%s", msg);
	}
	p->rc = rc;
	sqlite3SystemError(db, rc);
	testcase( sqlite3GlobalConfig.xLog!=0);
	sqlite3_log(rc, "statement aborts at %d: [%s] %s",
		    (int)(pOp - aOp), p->zSql, p->zErrMsg);
	sqlite3VdbeHalt(p);
	if (rc==SQLITE_IOERR_NOMEM) sqlite3OomFault(db);
	rc = SQLITE_ERROR;
	if (resetSchemaOnFault>0) {
		sqlite3SchemaClear(db);
	}

	/* This is the only way out of this procedure. */
vdbe_return:
	testcase( nVmStep>0);
	p->aCounter[SQLITE_STMTSTATUS_VM_STEP] += (int)nVmStep;
	assert(rc!=SQLITE_OK || nExtraDelete==0
		|| sqlite3_strlike("DELETE%",p->zSql,0)!=0
		);
	return rc;

	/* Jump to here if a string or blob larger than SQLITE_MAX_LENGTH
	 * is encountered.
	 */
too_big:
	sqlite3VdbeError(p, "string or blob too big");
	rc = SQLITE_TOOBIG;
	goto abort_due_to_error;

	/* Jump to here if a malloc() fails.
	 */
no_mem:
	sqlite3OomFault(db);
	sqlite3VdbeError(p, "out of memory");
	rc = SQLITE_NOMEM_BKPT;
	goto abort_due_to_error;

	/* Jump to here if the sqlite3_interrupt() API sets the interrupt
	 * flag.
	 */
abort_due_to_interrupt:
	assert(db->u1.isInterrupted);
	rc = db->mallocFailed ? SQLITE_NOMEM_BKPT : SQLITE_INTERRUPT;
	p->rc = rc;
	sqlite3VdbeError(p, "%s", sqlite3ErrStr(rc));
	goto abort_due_to_error;
}
