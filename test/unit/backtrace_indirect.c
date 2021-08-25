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

static void
co_fnc(void *arg)
{
	struct data *data = arg;
	baz(data);
}

static void
co_backtrace(void *rip_buf[], int *rip_cnt, struct coro_context *coro_ctx)
{
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
	"\tandq $0xfffffffffffffff0, %%rsp\n"
	"\tleaq %P4(%%rip), %%rax\n"
	"\tcall *%%rax\n"
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
