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

void
vclock_set(struct vclock *vclock, uint32_t server_id, int64_t lsn)
{
	assert(vclock_has(vclock, server_id));
	vclock->lsn[server_id] = lsn;
}

int64_t
vclock_cas(struct vclock *vclock, uint32_t server_id, int64_t lsn)
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

