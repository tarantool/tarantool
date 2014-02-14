#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#include "unit.h"
#include "avl_tree.h"
#include "../third_party/sptree.h"

#ifndef MAX
#define MAX(a,b) (((a)>(b))?(a):(b))
#endif //#ifndef MAX

SPTREE_DEF(test, realloc);
AVL_DEF(test, realloc);

static int
node_comp(const void *p1, const void *p2, void* unused)
{
	(void)unused;
	return *((const int *)p1) - *((const int *)p2);
}

static void
simple_check()
{
	header();
	
	avl_test t;
	
	int ini[] = {1, 10, 2, 9, 3, 8, 4, 7, 5, 6};
	int *use = (int *)malloc(sizeof(ini));
	memcpy(use, ini, sizeof(ini));
	int nuse = (int)(sizeof(ini) / sizeof(ini[0]));
	avl_test_init(&t, sizeof(int), use, nuse, 0, &node_comp, 0, 0);
	
	int add[] = {-1, 11, 0, 12};
	for (size_t i = 0; i < sizeof(add) / sizeof(add[0]); i++)
		avl_test_replace(&t, &add[i], 0);

	int rem[] = {3, 5, 7};
	for (size_t i = 0; i < sizeof(rem) / sizeof(rem[0]); i++)
		avl_test_delete(&t, &rem[i]);

	int *val;
	
	val = avl_test_first(&t);
	printf("%d ", *val);
	val = avl_test_last(&t);
	printf("%d\n", *val);
	
	avl_test_iterator* itr = avl_test_iterator_init(&t);
	while ((val = avl_test_iterator_next(itr)))
		printf("%d ", *val);
	printf("\n");
	avl_test_iterator_free(itr);
	
	itr = avl_test_iterator_reverse_init(&t);
	while ((val = avl_test_iterator_reverse_next(itr)))
		printf("%d ", *val);
	printf("\n");
	avl_test_iterator_free(itr);
	
	avl_test_destroy(&t);

	
	footer();
}

static void
compare_with_sptree_check()
{
	header();

	sptree_test spt_test;
	sptree_test_init(&spt_test, sizeof(int), 0, 0, 0, &node_comp, 0, 0);
	avl_test avl_test;
	avl_test_init(&avl_test, sizeof(int), 0, 0, 0, &node_comp, 0, 0);
	
	for (int i = 0; i < 64 * 1024; i++) {
		int rnd = rand() % (16 * 1024);
		int find_res1 = sptree_test_find(&spt_test, &rnd) ? 1 : 0;
		int find_res2 = avl_test_find(&avl_test, &rnd) ? 1 : 0;
		if (find_res1 ^ find_res2) {
			fail("trees identity", "false");
			continue;
		}
			
		if (find_res1 == 0) {
			sptree_test_replace(&spt_test, &rnd, NULL);
			avl_test_replace(&avl_test, &rnd, NULL);
			
		} else {
			sptree_test_delete(&spt_test, &rnd);
			avl_test_delete(&avl_test, &rnd);
		}
	}
	sptree_test_destroy(&spt_test);
	avl_test_destroy(&avl_test);
	
	footer();
}

int
main(void)
{
	simple_check();
	compare_with_sptree_check();
}