#include "salad/stailq.h"
#include <stdio.h>
#include <stdarg.h>
#include "unit.h"

#define PLAN		37

#define ITEMS		7

struct test {
	char ch;
	int  no;
	struct stailq_entry next;
};

static struct test items[ITEMS];

static struct stailq head;

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
	return check_plan();
}

