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

#define MATRAS_VERSION_COUNT 8

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

	struct matras mat;

	alloc_err_inj_enabled = false;
	for (unsigned int i = 0; i <= maxCapacity; i++) {
		matras_create(&mat, PROV_EXTENT_SIZE, PROV_BLOCK_SIZE, pta_alloc, pta_free);
		check(1u << mat.log2_capacity == maxCapacity, "Wrong capacity!");
		AllocatedItems.clear();
		for (unsigned int j = 0; j < i; j++) {
			unsigned int res = 0;
			void *data = matras_alloc(&mat, &res);
			check(data, "Alloc returned NULL");
			void *test_data = matras_get(&mat, res);
			check(data == test_data, "Alloc and Get mismatch");
			size_t provConsumedMemory = (size_t)matras_extent_count(&mat) * PROV_EXTENT_SIZE;
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
		size_t provConsumedMemory = (size_t)matras_extent_count(&mat) * PROV_EXTENT_SIZE;
		check(provConsumedMemory == AllocatedCount * PROV_EXTENT_SIZE, "ConsumedMemory counter failed (2)");
		matras_destroy(&mat);
		check(AllocatedCount == 0, "Not all memory freed (1)");
	}

	for (unsigned int i = 0; i <= maxCapacity; i++) {
		matras_create(&mat, PROV_EXTENT_SIZE, PROV_BLOCK_SIZE, pta_alloc, pta_free);
		for (unsigned int j = 0; j < i; j++) {
			unsigned int res = 0;
			void *data = matras_alloc(&mat, &res);
		}
		for (unsigned int j = 0; j < i; j++) {
			matras_dealloc(&mat);
			size_t provConsumedMemory = (size_t)matras_extent_count(&mat) * PROV_EXTENT_SIZE;
			check(provConsumedMemory == AllocatedCount * PROV_EXTENT_SIZE, "ConsumedMemory counter failed (3)");
		}
		check(AllocatedCount == 0, "Not all memory freed (2)");
		matras_destroy(&mat);
	}

	alloc_err_inj_enabled = true;
	for (unsigned int i = 0; i <= maxCapacity; i++) {
		matras_create(&mat, PROV_EXTENT_SIZE, PROV_BLOCK_SIZE, pta_alloc, pta_free);

		alloc_err_inj_countdown = i;

		for (unsigned int j = 0; j < maxCapacity; j++) {
			unsigned int res = 0;
			unsigned int prev_block_count = mat.head.block_count;
			void *data = matras_alloc(&mat, &res);
			if (!data) {
				check(prev_block_count == mat.head.block_count, "Created count changed during memory fail!");
				break;
			}
		}
		matras_destroy(&mat);
		check(AllocatedCount == 0, "Not all memory freed after memory fail!");
	}

	std::cout << "Testing matras_alloc successfully finished" << std::endl;
}

typedef uint64_t type_t;
const size_t VER_EXTENT_SIZE = 512;
int extents_in_use = 0;

void *all()
{
	extents_in_use++;
	return malloc(VER_EXTENT_SIZE);
}

void dea(void *p)
{
	extents_in_use--;
	free(p);
}

struct matras_view views[MATRAS_VERSION_COUNT];
int vermask = 1;

int reg_view_id()
{
	int id = __builtin_ctz(~vermask);
	vermask |= 1 << id;
	return id;
}

void unreg_view_id(int id)
{
	vermask &=~ (1 << id);
}

void
matras_vers_test()
{
	std::cout << "Testing matras versions..." << std::endl;

	std::vector<type_t> comps[MATRAS_VERSION_COUNT];
	int use_mask = 1;
	int cur_num_or_ver = 1;
	struct matras local;
	matras_create(&local, VER_EXTENT_SIZE, sizeof(type_t), all, dea);
	type_t val = 0;
	for (int s = 10; s < 8000; s = int(s * 1.5)) {
		for (int k = 0; k < 800; k++) {
			bool check_me = false;
			if (rand() % 16 == 0) {
				bool add_ver;
				if (cur_num_or_ver == 1)
					add_ver = true;
				else if (cur_num_or_ver == MATRAS_VERSION_COUNT)
					add_ver = false;
				else
					add_ver = rand() % 2 == 0;
				if (add_ver) {
					cur_num_or_ver++;
					matras_id_t new_ver = reg_view_id();
					matras_create_read_view(&local, views + new_ver);
					check(new_ver > 0, "create read view failed");
					use_mask |= (1 << new_ver);
					comps[new_ver] = comps[0];
				} else {
					cur_num_or_ver--;
					int del_ver;
					do {
						del_ver = 1 + rand() % (MATRAS_VERSION_COUNT - 1);
					} while ((use_mask & (1 << del_ver)) == 0);
					matras_destroy_read_view(&local, views + del_ver);
					unreg_view_id(del_ver);
					comps[del_ver].clear();
					use_mask &= ~(1 << del_ver);
				}
				check_me = true;
			} else {
				check_me = rand() % 16 == 0;
				if (rand() % 8 == 0 && comps[0].size() > 0) {
					matras_dealloc(&local);
					comps[0].pop_back();
				}
				int p = rand() % s;
				int mod = 0;
				while (p >= comps[0].size()) {
					comps[0].push_back(val * 10000 + mod);
					matras_id_t tmp;
					type_t *ptrval = (type_t *)matras_alloc(&local, &tmp);
					*ptrval = val * 10000 + mod;
					mod++;
				}
				val++;
				comps[0][p] = val;
				matras_touch(&local, p);
				*(type_t *)matras_get(&local, p) = val;
			}
			views[0] = local.head;

			for (int i = 0; i < MATRAS_VERSION_COUNT; i++) {
				if ((use_mask & (1 << i)) == 0)
					continue;
				check(comps[i].size() == views[i].block_count, "size mismatch");
				for (int j = 0; j < comps[i].size(); j++) {
					type_t val1 = comps[i][j];
					type_t val2 = *(type_t *)matras_view_get(&local, views + i, j);
					check(val1 == val2, "data mismatch");
				}
			}
		}
	}
	matras_destroy(&local);
	check(extents_in_use == 0, "memory leak");

	std::cout << "Testing matras_version successfully finished" << std::endl;
}

int
main(int, const char **)
{
	matras_alloc_test();
	matras_vers_test();
}
