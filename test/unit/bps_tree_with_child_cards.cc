/* Enable child_cards array for each tree tested. */
#define BPS_INNER_CHILD_CARDS
/*
 * Some branches lead to dummy rebalancing (moving data of zero size) when
 * the block size is small because of integer arithmetic on reballancing.
 * This raises assertions, because some data movement routines are designed
 * to only move non-zero amount of data. Let's make the block size greater
 * to prevent this.
 */
#define SMALL_BLOCK_SIZE 256
#define COMPARE_WITH_SPTREE_CHECK_BRANCHES_ELEM_LIMIT 2048
#define TEST_COUNT_IN_LEAF 30
#define TEST_COUNT_IN_INNER 12
#include "bps_tree_generic.cc"
