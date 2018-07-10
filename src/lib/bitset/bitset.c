/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "bitset/bitset.h"
#include "page.h"

#include <stddef.h>
#include <string.h>
#include <assert.h>

void
tt_bitset_create(struct tt_bitset *bitset,
		 void *(*realloc)(void *ptr, size_t size))
{
	memset(bitset, 0, sizeof(*bitset));
	bitset->realloc = realloc;

	/* Initialize pages tree */
	tt_bitset_pages_new(&bitset->pages);
}

static struct tt_bitset_page *
tt_bitset_destroy_iter_cb(tt_bitset_pages_t *t, struct tt_bitset_page *page,
			  void *arg)
{
	(void) t;
	struct tt_bitset *bitset = (struct tt_bitset *) arg;
	tt_bitset_page_destroy(page);
	bitset->realloc(page, 0);
	return NULL;
}

void
tt_bitset_destroy(struct tt_bitset *bitset)
{
	tt_bitset_pages_iter(&bitset->pages, NULL, tt_bitset_destroy_iter_cb,
			     bitset);
	memset(&bitset->pages, 0, sizeof(bitset->pages));
}

bool
tt_bitset_test(struct tt_bitset *bitset, size_t pos)
{
	struct tt_bitset_page key;
	key.first_pos = tt_bitset_page_first_pos(pos);

	/* Find a page in pages tree */
	struct tt_bitset_page *page =
		tt_bitset_pages_search(&bitset->pages, &key);
	if (page == NULL)
		return false;

	assert(page->first_pos <= pos && pos < page->first_pos +
	       BITSET_PAGE_DATA_SIZE * CHAR_BIT);
	return bit_test(tt_bitset_page_data(page), pos - page->first_pos);
}

int
tt_bitset_set(struct tt_bitset *bitset, size_t pos)
{
	struct tt_bitset_page key;
	key.first_pos = tt_bitset_page_first_pos(pos);

	/* Find a page in pages tree */
	struct tt_bitset_page *page =
		tt_bitset_pages_search(&bitset->pages, &key);
	if (page == NULL) {
		/* Allocate a new page */
		size_t size = tt_bitset_page_alloc_size(bitset->realloc);
		page = bitset->realloc(NULL, size);
		if (page == NULL)
			return -1;

		tt_bitset_page_create(page);
		page->first_pos = key.first_pos;

		/* Insert the page into pages tree */
		tt_bitset_pages_insert(&bitset->pages, page);
	}

	assert(page->first_pos <= pos && pos < page->first_pos +
	       BITSET_PAGE_DATA_SIZE * CHAR_BIT);
	bool prev = bit_set(tt_bitset_page_data(page), pos - page->first_pos);
	if (prev) {
		/* Value has not changed */
		return 1;
	}

	bitset->cardinality++;
	page->cardinality++;

	return 0;
}

int
tt_bitset_clear(struct tt_bitset *bitset, size_t pos)
{
	struct tt_bitset_page key;
	key.first_pos = tt_bitset_page_first_pos(pos);

	/* Find a page in the pages tree */
	struct tt_bitset_page *page =
		tt_bitset_pages_search(&bitset->pages, &key);
	if (page == NULL)
		return 0;

	assert(page->first_pos <= pos && pos < page->first_pos +
	       BITSET_PAGE_DATA_SIZE * CHAR_BIT);
	bool prev = bit_clear(tt_bitset_page_data(page), pos - page->first_pos);
	if (!prev) {
		return 0;
	}

	assert(bitset->cardinality > 0);
	assert(page->cardinality > 0);
	bitset->cardinality--;
	page->cardinality--;

	if (page->cardinality == 0) {
		/* Remove the page from the pages tree */
		tt_bitset_pages_remove(&bitset->pages, page);
		/* Free the page */
		tt_bitset_page_destroy(page);
		bitset->realloc(page, 0);
	}

	return 1;
}

extern inline size_t
tt_bitset_cardinality(const struct tt_bitset *bitset);

void
tt_bitset_info(struct tt_bitset *bitset, struct tt_bitset_info *info)
{
	memset(info, 0, sizeof(*info));
	info->page_data_size = BITSET_PAGE_DATA_SIZE;
	info->page_total_size = tt_bitset_page_alloc_size(bitset->realloc);
	info->page_data_alignment = BITSET_PAGE_DATA_ALIGNMENT;

	size_t cardinality_check = 0;
	struct tt_bitset_page *page = tt_bitset_pages_first(&bitset->pages);
	while (page != NULL) {
		info->pages++;
		cardinality_check += page->cardinality;
		page = tt_bitset_pages_next(&bitset->pages, page);
	}

	assert(tt_bitset_cardinality(bitset) == cardinality_check);
}

#if defined(DEBUG)
void
tt_bitset_dump(struct tt_bitset *bitset, int verbose, FILE *stream)
{
	struct tt_bitset_info info;
	tt_bitset_info(bitset, &info);

	size_t PAGE_BIT = (info.page_data_size * CHAR_BIT);

	fprintf(stream, "Bitset %p\n", bitset);
	fprintf(stream, "{\n");
	fprintf(stream, "    " "page_size   = %zu/%zu /* (data / total) */\n",
		info.page_data_size, info.page_total_size);
	fprintf(stream, "    " "page_bit    = %zu\n", PAGE_BIT);
	fprintf(stream, "    " "pages       = %zu\n", info.pages);


	size_t cardinality = bitset_cardinality(bitset);
	size_t capacity = PAGE_BIT * info.pages;
	fprintf(stream, "    " "cardinality = %zu\n", cardinality);
	fprintf(stream, "    " "capacity    = %zu\n", capacity);

	if (capacity > 0) {
		fprintf(stream, "    "
			"utilization = %-8.4f%% (%zu / %zu)\n",
			(float) cardinality * 100.0 / (capacity),
			cardinality,
			capacity
			);
	} else {
		fprintf(stream, "    "
			"utilization = undefined\n");
	}
	size_t mem_data  = info.page_data_size * info.pages;
	size_t mem_total = info.page_total_size * info.pages;

	fprintf(stream, "    " "mem_data    = %zu bytes\n", mem_data);
	fprintf(stream, "    " "mem_total   = %zu bytes "
		"/* data + padding + tree */\n", mem_total);
	if (cardinality > 0) {
		fprintf(stream, "    "
			"density     = %-8.4f bytes per value\n",
			(float) mem_total / cardinality);
	} else {
		fprintf(stream, "    "
			"density     = undefined\n");
	}

	if (verbose < 1) {
		goto exit;
	}

	fprintf(stream, "    " "pages = {\n");

	for (struct tt_bitset_page *page = tt_bitset_pages_first(&bitset->pages);
	     page != NULL; page = tt_bitset_pages_next(&bitset->pages, page)) {

		size_t page_last_pos = page->first_pos
				+ BITSET_PAGE_DATA_SIZE * CHAR_BIT;

		fprintf(stream, "        " "[%zu, %zu) ",
			page->first_pos, page_last_pos);

		fprintf(stream, "utilization = %8.4f%% (%zu/%zu)",
			(float) page->cardinality * 1e2 / PAGE_BIT,
			page->cardinality, PAGE_BIT);

		if (verbose < 2) {
			fprintf(stream, "\n");
			continue;
		}
		fprintf(stream, " ");

		fprintf(stream, "vals = {");

		size_t pos = 0;
		struct bit_iterator it;
		bit_iterator_init(&it, bitset_page_data(page),
			      BITSET_PAGE_DATA_SIZE, true);
		while ( (pos = bit_iterator_next(&it)) != SIZE_MAX) {
			fprintf(stream, "%zu, ", page->first_pos + pos);
		}

		fprintf(stream, "}\n");
	}

	fprintf(stream, "    " "}\n");

exit:
	fprintf(stream, "}\n");
}
#endif /* defined(DEBUG) */

