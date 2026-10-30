/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */

#include "cxx_abi.h"

#ifdef ENABLE_BACKTRACE
#include "say.h"

#include <cstddef>
#include <cstdlib>
#include <cxxabi.h>

#include <pthread.h>

/*
 * RAII wrapper around the demangle buffer used by `abi::__cxa_demangle`.
 */
struct DemangleBuf final {
public:
	DemangleBuf(DemangleBuf &other) = delete;

	DemangleBuf &operator=(DemangleBuf &other) = delete;

	static DemangleBuf &instance()
	{
		static thread_local DemangleBuf singleton;
		return singleton;
	}

	/*
	 * Dynamically allocated by `abi::__cxa_demangle` and used for
	 * demangling C++ function names.
	 */
	char *buf;
	/* Length of demangle buffer set by `abi::__cxa_demangle`. */
	std::size_t len;

private:
	DemangleBuf() noexcept : buf(nullptr), len(0) {}
	~DemangleBuf() { std::free(buf); }
};

const char *
cxx_abi_demangle(const char *mangled_name)
{
	int status;
	char *demangled_proc_name =
		abi::__cxa_demangle(mangled_name, DemangleBuf::instance().buf,
				    &DemangleBuf::instance().len, &status);
	if (status != 0 && status != -2) {
		say_error("abi::__cxa_demangle failed with "
			  "status: %d", status);
		demangled_proc_name = nullptr;
	}
	if (demangled_proc_name != nullptr) {
		DemangleBuf::instance().buf = demangled_proc_name;
		return demangled_proc_name;
	}

	size_t len = strlen(mangled_name) + 1;
	if (DemangleBuf::instance().len < len) {
		char *buf = (char *)xrealloc(DemangleBuf::instance().buf, len);
		DemangleBuf::instance().len = len;
		DemangleBuf::instance().buf = buf;
	}

	memcpy(DemangleBuf::instance().buf, mangled_name, len);
	return DemangleBuf::instance().buf;
}
#endif /* ENABLE_BACKTRACE */
