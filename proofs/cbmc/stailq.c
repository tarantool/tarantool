#include "salad/stailq.h"
#include "nondet.h"

/* stailq struct. */
struct PACKED test {
	/* Payload. */
	int no;
	/* Next in the list. */
	struct stailq_entry next;
};

static void
stailq_add_harness(void)
{
	static struct stailq head;
	struct test items[1];

	stailq_create(&head);

	stailq_add(&head, &items[0].next);
	__CPROVER_assert(stailq_first(&head) == &items[0].next, "first item");
	__CPROVER_assert(stailq_last(&head) == &items[0].next, "last item");
}

static void
stailq_add_tail_harness(void)
{
	static struct stailq head;
	struct test items[1];

	stailq_create(&head);

	stailq_add_tail(&head, &items[0].next);
	__CPROVER_assert(stailq_first(&head) == &items[0].next, "first item");
	__CPROVER_assert(stailq_last(&head) == &items[0].next, "last item");
}

static void
stailq_concat_harness(void)
{
	static struct stailq head, tail;
	struct test items[1];

	stailq_create(&head);
	stailq_create(&tail);

	/* Concatenation of two empty lists. */
	stailq_concat(&head, &tail);
	__CPROVER_assert(stailq_empty(&tail), "tail is not empty");

	/* Concatenation of two non-empty lists. */
	stailq_concat(&head, &tail);
	stailq_cut_tail(&head, &items[0].next, &tail);
	stailq_concat(&head, &tail);
	__CPROVER_assert(stailq_empty(&tail), "tail is empty after concat");
}

static void
stailq_create_harness(void)
{
	static struct stailq head;

	stailq_create(&head);
	__CPROVER_assert(stailq_empty(&head), "list is empty");
	__CPROVER_assert(stailq_first(&head) == NULL, "first item is NULL");
	__CPROVER_assert(stailq_last(&head) == NULL, "last item is NULL");
}

static void
stailq_cut_tail_harness(void)
{
	static struct stailq head, tail;
	struct test items[1];

	stailq_cut_tail(&head, NULL, &tail);
	__CPROVER_assert(stailq_empty(&head) == 1, "head is empty");
}

static void
stailq_empty_harness(void)
{
	static struct stailq head;
	struct test items[1];

	stailq_create(&head);
	__CPROVER_assert(stailq_empty(&head) == 1, "list is empty");

	stailq_add_tail(&head, &items[0].next);
	__CPROVER_assert(stailq_empty(&head) == 0, "list is not empty");
}

static void
stailq_first_harness(void)
{
	static struct stailq head;
	int N = 2;
	struct test items[N];

	stailq_create(&head);
	__CPROVER_assert(stailq_first(&head) == NULL, "first item");

	items[0].no = nondet_int();
	stailq_add_tail(&head, &items[0].next);

	__CPROVER_assert(stailq_first(&head) != &items[1].next, "first item");
}

static void
stailq_insert_harness(void)
{
	static struct stailq head;
	int n = 5;
	struct test items[n];

	stailq_create(&head);
	stailq_add_entry(&head, &items[0], next);

	stailq_insert(&head, &items[2].next, &items[0].next);
	stailq_insert(&head, &items[1].next, &items[0].next);

	stailq_insert_entry(&head, &items[4], &items[2], next);
	stailq_insert_entry(&head, &items[3], &items[2], next);

	__CPROVER_assert(stailq_first(&head) == &items[0].next,
			 "first item (insert)");
	__CPROVER_assert(stailq_last(&head) == &items[4].next,
			 "last item (insert)");
}

static void
stailq_insert_entry_harness(void)
{
	static struct stailq head;
	int n = 5;
	struct test items[n];

	stailq_create(&head);
	stailq_add_entry(&head, &items[0], next);

	stailq_insert(&head, &items[2].next, &items[0].next);
	stailq_insert(&head, &items[1].next, &items[0].next);

	stailq_insert_entry(&head, &items[4], &items[2], next);
	stailq_insert_entry(&head, &items[3], &items[2], next);

	__CPROVER_assert(stailq_first(&head) == &items[0].next,
			 "first item (insert)");
	__CPROVER_assert(stailq_last(&head) == &items[4].next,
			 "last item (insert)");
}

static void
stailq_last_harness(void)
{
	static struct stailq head;
	int N = 2;
	struct test items[N];

	stailq_create(&head);
	__CPROVER_assert(stailq_last(&head) == NULL, "last item");

	items[0].no = nondet_int();
	stailq_add_tail(&head, &items[0].next);

	__CPROVER_assert(stailq_last(&head) != &items[N - 1].next,
			 "last item");
}

static void
stailq_next_harness(void)
{
	static struct stailq head;
}

static void
stailq_reverse_harness(void)
{
	static struct stailq head;

	stailq_create(&head);

	/* Reverse empty list. */
	__CPROVER_assert(stailq_empty(&head), "list is empty");
	stailq_reverse(&head);
	__CPROVER_assert(stailq_empty(&head), "list is empty after reverse");

	/* Reverse non-empty list. */
	int N = 3;
	struct test items[N];
	items[0].no = nondet_int();
	items[1].no = nondet_int();
	items[2].no = nondet_int();

	stailq_add_entry(&head, &items[0], next);
	stailq_add_entry(&head, &items[1], next);
	stailq_add_entry(&head, &items[2], next);
	stailq_reverse(&head);
	__CPROVER_assert(stailq_shift(&head) == &items[0].next, "shift item");
	__CPROVER_assert(stailq_shift(&head) == &items[1].next, "shift item");
	__CPROVER_assert(stailq_shift(&head) == &items[2].next, "shift item");
}

static void
stailq_shift_harness(void)
{
	static struct stailq head;
	struct test items[1];
	items[0].no = nondet_int();

	stailq_create(&head);
	stailq_add_entry(&head, &items[0], next);

	__CPROVER_assert(stailq_shift(&head) == &items[0].next, "shift item");
	__CPROVER_assert(stailq_empty(&head), "list is empty after shift");
}

int
main(void)
{
#if defined(STAILQ_ADD)
	stailq_add_harness();
#elif defined(STAILQ_ADD_TAIL)
	stailq_add_tail_harness();
#elif defined(STAILQ_CONCAT)
	stailq_concat_harness();
#elif defined(STAILQ_CREATE)
	stailq_create_harness();
#elif defined(STAILQ_CUT_TAIL)
	stailq_cut_tail_harness();
#elif defined(STAILQ_EMPTY)
	stailq_empty_harness();
#elif defined(STAILQ_FIRST)
	stailq_first_harness();
#elif defined(STAILQ_INSERT)
	stailq_insert_harness();
#elif defined(STAILQ_INSERT_ENTRY)
	stailq_insert_entry_harness();
#elif defined(STAILQ_LAST)
	stailq_last_harness();
#elif defined(STAILQ_NEXT)
	stailq_next_harness();
#elif defined(STAILQ_REVERSE)
	stailq_reverse_harness();
#elif defined(STAILQ_SHIFT)
	stailq_shift_harness();
#endif
	return 0;
}
