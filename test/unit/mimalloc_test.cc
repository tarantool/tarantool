/**
 * Tests that the bundled mimalloc overrides C++ operator new/delete (see
 * src/box/mimalloc_new_delete.cc). The C allocator family (malloc/free/...)
 * is intentionally left on the system allocator.
 */
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include <mimalloc.h>
#include "trivia/util.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

static void
check_in_mimalloc_heap(const void *p, const char *name)
{
	fail_if(p == NULL);
	ok(mi_is_in_heap_region(p), "%s allocates by mimalloc", name);
}

static void
test_malloc(void)
{
	plan(1);
	header();

	void *p = xmalloc(64);
	ok(!mi_is_in_heap_region(p), "malloc doesn't allocate by mimalloc");
	free(p);

	footer();
	check_plan();
}

static void
test_new_delete(void)
{
	plan(6);
	header();

	void *p = ::operator new(64);
	check_in_mimalloc_heap(p, "operator new");
	::operator delete(p);

	p = ::operator new[](64);
	check_in_mimalloc_heap(p, "operator new[]");
	::operator delete[](p);

	p = ::operator new(64, std::nothrow);
	check_in_mimalloc_heap(p, "operator new(nothrow)");
	::operator delete(p, std::nothrow);

	p = ::operator new[](64, std::nothrow);
	check_in_mimalloc_heap(p, "operator new[](nothrow)");
	::operator delete[](p, std::nothrow);

	/* Sized delete. */
	p = ::operator new(64);
	check_in_mimalloc_heap(p, "operator new (for sized delete)");
	::operator delete(p, (size_t)64);

	/* Sized delete, array form. */
	p = ::operator new[](64);
	check_in_mimalloc_heap(p, "operator new[] (for sized delete)");
	::operator delete[](p, (size_t)64);

	footer();
	check_plan();
}

static void
test_new_delete_aligned(void)
{
	plan(6);
	header();

	auto align = static_cast<std::align_val_t>(128);
	void *p = ::operator new(256, align);
	fail_unless(((uintptr_t)p & 127) == 0);
	check_in_mimalloc_heap(p, "operator new(align_val_t)");
	::operator delete(p, align);

	p = ::operator new[](256, align);
	fail_unless(((uintptr_t)p & 127) == 0);
	check_in_mimalloc_heap(p, "operator new[](align_val_t)");
	::operator delete[](p, align);

	p = ::operator new(256, align, std::nothrow);
	check_in_mimalloc_heap(p, "operator new(align_val_t, nothrow)");
	::operator delete(p, align, std::nothrow);

	p = ::operator new[](256, align, std::nothrow);
	check_in_mimalloc_heap(p, "operator new[](align_val_t, nothrow)");
	::operator delete[](p, align, std::nothrow);

	/* Aligned, sized delete. */
	p = ::operator new(256, align);
	check_in_mimalloc_heap(p, "operator new (for sized delete)");
	::operator delete(p, (size_t)256, align);

	/* Aligned, sized delete, array form. */
	p = ::operator new[](256, align);
	check_in_mimalloc_heap(p, "operator new[] (for sized delete)");
	::operator delete[](p, (size_t)256, align);

	footer();
	check_plan();
}

/**
 * std::allocator<T>, and everything built on top of it, is specified to obtain
 * memory via ::operator new - no separate interception is needed for standard
 * containers or shared_ptr's make_shared control block.
 */
static void
test_stdlib_containers(void)
{
	plan(3);
	header();

	std::vector<uint64_t> v(64, 0x5a5a5a5a5a5a5a5aULL);
	check_in_mimalloc_heap(v.data(), "std::vector");

	std::string s(256, 'x');
	check_in_mimalloc_heap(v.data(), "std::string");

	auto sp = std::make_shared<std::array<char, 256>>();
	check_in_mimalloc_heap(sp.get(), "std::make_shared");

	footer();
	check_plan();
}

int
main(void)
{
	plan(4);

	test_malloc();
	test_new_delete();
	test_new_delete_aligned();
	test_stdlib_containers();

	return check_plan();
}
