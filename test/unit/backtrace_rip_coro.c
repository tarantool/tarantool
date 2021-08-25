#include "trivia/util.h"
#include "coro/coro.h"
#include "unit.h"

#include <libunwind.h>

#ifdef TARGET_OS_DARWIN
#include <dlfcn.h>
#endif

enum {
	BACKTRACE_RIP_LIMIT = 8
};

struct co_data {
	struct coro_context *parent_ctx;
	struct coro_context *child_ctx;
	int rip_cnt;
	void *rip_buf[BACKTRACE_RIP_LIMIT];
};

static int NOINLINE
rip_getcontext(void **rip_buf, int limit)
{
#ifndef TARGET_OS_DARWIN
	int frame_cnt = unw_backtrace(rip_buf, limit);
	if (frame_cnt < limit)
		rip_buf[frame_cnt] = NULL;

	return frame_cnt;
#else
	unw_cursor_t unw_cur;
	unw_context_t unw_ctx;
	int frame_no = 0;
	unw_word_t rip = 0;

	unw_getcontext(&unw_ctx);
	unw_init_local(&unw_cur, &unw_ctx);

	if (limit > 0)
		rip_buf[0] = (void *)rip_getcontext;

	for (frame_no = 1; frame_no < limit && unw_step(&unw_cur) > 0;
	     ++frame_no) {
		unw_get_reg(&unw_cur, UNW_REG_IP, &rip);
		rip_buf[frame_no] = (void *)rip;
	}

	return frame_no;
#endif /* TARGET_OS_DARWIN */
}

static int NOINLINE
rip_get_proc_name(void *rip, char bufp[], size_t buf_size)
{
#ifndef TARGET_OS_DARWIN
	unw_accessors_t *acc = unw_get_accessors(unw_local_addr_space);
	if (acc->get_proc_name != NULL) {
		unw_word_t offset = 0;
		if (acc->get_proc_name(unw_local_addr_space, (unw_word_t)rip,
				       bufp, buf_size, &offset, NULL) != 0) {
			diag("ERROR: get_proc_name() != 0");
			return -1;
		}
	} else {
		diag("ERROR: get_proc_name == NULL");
		return -1;
	}
#else
	Dl_info dli;
	if (dladdr(rip, &dli) == 0) {
		diag("ERROR: dladdr() == 0");
		return -1;
	}

	strncpy(bufp, dli.dli_sname, buf_size);
	bufp[buf_size - 1] = '\0';
#endif /* TARGET_OS_DARWIN */

	return 0;
}

static void NOINLINE
foo(struct co_data *data)
{
	data->rip_cnt = rip_getcontext(data->rip_buf, BACKTRACE_RIP_LIMIT);
	coro_transfer(data->child_ctx, data->parent_ctx);
}

static void NOINLINE
bar(struct co_data *data)
{
	foo(data);
}

static void NOINLINE
baz(struct co_data *data)
{
	bar(data);
}

static void
co_fnc(void *arg)
{
	struct co_data *data = arg;
	baz(data);
}

static void
test_unw()
{
	header();

	const unsigned int ssze = 1 << 16;
	struct coro_stack co_stk;
	struct coro_context parent_ctx;
	struct coro_context child_ctx;

	unw_context_t unw_ctx;
	unw_cursor_t unw_cur;
	struct co_data data = {
		.parent_ctx = &parent_ctx,
		.child_ctx = &child_ctx,
		.rip_cnt = 0,
		.rip_buf = {},
	};

	char proc_name[256];
	unw_word_t offset = 0;

	fail_if(coro_stack_alloc(&co_stk, ssze) == 0);
	// Empty context, used for initial coro_transfer
	coro_create(&parent_ctx, NULL, NULL, NULL, 0);
	coro_create(&child_ctx, co_fnc, &data, co_stk.sptr, co_stk.ssze);
	coro_transfer(&parent_ctx, &child_ctx);

	// Skip the first frame corresponding to the collecting function itself
	fail_unless(data.rip_cnt > 2);
	fail_if(rip_get_proc_name(data.rip_buf[1], proc_name,
				  sizeof(proc_name)) != 0);
	note("TOP %s", proc_name);
	fail_if(rip_get_proc_name(data.rip_buf[data.rip_cnt - 1], proc_name,
				  sizeof(proc_name)) != 0);
	note("BOTTOM %s", proc_name);

	coro_transfer(&parent_ctx, &child_ctx);
	coro_destroy(&parent_ctx);
	coro_destroy(&child_ctx);
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
