/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct ibuf;

/**
 * Take the global ibuf, or allocate a new one if the stash is empty.
 */
struct ibuf *
cord_ibuf_take(void);

/**
 * Put the global ibuf back. It is not necessary - the buffer is put back on the
 * next yield. But then it can't be reused/freed until the yield. Put it back
 * manually when possible.
 */
void
cord_ibuf_put(struct ibuf *ibuf);

/**
 * Put the global ibuf back and free its memory. So only the buffer object
 * itself is saved to the stash. Main reason why it is a dedicated function is
 * because it is often needed from Lua, and allows not to call :recycle() there,
 * which would be an additional FFI call before cord_ibuf_put().
 *
 * Drop is not necessary though, see the put() comment.
 *
 * XXX: recycle of the global buffer is a workaround for the ibuf being used in
 * some places working with Lua API, where it wasn't wanted to "reuse" it
 * anyhow. Instead, the global buffer is used to protect from the buffer leak in
 * case it would be created locally, and then a Lua error would be raised. When
 * the buffer is global, it is not a problem, because it is reused/recycled
 * later. But it hurts the places, where re-usage could be good. Probably it is
 * worth to separate take/put() from new/drop() API. Or delete drop() entirely.
 */
void
cord_ibuf_drop(struct ibuf *ibuf);

#if defined(__cplusplus)
}
#endif /* defined(__cplusplus) */
