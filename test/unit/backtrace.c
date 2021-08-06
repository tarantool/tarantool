#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "backtrace.h"
#include "memory.h"
#include "fiber.h"
#include "unit.h"
#include "trivia/util.h"

enum {
	BT_NAME_MAX = 64,
	BT_ENTRIES_MAX = 32,
	BT_RECURSE_CNT = 10,
};

struct bt_entry {
	char name[BT_NAME_MAX];
	void *addr;
	size_t offset;
};

static int
save_entry_cb(int idx, void *addr, const char *name, size_t offset,
	      void *cb_arg)
{
	if (idx >= BT_ENTRIES_MAX)
		return 1;

	struct bt_entry *entry_buf = (struct bt_entry *)cb_arg;
	entry_buf[idx].name[BT_NAME_MAX - 1] = '\0';
	strncpy(entry_buf[idx].name, name, BT_NAME_MAX - 1);
	entry_buf[idx].addr = addr;
	entry_buf[idx].offset = offset;

	return 0;
}

static int
compare_entries(const struct bt_entry *lhs, const struct bt_entry *rhs)
{
	if (strncmp(lhs->name, rhs->name, BT_NAME_MAX) != 0)
		return 1;
	if (lhs->addr != NULL && rhs->addr != NULL && lhs->addr != rhs->addr)
		return 1;
	if (lhs->offset != 0 && rhs->offset != 0 && lhs->offset != rhs->offset)
		return 1;

	return 0;
}

static int NOINLINE
foo(struct backtrace *bt, struct bt_entry entry_buf[])
{
	note("Collecting backtrace...");
#ifdef ENABLE_BACKTRACE
	backtrace_collect(bt, fiber());
	backtrace_foreach_current(save_entry_cb, NULL, fiber(), entry_buf);
#endif
	note("ok");

	return 0;
}

static int NOINLINE
bar(struct backtrace *bt, struct bt_entry entry_buf[])
{
	note("Calling foo()");
	int depth = foo(bt, entry_buf);

	return 1 + depth;
}

static int NOINLINE
baz(int n, struct backtrace *bt, struct bt_entry entry_buf[])
{
	int depth = 0;
	if (n <= 0) {
		note("Calling bar()");
		depth = bar(bt, entry_buf);
	} else {
		note("Calling baz()");
		depth = baz(n - 1, bt, entry_buf);
	}

	return 1 + depth;
}

static void
test_equal()
{
	header();

	struct backtrace bt;
	struct bt_entry entry_buf_local[BT_ENTRIES_MAX];
	struct bt_entry entry_buf_new[BT_ENTRIES_MAX];

	note("Calling baz()");
	int call_cnt = baz(BT_RECURSE_CNT, &bt, entry_buf_local);

	note("Resolving entries...");
#ifdef ENABLE_BACKTRACE
	int entries_cnt = backtrace_foreach(&bt, save_entry_cb, NULL,
					    entry_buf_new);
#endif
	note("ok");

	note("Comparing entries...");
#ifdef ENABLE_BACKTRACE
	int frame_max = MIN(call_cnt, entries_cnt);
	int frame_no = 0;
	for (; frame_no + 1 < frame_max; ++frame_no) {
		note("#%d %.*s", frame_no, BT_NAME_MAX,
		     entry_buf_new[frame_no].name);
		if (frame_no == 0) {
			fail_unless(strncmp(entry_buf_new[frame_no].name, "foo",
					    BT_NAME_MAX) == 0);
		} else if (frame_no == 1) {
			fail_unless(strncmp(entry_buf_new[frame_no].name, "bar",
					    BT_NAME_MAX) == 0);
		} else if (frame_no <= 2 + BT_RECURSE_CNT) {
			fail_unless(strncmp(entry_buf_new[frame_no].name, "baz",
					    BT_NAME_MAX) == 0);
		}
		fail_unless(compare_entries(&entry_buf_local[frame_no + 1],
					    &entry_buf_new[frame_no + 1]) == 0);
	}
#endif
	note("ok");

	footer();
}

int
main(int argc, char *argv[])
{
	setbuf(stdout, NULL);
	memory_init();
	fiber_init(fiber_c_invoke);
	backtrace_init(NULL, NULL);
	test_equal();
	fiber_free();
	memory_free();

	return 0;
}
