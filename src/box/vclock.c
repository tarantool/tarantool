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
#include "vclock.h"
#include "say.h"

int64_t
vclock_follow(struct vclock *vclock, uint32_t server_id, int64_t lsn)
{
	assert(lsn >= 0);
	assert(server_id < VCLOCK_MAX);
	int64_t prev_lsn = vclock->lsn[server_id];
	if (lsn <= prev_lsn) {
		/* Never confirm LSN out of order. */
		panic("LSN for %u is used twice or COMMIT order is broken: "
		      "confirmed: %lld, new: %lld",
		      (unsigned) server_id,
		      (long long) prev_lsn, (long long) lsn);
	}
	vclock->lsn[server_id] = lsn;
	return prev_lsn;
}

void
vclock_merge(struct vclock *to, const struct vclock *with)
{
	/* Botched logic:
	 * - imagine there is 5.snap and 1.xlog
	 * - 5.snap has 1: 5 vclock
	 * - 1.xlog has 1: 1 vclock
	 * We begin reading the xlog after snap,
	 * but we don't skip setlsn (we never skip setlsn).
	 * So we must not update server id 1 with lsn 1,
	 * hence the code below only updates target if it
	 * is less than the source.
	 */
	for (int i = 0; i < VCLOCK_MAX; i++)
		if (with->lsn[i] > to->lsn[i])
			to->lsn[i] = with->lsn[i];
}

static int
vclockset_node_compare(const struct vclock *a, const struct vclock *b)
{
	int res = vclock_compare(a, b);
	if (res == VCLOCK_ORDER_UNDEFINED)
		return 0;
	return res;
}

rb_gen(, vclockset_, vclockset_t, struct vclock, link, vclockset_node_compare);
