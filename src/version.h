#ifndef INCLUDES_TARANTOOL_VERSION_H
#define INCLUDES_TARANTOOL_VERSION_H
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

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * Pack version into uint32_t.
 * The highest byte or result means major version, next - minor,
 * middle - patch, last - revision.
 */
static inline uint32_t
version_id(unsigned major, unsigned minor, unsigned patch)
{
	return (((major << 8) | minor) << 8) | patch;
}

static inline unsigned
version_id_major(uint32_t version_id)
{
	return (version_id >> 16) & 0xff;
}

static inline unsigned
version_id_minor(uint32_t version_id)
{
	return (version_id >> 8) & 0xff;
}

static inline unsigned
version_id_patch(uint32_t version_id)
{
	return version_id & 0xff;
}

/**
 * Return Tarantool version as string
 */
const char *
tarantool_version(void);

/**
 * Get version (defined in PACKAGE_VERSION), packed into uint32_t
 * The highest byte or result means major version, next - minor,
 * middle - patch, last - revision.
 */
uint32_t
tarantool_version_id(void);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_VERSION_H */
