/* Enable storing cardinality in inner blocks for each tree tested. */
#define BPS_INNER_CARD
/*
 * Some branches lead to dummy rebalancing (moving data of zero size) when
 * the block size is small because of integer arithmetic on reballancing.
 * This raises assertions, because some data movement routines are designed
 * to only move non-zero amount of data. Let's make the block size greater
 * to prevent this.
 */
#define SMALL_BLOCK_SIZE 128
#define COMPARE_WITH_SPTREE_CHECK_BRANCHES_ELEM_LIMIT 1024
#define TEST_COUNT_IN_LEAF 14
#define TEST_COUNT_IN_INNER 9
#include "bps_tree_generic.cc"
