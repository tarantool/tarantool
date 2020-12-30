/*
 * Copyright 2010-2020, Tarantool AUTHORS, please see AUTHORS file.
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

/**
 * The file is a hack to force the linker keep the needed symbols
 * in the result tarantool executable file.
 *
 * Problem is that if a symbol is defined inside a static library,
 * but never used in the final executable, the linker may throw it
 * away. But many symbols are needed for Lua FFI and for the
 * public C API used by dynamic modules.
 *
 * This file creates a 'false usage' of needed symbols. It stores
 * pointers at them into a big array, and returns it like if the
 * caller will use them somehow. Call, write by their address, or
 * anything else.
 *
 * In reality the symbols are never touched after export from
 * here, but the compiler and the linker can't tell that.
 *
 * Below are some alternatives, which may replace the current
 * solution in future. Both are better in being able to declare a
 * symbol as exported right where it is defined. Or sometimes in
 * another place if necessary. For example, when the needed
 * symbol is defined somewhere in a third_party library.
 *
 * ** Solution 1 - user-defined ELF sections. **
 *
 * That way is similar to what is used by the Linux kernel. To
 * implement it there is a macros, lets call it EXPORT_SYMBOL. The
 * macros takes one parameter - symbol name. In its implementation
 * the macros defines a global struct keeping pointer at that
 * symbol, and stored in a special section. For example, .tntexp
 * section. Later when all is complied into the final executable,
 * there is a linker script, which takes all the symbols defined
 * in that section, and creates a reference at them, which is then
 * somehow used in the code.
 *
 * A pseudocode example of how can it look in theory:
 *
 * 	struct tnt_exported_symbol {
 * 		void *sym;
 * 	};
 *
 * 	#define EXPORT_SYMBOL(symbol) \
 * 		__attribute__((section(".tntexp")))
 * 		struct tnt_exported_symbol tnt_exported_##sym = { \
 * 			.sym = (void *) symbol \
 * 		};
 *
 * For more info see EXPORT_SYMBOL() macros in
 * include/linux/export.h file in the kernel sources.
 *
 * ** Solution 2 - precompile script which would find all exported
 *    functions and generate this file automatically. **
 *
 * Not much to explain. Introduce a macros EXPORT_SYMBOL, and walk
 * all the source code, looking for it. When see a symbol marked
 * so, remember it. Then generate the exports.c like it is defined
 * below. But automatically.
 */

#include "trivia/config.h"

/**
 * Symbol is just an address. No need to know its definition or
 * even type to get that address. Even an integer global variable
 * can be referenced as extern void(*)(void).
 */
#define EXPORT(symbol) extern void symbol(void);
#include "exports.h"
#undef EXPORT

void **
export_syms(void)
{
	/*
	 * Compiler should think the exported symbols are
	 * reachable. When they are returned as an array, the
	 * compiler can't assume anything, and can't remove them.
	 */
	#define EXPORT(symbol) ((void *)symbol),
	static void *symbols[] = {
		#include "exports.h"
	};
	#undef EXPORT
	return symbols;
}
