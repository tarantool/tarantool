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
#include "box/box.h"
#include "box/error.h"
#include "box/txn.h"
#include "box/tuple.h"
#include "box/port.h"
#include "sqlInt.h"
#include "mem.h"
#include "vdbeInt.h"
#include "tarantoolInt.h"

#include "msgpuck/msgpuck.h"
#include "mpstream/mpstream.h"

#include "box/schema.h"
#include "box/space.h"
#include "box/sequence.h"
#include "box/session_settings.h"

#ifdef SQL_DEBUG

/*
 * This routine prepares a memory cell for modification by breaking
 * its link to a shallow copy and by marking any current shallow
 * copies of this cell as invalid.
 *
 * This is used for testing and debugging only - to make sure shallow
 * copies are not misused.
 */
static void
sqlVdbeMemAboutToChange(Vdbe * pVdbe, Mem * pMem)
{
	int i;
	Mem *pX;
	for (i = 0, pX = pVdbe->aMem; i < pVdbe->nMem; i++, pX++) {
		if (mem_is_bytes(pX) && !mem_is_ephemeral(pX) &&
		    !mem_is_static(pX)) {
			if (pX->pScopyFrom == pMem) {
				mem_set_invalid(pX);
				pX->pScopyFrom = 0;
			}
		}
	}
	pMem->pScopyFrom = 0;
}

# define memAboutToChange(P,M) sqlVdbeMemAboutToChange(P,M)
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
#ifdef SQL_TEST
int sql_search_count = 0;
#endif

#ifdef SQL_TEST
/*
 * The following global variable is incremented in OP_RowData
 * whenever the xfer optimization is used. This is used on
 * testing purposes only - to make sure the transfer optimization
 * really is happening when it is supposed to.
 */
int sql_xfer_count = 0;
#endif

/*
 * The next global variable is incremented each type the OP_Sort opcode
 * is executed.  The test procedures use this information to make sure that
 * sorting is occurring or not occurring at appropriate times.   This variable
 * has no function other than to help verify the correct operation of the
 * library.
 */
#ifdef SQL_TEST
int sql_sort_count = 0;
#endif

/*
 * The next global variable records the size of the largest varbinary
 * or string that has been used by a VDBE opcode.  The test procedures
 * use this information to make sure that the zero-blob functionality
 * is working correctly.   This variable has no function other than to
 * help verify the correct operation of the library.
 */
#ifdef SQL_TEST
size_t sql_max_blobsize = 0;
static void
updateMaxBlobsize(Mem *p)
{
	if (mem_is_bytes(p) && p->n > sql_max_blobsize)
		sql_max_blobsize = p->n;
}
#endif

/*
 * The next global variable is incremented each time the OP_Found opcode
 * is executed.
 */
#ifdef SQL_TEST
int sql_found_count = 0;
#endif

/*
 * Test a register to see if it exceeds the current maximum blob size.
 * If it does, record the new maximum blob size.
 */
#if defined(SQL_TEST)
# define UPDATE_MAX_BLOBSIZE(P)  updateMaxBlobsize(P)
#else
# define UPDATE_MAX_BLOBSIZE(P)
#endif

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
	 * The memory cell for cursor 0 is aMem[0]. The rest are allocated from
	 * the top of the register space.  Cursor 1 is at Mem[p->nMem-1].
	 * Cursor 2 is at Mem[p->nMem-2]. And so forth.
	 */
	Mem *pMem = iCur>0 ? &p->aMem[p->nMem-iCur] : p->aMem;

	VdbeCursor *pCx = 0;
	int bt_offset = ROUND8(sizeof(VdbeCursor) + sizeof(uint32_t) * nField);
	int nByte = bt_offset +
		(eCurType==CURTYPE_TARANTOOL ? ROUND8(sizeof(BtCursor)) : 0);

	assert(iCur>=0 && iCur<p->nCursor);
	if (p->apCsr[iCur]) { /*OPTIMIZATION-IF-FALSE*/
		sqlVdbeFreeCursor(p->apCsr[iCur]);
		p->apCsr[iCur] = 0;
	}
	if (sqlVdbeMemClearAndResize(pMem, nByte) == 0) {
		p->apCsr[iCur] = pCx = (VdbeCursor*)pMem->z;
		memset(pCx, 0, offsetof(VdbeCursor,uc));
		pCx->eCurType = eCurType;
		pCx->nField = nField;
		vdbe_field_ref_create(&pCx->field_ref, nField);
		if (eCurType==CURTYPE_TARANTOOL) {
			pCx->uc.pCursor = (BtCursor*)&pMem->z[bt_offset];
			sqlCursorZero(pCx->uc.pCursor);
		}
	}
	return pCx;
}

#ifdef SQL_DEBUG
#  define REGISTER_TRACE(P,R,M)					\
	if ((P->sql_flags & SQL_VdbeTrace) != 0)		\
		printf("REG[%d] = %s\n", R, mem_str(M));
#else
#  define REGISTER_TRACE(P,R,M)
#endif

static struct Mem *
vdbe_prepare_null_out(struct Vdbe *v, int n)
{
	assert(n > 0);
	assert(n <= (v->nMem + 1 - v->nCursor));
	struct Mem *out = &v->aMem[n];
	memAboutToChange(v, out);
	mem_set_null(out);
	return out;
}

struct stailq *
vdbe_autoinc_id_list(struct Vdbe *vdbe)
{
	return &vdbe->autoinc_id_list;
}

static int
vdbe_add_new_autoinc_id(struct Vdbe *vdbe, int64_t id)
{
	assert(vdbe != NULL);
	struct autoinc_id_entry *id_entry =
		xregion_alloc_object(&fiber()->gc, typeof(*id_entry));
	id_entry->id = id;
	stailq_add_tail_entry(vdbe_autoinc_id_list(vdbe), id_entry, link);
	return 0;
}

static inline const struct tuple_field *
vdbe_field_ref_fetch_field(struct vdbe_field_ref *field_ref, uint32_t fieldno)
{
	if (field_ref->tuple == NULL)
		return NULL;
	struct tuple_format *format = tuple_format(field_ref->tuple);
	if (fieldno >= tuple_format_field_count(format))
		return NULL;
	return tuple_format_field(format, fieldno);
}

/**
 * Find the left closest field for a given fieldno in field_ref's
 * slot_bitmask. The fieldno is expected to be greater than 0.
 * @param field_ref The vdbe_field_ref instance to use.
 * @param fieldno Number of a field to find the nearest left
 *        neighbor of.
 * @retval >0 An index of the closest smaller fieldno
 *            initialized in slot_bitmask.
 */
static inline uint32_t
vdbe_field_ref_closest_slotno(struct vdbe_field_ref *field_ref,
			      uint32_t fieldno)
{
	uint64_t slot_bitmask = field_ref->slot_bitmask;
	assert(slot_bitmask != 0 && fieldno > 0);
	uint64_t le_mask = fieldno < 64 ? slot_bitmask & ((1LLU << fieldno) - 1)
			   : slot_bitmask;
	assert(bit_clz_u64(le_mask) < 64);
	return 64 - bit_clz_u64(le_mask) - 1;
}

/**
 * Get a tuple's field using field_ref's slot_bitmask, and tuple's
 * field_map when possible. Required field must be present in
 * tuple.
 * @param field_ref The vdbe_field_ref instance to use.
 * @param fieldno Number of a field to get.
 * @retval not NULL MessagePack field.
 */
static const char *
vdbe_field_ref_fetch_data(struct vdbe_field_ref *field_ref, uint32_t fieldno)
{
	if (field_ref->slots[fieldno] != 0 || fieldno == 0)
		return field_ref->data + field_ref->slots[fieldno];

	const char *field_begin;
	const struct tuple_field *field = vdbe_field_ref_fetch_field(field_ref,
								     fieldno);
	if (field != NULL && field->offset_slot != TUPLE_OFFSET_SLOT_NIL) {
		field_begin = tuple_field(field_ref->tuple, fieldno);
	} else {
		uint32_t prev = vdbe_field_ref_closest_slotno(field_ref,
							      fieldno);;
		if (fieldno >= 64) {
			/*
			 * There could be initialized slots
			 * that didn't fit in the bitmask.
			 * Try to find the biggest initialized
			 * slot.
			 */
			for (uint32_t it = fieldno - 1; it > prev; it--) {
				if (field_ref->slots[it] == 0)
					continue;
				prev = it;
				break;
			}
		}
		field_begin = field_ref->data + field_ref->slots[prev];
		for (prev++; prev < fieldno; prev++) {
			mp_next(&field_begin);
			field_ref->slots[prev] =
				(uint32_t)(field_begin - field_ref->data);
			bitmask64_set_bit(&field_ref->slot_bitmask, prev);
		}
		mp_next(&field_begin);
	}
	field_ref->slots[fieldno] = (uint32_t)(field_begin - field_ref->data);
	bitmask64_set_bit(&field_ref->slot_bitmask, fieldno);
	return field_begin;
}

/**
 * Fetch field by fieldno using vdbe_field_ref and store result
 * in dest_mem.
 * @param field_ref The initialized vdbe_field_ref instance to use.
 * @param fieldno The id of the field to fetch.
 * @param[out] dest_mem The memory variable to store result.
 * @retval 0 Status code in case of success.
 * @retval sql_ret_code Error code otherwise.
 */
static int
vdbe_field_ref_fetch(struct vdbe_field_ref *field_ref, uint32_t fieldno,
		     struct Mem *dest_mem)
{
	if (fieldno >= field_ref->field_count) {
		UPDATE_MAX_BLOBSIZE(dest_mem);
		return 0;
	}
	assert(sqlVdbeCheckMemInvariants(dest_mem) != 0);
	const char *data = vdbe_field_ref_fetch_data(field_ref, fieldno);
	uint32_t dummy;
	if (mem_from_mp(dest_mem, data, &dummy) != 0)
		return -1;
	UPDATE_MAX_BLOBSIZE(dest_mem);
	return 0;
}

/*
 * Execute as much of a VDBE program as we can.
 * This is the core of sql_step().
 */
int sqlVdbeExec(Vdbe *p)
{
	Op *aOp = p->aOp;          /* Copy of p->aOp */
	Op *pOp = aOp;             /* Current operation */
#if defined(SQL_DEBUG)
	Op *pOrigOp;               /* Value of pOp at the top of the loop */
#endif
	int rc = 0;        /* Value to return */
	/* The database */
	struct sql *db = sql_get();
	int iCompare = 0;          /* Result of last comparison */
	Mem *aMem = p->aMem;       /* Copy of p->aMem */
	Mem *pIn1 = 0;             /* 1st input operand */
	Mem *pIn2 = 0;             /* 2nd input operand */
	Mem *pIn3 = 0;             /* 3rd input operand */
	Mem *pOut = 0;             /* Output operand */
	int *aPermute = 0;         /* Permutation of columns for OP_Compare */
	/*** INSERT STACK UNION HERE ***/

	assert(p->magic==VDBE_MAGIC_RUN);  /* sql_step() verifies this */
	assert(!p->is_aborted);
	p->iCurrentTime = 0;
	assert(p->explain==0);
	p->pResultSet = 0;
#ifdef SQL_DEBUG
	if (p->pc == 0 &&
	    (p->sql_flags & (SQL_VdbeListing|SQL_VdbeEQP|SQL_VdbeTrace)) != 0) {
		int i;
		int once = 1;
		sqlVdbePrintSql(p);
		if ((p->sql_flags & SQL_VdbeListing) != 0) {
			printf("VDBE Program Listing:\n");
			for(i=0; i<p->nOp; i++) {
				sqlVdbePrintOp(stdout, i, &aOp[i]);
			}
		}
		if ((p->sql_flags & SQL_VdbeEQP) != 0) {
			for(i=0; i<p->nOp; i++) {
				if (aOp[i].opcode==OP_Explain) {
					if (once) printf("VDBE Query Plan:\n");
					printf("%s\n", aOp[i].p4.z);
					once = 0;
				}
			}
		}
		if ((p->sql_flags & SQL_VdbeTrace) != 0)
			printf("VDBE Trace:\n");
	}
#endif
	for(pOp=&aOp[p->pc]; 1; pOp++) {
		/* Errors are detected by individual opcodes, with an immediate
		 * jumps to abort_due_to_error.
		 */
		assert(rc == 0);

		assert(pOp>=aOp && pOp<&aOp[p->nOp]);

		/* Only allow tracing if SQL_DEBUG is defined.
		 */
#ifdef SQL_DEBUG
		if ((p->sql_flags & SQL_VdbeTrace) != 0)
			sqlVdbePrintOp(stdout, (int)(pOp - aOp), pOp);
#endif


		/* Sanity checking on other operands */
#ifdef SQL_DEBUG
		{
			u8 opProperty = sqlOpcodeProperty[pOp->opcode];
			if ((opProperty & OPFLG_IN1)!=0) {
				assert(pOp->p1>0);
				assert(pOp->p1<=(p->nMem+1 - p->nCursor));
				assert(memIsValid(&aMem[pOp->p1]));
				assert(sqlVdbeCheckMemInvariants(&aMem[pOp->p1]));
				REGISTER_TRACE(p, pOp->p1, &aMem[pOp->p1]);
			}
			if ((opProperty & OPFLG_IN2)!=0) {
				assert(pOp->p2>0);
				assert(pOp->p2<=(p->nMem+1 - p->nCursor));
				assert(memIsValid(&aMem[pOp->p2]));
				assert(sqlVdbeCheckMemInvariants(&aMem[pOp->p2]));
				REGISTER_TRACE(p, pOp->p2, &aMem[pOp->p2]);
			}
			if ((opProperty & OPFLG_IN3)!=0) {
				assert(pOp->p3>0);
				assert(pOp->p3<=(p->nMem+1 - p->nCursor));
				assert(memIsValid(&aMem[pOp->p3]));
				assert(sqlVdbeCheckMemInvariants(&aMem[pOp->p3]));
				REGISTER_TRACE(p, pOp->p3, &aMem[pOp->p3]);
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
#if defined(SQL_DEBUG)
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
 * The formatting of each case is important.  The makefile for sql
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
	goto jump_to_p2;
}

/* Opcode: SetDiag P1 P2 * P4 *
 *
 * Set diag error. After that jump to address P2 if it is not 0.
 * Otherwise, go to the next instruction. Note that is_aborted
 * flag is not set in this case, which allows to continue VDBE
 * execution. For instance, to provide auxiliary query-specific
 * clean-up.
 *
 * P1 parameter is an error code to be set. The P4 parameter is a
 * text description of the error.
 */
case OP_SetDiag: {             /* jump */
	box_error_set(__FILE__, __LINE__, pOp->p1, pOp->p4.z);
	if (pOp->p2 != 0)
		goto jump_to_p2;
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
	mem_set_uint(pIn1, pOp - aOp);
	REGISTER_TRACE(p, pOp->p1, pIn1);

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
	assert(mem_is_uint(pIn1));
	pOp = &aOp[pIn1->u.u];
	mem_set_invalid(pIn1);
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
	assert(pOp->p3>0 && pOp->p3<p->nOp);
	pOut = &aMem[pOp->p1];
	assert(!VdbeMemDynamic(pOut));
	mem_set_uint(pOut, pOp->p3 - 1);
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
	assert(mem_is_uint(pIn1));
	assert(pIn1->u.u < (uint64_t) p->nOp);
	pCaller = &aOp[pIn1->u.u];
	assert(pCaller->opcode==OP_Yield);
	assert(pCaller->p2>=0 && pCaller->p2<p->nOp);
	pOp = &aOp[pCaller->p2 - 1];
	mem_set_invalid(pIn1);
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
	pIn1 = &aMem[pOp->p1];
	assert(VdbeMemDynamic(pIn1)==0);
	int pcDest = (int)pIn1->u.u;
	mem_set_uint(pIn1, pOp - aOp);
	REGISTER_TRACE(p, pOp->p1, pIn1);
	pOp = &aOp[pcDest];
	break;
}

/* Opcode:  Halt P1 P2 * * *
 *
 * Exit immediately.  All open cursors, etc are closed
 * automatically.
 *
 * P1 is the result code returned by sql_exec(),
 * sql_stmt_reset(), or sql_stmt_finalize().  For a normal halt,
 * this should be 0.
 * For errors, it can be some other value.  If P1!=0 then P2 will
 * determine whether or not to rollback the current transaction.
 * Do not rollback if P2==ON_CONFLICT_ACTION_FAIL. Do the rollback
 * if P2==ON_CONFLICT_ACTION_ROLLBACK.  If
 * P2==ON_CONFLICT_ACTION_ABORT, then back out all changes that
 * have occurred during this execution of the VDBE, but do not
 * rollback the transaction.
 *
 * There is an implied "Halt 0 0 0" instruction inserted at the
 * very end of every program.  So a jump past the last instruction
 * of the program is the same as executing Halt.
 */
case OP_Halt: {
	VdbeFrame *pFrame;
	int pcx;
	assert(pOp->p1 == 0 || ! diag_is_empty(diag_get()));

	pcx = (int)(pOp - aOp);
	if (pOp->p1 == 0 && p->pFrame != NULL) {
		/* Halt the sub-program. Return control to the parent frame. */
		pFrame = p->pFrame;
		p->pFrame = pFrame->pParent;
		p->nFrame--;
		sqlVdbeSetChanges(p->nChange);
		pcx = sqlVdbeFrameRestore(pFrame);
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
	if (pOp->p1 != 0)
		p->is_aborted = true;
	p->errorAction = (u8)pOp->p2;
	p->pc = pcx;
	sqlVdbeHalt(p);
	rc = p->is_aborted ? -1 : SQL_DONE;
	goto vdbe_return;
}

/* Opcode: Integer P1 P2 * * *
 * Synopsis: r[P2]=P1
 *
 * The 32-bit integer value P1 is written into register P2.
 */
case OP_Integer: {         /* out2 */
	pOut = vdbe_prepare_null_out(p, pOp->p2);
	mem_set_int(pOut, pOp->p1, pOp->p1 < 0);
	break;
}

/* Opcode: Bool P1 P2 * * *
 * Synopsis: r[P2]=P1
 *
 * The boolean value P1 is written into register P2.
 */
case OP_Bool: {         /* out2 */
	pOut = vdbe_prepare_null_out(p, pOp->p2);
	assert(pOp->p1 == 1 || pOp->p1 == 0);
	mem_set_bool(pOut, pOp->p1);
	break;
}

/* Opcode: Int64 * P2 * P4 *
 * Synopsis: r[P2]=P4
 *
 * P4 is a pointer to a 64-bit integer value.
 * Write that value into register P2.
 */
case OP_Int64: {           /* out2 */
	pOut = vdbe_prepare_null_out(p, pOp->p2);
	assert(pOp->p4.pI64!=0);
	mem_set_int(pOut, *pOp->p4.pI64, pOp->p4type == P4_INT64);
	break;
}

/* Opcode: Real * P2 * P4 *
 * Synopsis: r[P2]=P4
 *
 * P4 is a pointer to a 64-bit floating point value.
 * Write that value into register P2.
 */
case OP_Real: {            /* same as TK_FLOAT, out2 */
	pOut = vdbe_prepare_null_out(p, pOp->p2);
	assert(!sqlIsNaN(*pOp->p4.pReal));
	mem_set_double(pOut, *pOp->p4.pReal);
	break;
}

/**
 * Opcode: Decimal * P2 * P4 *
 * Synopsis: r[P2]=P4
 *
 * P4 is a pointer to a DECIMAL value. Write that value into register P2.
 */
case OP_Decimal: {            /* same as TK_DECIMAL, out2 */
	pOut = vdbe_prepare_null_out(p, pOp->p2);
	mem_set_dec(pOut, pOp->p4.dec);
	break;
}

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
	pOp->opcode = OP_String;
	pOp->p1 = sqlStrlen30(pOp->p4.z);

	if (pOp->p1 > SQL_MAX_LENGTH)
		goto too_big;
	assert(rc == 0);
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
	pOut = vdbe_prepare_null_out(p, pOp->p2);
	assert(strlen(pOp->p4.z) == (size_t)pOp->p1);
	mem_set_str0_static(pOut, pOp->p4.z);
	UPDATE_MAX_BLOBSIZE(pOut);
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
 * NULL values will not compare equal even if SQL_NULLEQ is set on
 * OP_Ne or OP_Eq.
 */
case OP_Null: {           /* out2 */
	int cnt;
	pOut = vdbe_prepare_null_out(p, pOp->p2);
	cnt = pOp->p3-pOp->p2;
	assert(pOp->p3<=(p->nMem+1 - p->nCursor));
	if (pOp->p1 != 0)
		mem_set_null_clear(pOut);
	while( cnt>0) {
		pOut++;
		memAboutToChange(p, pOut);
		if (pOp->p1 != 0)
			mem_set_null_clear(pOut);
		else
			mem_set_null(pOut);
		cnt--;
	}
	break;
}

/* Opcode: Blob P1 P2 P3 P4 *
 * Synopsis: r[P2]=P4 (len=P1, subtype=P3)
 *
 * P4 points to a blob of data P1 bytes long.  Store this
 * blob in register P2.  Set subtype to P3.
 */
case OP_Blob: {                /* out2 */
	assert(pOp->p1 <= SQL_MAX_LENGTH);
	pOut = vdbe_prepare_null_out(p, pOp->p2);
	if (pOp->p3 == 0) {
		/*
		 * TODO: It is possible that vabinary should be stored as
		 * ephemeral or static depending on value. There is no way to
		 * determine right now, so it is stored as static.
		 */
		mem_set_bin_static(pOut, pOp->p4.z, pOp->p1);
	} else {
		assert(pOp->p3 == SQL_SUBTYPE_MSGPACK);
		if (mp_typeof(*pOp->p4.z) == MP_MAP)
			mem_set_map_static(pOut, pOp->p4.z, pOp->p1);
		else
			mem_set_array_static(pOut, pOp->p4.z, pOp->p1);
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
 * The P4 value is used by sql_bind_parameter_name().
 */
case OP_Variable: {            /* out2 */
	Mem *pVar;       /* Value being transferred */

	assert(pOp->p1>0 && pOp->p1<=p->nVar);
	assert(pOp->p4.z==0 || pOp->p4.z==sqlVListNumToName(p->pVList,pOp->p1));
	pVar = &p->aVar[pOp->p1 - 1];
	if (sqlVdbeMemTooBig(pVar)) {
		goto too_big;
	}
	pOut = vdbe_prepare_null_out(p, pOp->p2);
	mem_copy_as_ephemeral(pOut, pVar);
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
		mem_move(pOut, pIn1);
		REGISTER_TRACE(p, p2++, pOut);
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
		if (mem_copy(pOut, pIn1) != 0)
			goto abort_due_to_error;
		REGISTER_TRACE(p, pOp->p2+pOp->p3-n, pOut);
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
	mem_copy_as_ephemeral(pOut, pIn1);
#ifdef SQL_DEBUG
	if (pOut->pScopyFrom==0) pOut->pScopyFrom = pIn1;
#endif
	break;
}

/* Opcode: ResultRow P1 P2 * * *
 * Synopsis: output=r[P1@P2]
 *
 * The registers P1 through P1+P2-1 contain a single row of
 * results. This opcode causes the sql_step() call to terminate
 * with an SQL_ROW return code and it sets up the sql_stmt
 * structure to provide access to the r(P1)..r(P1+P2-1) values as
 * the result row.
 */
case OP_ResultRow: {
	assert(p->nResColumn==pOp->p2);
	assert(pOp->p1>0);
	assert(pOp->p1+pOp->p2<=(p->nMem+1 - p->nCursor)+1);
	assert(p->iStatement == 0 && p->anonymous_savepoint == NULL);

	/* Invalidate all ephemeral cursor row caches */
	p->cacheCtr = (p->cacheCtr + 2)|1;

	p->pResultSet = &aMem[pOp->p1];
#ifdef SQL_DEBUG
	struct Mem *pMem = p->pResultSet;
	for (int i = 0; i < pOp->p2; i++) {
		assert(memIsValid(&pMem[i]));
		REGISTER_TRACE(p, pOp->p1+i, &pMem[i]);
	}
#endif

	if (db->mTrace & SQL_TRACE_ROW) {
		db->xTrace(SQL_TRACE_ROW, db->pTraceArg, p, 0);
	}

	/* Return SQL_ROW
	 */
	p->pc = (int)(pOp - aOp) + 1;
	rc = SQL_ROW;
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
 *
 * Concatenation operator accepts only arguments of string-like
 * types (i.e. TEXT and BLOB).
 */
case OP_Concat: {           /* same as TK_CONCAT, in1, in2, out3 */
	pIn1 = &aMem[pOp->p1];
	pIn2 = &aMem[pOp->p2];
	pOut = &aMem[pOp->p3];
	if (mem_concat(pIn2, pIn1, pOut) != 0)
		goto abort_due_to_error;
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
case OP_Add: {                 /* same as TK_PLUS, in1, in2, out3 */
	pIn1 = &aMem[pOp->p1];
	pIn2 = &aMem[pOp->p2];
	pOut = &aMem[pOp->p3];
	if (mem_add(pIn2, pIn1, pOut) != 0)
		goto abort_due_to_error;
	break;
}

/* Opcode: Multiply P1 P2 P3 * *
 * Synopsis: r[P3]=r[P1]*r[P2]
 *
 *
 * Multiply the value in register P1 by the value in register P2
 * and store the result in register P3.
 * If either input is NULL, the result is NULL.
 */
case OP_Multiply: {            /* same as TK_STAR, in1, in2, out3 */
	pIn1 = &aMem[pOp->p1];
	pIn2 = &aMem[pOp->p2];
	pOut = &aMem[pOp->p3];
	if (mem_mul(pIn2, pIn1, pOut) != 0)
		goto abort_due_to_error;
	break;
}

/* Opcode: Subtract P1 P2 P3 * *
 * Synopsis: r[P3]=r[P2]-r[P1]
 *
 * Subtract the value in register P1 from the value in register P2
 * and store the result in register P3.
 * If either input is NULL, the result is NULL.
 */
case OP_Subtract: {           /* same as TK_MINUS, in1, in2, out3 */
	pIn1 = &aMem[pOp->p1];
	pIn2 = &aMem[pOp->p2];
	pOut = &aMem[pOp->p3];
	if (mem_sub(pIn2, pIn1, pOut) != 0)
		goto abort_due_to_error;
	break;
}

/* Opcode: Divide P1 P2 P3 * *
 * Synopsis: r[P3]=r[P2]/r[P1]
 *
 * Divide the value in register P1 by the value in register P2
 * and store the result in register P3 (P3=P2/P1). If the value in
 * register P1 is zero, then the result is NULL. If either input is
 * NULL, the result is NULL.
 */
case OP_Divide: {             /* same as TK_SLASH, in1, in2, out3 */
	pIn1 = &aMem[pOp->p1];
	pIn2 = &aMem[pOp->p2];
	pOut = &aMem[pOp->p3];
	if (mem_div(pIn2, pIn1, pOut) != 0)
		goto abort_due_to_error;
	break;
}

/* Opcode: Remainder P1 P2 P3 * *
 * Synopsis: r[P3]=r[P2]%r[P1]
 *
 * Compute the remainder after integer register P2 is divided by
 * register P1 and store the result in register P3.
 * If the value in register P1 is zero the result is NULL.
 * If either operand is NULL, the result is NULL.
 */
case OP_Remainder: {           /* same as TK_REM, in1, in2, out3 */
	pIn1 = &aMem[pOp->p1];
	pIn2 = &aMem[pOp->p2];
	pOut = &aMem[pOp->p3];
	if (mem_rem(pIn2, pIn1, pOut) != 0)
		goto abort_due_to_error;
	break;
}

/* Opcode: SkipLoad P1 * * * *
 *
 * If P1 is not zero, then it is a register that a subsequent min() or
 * max() aggregate will set to true if the current row is not the minimum or
 * maximum.  The P1 register is initialized to false by this instruction.
 */
case OP_SkipLoad: {
	if (pOp->p1) {
		mem_set_bool(&aMem[pOp->p1], false);
	}
	break;
}

/* Opcode: BuiltinFunction P1 P2 P3 P4 *
 * Synopsis: r[P3]=func(r[P2@P1])
 *
 * Invoke a user function (P4 is a pointer to an sql_context object that
 * contains a pointer to the function to be run) with P1 arguments taken
 * from register P2 and successors.  The result of the function is stored
 * in register P3.  Register P3 must not be one of the function inputs.
 *
 * See also: AggStep, AggFinal
 */
case OP_BuiltinFunction: {
	int argc = pOp->p1;
	sql_context *pCtx;

	assert(pOp->p4type==P4_FUNCCTX);
	pCtx = pOp->p4.pCtx;

	pOut = vdbe_prepare_null_out(p, pOp->p3);
	if (pCtx->pOut != pOut)
		pCtx->pOut = pOut;

#ifdef SQL_DEBUG
	for(int i = 0; i < argc; i++) {
		assert(memIsValid(&aMem[pOp->p2 + i]));
		REGISTER_TRACE(p, pOp->p2 + i, &aMem[pOp->p2 + i]);
	}
#endif
	pCtx->is_aborted = false;
	assert(pCtx->func->def->language == FUNC_LANGUAGE_SQL_BUILTIN);
	struct func_sql_builtin *func = (struct func_sql_builtin *)pCtx->func;
	func->call(pCtx, argc, &aMem[pOp->p2]);

	/* If the function returned an error, throw an exception */
	if (pCtx->is_aborted)
		goto abort_due_to_error;

	/* Copy the result of the function into register P3 */
	if (mem_is_bytes(pOut)) {
		if (sqlVdbeMemTooBig(pCtx->pOut)) goto too_big;
	}

	REGISTER_TRACE(p, pOp->p3, pCtx->pOut);
	UPDATE_MAX_BLOBSIZE(pCtx->pOut);
	break;
}

/* Opcode: FunctionByName P1 P2 P3 P4 *
 * Synopsis: r[P3]=func(r[P2@P1])
 *
 * Invoke a user function (P4 is a pointer to a function object
 * that defines the function) with P1 arguments taken from
 * register P2 and successors. The result of the function is
 * stored in register P3.
 */
case OP_FunctionByName: {
	assert(pOp->p4type == P4_DYNAMIC);
	struct func *func = func_by_name(pOp->p4.z, strlen(pOp->p4.z));
	if (unlikely(func == NULL)) {
		diag_set(ClientError, ER_NO_SUCH_FUNCTION, pOp->p4.z);
		goto abort_due_to_error;
	}
	/*
	 * Function call may yield so pointer to the function may
	 * turn out to be invalid after call.
	 */
	enum field_type returns = func->def->returns;
	int argc = pOp->p1;
	struct Mem *argv = &aMem[pOp->p2];
	struct port args, ret;

	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	port_vdbemem_create(&args, argv, argc);
	if (func_call(func, &args, &ret) != 0)
		goto abort_due_to_error;

	pOut = vdbe_prepare_null_out(p, pOp->p3);
	uint32_t size;
	struct Mem *mem = (struct Mem *)port_get_vdbemem(&ret, &size);
	port_destroy(&ret);
	if (mem == NULL) {
		region_truncate(region, region_svp);
		goto abort_due_to_error;
	}
	assert(size == 1);
	mem_move(pOut, &mem[0]);
	assert(mem_is_null(&mem[0]) && mem_is_trivial(&mem[0]));
	region_truncate(region, region_svp);
	if (!mem_is_field_compatible(pOut, returns)) {
		diag_set(ClientError, ER_FUNC_INVALID_RETURN_TYPE, pOp->p4.z,
			 field_type_strs[returns],
			 mp_type_strs[mem_mp_type(pOut)]);
		goto abort_due_to_error;
	}

	/*
	 * Copy the result of the function invocation into
	 * register P3.
	 */
	if (mem_is_bytes(pOut))
		if (sqlVdbeMemTooBig(pOut)) goto too_big;

	REGISTER_TRACE(p, pOp->p3, pOut);
	UPDATE_MAX_BLOBSIZE(pOut);
	break;
}

/* Opcode: BitAnd P1 P2 P3 * *
 * Synopsis: r[P3]=r[P1]&r[P2]
 *
 * Take the bit-wise AND of the values in register P1 and P2 and
 * store the result in register P3.
 * If either input is NULL, the result is NULL.
 */
case OP_BitAnd: {               /* same as TK_BITAND, in1, in2, out3 */
	pIn1 = &aMem[pOp->p1];
	pIn2 = &aMem[pOp->p2];
	pOut = &aMem[pOp->p3];
	if (mem_bit_and(pIn2, pIn1, pOut) != 0)
		goto abort_due_to_error;
	assert(pOut->type == MEM_TYPE_UINT || pOut->type == MEM_TYPE_NULL);
	break;
}

/* Opcode: BitOr P1 P2 P3 * *
 * Synopsis: r[P3]=r[P1]|r[P2]
 *
 * Take the bit-wise OR of the values in register P1 and P2 and
 * store the result in register P3.
 * If either input is NULL, the result is NULL.
 */
case OP_BitOr: {                /* same as TK_BITOR, in1, in2, out3 */
	pIn1 = &aMem[pOp->p1];
	pIn2 = &aMem[pOp->p2];
	pOut = &aMem[pOp->p3];
	if (mem_bit_or(pIn2, pIn1, pOut) != 0)
		goto abort_due_to_error;
	assert(pOut->type == MEM_TYPE_UINT || pOut->type == MEM_TYPE_NULL);
	break;
}

/* Opcode: ShiftLeft P1 P2 P3 * *
 * Synopsis: r[P3]=r[P2]<<r[P1]
 *
 * Shift the integer value in register P2 to the left by the
 * number of bits specified by the integer in register P1.
 * Store the result in register P3.
 * If either input is NULL, the result is NULL.
 */
case OP_ShiftLeft: {            /* same as TK_LSHIFT, in1, in2, out3 */
	pIn1 = &aMem[pOp->p1];
	pIn2 = &aMem[pOp->p2];
	pOut = &aMem[pOp->p3];
	if (mem_shift_left(pIn2, pIn1, pOut) != 0)
		goto abort_due_to_error;
	assert(pOut->type == MEM_TYPE_UINT || pOut->type == MEM_TYPE_NULL);
	break;
}

/* Opcode: ShiftRight P1 P2 P3 * *
 * Synopsis: r[P3]=r[P2]>>r[P1]
 *
 * Shift the integer value in register P2 to the right by the
 * number of bits specified by the integer in register P1.
 * Store the result in register P3.
 * If either input is NULL, the result is NULL.
 */
case OP_ShiftRight: {           /* same as TK_RSHIFT, in1, in2, out3 */
	pIn1 = &aMem[pOp->p1];
	pIn2 = &aMem[pOp->p2];
	pOut = &aMem[pOp->p3];
	if (mem_shift_right(pIn2, pIn1, pOut) != 0)
		goto abort_due_to_error;
	assert(pOut->type == MEM_TYPE_UINT || pOut->type == MEM_TYPE_NULL);
	break;
}

/* Opcode: AddImm  P1 P2 * * *
 * Synopsis: r[P1]=r[P1]+P2
 *
 * Add the constant P2 to the value in register P1.
 * Content of register P1 and value P2 are assumed to be
 * unsigned.
 */
case OP_AddImm: {            /* in1 */
	pIn1 = &aMem[pOp->p1];
	memAboutToChange(p, pIn1);
	assert(mem_is_uint(pIn1) && pOp->p2 >= 0);
	pIn1->u.u += pOp->p2;
	break;
}

/* Opcode: MustBeInt P1 P2 * * *
 *
 * Force the value in register P1 to be an integer.  If the value
 * in P1 is not an integer and cannot be converted into an integer
 * without data loss, then jump immediately to P2, or if P2==0
 * raise an ER_SQL_TYPE_MISMATCH error.
 */
case OP_MustBeInt: {            /* jump, in1 */
	pIn1 = &aMem[pOp->p1];
	if (mem_to_int_precise(pIn1) != 0) {
		if (pOp->p2 != 0)
			goto jump_to_p2;
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH,
			 mem_str(pIn1), "integer");
		goto abort_due_to_error;
	}
	break;
}

/* Opcode: Cast P1 P2 * * *
 * Synopsis: type(r[P1])
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
	pIn1 = &aMem[pOp->p1];
	rc = mem_cast_explicit(pIn1, pOp->p2);
	UPDATE_MAX_BLOBSIZE(pIn1);
	if (rc == 0)
		break;
	diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(pIn1),
		 field_type_strs[pOp->p2]);
	goto abort_due_to_error;
}

/* Opcode: Array P1 P2 P3 * *
 * Synopsis: r[P2]=array(P3@P1)
 *
 * Construct an ARRAY value from P1 registers starting at reg(P3).
 */
case OP_Array: {
	pOut = &aMem[pOp->p2];

	uint32_t size;
	struct region *region = &fiber()->gc;
	size_t svp = region_used(region);
	char *val = mem_encode_array(&aMem[pOp->p3], pOp->p1, &size, region);
	if (val == NULL || mem_copy_array(pOut, val, size) != 0) {
		region_truncate(region, svp);
		goto abort_due_to_error;
	}
	region_truncate(region, svp);
	break;
}

/**
 * Opcode: Map P1 P2 P3 * *
 * Synopsis: r[P2] = map(P3@P1)
 *
 * Construct an MAP value from P1 registers starting at reg(P3).
 */
case OP_Map: {
	pOut = &aMem[pOp->p2];

	uint32_t size;
	struct region *region = &fiber()->gc;
	size_t svp = region_used(region);
	char *val = mem_encode_map(&aMem[pOp->p3], pOp->p1, &size, region);
	if (val == NULL || mem_copy_map(pOut, val, size) != 0) {
		region_truncate(region, svp);
		goto abort_due_to_error;
	}
	region_truncate(region, svp);
	break;
}

/**
 * Opcode: Getitem P1 P2 P3 * *
 * Synopsis: r[P2] = value[P3@P1]
 *
 * Get an element from the value in register P3[P1] using values in
 * registers P3, ... P3 + (P1 - 1).
 */
case OP_Getitem: {
	int count = pOp->p1;
	assert(count > 0);
	struct Mem *value = &aMem[pOp->p3 + count];
	if (mem_is_null(value)) {
		diag_set(ClientError, ER_SQL_EXECUTE, "Selecting is not "
			 "possible from NULL");
		goto abort_due_to_error;
	}
	if (mem_is_any(value) || !mem_is_container(value)) {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(value),
			 "map or array");
		goto abort_due_to_error;
	}

	pOut = &aMem[pOp->p2];
	struct Mem *keys = &aMem[pOp->p3];
	if (mem_getitem(value, keys, count, pOut) != 0)
		goto abort_due_to_error;
	break;
}

/* Opcode: Eq P1 P2 P3 P4 P5
 * Synopsis: IF r[P3]==r[P1]
 *
 * Compare the values in register P1 and P3. If r[P3] == r[P1], then the action
 * is performed. The action is to jump to address P2 or store the comparison
 * result in register P2 if the SQL_STOREP2 flag is set in P5. In case both
 * values are STRINGs and collation is used for comparison, the collation is
 * specified in P4. If SQL_NULLEQ is set in P5, then the result of the
 * comparison is always either TRUE or FALSE and will never be NULL.
 */
/* Opcode: Ne P1 P2 P3 P4 P5
 * Synopsis: IF r[P3]!=r[P1]
 *
 * This works just like the Eq opcode except that the action is performed if
 * r[P3] != r[P1]. See the Eq opcode for additional information.
 */
case OP_Eq:               /* same as TK_EQ, jump, in1, in3 */
case OP_Ne: {             /* same as TK_NE, jump, in1, in3 */
	pIn1 = &aMem[pOp->p1];
	pIn3 = &aMem[pOp->p3];
	if (mem_is_any_null(pIn1, pIn3) && (pOp->p5 & SQL_NULLEQ) == 0) {
		/*
		 * SQL_NULLEQ is clear and at least one operand is NULL, then
		 * the result is always NULL. The jump is taken if the
		 * SQL_JUMPIFNULL bit is set.
		 */
		if ((pOp->p5 & SQL_STOREP2) != 0) {
			pOut = vdbe_prepare_null_out(p, pOp->p2);
			iCompare = 1;
			REGISTER_TRACE(p, pOp->p2, pOut);
			break;
		}
		if ((pOp->p5 & SQL_JUMPIFNULL) != 0)
			goto jump_to_p2;
		break;
	}
	int cmp_res;
	if (mem_cmp(pIn3, pIn1, &cmp_res, pOp->p4.pColl) != 0)
		goto abort_due_to_error;
	bool result = pOp->opcode == OP_Eq ? cmp_res == 0 : cmp_res != 0;
	if ((pOp->p5 & SQL_STOREP2) != 0) {
		iCompare = cmp_res;
		pOut = &aMem[pOp->p2];
		mem_set_bool(pOut, result);
		REGISTER_TRACE(p, pOp->p2, pOut);
		break;
	}
	if (result)
		goto jump_to_p2;
	break;
}

/* Opcode: Lt P1 P2 P3 P4 P5
 * Synopsis: IF r[P3]<r[P1]
 *
 * Compare the values in register P1 and P3. If r[P3] < r[P1], then the action
 * is performed. The action is to jump to address P2 or store the comparison
 * result in register P2 if the SQL_STOREP2 flag is set in P5. In case both
 * values are STRINGs and collation is used for comparison, the collation is
 * specified in P4.
 */
/* Opcode: Le P1 P2 P3 P4 P5
 * Synopsis: IF r[P3]<=r[P1]
 *
 * This works just like the Lt opcode except that the action is performed if
 * r[P3] <= r[P1]. See the Lt opcode for additional information.
 */
/* Opcode: Gt P1 P2 P3 P4 P5
 * Synopsis: IF r[P3]>r[P1]
 *
 * This works just like the Lt opcode except that the action is performed if
 * r[P3] > r[P1]. See the Lt opcode for additional information.
 */
/* Opcode: Ge P1 P2 P3 P4 P5
 * Synopsis: IF r[P3]>=r[P1]
 *
 * This works just like the Lt opcode except that the action is performed if
 * r[P3] >= r[P1]. See the Lt opcode for additional information.
 */
case OP_Lt:               /* same as TK_LT, jump, in1, in3 */
case OP_Le:               /* same as TK_LE, jump, in1, in3 */
case OP_Gt:               /* same as TK_GT, jump, in1, in3 */
case OP_Ge: {             /* same as TK_GE, jump, in1, in3 */
	pIn1 = &aMem[pOp->p1];
	pIn3 = &aMem[pOp->p3];
	if (mem_is_any_null(pIn1, pIn3)) {
		if ((pOp->p5 & SQL_STOREP2) != 0) {
			pOut = vdbe_prepare_null_out(p, pOp->p2);
			iCompare = 1;
			REGISTER_TRACE(p, pOp->p2, pOut);
			break;
		}
		if ((pOp->p5 & SQL_JUMPIFNULL) != 0)
			goto jump_to_p2;
		break;
	}
	int cmp_res;
	if (mem_cmp(pIn3, pIn1, &cmp_res, pOp->p4.pColl) != 0)
		goto abort_due_to_error;

	bool result;
	switch(pOp->opcode) {
	case OP_Lt:
		result = cmp_res < 0;
		break;
	case OP_Le:
		result = cmp_res <= 0;
		break;
	case OP_Gt:
		result = cmp_res > 0;
		break;
	case OP_Ge:
		result = cmp_res >= 0;
		break;
	default:
		unreachable();
	}

	if ((pOp->p5 & SQL_STOREP2) != 0) {
		iCompare = cmp_res;
		pOut = &aMem[pOp->p2];
		mem_set_bool(pOut, result);
		REGISTER_TRACE(p, pOp->p2, pOut);
		break;
	}
	if (result)
		goto jump_to_p2;
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
	assert(pOp[-1].p5 & SQL_STOREP2);
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

	assert(pOp->p4type == P4_KEYINFO);
	assert(n>0);
	p1 = pOp->p1;
	p2 = pOp->p2;

	struct key_def *def = sql_key_info_to_key_def(pOp->p4.key_info);
	if (def == NULL)
		goto abort_due_to_error;
#if SQL_DEBUG
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
#endif /* SQL_DEBUG */
	for(int i = 0; i < n; i++) {
		idx = aPermute ? aPermute[i] : i;
		assert(memIsValid(&aMem[p1+idx]));
		assert(memIsValid(&aMem[p2+idx]));
		REGISTER_TRACE(p, p1+idx, &aMem[p1+idx]);
		REGISTER_TRACE(p, p2+idx, &aMem[p2+idx]);
		assert(i < (int)def->part_count);
		struct coll *coll = def->parts[i].coll;
		bool is_rev = def->parts[i].sort_order == SORT_ORDER_DESC;
		struct Mem *a = &aMem[p1+idx];
		struct Mem *b = &aMem[p2+idx];
		if (!mem_is_comparable(a)) {
			diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(a),
				 "comparable type");
			goto abort_due_to_error;
		}
		if (!mem_is_comparable(b)) {
			diag_set(ClientError, ER_SQL_TYPE_MISMATCH, mem_str(b),
				 "comparable type");
			goto abort_due_to_error;
		}
		iCompare = mem_cmp_scalar(a, b, coll);
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
	if (iCompare < 0)
		pOp = &aOp[pOp->p1 - 1];
	else if (iCompare == 0)
		pOp = &aOp[pOp->p2 - 1];
	else
		pOp = &aOp[pOp->p3 - 1];
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
	if (mem_is_null(pIn1)) {
		v1 = 2;
	} else if (mem_is_bool(pIn1)) {
		v1 = pIn1->u.b;
	} else {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH,
			 mem_str(pIn1), "boolean");
		goto abort_due_to_error;
	}
	pIn2 = &aMem[pOp->p2];
	if (mem_is_null(pIn2)) {
		v2 = 2;
	} else if (mem_is_bool(pIn2)) {
		v2 = pIn2->u.b;
	} else {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH,
			 mem_str(pIn2), "boolean");
		goto abort_due_to_error;
	}
	if (pOp->opcode==OP_And) {
		static const unsigned char and_logic[] = { 0, 0, 0, 0, 1, 2, 0, 2, 2 };
		v1 = and_logic[v1*3+v2];
	} else {
		static const unsigned char or_logic[] = { 0, 1, 2, 1, 1, 1, 2, 1, 2 };
		v1 = or_logic[v1*3+v2];
	}
	pOut = vdbe_prepare_null_out(p, pOp->p3);
	if (v1 != 2)
		mem_set_bool(pOut, v1);
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
	pOut = vdbe_prepare_null_out(p, pOp->p2);
	if (!mem_is_null(pIn1)) {
		if (!mem_is_bool(pIn1)) {
			diag_set(ClientError, ER_SQL_TYPE_MISMATCH,
				 mem_str(pIn1), "boolean");
			goto abort_due_to_error;
		}
		mem_set_bool(pOut, ! pIn1->u.b);
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
	if (mem_bit_not(pIn1, pOut) != 0)
		goto abort_due_to_error;
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
	if (p->aOp[0].p1==pOp->p1) {
		goto jump_to_p2;
	} else {
		pOp->p1 = p->aOp[0].p1;
	}
	break;
}

/* Opcode: If P1 P2 P3 * *
 *
 * Jump to P2 if the value in register P1 is true. If the value
 * in P1 is NULL then take the jump if and only if P3 is non-zero.
 */
/* Opcode: IfNot P1 P2 P3 * *
 *
 * Jump to P2 if the value in register P1 is False. If the value
 * in P1 is NULL then take the jump if and only if P3 is non-zero.
 */
case OP_If:                 /* jump, in1 */
case OP_IfNot: {            /* jump, in1 */
	int c;
	pIn1 = &aMem[pOp->p1];
	if (mem_is_null(pIn1)) {
		c = pOp->p3;
	} else if (mem_is_bool(pIn1)) {
		c = pOp->opcode == OP_IfNot ? ! pIn1->u.b : pIn1->u.b;
	} else {
		diag_set(ClientError, ER_SQL_TYPE_MISMATCH,
			 mem_str(pIn1), "boolean");
		goto abort_due_to_error;
	}
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
	if (mem_is_null(pIn1)) {
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
	if (!mem_is_null(pIn1)) {
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
	Mem *pDest;        /* Where to write the extracted value */
	Mem *pReg;         /* PseudoTable input register */

	pC = p->apCsr[pOp->p1];
	p2 = pOp->p2;

	assert(pOp->p3>0 && pOp->p3<=(p->nMem+1 - p->nCursor));
	pDest = vdbe_prepare_null_out(p, pOp->p3);
	assert(pOp->p1>=0 && pOp->p1<p->nCursor);
	assert(pC!=0);
	assert(p2<pC->nField);
	assert(pC->eCurType!=CURTYPE_PSEUDO || pC->nullRow);
	assert(pC->eCurType!=CURTYPE_SORTER);

	if (pC->cacheStatus!=p->cacheCtr) {                /*OPTIMIZATION-IF-FALSE*/
		if (pC->nullRow) {
			if (pC->eCurType==CURTYPE_PSEUDO) {
				assert(pC->uc.pseudoTableReg>0);
				pReg = &aMem[pC->uc.pseudoTableReg];
				assert(mem_is_bin(pReg));
				assert(memIsValid(pReg));
				vdbe_field_ref_prepare_data(&pC->field_ref,
							    pReg->z, pReg->n);
			} else {
				goto op_column_out;
			}
		} else {
			pCrsr = pC->uc.pCursor;
			assert(pC->eCurType==CURTYPE_TARANTOOL);
			assert(pCrsr);
			assert(sqlCursorIsValid(pCrsr));
			assert(pCrsr->curFlags & BTCF_TaCursor ||
			       pCrsr->curFlags & BTCF_TEphemCursor);
			vdbe_field_ref_prepare_tuple(&pC->field_ref,
						     pCrsr->last_tuple);
		}
		pC->cacheStatus = p->cacheCtr;
	}
	assert(pC->eCurType == CURTYPE_TARANTOOL ||
	       pC->eCurType == CURTYPE_PSEUDO);
	struct Mem *default_val_mem =
		pOp->p4type == P4_MEM ? pOp->p4.pMem : NULL;
	if (vdbe_field_ref_fetch(&pC->field_ref, p2, pDest) != 0)
		goto abort_due_to_error;

	if (mem_is_null(pDest) &&
	    (uint32_t) p2  >= pC->field_ref.field_count &&
	    default_val_mem != NULL) {
		mem_copy_as_ephemeral(pDest, default_val_mem);
	}
	if (pDest->type == MEM_TYPE_NULL)
		goto op_column_out;
	enum field_type field_type = field_type_MAX;
	/* Currently PSEUDO cursor does not have info about field types. */
	if (pC->eCurType == CURTYPE_TARANTOOL)
		field_type = pC->uc.pCursor->space->def->fields[p2].type;
	if (field_type == FIELD_TYPE_ANY)
		pDest->flags |= MEM_Any;
	else if (field_type == FIELD_TYPE_SCALAR)
		pDest->flags |= MEM_Scalar;
	else if (field_type == FIELD_TYPE_NUMBER)
		pDest->flags |= MEM_Number;
op_column_out:
	REGISTER_TRACE(p, pOp->p3, pDest);
	break;
}

/**
 * Opcode: FetchByName P1 * P3 * P4
 * Synopsis: r[P3]=PX
 *
 * Interpret data P1 points at as an initialized vdbe_field_ref object.
 * P4 contains the name of the field to retrieve. The retrieved value is stored
 * in register P3.
 */
case OP_FetchByName: {
	struct vdbe_field_ref *ref = p->aMem[pOp->p1].u.p;
	assert(pOp->p4type == P4_DYNAMIC);
	uint32_t id;
	if (ref->format != NULL) {
		const char *name = pOp->p4.z;
		uint32_t len = strlen(name);
		uint32_t hash = field_name_hash(name, len);
		struct tuple_dictionary *dict = ref->format->dict;
		if (tuple_fieldno_by_name(dict, name, len, hash, &id) != 0) {
			diag_set(ClientError, ER_SQL_CANT_RESOLVE_FIELD, name);
			goto abort_due_to_error;
		}
	} else {
		/*
		 * If no format is specified, we assume that the vdbe_field_ref
		 * contains one field for the field constraint, and assume that
		 * this was already checked when the field constraint was
		 * created.
		 */
		assert(ref->field_count == 1);
		id = 0;
	}
	struct Mem *res = vdbe_prepare_null_out(p, pOp->p3);
	if (vdbe_field_ref_fetch(ref, id, res) != 0)
		goto abort_due_to_error;
	REGISTER_TRACE(p, pOp->p3, res);
	break;
}

/* Opcode: Fetch P1 P2 P3 * *
 * Synopsis: r[P3]=PX
 *
 * Interpret data P1 points at as an initialized vdbe_field_ref object. Extract
 * the P2th field from the tuple. The retrieved value is stored in register P3.
 */
case OP_Fetch: {
	struct vdbe_field_ref *ref = p->aMem[pOp->p1].u.p;
	struct Mem *res = vdbe_prepare_null_out(p, pOp->p3);
	if (vdbe_field_ref_fetch(ref, pOp->p2, res) != 0)
		goto abort_due_to_error;
	REGISTER_TRACE(p, pOp->p3, res);
	break;
}

/* Opcode: ApplyType P1 P2 * P4 *
 * Synopsis: type(r[P1@P2])
 *
 * Check that types of P2 registers starting from register P1 are
 * compatible with given field types in P4. If the MEM_type of the
 * value and the given type are incompatible according to
 * field_mp_plain_type_is_compatible(), but both are numeric,
 * this opcode attempts to convert the value to the type.
 */
case OP_ApplyType: {
	enum field_type *types = pOp->p4.types;
	assert(types != NULL);
	pIn1 = &aMem[pOp->p1];
	for (int i = 0; i < pOp->p2; ++i, ++pIn1) {
		enum field_type type = types[i];
		assert(pIn1 <= &p->aMem[(p->nMem+1 - p->nCursor)]);
		assert(memIsValid(pIn1));
		if (mem_cast_implicit(pIn1, type) != 0) {
			diag_set(ClientError, ER_SQL_TYPE_MISMATCH,
				 mem_str(pIn1), field_type_strs[type]);
			goto abort_due_to_error;
		}
	}
	break;
}

/* Opcode: MakeRecord P1 P2 P3 * P5
 * Synopsis: r[P3]=mkrec(r[P1@P2])
 *
 * Convert P2 registers beginning with P1 into the [record format]
 * use as a data record in a database table or as a key
 * in an index.  The OP_Column opcode can decode the record later.
 *
 * If P5 is not NULL then record under construction is intended to be inserted
 * into ephemeral space. Thus, sort of memory optimization can be performed.
 */
case OP_MakeRecord: {
	Mem *pData0;           /* First field to be combined into the record */
	int nField;            /* Number of fields in the record */
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
	 * corresponding data element. The hdr-size field is also a varint which
	 * is the offset from the beginning of the record to data0.
	 */
	nField = pOp->p1;
	bIsEphemeral = pOp->p5;
	assert(nField>0 && pOp->p2>0 && pOp->p2+nField<=(p->nMem+1 - p->nCursor)+1);
	pData0 = &aMem[nField];
	nField = pOp->p2;

	/* Identify the output register */
	assert(pOp->p3<pOp->p1 || pOp->p3>=pOp->p1+pOp->p2);
	pOut = vdbe_prepare_null_out(p, pOp->p3);

	struct region *region = &fiber()->gc;
	size_t used = region_used(region);
	uint32_t tuple_size;
	char *tuple = mem_encode_array(pData0, nField, &tuple_size, region);
	if (tuple == NULL)
		goto abort_due_to_error;
	if (tuple_size > SQL_MAX_LENGTH)
		goto too_big;

	/* In case of ephemeral space, it is possible to save some memory
	 * allocating one by ordinary malloc: instead of cutting pieces
	 * from region and waiting while they will be freed after
	 * statement commitment, it is better to reuse the same chunk.
	 * Such optimization is prohibited for ordinary spaces, since
	 * memory shouldn't be reused until it is written into WAL.
	 *
	 * However, if memory for ephemeral space is allocated
	 * on region, it will be freed only in sql_stmt_finalize()
	 * routine.
	 */
	if (bIsEphemeral) {
		if (mem_copy_bin(pOut, tuple, tuple_size) != 0)
			goto abort_due_to_error;
		region_truncate(region, used);
	} else {
		/* Allocate memory on the region for the tuple
		 * to be passed to Tarantool. Before that, make
		 * sure previously allocated memory has gone.
		 */
		mem_destroy(pOut);
		mem_set_bin_ephemeral(pOut, tuple, tuple_size);
	}
	assert(sqlVdbeCheckMemInvariants(pOut));
	assert(pOp->p3>0 && pOp->p3<=(p->nMem+1 - p->nCursor));
	REGISTER_TRACE(p, pOp->p3, pOut);
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
	if (pCrsr->curFlags & BTCF_TaCursor) {
		nEntry = tarantoolsqlCount(pCrsr);
	} else {
		assert((pCrsr->curFlags & BTCF_TEphemCursor) != 0);
		nEntry = tarantoolsqlEphemeralCount(pCrsr);
	}
	pOut = vdbe_prepare_null_out(p, pOp->p2);
	mem_set_uint(pOut, nEntry);
	break;
}

/**
 * Opcode: CreateForeignKey P1 * * P4 *
 *
 * Create a new foreign key. The foreign key name is stored in P4. Register
 * r[P1] contains the ID of the child space, r[P1 + 1] contains the ID of the
 * parent space. If the type of register r[P1 + 2] is VARBINARY, then register
 * r[P1 + 2] contains the encoded mapping, otherwise register r[P1 + 2] contains
 * the fieldno of the child field, and r[P1 + 3] contains the fieldno of the
 * parent fields.
 */
case OP_CreateForeignKey: {
	assert(pOp->p1 >= 0);
	struct Mem *mems = &aMem[pOp->p1];
	assert(mem_is_uint(&mems[0]) && mem_is_uint(&mems[1]));
	uint32_t child_id = mems[0].u.u;
	uint32_t parent_id = mems[1].u.u;
	const char *name = pOp->p4.z;
	const char *mapping = NULL;
	uint32_t child_fieldno = 0;
	uint32_t parent_fieldno = 0;
	if (mem_is_uint(&mems[2])) {
		assert(mem_is_uint(&mems[3]));
		child_fieldno = mems[2].u.u;
		parent_fieldno = mems[3].u.u;
	} else {
		assert(mem_is_map(&mems[2]));
		mapping = mems[2].z;
	}
	if (sql_foreign_key_create(name, child_id, parent_id, child_fieldno,
				   parent_fieldno, mapping) != 0)
		goto abort_due_to_error;
	if (p->nChange == 0)
		p->nChange = 1;
	break;
}

/**
 * Opcode: CreateCheck P1 P2 P3 P4 P5
 *
 * Create a new check constraint. The check name is stored in P4. Register
 * r[P1] contains the ID of the space, register r[P2] contains the ID of the
 * function. If P5 is not 0 than P3 is fieldno of field containing this check.
 */
case OP_CreateCheck: {
	assert(pOp->p1 >= 0 && pOp->p2 >= 0 && pOp->p3 >= 0);
	uint32_t space_id = aMem[pOp->p1].u.u;
	uint32_t func_id = aMem[pOp->p2].u.u;
	const char *name = pOp->p4.z;
	bool is_field_ck = pOp->p5 != 0;
	uint32_t fieldno = pOp->p3;
	if (sql_check_create(name, space_id, func_id, fieldno,
			     is_field_ck) != 0)
		goto abort_due_to_error;
	if (p->nChange == 0)
		p->nChange = 1;
	break;
}

/**
 * Opcode: DropTupleConstraint P1 * * P4 *
 * Synopsis: Drop constraint from box.space[P1]
 *
 * Drop constraint named P4.z from space P1.
 */
case OP_DropTupleConstraint: {
	assert(pOp->p1 >= 0 && pOp->p4.z != NULL);
	if (sql_constraint_drop(pOp->p1, pOp->p4.z) != 0)
		goto abort_due_to_error;
	assert(p->nChange == 0);
	p->nChange = 1;
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
	struct txn *txn = in_txn();

	if (txn == NULL) {
		assert(!box_txn());
		diag_set(ClientError, ER_NO_TRANSACTION);
		goto abort_due_to_error;
	}
	p1 = pOp->p1;
	zName = pOp->p4.z;

	/* Assert that the p1 parameter is valid. Also that if there is no open
	 * transaction, then there cannot be any savepoints.
	 */
	assert(rlist_empty(&txn->savepoints) || box_txn());
	assert(p1==SAVEPOINT_BEGIN||p1==SAVEPOINT_RELEASE||p1==SAVEPOINT_ROLLBACK);

	if (p1==SAVEPOINT_BEGIN) {
		/*
		 * Savepoint is available by its name so we don't
		 * care about object itself.
		 */
		if (txn_savepoint_new(txn, zName) == NULL)
			goto abort_due_to_error;
	} else {
		/* Find the named savepoint. If there is no such savepoint, then an
		 * an error is returned to the user.
		 */
		struct txn_savepoint *sv = txn_savepoint_by_name(txn, zName);
		if (sv == NULL) {
			diag_set(ClientError, ER_NO_SUCH_SAVEPOINT);
			goto abort_due_to_error;
		}
		if (p1 == SAVEPOINT_RELEASE) {
			txn_savepoint_release(sv);
		} else {
			assert(p1 == SAVEPOINT_ROLLBACK);
			if (box_txn_rollback_to_savepoint(sv) != 0)
				goto abort_due_to_error;
		}
	}

	break;
}

/* Opcode: CheckViewReferences P1 * * * *
 * Synopsis: r[P1] = space id
 *
 * Check that space to be dropped doesn't have any view
 * references. This opcode is needed since Tarantool lacks
 * DDL transaction. On the other hand, to drop space, we must
 * firstly drop secondary indexes from _index system space,
 * clear _truncate table etc.
 */
case OP_CheckViewReferences: {
	assert(pOp->p1 > 0);
	pIn1 = &aMem[pOp->p1];
	uint64_t space_id = pIn1->u.u;
	assert(space_id <= INT32_MAX);
	struct space *space = space_by_id(space_id);
	assert(space != NULL);
	if (space->def->view_ref_count > 0) {
		diag_set(ClientError, ER_DROP_SPACE, space->def->name,
			 "other views depend on this space");
		goto abort_due_to_error;
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
	if (in_txn()) {
		diag_set(ClientError, ER_ACTIVE_TRANSACTION);
		goto abort_due_to_error;
	}
	if (txn_begin() == NULL)
		goto abort_due_to_error;
	p->auto_commit = false	;
	break;
}

/* Opcode: TransactionCommit * * * * *
 *
 * Commit Tarantool's transaction.
 * If there is no active transaction, raise an error.
 * After txn was committed VDBE should take care of region. Region is needed
 * to get information of autogenerated ids during sql response dump.
 */
case OP_TransactionCommit: {
	struct txn *txn = in_txn();
	if (txn != NULL) {
		if (txn_commit(txn) != 0)
			goto abort_due_to_error;
	} else {
		diag_set(ClientError, ER_SQL_EXECUTE, "cannot commit - no "\
			 "transaction is active");
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
		if (box_txn_rollback() != 0)
			goto abort_due_to_error;
	} else {
		diag_set(ClientError, ER_SQL_EXECUTE, "cannot rollback - no "\
			 "transaction is active");
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
		if (txn_begin() == NULL)
			goto abort_due_to_error;
	} else {
		p->anonymous_savepoint = txn_savepoint_new(in_txn(), NULL);
		if (p->anonymous_savepoint == NULL)
			goto abort_due_to_error;
	}
	break;
}

/* Opcode: IteratorOpen P1 P2 P3 * P5
 * Synopsis: index id = P2, space ptr = reg[P3]
 *
 * Open a cursor for a space specified by pointer in  the register P3 and index
 * id in P2. Give the new cursor an identifier of P1. The P1 values need not be
 * contiguous but all P1 values should be small integers. It is an error for P1
 * to be negative.
 */
case OP_IteratorOpen: {
	struct VdbeCursor *cur = p->apCsr[pOp->p1];
	if (box_schema_version() != p->schema_ver &&
	    (pOp->p5 & OPFLAG_SYSTEMSP) == 0) {
		p->expired = 1;
		diag_set(ClientError, ER_SQL_EXECUTE, "schema version has "\
			 "changed: need to re-compile SQL statement");
		goto abort_due_to_error;
	}
	struct space *space = aMem[pOp->p3].u.p;
	assert(space != NULL);
	if (access_check_space(space, PRIV_R) != 0)
		goto abort_due_to_error;

	struct index *index = space_index(space, pOp->p2);
	assert(index != NULL);
	assert(pOp->p1 >= 0);
	cur = allocateCursor(p, pOp->p1,
			     space->def->exact_field_count == 0 ?
			     space->def->field_count :
			     space->def->exact_field_count,
			     CURTYPE_TARANTOOL);
	if (cur == NULL)
		goto abort_due_to_error;
	struct BtCursor *bt_cur = cur->uc.pCursor;
	bt_cur->curFlags |= space->def->id == 0 ? BTCF_TEphemCursor :
				BTCF_TaCursor;
	bt_cur->space = space;
	bt_cur->index = index;
	bt_cur->eState = CURSOR_INVALID;
	/* Key info still contains sorter order and collation. */
	cur->key_def = index->def->key_def;
	cur->nullRow = 1;
	cur->uc.pCursor->hints = pOp->p5 & OPFLAG_SEEKEQ;
	break;
}

/**
 * Opcode: OP_OpenSpace P1 P2 * * *
 * Synopsis: reg[P1] = space_by_id(P2)
 *
 * Open the space using its ID stored in register P2 and write a pointer to the
 * space to register P1.
 */
case OP_OpenSpace: {
	assert(pOp->p1 >= 0 && pOp->p1 > 0);
	struct space *space = space_by_id(pOp->p2);
	assert(space != NULL);
	mem_set_ptr(&aMem[pOp->p1], space);
	break;
}

/**
 * Opcode: OpenTEphemeral P1 * * P4 *
 * Synopsis:
 * @param P1 register, where pointer to new space is stored.
 * @param P4 key def for new table, NULL is allowed.
 *
 * This opcode creates Tarantool's ephemeral table and stores pointer
 * to it into P1 register.
 */
case OP_OpenTEphemeral: {
	assert(pOp->p1 >= 0);

	assert(pOp->p4type == P4_DYNAMIC || pOp->p4type == P4_STATIC);
	struct sql_space_info *info = pOp->p4.space_info;
	assert(info != NULL);
	struct space *space = sql_ephemeral_space_new(info);

	if (space == NULL)
		goto abort_due_to_error;
	mem_set_ptr(&aMem[pOp->p1], space);
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
	struct key_def *def = sql_key_info_to_key_def(pOp->p4.key_info);
	if (def == NULL)
		goto abort_due_to_error;
	pCx = allocateCursor(p, pOp->p1, pOp->p2, CURTYPE_SORTER);
	if (pCx == NULL)
		goto abort_due_to_error;
	pCx->key_def = def;
	if (sqlVdbeSorterInit(pCx) != 0)
		goto abort_due_to_error;
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
 * MEM with binary content contained in register P2.
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
	if (pCx == NULL)
		goto abort_due_to_error;
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
	sqlVdbeFreeCursor(p->apCsr[pOp->p1]);
	p->apCsr[pOp->p1] = 0;
	break;
}

/* Opcode: SeekLT P1 P2 P3 P4 *
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
 * See also: Found, NotFound, SeekGt, SeekGe, SeekLe
 */
/* Opcode: SeekGT P1 P2 P3 P4 *
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
 */
case OP_SeekLT:         /* jump, in3 */
case OP_SeekGT: {       /* jump, in3 */
	bool is_lt = pOp->opcode == OP_SeekLT;
	struct VdbeCursor *cur = p->apCsr[pOp->p1];
#ifdef SQL_DEBUG
	cur->seekOp = pOp->opcode;
#endif
	cur->nullRow = 0;
	cur->uc.pCursor->iter_type = is_lt ? ITER_LT : ITER_GT;

	uint32_t len = pOp->p4.i;
	assert(pOp->p4type == P4_INT32);
	assert(len <= cur->key_def->part_count);
	struct Mem *mems = &aMem[pOp->p3];
	bool is_op_change = false;
	for (uint32_t i = 0; i < len; ++i) {
		enum field_type type = cur->key_def->parts[i].type;
		struct Mem *mem = &mems[i];
		if (mem_is_field_compatible(mem, type))
			continue;
		if (!sql_type_is_numeric(type) || !mem_is_num(mem)) {
			diag_set(ClientError, ER_SQL_TYPE_MISMATCH,
				 mem_str(mem), field_type_strs[type]);
			goto abort_due_to_error;
		}
		int cmp = mem_cast_implicit_number(mem, type);
		is_op_change = is_op_change || (is_lt && cmp > 0) ||
			       (!is_lt && cmp < 0);
	}
	if (is_op_change)
		cur->uc.pCursor->iter_type = is_lt ? ITER_LE : ITER_GE;

	int res;
	if (sql_cursor_seek(cur->uc.pCursor, mems, len, &res) != 0)
		goto abort_due_to_error;
	assert((res != 0) == (cur->uc.pCursor->eState == CURSOR_INVALID));
	cur->cacheStatus = CACHE_STALE;
#ifdef SQL_TEST
	sql_search_count++;
#endif
	assert(pOp->p2 > 0);
	if (res != 0)
		goto jump_to_p2;
	break;
}

/* Opcode: SeekLE P1 P2 P3 P4 *
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
/* Opcode: SeekGE P1 P2 P3 P4 *
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
 * See also: Found, NotFound, SeekLt, SeekGt, SeekLe
 */
case OP_SeekLE:         /* jump, in3 */
case OP_SeekGE: {       /* jump, in3 */
	bool is_le = pOp->opcode == OP_SeekLE;
	struct VdbeCursor *cur = p->apCsr[pOp->p1];
#ifdef SQL_DEBUG
	cur->seekOp = pOp->opcode;
#endif
	cur->nullRow = 0;
	bool is_eq = (cur->uc.pCursor->hints & OPFLAG_SEEKEQ) != 0;
	if (is_le)
		cur->uc.pCursor->iter_type = is_eq ? ITER_REQ : ITER_LE;
	else
		cur->uc.pCursor->iter_type = is_eq ? ITER_EQ : ITER_GE;
	assert(!is_eq || pOp[1].opcode == OP_IdxLT ||
	       pOp[1].opcode == OP_IdxGT);

	uint32_t len = pOp->p4.i;
	assert(pOp->p4type == P4_INT32);
	assert(len <= cur->key_def->part_count);
	struct Mem *mems = &aMem[pOp->p3];
	bool is_op_change = false;
	bool is_zero = false;
	for (uint32_t i = 0; i < len; ++i) {
		enum field_type type = cur->key_def->parts[i].type;
		struct Mem *mem = &mems[i];
		if (mem_is_field_compatible(mem, type))
			continue;
		if (!sql_type_is_numeric(type) || !mem_is_num(mem)) {
			diag_set(ClientError, ER_SQL_TYPE_MISMATCH,
				 mem_str(mem), field_type_strs[type]);
			goto abort_due_to_error;
		}
		int cmp = mem_cast_implicit_number(mem, type);
		is_op_change = is_op_change || (is_le && cmp < 0) ||
			       (!is_le && cmp > 0);
		/*
		 * In case search using EQ or REQ, we will not find anything if
		 * conversion cannot be precise.
		 */
		is_zero = is_zero || (is_eq && cmp != 0);
	}
	if (is_zero) {
		assert(pOp->p2 > 0);
		goto jump_to_p2;
	}
	if (!is_eq && is_op_change)
		cur->uc.pCursor->iter_type = is_le ? ITER_LT : ITER_GT;

	int res;
	if (sql_cursor_seek(cur->uc.pCursor, mems, len, &res) != 0)
		goto abort_due_to_error;
	assert((res != 0) == (cur->uc.pCursor->eState == CURSOR_INVALID));
	cur->cacheStatus = CACHE_STALE;
#ifdef SQL_TEST
	sql_search_count++;
#endif
	assert(pOp->p2 > 0);
	if (res != 0)
		goto jump_to_p2;
	/* Skip the OP_IdxLT/OP_IdxGT that follows if we have EQ. */
	if (is_eq)
		pOp++;
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

#ifdef SQL_TEST
	if (pOp->opcode!=OP_NoConflict) sql_found_count++;
#endif

	assert(pOp->p1>=0 && pOp->p1<p->nCursor);
	assert(pOp->p4type==P4_INT32);
	pC = p->apCsr[pOp->p1];
	assert(pC!=0);
#ifdef SQL_DEBUG
	pC->seekOp = pOp->opcode;
#endif
	pIn3 = &aMem[pOp->p3];
	assert(pC->eCurType==CURTYPE_TARANTOOL);
	assert(pC->uc.pCursor!=0);
	if (pOp->p4.i>0) {
		r.key_def = pC->key_def;
		r.nField = (u16)pOp->p4.i;
		r.aMem = pIn3;
#ifdef SQL_DEBUG
		for(ii=0; ii<r.nField; ii++) {
			assert(memIsValid(&r.aMem[ii]));
			if (ii != 0)
				REGISTER_TRACE(p, pOp->p3+ii, &r.aMem[ii]);
		}
#endif
		pIdxKey = &r;
		pFree = 0;
	} else {
		pIdxKey = sqlVdbeAllocUnpackedRecord(pC->key_def);
		pFree = pIdxKey;
		assert(mem_is_bin(pIn3));
		sqlVdbeRecordUnpackMsgpack(pC->key_def,
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
			if (mem_is_null(&pIdxKey->aMem[ii])) {
				takeJump = 1;
				break;
			}
		}
	}
	pC->uc.pCursor->iter_type = ITER_EQ;
	rc = sql_cursor_seek(pC->uc.pCursor, pIdxKey->aMem, pIdxKey->nField,
			     &res);
	if (pFree != NULL)
		sql_xfree(pFree);
	if (rc != 0)
		goto abort_due_to_error;
	pC->seekResult = res;
	alreadyExists = (res==0);
	pC->nullRow = 1-alreadyExists;
	pC->cacheStatus = CACHE_STALE;
	if (pOp->opcode == OP_Found) {
		if (alreadyExists)
			goto jump_to_p2;
	} else {
		if (takeJump || !alreadyExists)
			goto jump_to_p2;
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
	pOut = vdbe_prepare_null_out(p, pOp->p2);
	int64_t seq_val = p->apCsr[pOp->p1]->seqCount++;
	mem_set_uint(pOut, seq_val);
	break;
}

/* Opcode: NextSystemSpaceId P1 P2 P3 * *
 * Synopsis: r[P2]=New ID of space P1.
 *
 * Place the next value of the primary key of the _sequence or _func space into
 * register P2. P1 is the system space identifier. P3 is fieldno of primary key.
 */
case OP_NextSystemSpaceId: {
	assert(pOp->p1 >= 0 && pOp->p3 >= 0);
	uint32_t space_id = pOp->p1;
	assert(space_id == BOX_SEQUENCE_ID || space_id == BOX_FUNC_ID);
	struct Mem *res = &p->aMem[pOp->p2];
	char key[1];
	struct tuple *tuple;
	char *key_end = mp_encode_array(key, 0);
	assert(key_end - key == 1);
	if (box_index_max(space_id, 0, key, key_end, &tuple) != 0)
		goto abort_due_to_error;
	if (tuple == NULL) {
		mem_set_uint(res, 1);
		break;
	}
	uint32_t fieldno = pOp->p3;
	uint64_t id;
	if (tuple_field_u64(tuple, fieldno, &id) != 0)
		goto abort_due_to_error;
	mem_set_uint(res, id + 1);
	break;
}

/* Opcode: NextIdEphemeral P1 P2 * * *
 * Synopsis: r[P2]=get_next_rowid(space[P1])
 *
 * This opcode stores next `rowid` for the ephemeral space to
 * P2 register. `rowid` is required, because inserted to
 * ephemeral space tuples may be not unique. Meanwhile,
 * Tarantool`s ephemeral spaces can contain only unique tuples
 * due to only one index (which is PK over all columns in space).
 */
case OP_NextIdEphemeral: {
	struct space *space = (struct space*)p->aMem[pOp->p1].u.p;
	assert(space->def->id == 0);
	uint64_t rowid;
	if (space->vtab->ephemeral_rowid_next(space, &rowid) != 0)
		goto abort_due_to_error;
	/*
	 * FIXME: since memory cell can comprise only 32-bit
	 * integer, make sure it can fit in. This check should
	 * be removed when memory cell is extended with unsigned
	 * 64-bit integer.
	 */
	if (rowid > INT32_MAX) {
		diag_set(ClientError, ER_ROWID_OVERFLOW);
		goto abort_due_to_error;
	}
	pOut = vdbe_prepare_null_out(p, pOp->p2);
	mem_set_uint(pOut, rowid);
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

	if ((pOp->p3 & OPFLAG_NOOP_IF_NULL) != 0 && mem_is_null(pIn1)) {
		pOut = vdbe_prepare_null_out(p, pOp->p2);
	} else {
		assert(memIsValid(pIn1));
		assert(mem_is_int(pIn1));

		pOut = vdbe_prepare_null_out(p, pOp->p2);
		mem_copy_as_ephemeral(pOut, pIn1);
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
		rc = tarantoolsqlDelete(pBtCur);
	} else if (pBtCur->curFlags & BTCF_TEphemCursor) {
		rc = tarantoolsqlEphemeralDelete(pBtCur);
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
 * change counter (returned by subsequent calls to sql_changes()).
 * Then the VMs internal change counter resets to 0.
 * This is used by trigger programs.
 */
case OP_ResetCount: {
	sqlVdbeSetChanges(p->nChange);
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
			if (sqlVdbeSorterCompare(pC, pIn3, nKeyCol, &res) != 0)
				goto abort_due_to_error;
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

	pOut = vdbe_prepare_null_out(p, pOp->p2);
	pC = p->apCsr[pOp->p1];
	assert(isSorter(pC));
	if (sqlVdbeSorterRowkey(pC, pOut) != 0)
		goto abort_due_to_error;
	assert(mem_is_bin(pOut));
	assert(pOp->p1>=0 && pOp->p1<p->nCursor);
	p->apCsr[pOp->p3]->cacheStatus = CACHE_STALE;
	break;
}

/* Opcode: RowData P1 P2 * * P5
 * Synopsis: r[P2]=data
 *
 * Write into register P2 the complete row content for the row at
 * which cursor P1 is currently pointing.
 * There is no interpretation of the data.
 * It is just copied onto the P2 register exactly as
 * it is found in the database file.
 * P5 can be used in debug mode to check if xferOptimization has
 * actually started processing.
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

/*
 * Flag P5 is cleared after the first insertion using xfer
 * optimization.
 */
#ifdef SQL_TEST
	if ((pOp->p5 & OPFLAG_XFER_OPT) != 0) {
		pOp->p5 &= ~OPFLAG_XFER_OPT;
		sql_xfer_count++;
	}
#endif

	pOut = vdbe_prepare_null_out(p, pOp->p2);

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
	assert(sqlCursorIsValid(pCrsr));
	assert(pCrsr->eState == CURSOR_VALID);
	assert(pCrsr->curFlags & BTCF_TaCursor ||
	       pCrsr->curFlags & BTCF_TEphemCursor);
	tarantoolsqlPayloadFetch(pCrsr, &n);
	if (n > SQL_MAX_LENGTH)
		goto too_big;

	char *buf = xregion_alloc(&fiber()->gc, n);
	sqlCursorPayload(pCrsr, 0, n, buf);
	mem_set_bin_ephemeral(pOut, buf, n);
	assert(sqlVdbeCheckMemInvariants(pOut));
	UPDATE_MAX_BLOBSIZE(pOut);
	REGISTER_TRACE(p, pOp->p2, pOut);
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
#ifdef SQL_DEBUG
	pC->seekOp = OP_Last;
#endif
	if (pOp->p3==0 || !sqlCursorIsValidNN(pCrsr)) {
		if (tarantoolsqlLast(pCrsr, &res) != 0)
			goto abort_due_to_error;
		pC->nullRow = (u8)res;
		pC->cacheStatus = CACHE_STALE;
		if (pOp->p2 > 0 && res != 0)
			goto jump_to_p2;
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
#ifdef SQL_TEST
			sql_sort_count++;
			sql_search_count--;
#endif
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
#ifdef SQL_DEBUG
	pC->seekOp = OP_Rewind;
#endif
	if (isSorter(pC)) {
		if (sqlVdbeSorterRewind(pC, &res) != 0)
			goto abort_due_to_error;
	} else {
		assert(pC->eCurType==CURTYPE_TARANTOOL);
		pCrsr = pC->uc.pCursor;
		assert(pCrsr);
		if (tarantoolsqlFirst(pCrsr, &res) != 0)
			goto abort_due_to_error;
		pC->cacheStatus = CACHE_STALE;
	}
	pC->nullRow = (u8)res;
	assert(pOp->p2>0 && pOp->p2<p->nOp);
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
 * sqlBtreeNext().
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
 * sqlBtreePrevious().
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
	if (sqlVdbeSorterNext(pC, &res) != 0)
		goto abort_due_to_error;
	goto next_tail;
case OP_PrevIfOpen:    /* jump */
case OP_NextIfOpen:    /* jump */
	if (p->apCsr[pOp->p1]==0) break;
	/* Fall through */
case OP_Prev:          /* jump */
case OP_Next:          /* jump */
	assert(pOp->p1>=0 && pOp->p1<p->nCursor);
	pC = p->apCsr[pOp->p1];
	res = pOp->p3;
	assert(pC!=0);
	assert(pC->eCurType==CURTYPE_TARANTOOL);
	assert(res==0 || res==1);
	assert(pOp->opcode!=OP_Next || pOp->p4.xAdvance == sqlCursorNext);
	assert(pOp->opcode!=OP_Prev || pOp->p4.xAdvance == sqlCursorPrevious);
	assert(pOp->opcode!=OP_NextIfOpen || pOp->p4.xAdvance == sqlCursorNext);
	assert(pOp->opcode!=OP_PrevIfOpen || pOp->p4.xAdvance == sqlCursorPrevious);

	/* The Next opcode is only used after SeekGT, SeekGE, and Rewind.
	 * The Prev opcode is only used after SeekLT, SeekLE, and Last.
	 */
	assert(pOp->opcode!=OP_Next || pOp->opcode!=OP_NextIfOpen
	       || pC->seekOp==OP_SeekGT || pC->seekOp==OP_SeekGE
	       || pC->seekOp==OP_Rewind || pC->seekOp==OP_Found);
	assert(pOp->opcode!=OP_Prev || pOp->opcode!=OP_PrevIfOpen
	       || pC->seekOp==OP_SeekLT || pC->seekOp==OP_SeekLE
	       || pC->seekOp==OP_Last);

	if (pOp->p4.xAdvance(pC->uc.pCursor, &res) != 0)
		goto abort_due_to_error;
			next_tail:
	pC->cacheStatus = CACHE_STALE;
	if (res == 0) {
		pC->nullRow = 0;
#ifdef SQL_TEST
		sql_search_count++;
#endif
		goto jump_to_p2;
	} else {
		pC->nullRow = 1;
	}
	break;
}

/* Opcode: SorterInsert P1 P2 * * *
 * Synopsis: key=r[P2]
 *
 * Register P2 holds an SQL index key made using the
 * MakeRecord instructions.  This opcode writes that key
 * into the sorter P1.  Data for the entry is nil.
 */
case OP_SorterInsert: {      /* in2 */
	assert(pOp->p1 >= 0 && pOp->p1 < p->nCursor);
	struct VdbeCursor *cursor = p->apCsr[pOp->p1];
	assert(cursor != NULL);
	assert(isSorter(cursor));
	pIn2 = &aMem[pOp->p2];
	assert(mem_is_bin(pIn2));
	if (sqlVdbeSorterWrite(cursor, pIn2) != 0)
		goto abort_due_to_error;
	break;
}

/* Opcode: IdxInsert P1 P2 P3 * P5
 * Synopsis: key=r[P1]
 *
 * @param P1 Index of a register with MessagePack data to insert.
 * @param P2 Register containing pointer to space to insert into.
 * @param P3 If not 0, than it is an index of a register that
 *           contains value that will be inserted into field with
 *           AUTOINCREMENT. If the value is NULL, than the newly
 *           generated autoincrement value will be saved to VDBE
 *           context.
 * @param P5 Flags. If P5 contains OPFLAG_NCHANGE, then VDBE
 *        accounts the change in a case of successful insertion in
 *        nChange counter. If P5 contains OPFLAG_OE_IGNORE, then
 *        we are processing INSERT OR INGORE statement. Thus, in
 *        case of conflict we don't raise an error.
 */
/* Opcode: IdxReplace P1 P2 P3 * P5
 * Synopsis: key=r[P1]
 *
 * This opcode works exactly as IdxInsert does, but in Tarantool
 * internals it invokes box_replace() instead of box_insert().
 */
case OP_IdxReplace:
case OP_IdxInsert: {
	pIn2 = &aMem[pOp->p1];
	assert(mem_is_bin(pIn2));
	struct space *space = aMem[pOp->p2].u.p;
	assert(space != NULL);
	if (space->def->id != 0) {
		/* Make sure that memory has been allocated on region. */
		assert(mem_is_ephemeral(&aMem[pOp->p1]));
		if (pOp->opcode == OP_IdxInsert) {
			rc = tarantoolsqlInsert(space, pIn2->z,
						    pIn2->z + pIn2->n);
		} else {
			rc = tarantoolsqlReplace(space, pIn2->z,
						     pIn2->z + pIn2->n);
		}
	} else {
		rc = tarantoolsqlEphemeralInsert(space, pIn2->z,
						     pIn2->z + pIn2->n);
	}
	if (rc != 0) {
		if ((pOp->p5 & OPFLAG_OE_IGNORE) != 0) {
			/*
			 * Ignore any kind of fails and do not
			 * raise error message. If we are in
			 * trigger, increment ignore raised
			 * counter.
			 */
			rc = 0;
			if (p->pFrame != NULL)
				p->ignoreRaised++;
			break;
		}
		if ((pOp->p5 & OPFLAG_OE_FAIL) != 0) {
			p->errorAction = ON_CONFLICT_ACTION_FAIL;
		} else if ((pOp->p5 & OPFLAG_OE_ROLLBACK) != 0) {
			p->errorAction = ON_CONFLICT_ACTION_ROLLBACK;
		}
		goto abort_due_to_error;
	}
	if ((pOp->p5 & OPFLAG_NCHANGE) != 0)
		p->nChange++;
	if (pOp->p3 > 0 && mem_is_null(&aMem[pOp->p3])) {
		assert(space->sequence != NULL);
		int64_t value;
		if (sequence_get_value(space->sequence, &value) != 0)
			goto abort_due_to_error;
		if (vdbe_add_new_autoinc_id(p, value) != 0)
			goto abort_due_to_error;
	}
	break;
}

/* Opcode: Update P1 P2 P3 P4 P5
 * Synopsis: key=r[P1]
 *
 * Process UPDATE operation. Primary key fields can not be
 * modified.
 * Under the hood it performs box_update() call.
 * For the performance sake, it takes whole affected row (P1)
 * and encodes into msgpack only fields to be updated (P3).
 *
 * @param P1 The first field to be updated. Fields are located
 *           in the range of [P1...] in decoded state.
 *           Encoded only fields which numbers are presented
 *           in @P3 array.
 * @param P2 P2 Encoded key to be passed to box_update().
 * @param P3 Index of a register with upd_fields blob.
 *           It's items are numbers of fields to be replaced with
 *           new values from P1. They must be sorted in ascending
 *           order.
 * @param P4 Register containing pointer to space to update.
 * @param P5 Flags. If P5 contains OPFLAG_NCHANGE, then VDBE
 *           accounts the change in a case of successful
 *           insertion in nChange counter. If P5 contains
 *           OPFLAG_OE_IGNORE, then we are processing INSERT OR
 *           INGORE statement. Thus, in case of conflict we don't
 *           raise an error.
 */
case OP_Update: {
	struct Mem *new_tuple = &aMem[pOp->p1];
	if (pOp->p5 & OPFLAG_NCHANGE)
		p->nChange++;

	struct space *space = aMem[pOp->p4.i].u.p;
	assert(pOp->p4type == P4_INT32);

	struct Mem *key_mem = &aMem[pOp->p2];
	assert(mem_is_bin(key_mem));

	struct Mem *upd_fields_mem = &aMem[pOp->p3];
	assert(mem_is_bin(upd_fields_mem));
	uint32_t *upd_fields = (uint32_t *)upd_fields_mem->z;
	uint32_t upd_fields_cnt = upd_fields_mem->n / sizeof(uint32_t);

	/* Prepare Tarantool update ops msgpack. */
	struct region *region = &fiber()->gc;
	size_t used = region_used(region);
	bool is_error = false;
	struct mpstream stream;
	mpstream_init(&stream, region, region_reserve_cb, region_alloc_cb,
		      set_encode_error, &is_error);
	mpstream_encode_array(&stream, upd_fields_cnt);
	for (uint32_t i = 0; i < upd_fields_cnt; i++) {
		uint32_t field_idx = upd_fields[i];
		assert(field_idx < space->def->field_count);
		mpstream_encode_array(&stream, 3);
		mpstream_encode_strn(&stream, "=", 1);
		mpstream_encode_uint(&stream, field_idx);
		mem_to_mpstream(new_tuple + field_idx, &stream);
	}
	mpstream_flush(&stream);
	if (is_error) {
		region_truncate(&fiber()->gc, used);
		diag_set(OutOfMemory, stream.pos - stream.buf,
			"mpstream_flush", "stream");
		goto abort_due_to_error;
	}
	uint32_t ops_size = region_used(region) - used;
	const char *ops = xregion_join(region, ops_size);
	assert(rc == 0);
	rc = box_update(space->def->id, 0, key_mem->z, key_mem->z + key_mem->n,
			ops, ops + ops_size, 0, NULL);
	region_truncate(&fiber()->gc, used);

	if (pOp->p5 & OPFLAG_OE_IGNORE) {
		/*
		 * Ignore any kind of fails and do not raise
		 * error message
		 */
		rc = 0;
		/*
		 * If we are in trigger, increment ignore raised
		 * counter.
		 */
		if (p->pFrame)
			p->ignoreRaised++;
	} else if (pOp->p5 & OPFLAG_OE_FAIL) {
		p->errorAction = ON_CONFLICT_ACTION_FAIL;
	} else if (pOp->p5 & OPFLAG_OE_ROLLBACK) {
		p->errorAction = ON_CONFLICT_ACTION_ROLLBACK;
	}
	if (rc != 0)
		goto abort_due_to_error;
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
	assert(p->errorAction == ON_CONFLICT_ACTION_ABORT);
	if (tarantoolsqlInsert(space, pIn2->z, pIn2->z + pIn2->n) != 0)
		goto abort_due_to_error;
	if (pOp->p5 & OPFLAG_NCHANGE)
		p->nChange++;
	break;
}

/* Opcode: SDelete P1 P2 P3 * P5
 * Synopsis: space id = P1, key = r[P2], searching index id = P3
 *
 * This opcode is used only during DDL routine.
 * Delete entry with given key from system space. P3 is the index
 * number by which to search for the key.
 *
 * If P5 is set to OPFLAG_NCHANGE, account overall changes
 * made to database.
 */
case OP_SDelete: {
	assert(pOp->p1 > 0);
	assert(pOp->p2 >= 0);
	assert(pOp->p3 >= 0);

	pIn2 = &aMem[pOp->p2];
	struct space *space = space_by_id(pOp->p1);
	assert(space != NULL);
	assert(space_is_system(space));
	assert(p->errorAction == ON_CONFLICT_ACTION_ABORT);
	if (sql_delete_by_key(space, pOp->p3, pIn2->z, pIn2->n) != 0)
		goto abort_due_to_error;
	if (pOp->p5 & OPFLAG_NCHANGE)
		p->nChange++;
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

	assert(pOp->p3>0);
	assert(pOp->p2>0 && pOp->p2+pOp->p3<=(p->nMem+1 - p->nCursor)+1);
	assert(pOp->p1>=0 && pOp->p1<p->nCursor);
	pC = p->apCsr[pOp->p1];
	assert(pC!=0);
	assert(pC->eCurType==CURTYPE_TARANTOOL);
	pCrsr = pC->uc.pCursor;
	assert(pCrsr!=0);
	assert(pOp->p5==0);
	if (sql_cursor_seek(pCrsr, &aMem[pOp->p2], (u16)pOp->p3, &res) != 0)
		goto abort_due_to_error;
	if (res==0) {
		assert(pCrsr->eState == CURSOR_VALID);
		if (pCrsr->curFlags & BTCF_TaCursor) {
			if (tarantoolsqlDelete(pCrsr) != 0)
				goto abort_due_to_error;
		} else if (pCrsr->curFlags & BTCF_TEphemCursor) {
			if (tarantoolsqlEphemeralDelete(pCrsr) != 0)
				goto abort_due_to_error;
		} else {
			unreachable();
		}
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
#ifdef SQL_DEBUG
	{ int i; for(i=0; i<r.nField; i++) assert(memIsValid(&r.aMem[i])); }
#endif
	int res =  tarantoolsqlIdxKeyCompare(pC->uc.pCursor, &r);
	assert((OP_IdxLE&1)==(OP_IdxLT&1) && (OP_IdxGE&1)==(OP_IdxGT&1));
	if ((pOp->opcode&1)==(OP_IdxLT&1)) {
		assert(pOp->opcode==OP_IdxLE || pOp->opcode==OP_IdxLT);
		res = -res;
	} else {
		assert(pOp->opcode==OP_IdxGE || pOp->opcode==OP_IdxGT);
		res++;
	}
	if (res > 0)
		goto jump_to_p2;
	break;
}

/* Opcode: Clear P1 P2 * * P5
 * Synopsis: space id = P1
 * If P2 is not 0, use Truncate semantics.
 *
 * Delete all contents of the space, which space id is given
 * in P1 argument. It is worth mentioning, that clearing routine
 * doesn't involve truncating, since it features completely
 * different mechanism under hood.
 *
 * If the OPFLAG_NCHANGE flag is set, then the row change count
 * is incremented by the number of deleted tuples.
 */
case OP_Clear: {
	assert(pOp->p1 > 0);
	uint32_t space_id = pOp->p1;
	struct space *space = space_by_id(space_id);
	assert(space != NULL);
	if (pOp->p2 > 0) {
		if (box_truncate(space_id) != 0)
			goto abort_due_to_error;
	} else {
		uint32_t tuple_count;
		if (tarantoolsqlClearTable(space, &tuple_count) != 0)
			goto abort_due_to_error;
		if ((pOp->p5 & OPFLAG_NCHANGE) != 0)
			p->nChange += tuple_count;
	}
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
		sqlVdbeSorterReset(pC->uc.pSorter);
	} else {
		assert(pC->eCurType==CURTYPE_TARANTOOL);
		assert(pC->uc.pCursor->curFlags & BTCF_TEphemCursor);
		if (tarantoolsqlEphemeralClearTable(pC->uc.pCursor) != 0)
			goto abort_due_to_error;
	}
	break;
}

/* Opcode: RenameTable P1 * * P4 *
 * Synopsis: P1 = space_id, P4 = name
 *
 * Rename table P1 with name from P4.
 * Invoke tarantoolsqlRenameTable, which updates tuple with
 * corresponding space_id in _space: changes string of statement, which creates
 * table and its name. Removes hash of old table name and updates SQL schema
 * by calling sqlInitCallback.
 * In presence of triggers or foreign keys, their statements
 * are also updated in _trigger and in parent table.
 *
 */
case OP_RenameTable: {
	uint32_t space_id;
	struct space *space;
	char *zOldTableName;
	const char *zNewTableName;

	space_id = pOp->p1;
	space = space_by_id(space_id);
	assert(space);
	/* Rename space op doesn't change triggers. */
	struct sql_trigger *triggers = space->sql_triggers;
	assert(space->def->name != NULL);
	zNewTableName = pOp->p4.z;
	zOldTableName = sql_xstrdup(space_name(space));
	if (sql_rename_table(space_id, zNewTableName) != 0)
		goto abort_due_to_error;
	/*
	 * Rebuild 'CREATE TRIGGER' expressions of all triggers
	 * created on this table. Sure, this action is not atomic
	 * due to lack of transactional DDL, but just do the best
	 * effort.
	 */
	for (struct sql_trigger *trigger = triggers; trigger != NULL; ) {
		/* Store pointer as trigger will be destructed. */
		struct sql_trigger *next_trigger = trigger->next;
		/*
		 * FIXME: In the case of error, part of triggers
		 * would have invalid space name in tuple so can
		 * not been persisted. Server could be restarted.
		 * In this case, rename table back and try again.
		 */
		if (tarantoolsqlRenameTrigger(trigger->zName, zOldTableName,
					      zNewTableName) != 0)
			goto abort_due_to_error;
		trigger = next_trigger;
	}
	sql_xfree(zOldTableName);
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
	/* TODO: Enable analysis. */
	/*
	if (sql_analysis_load(db) != 0)
		goto abort_due_to_error;
	*/
	break;
}

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
	 * is really a trigger, not a foreign key action, and the setting
	 * 'recursive_triggers' is not set).
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

	if (p->nFrame >= SQL_MAX_TRIGGER_DEPTH) {
		diag_set(ClientError, ER_SQL_EXECUTE, "too many levels of "\
			 "trigger recursion");
		goto abort_due_to_error;
	}

	/* Register pRt is used to store the memory required to save the state
	 * of the current program, and the memory required at runtime to execute
	 * the trigger program. If this trigger has been fired before, then pRt
	 * is already allocated. Otherwise, it must be initialized.
	 */
	if (!mem_is_frame(pRt)) {
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
		pFrame = sql_xmalloc0(nByte);
		mem_set_frame(pRt, pFrame);

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

		pEnd = &VdbeFrameMem(pFrame)[pFrame->nChildMem];
		for(pMem=VdbeFrameMem(pFrame); pMem!=pEnd; pMem++) {
			mem_create(pMem);
			mem_set_invalid(pMem);
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
	pFrame->nDbChange = db->nChange;
	p->nChange = 0;
	p->pFrame = pFrame;
	p->aMem = aMem = VdbeFrameMem(pFrame);
	p->nMem = pFrame->nChildMem;
	p->nCursor = (u16)pFrame->nChildCsr;
	p->apCsr = (VdbeCursor **)&aMem[p->nMem];
	p->aOp = aOp = pProgram->aOp;
	p->nOp = pProgram->nOp;
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
	pOut = vdbe_prepare_null_out(p, pOp->p2);
	pFrame = p->pFrame;
	pIn = &pFrame->aMem[pOp->p1 + pFrame->aOp[pFrame->pc].p1];
	mem_copy_as_ephemeral(pOut, pIn);
	break;
}

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
	assert(mem_is_int(pIn1));
	if (mem_is_uint(pIn1) && pIn1->u.u != 0) {
		assert(pOp->p3 >= 0);
		uint64_t res = pIn1->u.u - (uint64_t) pOp->p3;
		/*
		 * To not bother setting integer flag in case
		 * result of subtraction is negative, just
		 * use saturated arithmetic.
		 */
		res &= -(res <= pIn1->u.u);
		pIn1->u.u = res;
		goto jump_to_p2;
	}
	break;
}

/* Opcode: OffsetLimit P1 P2 P3 * *
 * Synopsis: r[P2]=r[P1]+r[P3]
 *
 * This opcode performs a commonly used computation associated with
 * LIMIT and OFFSET process.  r[P1] holds the limit counter.  r[P3]
 * holds the offset counter.  The opcode computes the combined value
 * of the LIMIT and OFFSET and stores that value in r[P2].  The r[P2]
 * value computed is the total number of rows that will need to be
 * visited in order to complete the query.
 *
 * Otherwise, r[P2] is set to the sum of r[P1] and r[P3]. If the
 * sum is larger than 2^63-1 (i.e. overflow takes place) then
 * error is raised.
 */
case OP_OffsetLimit: {    /* in1, out2, in3 */
	pIn1 = &aMem[pOp->p1];
	pIn3 = &aMem[pOp->p3];
	pOut = vdbe_prepare_null_out(p, pOp->p2);

	assert(mem_is_uint(pIn1));
	assert(mem_is_uint(pIn3));
	uint64_t x = pIn1->u.u;
	uint64_t rhs = pIn3->u.u;
	bool unused;
	if (sql_add_int(x, false, rhs, false, (int64_t *) &x, &unused) != 0) {
		diag_set(ClientError, ER_SQL_EXECUTE, "sum of LIMIT and OFFSET "
			"values should not result in integer overflow");
		goto abort_due_to_error;
	}
	mem_set_uint(pOut, x);
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
	assert(mem_is_uint(pIn1));
	if (pIn1->u.u > 0) {
		pIn1->u.u--;
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
	assert(mem_is_uint(pIn1));
	if (pIn1->u.u > 0)
		pIn1->u.u--;
	if (pIn1->u.u == 0) goto jump_to_p2;
	break;
}


/* Opcode: AggStep P1 P2 P3 P4 *
 * Synopsis: accum=r[P3] step(r[P2@P1])
 *
 * Execute the step function for an aggregate.  The
 * function has P1 arguments.   P4 is a pointer to an sql_context
 * object that is used to run the function.  Register P3 is
 * as the accumulator.
 *
 * The P1 arguments are taken from register P2 and its
 * successors.
 */
case OP_AggStep: {
	int argc = pOp->p1;
	sql_context *pCtx;
	Mem *pMem;

	assert(pOp->p4type==P4_FUNCCTX);
	pCtx = pOp->p4.pCtx;
	pMem = &aMem[pOp->p3];

	if (pCtx->pOut != pMem)
		pCtx->pOut = pMem;

#ifdef SQL_DEBUG
	for(int i = 0; i < argc; i++) {
		assert(memIsValid(&aMem[pOp->p2 + i]));
		REGISTER_TRACE(p, pOp->p2 + i, &aMem[pOp->p2 + i]);
	}
#endif

	pCtx->skipFlag = 0;
	assert(pCtx->func->def->language == FUNC_LANGUAGE_SQL_BUILTIN);
	struct func_sql_builtin *func = (struct func_sql_builtin *)pCtx->func;
	func->call(pCtx, argc, &aMem[pOp->p2]);
	if (pCtx->is_aborted)
		goto abort_due_to_error;
	if (pCtx->skipFlag) {
		assert(pOp[-1].opcode == OP_SkipLoad);
		int i = pOp[-1].p1;
		if (i) mem_set_bool(&aMem[i], true);
	}
	break;
}

/* Opcode: AggFinal P1 * * P4 *
 * Synopsis: accum=r[P1]
 *
 * Execute the finalizer function for an aggregate. P1 is the memory location
 * that is the accumulator for the aggregate. P4 is a pointer to the function.
 */
case OP_AggFinal: {
	assert(pOp->p1>0 && pOp->p1<=(p->nMem+1 - p->nCursor));
	struct func_sql_builtin *func = (struct func_sql_builtin *)pOp->p4.func;
	struct Mem *pIn1 = &aMem[pOp->p1];

	if (func->finalize != NULL && func->finalize(pIn1) != 0)
		goto abort_due_to_error;
	UPDATE_MAX_BLOBSIZE(pIn1);
	if (sqlVdbeMemTooBig(pIn1) != 0)
		goto too_big;
	break;
}

/* Opcode: Expire P1 * * * *
 *
 * Cause precompiled statements to expire.
 *
 * If P1 is 0, then all SQL statements become expired. If P1 is non-zero,
 * then only the currently executing statement is expired.
 */
case OP_Expire: {
	if (!pOp->p1) {
		sqlExpirePreparedStatements();
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
 * If tracing is enabled (by the sql_trace()) interface, then
 * the UTF-8 string contained in P4 is emitted on the trace callback.
 * Or if P4 is blank, use the string returned by sql_sql().
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
	 * would have been returned by the legacy sql_trace() interface by
	 * using the X argument when X begins with "--" and invoking
	 * sql_expanded_sql(P) otherwise.
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
		sql_xfree(p);
		rc = -1;
		break;
	}

	if ((db->mTrace & SQL_TRACE_STMT)!=0
	    && !p->doingRerun
	    && (zTrace = (pOp->p4.z ? pOp->p4.z : p->zSql))!=0
		) {
		{
			(void)db->xTrace(SQL_TRACE_STMT, db->pTraceArg, p, zTrace);
		}
	}
#ifdef SQL_DEBUG
	if ((p->sql_flags & SQL_SqlTrace) != 0 &&
	    (zTrace = (pOp->p4.z ? pOp->p4.z : p->zSql)) != 0)
		sqlDebugPrintf("SQL-trace: %s\n", zTrace);
#endif /* SQL_DEBUG */
	assert(pOp->p2>0);
	if (pOp->p1>=sqlGlobalConfig.iOnceResetThreshold) {
		for(i=1; i<p->nOp; i++) {
			if (p->aOp[i].opcode==OP_Once) p->aOp[i].p1 = 0;
		}
		pOp->p1 = 0;
	}
	pOp->p1++;
	goto jump_to_p2;
}

/* Opcode: GenSpaceid P1 * * * *
 * Synopsis: r[P1]=new space ID
 *
 * Generate unique id for a non-system space and store it in register
 * specified by first operand. It is system opcode and must be used only
 * during DDL routine.
 */
case OP_GenSpaceid: {
	assert(pOp->p1 > 0);
	pOut = vdbe_prepare_null_out(p, pOp->p1);
	uint32_t u;
	if (box_generate_space_id(&u, false) != 0)
		goto abort_due_to_error;
	mem_set_uint(pOut, u);
	break;
}

/* Opcode: SetSession P1 * * P4 *
 *
 * Set new value of the session setting. P4 is the name of the
 * setting being updated, P1 is the register holding a value.
 */
case OP_SetSession: {
	assert(pOp->p4type == P4_DYNAMIC);
	const char *setting_name = pOp->p4.z;
	int sid = session_setting_find(setting_name);
	if (sid < 0) {
		diag_set(ClientError, ER_NO_SUCH_SESSION_SETTING, setting_name);
		goto abort_due_to_error;
	}
	pIn1 = &aMem[pOp->p1];
	struct session_setting *setting = &session_settings[sid];
	switch (setting->field_type) {
	case FIELD_TYPE_BOOLEAN: {
		if (!mem_is_bool(pIn1))
			goto invalid_type;
		bool value = pIn1->u.b;
		size_t size = mp_sizeof_bool(value);
		char *mp_value = (char *) static_alloc(size);
		mp_encode_bool(mp_value, value);
		if (setting->set(sid, mp_value) != 0)
			goto abort_due_to_error;
		break;
	}
	case FIELD_TYPE_STRING: {
		if (!mem_is_str(pIn1))
			goto invalid_type;
		const char *str = pIn1->z;
		uint32_t size = mp_sizeof_str(pIn1->n);
		char *mp_value = (char *) static_alloc(size);
		if (mp_value == NULL) {
			diag_set(OutOfMemory, size, "static_alloc", "mp_value");
			goto abort_due_to_error;
		}
		mp_encode_str(mp_value, str, pIn1->n);
		if (setting->set(sid, mp_value) != 0)
			goto abort_due_to_error;
		break;
	}
	default:
	invalid_type:
		diag_set(ClientError, ER_SESSION_SETTING_INVALID_VALUE,
			 session_setting_strs[sid],
			 field_type_strs[setting->field_type]);
		goto abort_due_to_error;
	}
	p->nChange++;
	break;
}

/**
 * Opcode: ShowCreateTable P1 P2 * * *
 * Synopsis: r[P2, P2 + 1]=description of a space with ID == r[P1]
 *
 * Set the space description with the identifier from register P1 to register
 * P2. All errors detected during the construction of the description are set to
 * register P2 + 1.
 */
case OP_ShowCreateTable: {
	struct Mem *ret = &aMem[pOp->p2];
	struct Mem *err = &aMem[pOp->p2 + 1];
	sql_show_create_table(aMem[pOp->p1].u.i, ret, err);
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

		/* The following code adds nothing to the actual functionality
		 * of the program.  It is only here for testing and debugging.
		 * On the other hand, it does burn CPU cycles every time through
		 * the evaluator loop.  So we can leave it out when NDEBUG is defined.
		 */
#ifndef NDEBUG
		assert(pOp>=&aOp[-1] && pOp<&aOp[p->nOp-1]);

#ifdef SQL_DEBUG
		if ((p->sql_flags & SQL_VdbeTrace) != 0) {
			u8 opProperty = sqlOpcodeProperty[pOrigOp->opcode];
			if (rc!=0) printf("rc=%d\n",rc);
			if ((opProperty & OPFLG_OUT2) != 0) {
				REGISTER_TRACE(p, pOrigOp->p2,
					       &aMem[pOrigOp->p2]);
			}
			if ((opProperty & OPFLG_OUT3) != 0) {
				REGISTER_TRACE(p, pOrigOp->p3,
					       &aMem[pOrigOp->p3]);
			}
		}
#endif  /* SQL_DEBUG */
#endif  /* NDEBUG */
	}  /* The end of the for(;;) loop the loops through opcodes */

	/* If we reach this point, it means that execution is finished with
	 * an error of some kind.
	 */
abort_due_to_error:
	rc = -1;
	p->is_aborted = true;

	/* This is the only way out of this procedure. */
vdbe_return:
	assert(rc == 0 || rc == -1 || rc == SQL_ROW || rc == SQL_DONE);
	return rc;

	/* Jump to here if a string or blob larger than SQL_MAX_LENGTH
	 * is encountered.
	 */
too_big:
	diag_set(ClientError, ER_SQL_EXECUTE, "string or blob too big");
	goto abort_due_to_error;
}
