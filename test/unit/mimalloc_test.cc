/**
 * Tests that the bundled mimalloc overrides the standard allocator
 * entry points (see cmake/BuildMimalloc.cmake): every entry point
 * must return memory owned by mimalloc, so the whole process,
 * including shared libraries, allocates from the mimalloc heap.
 */
#include <malloc.h>
#include <mimalloc.h>
#include "trivia/util.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

static void
check_in_mimalloc_heap(void *p, size_t size, const char *name)
{
	fail_if(p == NULL);
	ok(mi_is_in_heap_region(p), "%s returns a mimalloc pointer", name);
}

static void
test_malloc_family(void)
{
	plan(5);
	header();

	void *p = xmalloc(64);
	check_in_mimalloc_heap(p, 64, "malloc");
	free(p);
	p = xcalloc(4, 16);
	check_in_mimalloc_heap(p, 64, "calloc");
	p = xrealloc(p, 128);
	check_in_mimalloc_heap(p, 128, "realloc");
	free(p);
	p = reallocarray(NULL, 4, 16);
	check_in_mimalloc_heap(p, 64, "reallocarray");
	free(p);
	p = __builtin_malloc(64);
	check_in_mimalloc_heap(p, 64, "compiler builtin malloc");
	free(p);

	footer();
	check_plan();
}

static void
test_aligned_alloc(void)
{
	plan(5);
	header();

	void *p = aligned_alloc(64, 128);
	fail_unless(((uintptr_t)p & 63) == 0);
	check_in_mimalloc_heap(p, 128, "aligned_alloc");
	free(p);

	p = memalign(64, 128);
	fail_unless(((uintptr_t)p & 63) == 0);
	check_in_mimalloc_heap(p, 128, "memalign");
	free(p);

	fail_unless(posix_memalign(&p, 64, 128) == 0);
	fail_unless(((uintptr_t)p & 63) == 0);
	check_in_mimalloc_heap(p, 128, "posix_memalign");
	free(p);

	p = valloc(64);
	fail_unless(((uintptr_t)p & 63) == 0);
	check_in_mimalloc_heap(p, 64, "valloc");
	free(p);

	p = pvalloc(64);
	fail_unless(((uintptr_t)p & 63) == 0);
	check_in_mimalloc_heap(p, 64, "pvalloc");
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
	check_in_mimalloc_heap(p, 64, "operator new");
	::operator delete(p);

	p = ::operator new[](64);
	check_in_mimalloc_heap(p, 64, "operator new[]");
	::operator delete[](p);

	p = ::operator new(64, std::nothrow);
	check_in_mimalloc_heap(p, 64, "operator new(nothrow)");
	::operator delete(p, std::nothrow);

	p = ::operator new[](64, std::nothrow);
	check_in_mimalloc_heap(p, 64, "operator new[](nothrow)");
	::operator delete[](p, std::nothrow);

	/* Sized delete. */
	p = ::operator new(64);
	check_in_mimalloc_heap(p, 64, "operator new (for sized delete)");
	::operator delete(p, (size_t)64);

	/* Sized delete, array form. */
	p = ::operator new[](64);
	check_in_mimalloc_heap(p, 64, "operator new[] (for sized delete)");
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
	check_in_mimalloc_heap(p, 256, "operator new(align_val_t)");
	::operator delete(p, align);

	p = ::operator new[](256, align);
	fail_unless(((uintptr_t)p & 127) == 0);
	check_in_mimalloc_heap(p, 256, "operator new[](align_val_t)");
	::operator delete[](p, align);

	p = ::operator new(256, align, std::nothrow);
	check_in_mimalloc_heap(p, 256, "operator new(align_val_t, nothrow)");
	::operator delete(p, align, std::nothrow);

	p = ::operator new[](256, align, std::nothrow);
	check_in_mimalloc_heap(p, 256, "operator new[](align_val_t, nothrow)");
	::operator delete[](p, align, std::nothrow);

	/* Aligned, sized delete. */
	p = ::operator new(256, align);
	check_in_mimalloc_heap(p, 256, "operator new (for sized delete)");
	::operator delete(p, (size_t)256, align);

	/* Aligned, sized delete, array form. */
	p = ::operator new[](256, align);
	check_in_mimalloc_heap(p, 256, "operator new[] (for sized delete)");
	::operator delete[](p, (size_t)256, align);

	footer();
	check_plan();
}

static void
test_misc(void)
{
	plan(2);
	header();

	void *p = xstrdup("mimalloc");
	fail_unless(strcmp((char *)p, "mimalloc") == 0);
	check_in_mimalloc_heap(p, strlen("mimalloc") + 1, "strdup");
	free(p);

	p = xstrndup("mimalloc-abcd", 8);
	fail_unless(strcmp((char *)p, "mimalloc") == 0);
	check_in_mimalloc_heap(p, strlen((char *)p) + 1, "strndup");
	free(p);

	footer();
	check_plan();
}

int
main(void)
{
	plan(5);

	test_malloc_family();
	test_aligned_alloc();
	test_new_delete();
	test_new_delete_aligned();
	test_misc();

	return check_plan();
}
