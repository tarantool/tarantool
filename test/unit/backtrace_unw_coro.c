#include <libunwind.h>

#include "coro/coro.h"
#include "unit.h"
#include "trivia/util.h"

struct co_data {
	struct coro_context *parent_ctx;
	struct coro_context *child_ctx;
	unw_context_t *unw_ctx;
};

static void NOINLINE
foo(struct co_data *data)
{
	unw_getcontext(data->unw_ctx);
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
		.unw_ctx = &unw_ctx,
	};

	char proc_name[256];
	unw_word_t offset = 0;

	fail_if(coro_stack_alloc(&co_stk, ssze) == 0);
	// Empty context, used for initial coro_transfer
	coro_create(&parent_ctx, NULL, NULL, NULL, 0);
	coro_create(&child_ctx, co_fnc, &data, co_stk.sptr, co_stk.ssze);
	coro_transfer(&parent_ctx, &child_ctx);

	unw_init_local(&unw_cur, &unw_ctx);
	fail_if(unw_get_proc_name(&unw_cur, proc_name,
				  sizeof(proc_name), &offset) != 0);
	note("TOP %s", proc_name);
	while (unw_step(&unw_cur) > 0) {
		// `proc_name` and `offset` may differ under different systems.
		fail_if(unw_get_proc_name(&unw_cur, proc_name,
					  sizeof(proc_name), &offset) != 0);
	}
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
