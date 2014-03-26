#include "small/pt_alloc.h"
#include <set>
#include <vector>
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <iostream>

static void *
pta_alloc();
static void
pta_free(void *p);

#define PROV_BLOCK_SIZE 16
#define PROV_EXTENT_SIZE 64

static size_t AllocatedCount = 0;
static std::set<void*> AllocatedBlocks;
static std::set<void*> AllocatedItems;

static void
check_file_line(bool expr, const char *err_message, const char *file, int line)
{
	if (!expr) {
		std::cout << " ****************************************\n"
		          << " * " << file << ":" << line
		          << " ERROR: " << err_message << "\n";
	}
	assert(expr);
	if (!expr) {
		throw err_message;
	}
}

#define check(e, m) check_file_line(e, m, __FILE__, __LINE__)

bool alloc_err_inj_enabled = false;
unsigned int alloc_err_inj_countdown = 0;

static void *
pta_alloc()
{
	if (alloc_err_inj_enabled) {
		if (alloc_err_inj_countdown == 0)
			return 0;
		alloc_err_inj_countdown--;
	}
	void *p = new char[PROV_EXTENT_SIZE];
	AllocatedCount++;
	AllocatedBlocks.insert(p);
	return p;
}
static void
pta_free(void *p)
{
	check(AllocatedBlocks.find(p) != AllocatedBlocks.end(), "Bad free");
	AllocatedBlocks.erase(p);
	delete [] static_cast<char *>(p);
	AllocatedCount--;
}

void pt3_alloc_test()
{
	std::cout << "Testing pt3_alloc..." << std::endl;
	unsigned int maxCapacity =  PROV_EXTENT_SIZE / PROV_BLOCK_SIZE;
	maxCapacity *= PROV_EXTENT_SIZE / sizeof(void *);
	maxCapacity *= PROV_EXTENT_SIZE / sizeof(void *);
	std::cout << "pt3 capacity : " << maxCapacity << std::endl;

	pt3 pta;

	alloc_err_inj_enabled = false;
	for (unsigned int i = 0; i <= maxCapacity; i++) {
		pt3_construct(&pta, PROV_EXTENT_SIZE, PROV_BLOCK_SIZE, pta_alloc, pta_free);
		check(1u << pta.log2_capacity == maxCapacity, "Wrong capacity!");
		AllocatedItems.clear();
		for (unsigned int j = 0; j < i; j++) {
			unsigned int res = 0;
			void *data = pt3_alloc(&pta, &res);
			check(data, "Alloc returned NULL");
			void *test_data = pt3_get(&pta, res);
			check(data == test_data, "Alloc and Get mismatch");
			size_t provConsumedMemory = (size_t)pt3_extents_count(&pta) * PROV_EXTENT_SIZE;
			check(provConsumedMemory == AllocatedCount * PROV_EXTENT_SIZE, "ConsumedMemory counter failed (1)");
			check(res == j, "Index mismatch");
			{
				check(!AllocatedBlocks.empty(), "Alloc w/o alloc!");
				std::set<void*>::iterator itr = AllocatedBlocks.lower_bound(data);
				if (itr == AllocatedBlocks.end() || *itr != data) {
					check(itr != AllocatedBlocks.begin(), "Pointer to not allocatead region! (1)");
					--itr;
				}
				check (itr != AllocatedBlocks.end(), "Pointer to not allocatead region! (2)");
				check(data <= (void*)( ((char*)(*itr)) + PROV_EXTENT_SIZE - PROV_BLOCK_SIZE), "Pointer to not allocatead region! (3)");
			}
			{
				if (!AllocatedItems.empty()) {
					std::set<void*>::iterator itr = AllocatedItems.lower_bound(data);
					if (itr != AllocatedItems.end()) {
						check(*itr >= (void*)(((char*)data) + PROV_BLOCK_SIZE), "Data regions overlaps! (1)");
					}
					if (itr != AllocatedItems.begin()) {
						--itr;
						check(data >= (void*)(((char*)(*itr)) + PROV_BLOCK_SIZE), "Data regions overlaps! (2)");
					}
				}
			}
			AllocatedItems.insert(data);
		}
		size_t provConsumedMemory = (size_t)pt3_extents_count(&pta) * PROV_EXTENT_SIZE;
		check(provConsumedMemory == AllocatedCount * PROV_EXTENT_SIZE, "ConsumedMemory counter failed (2)");
		pt3_destroy(&pta);
		check(AllocatedCount == 0, "Not all memory freeed");
	}

	alloc_err_inj_enabled = true;
	for (unsigned int i = 0; i <= maxCapacity; i++) {
		pt3_construct(&pta, PROV_EXTENT_SIZE, PROV_BLOCK_SIZE, pta_alloc, pta_free);

		alloc_err_inj_countdown = i;

		for (unsigned int j = 0; j < maxCapacity; j++) {
			unsigned int res = 0;
			unsigned int prev_created = pta.created;
			void *data = pt3_alloc(&pta, &res);
			if (!data) {
				check(prev_created == pta.created, "Created count changed during memory fail!");
				break;
			}
		}
		pt3_destroy(&pta);
		check(AllocatedCount == 0, "Not all memory freeed after memory fail!");
	}

	std::cout << "Testing pt3_alloc successfully finished" << std::endl;
}

void pt2_alloc_test()
{
	std::cout << "Testing pt2_alloc..." << std::endl;
	unsigned int maxCapacity =  PROV_EXTENT_SIZE / PROV_BLOCK_SIZE;
	maxCapacity *= PROV_EXTENT_SIZE / sizeof(void *);
	std::cout << "pt2 capacity : " << maxCapacity << std::endl;

	pt2 pta;

	alloc_err_inj_enabled = false;
	for (unsigned int i = 0; i <= maxCapacity; i++) {
		pt2_construct(&pta, PROV_EXTENT_SIZE, PROV_BLOCK_SIZE, pta_alloc, pta_free);
		check(1u << pta.log2_capacity == maxCapacity, "Wrong capacity!");
		AllocatedItems.clear();
		for (unsigned int j = 0; j < i; j++) {
			unsigned int res = 0;
			void *data = pt2_alloc(&pta, &res);
			check(data, "Alloc returned NULL");
			void *test_data = pt2_get(&pta, res);
			check(data == test_data, "Alloc and Get mismatch");
			size_t provConsumedMemory = (size_t)pt2_extents_count(&pta) * PROV_EXTENT_SIZE;
			check(provConsumedMemory == AllocatedCount * PROV_EXTENT_SIZE, "ConsumedMemory counter failed (1)");
			check(res == j, "Index mismatch");
			{
				check(!AllocatedBlocks.empty(), "Alloc w/o alloc!");
				std::set<void*>::iterator itr = AllocatedBlocks.lower_bound(data);
				if (itr == AllocatedBlocks.end() || *itr != data) {
					check(itr != AllocatedBlocks.begin(), "Pointer to not allocatead region! (1)");
					--itr;
				}
				check (itr != AllocatedBlocks.end(), "Pointer to not allocatead region! (2)");
				check(data <= (void*)( ((char*)(*itr)) + PROV_EXTENT_SIZE - PROV_BLOCK_SIZE), "Pointer to not allocatead region! (3)");
			}
			{
				if (!AllocatedItems.empty()) {
					std::set<void*>::iterator itr = AllocatedItems.lower_bound(data);
					if (itr != AllocatedItems.end()) {
						check(*itr >= (void*)(((char*)data) + PROV_BLOCK_SIZE), "Data regions overlaps! (1)");
					}
					if (itr != AllocatedItems.begin()) {
						--itr;
						check(data >= (void*)(((char*)(*itr)) + PROV_BLOCK_SIZE), "Data regions overlaps! (2)");
					}
				}
			}
			AllocatedItems.insert(data);
		}
		size_t provConsumedMemory = (size_t)pt2_extents_count(&pta) * PROV_EXTENT_SIZE;
		check(provConsumedMemory == AllocatedCount * PROV_EXTENT_SIZE, "ConsumedMemory counter failed (2)");
		pt2_destroy(&pta);
		check(AllocatedCount == 0, "Not all memory freeed");
	}

	alloc_err_inj_enabled = true;
	for (unsigned int i = 0; i <= maxCapacity; i++) {
		pt2_construct(&pta, PROV_EXTENT_SIZE, PROV_BLOCK_SIZE, pta_alloc, pta_free);

		alloc_err_inj_countdown = i;

		for (unsigned int j = 0; j < maxCapacity; j++) {
			unsigned int res = 0;
			unsigned int prev_created = pta.created;
			void *data = pt2_alloc(&pta, &res);
			if (!data) {
				check(prev_created == pta.created, "Created count changed during memory fail!");
				break;
			}
		}
		pt2_destroy(&pta);
		check(AllocatedCount == 0, "Not all memory freeed after memory fail!");
	}

	std::cout << "Testing pt2_alloc successfully finished" << std::endl;
}

int
main(int, const char **)
{
	pt2_alloc_test();
	pt3_alloc_test();
}