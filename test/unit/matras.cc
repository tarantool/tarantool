#include "small/matras.h"
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

void matras_alloc_test()
{
	std::cout << "Testing matras_alloc..." << std::endl;
	unsigned int maxCapacity =  PROV_EXTENT_SIZE / PROV_BLOCK_SIZE;
	maxCapacity *= PROV_EXTENT_SIZE / sizeof(void *);
	maxCapacity *= PROV_EXTENT_SIZE / sizeof(void *);
	std::cout << "matras capacity : " << maxCapacity << std::endl;

	struct matras pta;

	alloc_err_inj_enabled = false;
	for (unsigned int i = 0; i <= maxCapacity; i++) {
		matras_create(&pta, PROV_EXTENT_SIZE, PROV_BLOCK_SIZE, pta_alloc, pta_free);
		check(1u << pta.log2_capacity == maxCapacity, "Wrong capacity!");
		AllocatedItems.clear();
		for (unsigned int j = 0; j < i; j++) {
			unsigned int res = 0;
			void *data = matras_alloc(&pta, &res);
			check(data, "Alloc returned NULL");
			void *test_data = matras_get(&pta, res);
			check(data == test_data, "Alloc and Get mismatch");
			size_t provConsumedMemory = (size_t)matras_extents_count(&pta) * PROV_EXTENT_SIZE;
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
		size_t provConsumedMemory = (size_t)matras_extents_count(&pta) * PROV_EXTENT_SIZE;
		check(provConsumedMemory == AllocatedCount * PROV_EXTENT_SIZE, "ConsumedMemory counter failed (2)");
		matras_destroy(&pta);
		check(AllocatedCount == 0, "Not all memory freed (1)");
	}

	for (unsigned int i = 0; i <= maxCapacity; i++) {
		matras_create(&pta, PROV_EXTENT_SIZE, PROV_BLOCK_SIZE, pta_alloc, pta_free);
		for (unsigned int j = 0; j < i; j++) {
			unsigned int res = 0;
			void *data = matras_alloc(&pta, &res);
		}
		for (unsigned int j = 0; j < i; j++) {
			matras_dealloc(&pta);
			size_t provConsumedMemory = (size_t)matras_extents_count(&pta) * PROV_EXTENT_SIZE;
			check(provConsumedMemory == AllocatedCount * PROV_EXTENT_SIZE, "ConsumedMemory counter failed (3)");
		}
		check(AllocatedCount == 0, "Not all memory freed (2)");
		matras_destroy(&pta);
	}

	alloc_err_inj_enabled = true;
	for (unsigned int i = 0; i <= maxCapacity; i++) {
		matras_create(&pta, PROV_EXTENT_SIZE, PROV_BLOCK_SIZE, pta_alloc, pta_free);

		alloc_err_inj_countdown = i;

		for (unsigned int j = 0; j < maxCapacity; j++) {
			unsigned int res = 0;
			unsigned int prev_block_count = pta.block_count;
			void *data = matras_alloc(&pta, &res);
			if (!data) {
				check(prev_block_count == pta.block_count, "Created count changed during memory fail!");
				break;
			}
		}
		matras_destroy(&pta);
		check(AllocatedCount == 0, "Not all memory freed after memory fail!");
	}

	std::cout << "Testing matras_alloc successfully finished" << std::endl;
}


int
main(int, const char **)
{
	matras_alloc_test();
}
