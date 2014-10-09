#ifndef TARANTOOL_IOVEC_H_INCLUDED
#define TARANTOOL_IOVEC_H_INCLUDED
/*
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

#include <stddef.h>
#include <sys/uio.h> /* struct iovec */

#include "trivia/util.h"
#include "small/region.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * \brief Calculate total length of \a iov
 * \param iov iovec
 * \param iovcnt size of \a iov
 * \return total length of \a iov
 */
static inline size_t
iovec_len(struct iovec *iov, int iovcnt)
{
	size_t len = 0;
	for (int i = 0; i < iovcnt; i++)
		len += iov[i].iov_len;
	return len;
}

/**
 * \brief Copy \a src into \a dst up to \a size bytes
 * \param dst iovec
 * \param src iovec
 * \param size size of \a iov
 */
static inline size_t
iovec_copy(struct iovec *dst, struct iovec *src, int iovcnt)
{
	size_t len = 0;
	for (int i = 0; i < iovcnt; i++) {
		len += src->iov_len;
		*(dst++) = *(src++);
	}
	return len;
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#if defined(__cplusplus)

/**
 * \brief Conventional function to join iovec into a solid memory chunk.
 * For iovec of size 1 returns iov->iov_base without allocation extra memory.
 * \param region region alloc
 * \param iov vector
 * \param iovcnt size of iovec
 * \param[out] size calculated length of \a iov
 * \return solid memory chunk
 */
static inline void *
iovec_join(struct region *region, struct iovec *iov, int iovcnt, size_t *plen)
{
	assert(iovcnt > 0 && plen != NULL);
	if (likely(iovcnt == 1)) {
		/* Fast path for single iovec or zero size */
		*plen = iov->iov_len;
		return iov->iov_base;
	}

	size_t len = iovec_len(iov, iovcnt);
	char *data = (char *) region_alloc(region, len);
	char *pos = data;
	for (int i = 0; i < iovcnt; i++)
		memcpy(pos, iov[i].iov_base, iov[i].iov_len);

	*plen = len;
	return data;
}

#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_IOVEC_H_INCLUDED */
