#include <libunwind.h>

#include "trivia/util.h"
#include "coro/coro.h"
#include "unit.h"

enum {
	BACKTRACE_RIP_LIMIT = 8
};

struct data {
	struct coro_context parent_ctx;
	struct coro_context child_ctx;
	int csw;
};

#ifdef __x86_64__
__attribute__ ((force_align_arg_pointer))
#endif /* __x86_64__ */
static void * NOINLINE
rip_getcontext(void **rip_buf, int *rip_cnt, void *stack)
{
#ifndef TARGET_OS_DARWIN
	int frame_cnt = unw_backtrace(rip_buf, BACKTRACE_RIP_LIMIT);
	if (frame_cnt < BACKTRACE_RIP_LIMIT)
		rip_buf[frame_cnt] = NULL;

	if (rip_cnt != NULL)
		*rip_cnt = frame_cnt;
#else
	unw_cursor_t unw_cur;
	unw_context_t unw_ctx;
	int frame_no = 0;
	unw_word_t rip = 0;

	unw_getcontext(&unw_ctx);
	unw_init_local(&unw_cur, &unw_ctx);

	rip_buf[0] = (void *)rip_getcontext;
	for (frame_no = 1;
	     frame_no < BACKTRACE_RIP_LIMIT && unw_step(&unw_cur) > 0;
	     ++frame_no) {
		unw_get_reg(&unw_cur, UNW_REG_IP, &rip);
		rip_buf[frame_no] = (void *)rip;
	}

	if (rip_cnt != NULL)
		*rip_cnt = frame_no;
#endif /* TARGET_OS_DARWIN */

	return stack;
}

static void NOINLINE
foo(struct data *data)
{
	++data->csw;
	coro_transfer(&data->child_ctx, &data->parent_ctx);
	++data->csw;
	coro_transfer(&data->child_ctx, &data->parent_ctx);
}

static void NOINLINE
bar(struct data *data)
{
	foo(data);
}

static void NOINLINE
baz(struct data *data)
{
	bar(data);
}

static void NOINLINE
co_fnc(void *arg)
{
	struct data *data = arg;
	baz(data);
}

static void NOINLINE
co_backtrace(void *rip_buf[], int *rip_cnt, struct coro_context *coro_ctx)
{
#if __x86_64__
	__asm__ volatile(
	/* Preserve current context */
	"\tpushq %%rbp\n"
	"\tpushq %%rbx\n"
	"\tpushq %%r12\n"
	"\tpushq %%r13\n"
	"\tpushq %%r14\n"
	"\tpushq %%r15\n"
	/* Set first arg */
	"\tmovq %1, %%rdi\n"
	/* Set second arg */
	"\tmovq %2, %%rsi\n"
	/* Setup third arg as old sp */
	"\tmovq %%rsp, %%rdx\n"
	/* Restore target context, but not increment sp to preserve it */
	"\tmovq 0(%3), %%rsp\n"
	"\tmovq 0(%%rsp), %%r15\n"
	"\tmovq 8(%%rsp), %%r14\n"
	"\tmovq 16(%%rsp), %%r13\n"
	"\tmovq 24(%%rsp), %%r12\n"
	"\tmovq 32(%%rsp), %%rbx\n"
	"\tmovq 40(%%rsp), %%rbp\n"
	".cfi_remember_state\n"
	"\t.cfi_def_cfa %%rsp, 8 * 7\n"
	"\tleaq %P4(%%rip), %%rax\n"
	"\tcall *%%rax\n"
	".cfi_restore_state\n"
	/* Restore old sp and context */
	"\tmov %%rax, %%rsp\n"
	"\tpopq %%r15\n"
	"\tpopq %%r14\n"
	"\tpopq %%r13\n"
	"\tpopq %%r12\n"
	"\tpopq %%rbx\n"
	"\tpopq %%rbp\n"
	: "=m" (*rip_buf)
	: "r" (rip_buf), "r" (rip_cnt), "r" (coro_ctx), "i" (rip_getcontext)
	: "rdi", "rsi", "rdx", "rax", "memory"
	);
#elif __aarch64__
__asm__ volatile(
	/* Setup first arg */
	"\tmov x0, %1\n"
	/* Setup second arg */
	"\tmov x1, %2\n"
	/* Save current context */
	"\tsub x2, sp, #8 * 20\n"
	"\tstp x19, x20, [x2, #16 * 0]\n"
	"\tstp x21, x22, [x2, #16 * 1]\n"
	"\tstp x23, x24, [x2, #16 * 2]\n"
	"\tstp x25, x26, [x2, #16 * 3]\n"
	"\tstp x27, x28, [x2, #16 * 4]\n"
	"\tstp x29, x30, [x2, #16 * 5]\n"
	"\tstp d8,  d9,  [x2, #16 * 6]\n"
	"\tstp d10, d11, [x2, #16 * 7]\n"
	"\tstp d12, d13, [x2, #16 * 8]\n"
	"\tstp d14, d15, [x2, #16 * 9]\n"
	/* Restore target context */
	"\tldr x3, [%3]\n"
	"\tldp x19, x20, [x3, #16 * 0]\n"
	"\tldp x21, x22, [x3, #16 * 1]\n"
	"\tldp x23, x24, [x3, #16 * 2]\n"
	"\tldp x25, x26, [x3, #16 * 3]\n"
	"\tldp x27, x28, [x3, #16 * 4]\n"
	"\tldp x29, x30, [x3, #16 * 5]\n"
	"\tldp d8,  d9,  [x3, #16 * 6]\n"
	"\tldp d10, d11, [x3, #16 * 7]\n"
	"\tldp d12, d13, [x3, #16 * 8]\n"
	"\tldp d14, d15, [x3, #16 * 9]\n"
	"\tmov sp, x3\n"
	".cfi_remember_state\n"
	".cfi_def_cfa sp, 16 * 10\n"
	".cfi_offset x29, -16 * 5\n"
	".cfi_offset x30, -16 * 5 + 8\n"
	"\tbl %4\n"
	".cfi_restore_state\n"
	/* Restore context (old sp in x0) */
	"\tldp x19, x20, [x0, #16 * 0]\n"
	"\tldp x21, x22, [x0, #16 * 1]\n"
	"\tldp x23, x24, [x0, #16 * 2]\n"
	"\tldp x25, x26, [x0, #16 * 3]\n"
	"\tldp x27, x28, [x0, #16 * 4]\n"
	"\tldp x29, x30, [x0, #16 * 5]\n"
	"\tldp d8,  d9,  [x0, #16 * 6]\n"
	"\tldp d10, d11, [x0, #16 * 7]\n"
	"\tldp d12, d13, [x0, #16 * 8]\n"
	"\tldp d14, d15, [x0, #16 * 9]\n"
	"\tadd sp, x0, #8 * 20\n"
	: "=m" (*rip_buf)
	: "r" (rip_buf), "r" (rip_cnt), "r" (coro_ctx), "S" (rip_getcontext)
	: /*"lr", "r0", "r1", "r2", "ip" */
	 "x0", "x1", "x2", "x3", "x30", "memory"
	);
#endif
}

static void
test_unw()
{
	header();

	const unsigned int ssze = 1 << 16;
	struct coro_stack co_stk;

	int rip_cnt = 0;
	void *rip_buf[BACKTRACE_RIP_LIMIT];
	struct data data;

	fail_if(coro_stack_alloc(&co_stk, ssze) == 0);
	// Empty context, used for initial coro_transfer
	coro_create(&data.parent_ctx, NULL, NULL, NULL, 0);
	coro_create(&data.child_ctx, co_fnc, &data, co_stk.sptr, co_stk.ssze);
	data.csw = 0;

	coro_transfer(&data.parent_ctx, &data.child_ctx);
	fail_unless(data.csw == 1);

	co_backtrace(rip_buf, &rip_cnt, &data.child_ctx);

	coro_transfer(&data.parent_ctx, &data.child_ctx);
	fail_unless(data.csw == 2);

	coro_destroy(&data.parent_ctx);
	coro_destroy(&data.child_ctx);
	coro_stack_free(&co_stk);

	footer();
}

int
main(int argc, char* argv[])
{
	setbuf(stdout, NULL);
	test_unw();

	return 0;
}
