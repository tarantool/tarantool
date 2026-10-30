/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026, Tarantool AUTHORS, please see AUTHORS file.
 */

#define UNIT_TAP_COMPATIBLE 1

#include <stdint.h>
#include <dlfcn.h>

#include "backtrace.h"
#include "unit.h"

static void
test_macos_dladdr_no_symbol(void)
{
	header();
	plan(6);
	void *lib = dlopen(DLADDR_NO_SYMBOL_LIB, RTLD_NOW);
	ok(lib != NULL, "load fixture from %s", DLADDR_NO_SYMBOL_LIB);
	if (lib == NULL)
		goto out;
	void *(*get_address)(void) =
		(void *(*)(void))dlsym(lib, "no_symbol_address");
	ok(get_address != NULL, "find fixture accessor");
	if (get_address == NULL)
		goto out;
	void *ip = get_address();
	Dl_info info;
	is(dladdr(ip, &info), 1, "dladdr finds the image");
	is(info.dli_sname, NULL, "dladdr cannot find a symbol in the image");
	struct backtrace_frame frame = {.ip = ip};
	uintptr_t offset = 0;
	is(backtrace_frame_resolve(&frame, &offset), NULL,
	   "image without symbol resolves to an unknown frame");
	is(offset, 0, "unknown frame does not acquire a bogus offset");
out:
	if (lib != NULL)
		dlclose(lib);
	check_plan();
	footer();
}

int
main(void)
{
	plan(1);
	test_macos_dladdr_no_symbol();
	return check_plan();
}
