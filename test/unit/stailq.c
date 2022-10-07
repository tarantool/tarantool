#include "salad/stailq.h"
#include <stdio.h>
#include <stdarg.h>

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

#define PLAN		75

#define ITEMS		7

struct test {
	char ch;
	int  no;
	struct stailq_entry next;
};

static struct test items[ITEMS];

static struct stailq head, tail;

int
main(void)
{
	int i;
	struct test *it;
	struct stailq_entry *entry;

	stailq_create(&head);

	plan(PLAN);
	ok(stailq_empty(&head), "list is empty");
	stailq_reverse(&head);
	ok(stailq_empty(&head), "list is empty after reverse");
	for (i = 0; i < ITEMS; i++) {
		items[i].no = i;
		stailq_add_tail(&head, &(items[i].next));
	}
	is(stailq_first(&head), &items[0].next, "first item");
	is(stailq_last(&head), &items[6].next, "last item");
	i = 0;
	stailq_foreach(entry, &head) {
		is(entry, &items[i].next, "element (foreach) %d", i);
		i++;
	}
	isnt(stailq_first(&head), &items[ITEMS - 1].next, "first item");

	ok(!stailq_empty(&head), "head is not empty");

	is(stailq_first_entry(&head, struct test, next),
	   &items[0], "first entry");
	for (i = 0; i < ITEMS; i++)
		is(stailq_shift(&head), &items[i].next, "shift item %d", i);
	ok(stailq_empty(&head), "list is empty after shift");

	stailq_create(&head);
	ok(stailq_empty(&head), "next is empty");
	for (i = 0; i < ITEMS; i++) {
		items[i].no = i;
		stailq_add_entry(&head, &items[i], next);
	}
	stailq_foreach_entry(it, &head, next) {
		i--;
		is(it, items + i, "element (foreach_entry) %d", i);
	}

	stailq_create(&head);
	for (i = 0; i < ITEMS; i++) {
		items[i].no = ITEMS - i;
		stailq_add_tail_entry(&head, &items[i], next);
	}
	i = 0;
	stailq_foreach_entry(it, &head, next) {
		is(it, items + i, "element (foreach_entry) %d", i);
		i++;
	}

	stailq_create(&head);
	for (i = 0; i < ITEMS; i++) {
		items[i].no = ITEMS - i;
		stailq_add_tail_entry(&head, &items[i], next);
	}
	stailq_cut_tail(&head, NULL, &tail);
	ok(stailq_empty(&head), "head is empty after cut at first");
	i = 0;
	stailq_foreach_entry(it, &tail, next) {
		is(it, items + i, "tail element after cut at first %d", i);
		i++;
	}
	stailq_concat(&head, &tail);
	stailq_cut_tail(&head, stailq_last(&head), &tail);
	ok(stailq_empty(&tail), "tail is empty after cut at last");
	i = 0;
	stailq_foreach_entry(it, &head, next) {
		is(it, items + i, "head element after cut at last %d", i);
		i++;
	}
	stailq_concat(&head, &tail);
	stailq_cut_tail(&head, &items[3].next, &tail);
	i = 0;
	stailq_foreach_entry(it, &head, next) {
		is(it, items + i, "head element after cut at middle %d", i);
		i++;
	}
	stailq_foreach_entry(it, &tail, next) {
		is(it, items + i, "tail element after cut at middle %d", i);
		i++;
	}
	stailq_concat(&head, &tail);
	ok(stailq_empty(&tail), "tail is empty after concat");
	i = 0;
	stailq_foreach_entry(it, &head, next) {
		is(it, items + i, "head element after concat %d", i);
		i++;
	}

	stailq_create(&head);
	stailq_add_entry(&head, &items[0], next);
	stailq_insert(&head, &items[2].next, &items[0].next);
	stailq_insert(&head, &items[1].next, &items[0].next);
	stailq_insert_entry(&head, &items[4], &items[2], next);
	stailq_insert_entry(&head, &items[3], &items[2], next);
	i = 0;
	stailq_foreach_entry(it, &head, next) {
		is(it, items + i, "element %d (insert)", i);
		i++;
	}
	is(stailq_first(&head), &items[0].next, "first item (insert)");
	is(stailq_last(&head), &items[4].next, "last item (insert)");
	return check_plan();
}
