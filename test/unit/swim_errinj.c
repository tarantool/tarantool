/*
 * Copyright 2010-2019, Tarantool AUTHORS, please see AUTHORS file.
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
#include "swim_test_utils.h"
#include "errinj.h"

/**
 * Test result is a real returned value of main_f. Fiber_join can
 * not be used, because it expects if a returned value < 0 then
 * diag is not empty. But in unit tests it can be violated -
 * check_plan() does not set diag.
 */
static int test_result;

static void
swim_test_payload_refutation(void)
{
	swim_start_test(11);
	int size, cluster_size = 3;
	struct swim_cluster *cluster = swim_cluster_new(cluster_size);
	swim_cluster_set_ack_timeout(cluster, 1);
	for (int i = 0; i < cluster_size; ++i) {
		for (int j = i + 1; j < cluster_size; ++j)
			swim_cluster_interconnect(cluster, i, j);
	}
	const char *s0_old_payload = "s0 payload";
	int s0_old_payload_size = strlen(s0_old_payload) + 1;
	fail_if(swim_cluster_member_set_payload(cluster, 0, s0_old_payload,
						s0_old_payload_size) != 0);
	fail_if(swim_cluster_wait_payload_everywhere(cluster, 0, s0_old_payload,
						     s0_old_payload_size,
						     3) != 0);
	/*
	 * The test checks the following case. Assume there are 3
	 * nodes: S1, S2, S3. They all know each other. S1 sets
	 * new payload, S2 and S3 knows that. They all see that S1
	 * has version 1 and payload P1.
	 *
	 * Now S1 changes payload to P2. Its version becomes
	 * 2. During next entire round its round messages are
	 * lost, however ACKs work ok. Assume also, that
	 * anti-entropy does not work. For example, if the cluster
	 * is huge, and S1 does not fit into that section.
	 */
	const char *s0_new_payload = "s0 second payload";
	int s0_new_payload_size = strlen(s0_new_payload);
	fail_if(swim_cluster_member_set_payload(cluster, 0, s0_new_payload,
						s0_new_payload_size) != 0);
	struct errinj *errinj = &errinjs[ERRINJ_SWIM_FD_ONLY];
	errinj->bparam = true;
	swim_run_for(3);
	errinj->bparam = false;

	is(swim_cluster_member_incarnation(cluster, 1, 0).version, 2,
	   "S2 sees new version of S1");
	is(swim_cluster_member_incarnation(cluster, 2, 0).version, 2,
	   "S3 does the same");

	const char *tmp = swim_cluster_member_payload(cluster, 1, 0, &size);
	ok(size == s0_old_payload_size &&
	   memcmp(tmp, s0_old_payload, size) == 0,
	   "but S2 does not known the new payload");

	tmp = swim_cluster_member_payload(cluster, 2, 0, &size);
	ok(size == s0_old_payload_size &&
	   memcmp(tmp, s0_old_payload, size) == 0,
	   "as well as S3");

	/* Restore normal ACK timeout. */
	swim_cluster_set_ack_timeout(cluster, 30);

	/*
	 * Now S1's payload TTD is 0, but via ACKs S1 sent its new
	 * version to S2 and S3. Despite that they should
	 * apply new S1's payload via anti-entropy. Next lines
	 * test that:
	 *
	 * 1) S2 can apply new S1's payload from S1's
	 *    anti-entropy;
	 *
	 * 2) S2 will not receive the old S1's payload from S3.
	 *    S3 knows, that its payload is outdated, and should
	 *    not send it;
	 *
	 * 2) S3 can apply new S1's payload from S2's
	 *    anti-entropy. Note, that here S3 applies the payload
	 *    not directly from the originator. It is the most
	 *    complex case.
	 *
	 * Next lines test the case (1).
	 */

	/* S3 does not participate in the test (1). */
	swim_cluster_set_drop(cluster, 2, 100);
	swim_run_for(3);

	tmp = swim_cluster_member_payload(cluster, 1, 0, &size);
	ok(size == s0_new_payload_size &&
	   memcmp(tmp, s0_new_payload, size) == 0,
	   "S2 learned S1's payload via anti-entropy");
	is(swim_cluster_member_incarnation(cluster, 1, 0).version, 2,
	   "version still is the same");

	tmp = swim_cluster_member_payload(cluster, 2, 0, &size);
	ok(size == s0_old_payload_size &&
	   memcmp(tmp, s0_old_payload, size) == 0,
	   "S3 was blocked and does not know anything");
	is(swim_cluster_member_incarnation(cluster, 2, 0).version, 2,
	   "version still is the same");

	/* S1 will not participate in the tests further. */
	swim_cluster_set_drop(cluster, 0, 100);

	/*
	 * Now check the case (2) - S3 will not send outdated
	 * version of S1's payload. To maintain the experimental
	 * integrity S1 and S2 are silent. Only S3 sends packets.
	 */
	swim_cluster_set_drop(cluster, 2, 0);
	swim_cluster_set_drop_out(cluster, 1, 100);
	swim_run_for(3);

	tmp = swim_cluster_member_payload(cluster, 1, 0, &size);
	ok(size == s0_new_payload_size &&
	   memcmp(tmp, s0_new_payload, size) == 0,
	   "S2 keeps the same new S1's payload, S3 did not rewrite it");

	tmp = swim_cluster_member_payload(cluster, 2, 0, &size);
	ok(size == s0_old_payload_size &&
	   memcmp(tmp, s0_old_payload, size) == 0,
	   "S3 still does not know anything");

	/*
	 * Now check the case (3) - S3 accepts new S1's payload
	 * from S2. Even knowing the same S1's version.
	 */
	swim_cluster_set_drop(cluster, 1, 0);
	swim_cluster_set_drop_out(cluster, 2, 100);
	is(swim_cluster_wait_payload_everywhere(cluster, 0, s0_new_payload,
						s0_new_payload_size, 3), 0,
	  "S3 learns S1's payload from S2")

	swim_cluster_delete(cluster);
	swim_finish_test();
}


static int
main_f(va_list ap)
{
	swim_start_test(1);

	(void) ap;
	swim_test_ev_init();
	swim_test_transport_init();

	swim_test_payload_refutation();

	swim_test_transport_free();
	swim_test_ev_free();

	test_result = check_plan();
	footer();
	return 0;
}

int
main()
{
	swim_run_test("swim_errinj.txt", main_f);
	return test_result;
}
