/*
 * Copyright 2026, Tarantool AUTHORS, please see AUTHORS file.
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
#include <cstdlib>
#include <new>

/**
 * Not linked into the tarantool binary. Compiled standalone so that
 * `cmake/MimallocExports.cmake' can dump the Itanium ABI mangled names of
 * these declarations into `extra/exports_mimalloc'. Keep the signatures
 * in sync with the `mimalloc-new-delete.h' overrides they stand in for.
 */
void operator delete(void *) noexcept {}
void operator delete[](void *) noexcept {}
void operator delete(void *, const std::nothrow_t &) noexcept {}
void operator delete[](void *, const std::nothrow_t &) noexcept {}

void *operator new(std::size_t) { abort(); }
void *operator new[](std::size_t) { abort(); }
void *operator new(std::size_t, const std::nothrow_t &) noexcept { abort(); }
void *operator new[](std::size_t, const std::nothrow_t &) noexcept { abort(); }

void operator delete(void *, std::size_t) noexcept {}
void operator delete[](void *, std::size_t) noexcept {}

void operator delete(void *, std::align_val_t) noexcept {}
void operator delete[](void *, std::align_val_t) noexcept {}
void operator delete(void *, std::size_t, std::align_val_t) noexcept {}
void operator delete[](void *, std::size_t, std::align_val_t) noexcept {}
void operator delete(void *, std::align_val_t, const std::nothrow_t &) noexcept {}
void operator delete[](void *, std::align_val_t, const std::nothrow_t &) noexcept {}

void *operator new(std::size_t, std::align_val_t) { abort(); }
void *operator new[](std::size_t, std::align_val_t) { abort(); }
void *operator new(std::size_t, std::align_val_t, const std::nothrow_t &) noexcept { abort(); }
void *operator new[](std::size_t, std::align_val_t, const std::nothrow_t &) noexcept { abort(); }
