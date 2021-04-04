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
#include "trigger.h"
#include <math.h>

/**
 * Test result is a real returned value of main_f. Fiber_join can
 * not be used, because it expects if a returned value < 0 then
 * diag is not empty. But in unit tests it can be violated -
 * check_plan() does not set diag.
 */
static int test_result;

static void
swim_test_one_link(void)
{
	swim_start_test(6);
	/*
	 * Run a simple cluster of two elements. One of them
	 * learns about another explicitly. Another should add the
	 * former into his table of members.
	 */
	struct swim_cluster *cluster = swim_cluster_new(2);
	fail_if(swim_cluster_add_link(cluster, 0, 1) != 0);
	is(swim_cluster_wait_fullmesh(cluster, 0.9), -1,
	   "no rounds - no fullmesh");
	is(swim_cluster_wait_fullmesh(cluster, 0.1), 0, "one link");

	is(swim_cluster_member_status(cluster, 0, 0), MEMBER_ALIVE,
	   "self 0 is alive");
	is(swim_cluster_member_status(cluster, 1, 1), MEMBER_ALIVE,
	   "self 1 is alive");
	is(swim_cluster_member_status(cluster, 0, 1), MEMBER_ALIVE,
	   "0 sees 1 as alive");
	is(swim_cluster_member_status(cluster, 1, 0), MEMBER_ALIVE,
	   "1 sees 0 as alive");
	swim_cluster_delete(cluster);

	swim_finish_test();
}

static void
swim_test_sequence(void)
{
	swim_start_test(1);
	/*
	 * Run a simple cluster of several elements. Build a
	 * 'forward list' from them. It should turn into fullmesh
	 * in O(N) time. Time is not fixed because of randomness,
	 * so here just in case 2N is used - it should be enough.
	 */
	struct swim_cluster *cluster = swim_cluster_new(5);
	for (int i = 0; i < 4; ++i)
		swim_cluster_add_link(cluster, i, i + 1);
	is(swim_cluster_wait_fullmesh(cluster, 10), 0, "sequence");
	swim_cluster_delete(cluster);

	swim_finish_test();
}

static void
swim_test_uuid_update(void)
{
	swim_start_test(7);

	struct swim_cluster *cluster = swim_cluster_new(2);
	swim_cluster_add_link(cluster, 0, 1);
	fail_if(swim_cluster_wait_fullmesh(cluster, 1) != 0);
	struct swim *s = swim_cluster_member(cluster, 0);
	struct tt_uuid old_uuid = *swim_member_uuid(swim_self(s));
	struct tt_uuid new_uuid = uuid_nil;
	new_uuid.time_low = 1000;
	is(swim_cluster_update_uuid(cluster, 0, &new_uuid), 0, "UUID update");
	is(swim_member_status(swim_member_by_uuid(s, &old_uuid)), MEMBER_LEFT,
	   "old UUID is marked as 'left'");
	swim_run_for(5);
	is(swim_member_by_uuid(s, &old_uuid), NULL,
	   "old UUID is dropped after a while");
	ok(swim_cluster_is_fullmesh(cluster), "dropped everywhere");
	is(swim_size(s), 2, "two members in each");
	new_uuid.time_low = 2;
	is(swim_cluster_update_uuid(cluster, 0, &new_uuid), -1,
	   "can not update to an existing UUID - swim_cfg fails");
	ok(swim_error_check_match("exists"), "diag says 'exists'");
	swim_cluster_delete(cluster);

	swim_finish_test();
}

static void
swim_test_cfg(void)
{
	swim_start_test(16);

	struct swim *s = swim_new(0);
	assert(s != NULL);
	is(swim_cfg(s, NULL, -1, -1, -1, NULL), -1, "first cfg failed - no URI");
	ok(swim_error_check_match("mandatory"), "diag says 'mandatory'");
	const char *uri = "127.0.0.1:1";
	is(swim_cfg(s, uri, -1, -1, -1, NULL), -1, "first cfg failed - no UUID");
	ok(swim_error_check_match("mandatory"), "diag says 'mandatory'");
	struct tt_uuid uuid = uuid_nil;
	uuid.time_low = 1;
	is(swim_cfg(s, uri, -1, -1, -1, &uuid), 0, "configured first time");
	is(swim_cfg(s, NULL, -1, -1, -1, NULL), 0, "second time can omit URI, UUID");
	is(swim_cfg(s, NULL, 2, 2, -1, NULL), 0, "hearbeat is dynamic");
	const char *self_uri = swim_member_uri(swim_self(s));
	is(strcmp(self_uri, uri), 0, "URI is unchanged after recfg with NULL "\
	   "URI");

	struct swim *s2 = swim_new(0);
	assert(s2 != NULL);
	const char *bad_uri1 = "127.1.1.1.1.1.1:1";
	const char *bad_uri2 = "google.com:1";
	const char *bad_uri3 = "unix/:/home/gerold103/any/dir";
	struct tt_uuid uuid2 = uuid_nil;
	uuid2.time_low = 2;
	is(swim_cfg(s2, bad_uri1, -1, -1, -1, &uuid2), -1,
	   "can not use invalid URI");
	ok(swim_error_check_match("invalid uri"), "diag says 'invalid uri'");
	is(swim_cfg(s2, bad_uri2, -1, -1, -1, &uuid2), -1,
	   "can not use domain names");
	ok(swim_error_check_match("invalid uri"), "diag says 'invalid uri'");
	is(swim_cfg(s2, bad_uri3, -1, -1, -1, &uuid2), -1,
		    "UNIX sockets are not supported");
	ok(swim_error_check_match("only IP"), "diag says 'only IP'");
	is(swim_cfg(s2, uri, -1, -1, -1, &uuid2), -1,
		    "can not bind to an occupied port");
	ok(swim_error_check_match("bind"), "diag says 'bind'");
	swim_delete(s2);
	swim_delete(s);

	swim_finish_test();
}

static void
swim_test_add_remove(void)
{
	swim_start_test(14);

	struct swim_cluster *cluster = swim_cluster_new(2);
	swim_cluster_add_link(cluster, 0, 1);
	fail_if(swim_cluster_wait_fullmesh(cluster, 1) != 0);
	struct swim *s1 = swim_cluster_member(cluster, 0);
	struct swim *s2 = swim_cluster_member(cluster, 1);
	const struct swim_member *s2_self = swim_self(s2);

	is(swim_add_member(s1, swim_member_uri(s2_self),
			   swim_member_uuid(s2_self)), -1,
	   "can not add an existing member");
	ok(swim_error_check_match("already exists"),
	   "diag says 'already exists'");

	const char *bad_uri = "127.0.0101010101";
	struct tt_uuid uuid = uuid_nil;
	uuid.time_low = 1000;
	is(swim_add_member(s1, bad_uri, &uuid), -1,
	   "can not add a invalid uri");
	ok(swim_error_check_match("invalid uri"), "diag says 'invalid uri'");

	is(swim_remove_member(s2, swim_member_uuid(s2_self)), -1,
	   "can not remove self");
	ok(swim_error_check_match("can not remove self"),
	   "diag says the same");

	isnt(swim_member_by_uuid(s1, swim_member_uuid(s2_self)), NULL,
	     "find by UUID works");
	is(swim_remove_member(s1, swim_member_uuid(s2_self)), 0,
	   "now remove one element");
	is(swim_member_by_uuid(s1, swim_member_uuid(s2_self)), NULL,
	   "and it can not be found anymore");

	is(swim_remove_member(s1, &uuid), 0, "remove of a not existing member");

	is(swim_cluster_is_fullmesh(cluster), false,
	   "after removal the cluster is not in fullmesh");
	is(swim_cluster_wait_fullmesh(cluster, 1), 0,
	   "but it is back in 1 step");

	/*
	 * On each step s1 sends itself to s2. However s2 can be
	 * removed from s1 after the message is scheduled but
	 * before its completion.
	 */
	swim_cluster_block_io(cluster, 0);
	swim_run_for(1);
	/*
	 * Now the message from s1 is in 'fly', round step is not
	 * finished.
	 */
	swim_remove_member(s1, swim_member_uuid(s2_self));
	swim_cluster_unblock_io(cluster, 0);
	is(swim_cluster_wait_fullmesh(cluster, 1), 0,
	   "back in fullmesh after a member removal in the middle of a step");
	/*
	 * Check that member removal does not delete a member,
	 * only unrefs.
	 */
	const struct tt_uuid *s1_uuid = swim_member_uuid(swim_self(s1));
	struct swim_member *s1_view = swim_member_by_uuid(s2, s1_uuid);
	swim_member_ref(s1_view);
	swim_remove_member(s2, s1_uuid);
	ok(swim_member_is_dropped(s1_view), "if a referenced "\
	   "member is dropped, it can be detected from the public API");
	swim_member_unref(s1_view);

	swim_cluster_delete(cluster);

	swim_finish_test();
}

static void
swim_test_basic_failure_detection(void)
{
	swim_start_test(9);
	struct swim_cluster *cluster = swim_cluster_new(2);
	swim_cluster_set_ack_timeout(cluster, 0.5);

	swim_cluster_add_link(cluster, 0, 1);
	is(swim_cluster_member_status(cluster, 0, 1), MEMBER_ALIVE,
	   "node is added as alive");
	swim_cluster_block_io(cluster, 1);
	/* Roll one round to send a first ping. */
	swim_run_for(1);

	is(swim_cluster_wait_status(cluster, 0, 1, MEMBER_SUSPECTED, 0.9), -1,
	   "member still is not suspected after 1 noack");
	is(swim_cluster_wait_status(cluster, 0, 1, MEMBER_SUSPECTED, 0.1), 0,
	   "but it is suspected after one more");
	is(swim_cluster_wait_status(cluster, 0, 1, MEMBER_DEAD, 1.4), -1,
	   "it is not dead after 2 more noacks");
	is(swim_cluster_wait_status(cluster, 0, 1, MEMBER_DEAD, 0.1), 0,
	   "but it is dead after one more");

	swim_run_for(1);
	is(swim_cluster_member_status(cluster, 0, 1), MEMBER_DEAD, "after 2 "\
	   "more unacks the member still is not deleted - dissemination TTD "\
	   "keeps it");
	is(swim_cluster_wait_status(cluster, 0, 1, swim_member_status_MAX, 2),
	   0, "but it is dropped after 2 rounds when TTD gets 0");

	/*
	 * After IO unblock pending messages will be processed all
	 * at once. S2 will learn about S1. After one more round
	 * step it should be fullmesh.
	 */
	swim_cluster_unblock_io(cluster, 1);
	is(swim_cluster_wait_fullmesh(cluster, 1), 0, "fullmesh is restored");

	/* A member can be removed during an ACK wait. */
	swim_cluster_block_io(cluster, 1);
	/* Next round after 1 sec + let ping hang for 0.25 sec. */
	swim_run_for(1.25);
	struct swim *s1 = swim_cluster_member(cluster, 0);
	struct swim *s2 = swim_cluster_member(cluster, 1);
	const struct swim_member *s2_self = swim_self(s2);
	swim_remove_member(s1, swim_member_uuid(s2_self));
	swim_cluster_unblock_io(cluster, 1);
	swim_run_for(0.1);
	is(swim_cluster_member_status(cluster, 0, 1), MEMBER_ALIVE,
	   "a member is added back on an ACK");

	swim_cluster_delete(cluster);
	swim_finish_test();
}

static void
swim_test_basic_gossip(void)
{
	swim_start_test(4);
	struct swim_cluster *cluster = swim_cluster_new(3);
	swim_cluster_set_ack_timeout(cluster, 10);
	/*
	 * Test basic gossip. S1 and S2 know each other. Then S2
	 * starts losing packets. S1 does not receive 2 ACKs from
	 * S2. Then S3 joins the cluster and explicitly learns
	 * about S1 and S2. After one more unack S1 declares S2 as
	 * dead, and via anti-entropy S3 learns the same. Even
	 * earlier than it could discover the same via its own
	 * pings to S2.
	 */
	swim_cluster_add_link(cluster, 0, 1);
	swim_cluster_add_link(cluster, 1, 0);
	swim_cluster_set_drop(cluster, 1, 100);
	/*
	 * Wait one no-ACK on S1 from S2. +1 sec to send a first
	 * ping.
	 */
	swim_run_for(10 + 1);
	swim_cluster_add_link(cluster, 0, 2);
	swim_cluster_add_link(cluster, 2, 1);
	/*
	 * After 10 seconds (one ack timeout) S1 should see S2 as
	 * suspected. But S3 still should see S2 as alive. To
	 * prevent S1 from informing S3 about that the S3 IO is
	 * blocked for a short time.
	 */
	swim_run_for(9);
	is(swim_cluster_member_status(cluster, 0, 1), MEMBER_ALIVE,
	   "S1 still thinks that S2 is alive");
	swim_cluster_block_io(cluster, 2);
	swim_run_for(1);
	is(swim_cluster_member_status(cluster, 0, 1), MEMBER_SUSPECTED,
	   "but one more second, and a second ack timed out - S1 sees S2 as "\
	   "suspected");
	is(swim_cluster_member_status(cluster, 2, 1), MEMBER_ALIVE,
	   "S3 still thinks that S2 is alive");
	swim_cluster_unblock_io(cluster, 2);
	/*
	 * At most after two round steps S1 sends
	 * 'S2 is suspected' to S3.
	 */
	is(swim_cluster_wait_status(cluster, 2, 1, MEMBER_SUSPECTED, 2), 0,
	   "S3 learns about suspected S2 from S1");

	swim_cluster_delete(cluster);
	swim_finish_test();
}

static void
swim_test_probe(void)
{
	swim_start_test(3);
	struct swim_cluster *cluster = swim_cluster_new(2);

	struct swim *s1 = swim_cluster_member(cluster, 0);
	struct swim *s2 = swim_cluster_member(cluster, 1);
	const char *s2_uri = swim_member_uri(swim_self(s2));
	is(swim_probe_member(s1, NULL), -1, "probe validates URI");
	is(swim_probe_member(s1, s2_uri), 0, "send probe");
	is(swim_cluster_wait_fullmesh(cluster, 0.1), 0,
	   "receive ACK on probe and get fullmesh")

	swim_cluster_delete(cluster);
	swim_finish_test();
}

static void
swim_test_refute(void)
{
	swim_start_test(6);
	struct swim_cluster *cluster = swim_cluster_new(2);
	swim_cluster_set_ack_timeout(cluster, 2);

	swim_cluster_add_link(cluster, 0, 1);
	swim_cluster_set_drop(cluster, 1, 100);
	/* Roll one round to send a first ping. */
	swim_run_for(1);

	fail_if(swim_cluster_wait_status(cluster, 0, 1,
					 MEMBER_SUSPECTED, 4) != 0);
	swim_cluster_set_drop(cluster, 1, 0);
	is(swim_cluster_wait_incarnation(cluster, 1, 1, 0, 1, 1), 0,
	   "S2 increments its own incarnation to refute its suspicion");
	is(swim_cluster_wait_incarnation(cluster, 0, 1, 0, 1, 1), 0,
	   "new incarnation has reached S1 with a next round message");

	swim_cluster_restart_node(cluster, 1);
	struct swim_incarnation inc =
		swim_cluster_member_incarnation(cluster, 1, 1);
	is(inc.version, 0, "after restart S2's version is 0 again");
	is(inc.generation, 1, "but generation is new");

	is(swim_cluster_wait_incarnation(cluster, 0, 1, 1, 0, 1), 0,
	   "S2 disseminates new incarnation, S1 learns it");
	is(swim_cluster_member_status(cluster, 0, 1), MEMBER_ALIVE,
	   "and considers S2 alive");

	swim_cluster_delete(cluster);
	swim_finish_test();
}

static void
swim_test_too_big_packet(void)
{
	swim_start_test(3);
	int size = 50;
	double ack_timeout = 1;
	double first_dead_timeout = 30;
	double everywhere_dead_timeout = size;
	int drop_id = size / 2;

	struct swim_cluster *cluster = swim_cluster_new(size);
	for (int i = 1; i < size; ++i)
		swim_cluster_add_link(cluster, 0, i);

	is(swim_cluster_wait_fullmesh(cluster, size * 3), 0, "despite S1 can "\
	   "not send all the %d members in a one packet, fullmesh is "\
	   "eventually reached", size);

	swim_cluster_set_ack_timeout(cluster, ack_timeout);
	swim_cluster_set_drop(cluster, drop_id, 100);
	is(swim_cluster_wait_status_anywhere(cluster, drop_id, MEMBER_DEAD,
					     first_dead_timeout), 0,
	   "a dead member is detected in time not depending on cluster size");
	/*
	 * GC is off to simplify and speed up checks. When no GC
	 * the test is sure that it is safe to check for
	 * MEMBER_DEAD everywhere, because it is impossible that a
	 * member is considered dead in one place, but already
	 * deleted on another. Also, total member deletion takes
	 * linear time, because a member is deleted from an
	 * instance only when *that* instance will not receive
	 * some direct acks from the member. Deletion and
	 * additional pings are not triggered if a member dead
	 * status is received indirectly via dissemination or
	 * anti-entropy. Otherwise it could produce linear network
	 * load on the already weak member.
	 */
	swim_cluster_set_gc(cluster, SWIM_GC_OFF);
	is(swim_cluster_wait_status_everywhere(cluster, drop_id, MEMBER_DEAD,
					       everywhere_dead_timeout), 0,
	   "S%d death is eventually learned by everyone", drop_id + 1);

	swim_cluster_delete(cluster);
	swim_finish_test();
}

static void
swim_test_packet_loss(void)
{
	double network_drop_rate[] = {5, 10, 20, 50, 90};
	swim_start_test(lengthof(network_drop_rate));
	int size = 20;
	int drop_id = 0;
	double ack_timeout = 1;

	for (int i = 0; i < (int) lengthof(network_drop_rate); ++i) {
		double rate = network_drop_rate[i];
		struct swim_cluster *cluster = swim_cluster_new(size);
		for (int j = 0; j < size; ++j) {
			swim_cluster_set_drop(cluster, j, rate);
			for (int k = 0; k < size; ++k)
				swim_cluster_add_link(cluster, j, k);
		}
		swim_cluster_set_ack_timeout(cluster, ack_timeout);
		swim_cluster_set_drop(cluster, drop_id, 100);
		swim_cluster_set_gc(cluster, SWIM_GC_OFF);
		double timeout = size * 100.0 / (100 - rate);
		is(swim_cluster_wait_status_everywhere(cluster, drop_id,
						       MEMBER_DEAD, 1000), 0,
		   "drop rate = %.2f, but the failure is disseminated", rate);
		swim_cluster_delete(cluster);
	}
	swim_finish_test();
}

static void
swim_test_undead(void)
{
	swim_start_test(2);
	struct swim_cluster *cluster = swim_cluster_new(2);
	swim_cluster_set_gc(cluster, SWIM_GC_OFF);
	swim_cluster_set_ack_timeout(cluster, 1);
	swim_cluster_add_link(cluster, 0, 1);
	swim_cluster_add_link(cluster, 1, 0);
	swim_cluster_set_drop(cluster, 1, 100);
	/* Roll one round to send a first ping. */
	swim_run_for(1);
	is(swim_cluster_wait_status(cluster, 0, 1, MEMBER_DEAD, 5), 0,
	   "member S2 is dead");
	swim_run_for(5);
	is(swim_cluster_member_status(cluster, 0, 1), MEMBER_DEAD,
	   "but it is never deleted due to the cfg option");
	swim_cluster_delete(cluster);
	swim_finish_test();
}

static void
swim_test_quit(void)
{
	swim_start_test(10);
	int size = 3;
	struct swim_cluster *cluster = swim_cluster_new(size);
	for (int i = 0; i < size; ++i) {
		for (int j = 0; j < size; ++j)
			swim_cluster_add_link(cluster, i, j);
	}
	struct swim *s0 = swim_cluster_member(cluster, 0);
	struct swim_member *s0_self = swim_self(s0);
	swim_member_ref(s0_self);
	swim_cluster_quit_node(cluster, 0);
	is(swim_member_status(s0_self), MEMBER_LEFT,
	   "'self' is 'left' immediately after quit");
	swim_member_unref(s0_self);
	is(swim_cluster_wait_status_everywhere(cluster, 0, MEMBER_LEFT, 0),
	   0, "'quit' is sent to all the members without delays between "\
	   "dispatches")
	/*
	 * Return the instance back and check that it refutes the
	 * old LEFT status.
	 */
	swim_cluster_restart_node(cluster, 0);
	is(swim_cluster_wait_incarnation(cluster, 0, 0, 1, 0, 2), 0,
	   "quited member S1 has returned and refuted the old status");
	fail_if(swim_cluster_wait_fullmesh(cluster, 2) != 0);
	/*
	 * Not trivial test. A member can receive its own 'quit'
	 * message. It can be reproduced if a member has quited.
	 * Then another member took the spare UUID, and then
	 * received the 'quit' message with the same UUID. Of
	 * course, it should be refuted.
	 */
	s0 = swim_cluster_member(cluster, 0);
	struct tt_uuid s0_uuid = *swim_member_uuid(swim_self(s0));
	struct swim *s1 = swim_cluster_member(cluster, 1);
	swim_remove_member(s1, &s0_uuid);
	struct swim *s2 = swim_cluster_member(cluster, 2);
	swim_remove_member(s2, &s0_uuid);
	swim_cluster_quit_node(cluster, 0);

	/* Steal UUID of the quited node. */
	swim_cluster_block_io(cluster, 1);
	is(swim_cluster_update_uuid(cluster, 1, &s0_uuid), 0, "another "\
	   "member S2 has taken the quited UUID");

	/* Ensure that S1 is not added back to S3 on quit. */
	swim_run_for(1);
	is(swim_cluster_member_status(cluster, 2, 0), swim_member_status_MAX,
	   "S3 did not add S1 back when received its 'quit'");

	/*
	 * Now allow S2 to get the 'self-quit' message. Note,
	 * together with 'quit' it receives new generation, which
	 * belonged to S1 before. Of course, it is a bug, but in
	 * a user application - UUIDs are messed.
	 */
	swim_cluster_unblock_io(cluster, 1);
	is(swim_cluster_wait_incarnation(cluster, 1, 1, 1, 1, 0), 0,
	   "S2 finally got 'quit' message from S1, but with its 'own' UUID - "\
	   "refute it")
	swim_cluster_delete(cluster);

	/**
	 * Test that if a new member has arrived with LEFT status
	 * via dissemination or anti-entropy - it is not added.
	 * Even if GC is off.
	 */
	cluster = swim_cluster_new(3);
	swim_cluster_set_gc(cluster, SWIM_GC_OFF);
	swim_cluster_interconnect(cluster, 0, 2);
	swim_cluster_interconnect(cluster, 1, 2);

	swim_cluster_quit_node(cluster, 0);
	swim_run_for(2);
	is(swim_cluster_member_status(cluster, 2, 0), MEMBER_LEFT,
	   "S3 sees S1 as left");
	is(swim_cluster_member_status(cluster, 1, 0), swim_member_status_MAX,
	   "S2 does not see S1 at all");
	swim_run_for(2);
	is(swim_cluster_member_status(cluster, 2, 0), swim_member_status_MAX,
	   "after more time S1 is dropped from S3");
	is(swim_cluster_member_status(cluster, 1, 0), swim_member_status_MAX,
	   "and still is not added to S2 - left members can not be added");

	swim_cluster_delete(cluster);
	swim_finish_test();
}

static void
swim_test_uri_update(void)
{
	swim_start_test(2);
	/*
	 * The test checks how a member address is updated. There
	 * is a cluster of 3 members: S1, S2, S3, and links:
	 * S1 <-> S2, S3 -> S1, S3 -> S2. S1 updates its address.
	 * The new address is sent to S2 and is updated here. Then
	 * S3 wakes up and disseminates the old address of S1.
	 * Member S2 should ignore that old address. It is
	 * achievable only via new incarnation on each address
	 * update.
	 */
	struct swim_cluster *cluster = swim_cluster_new(3);
	swim_cluster_interconnect(cluster, 0, 1);
	/*
	 * S3 should not accept packets so as to keep old address
	 * of S1.
	 */
	swim_cluster_set_drop(cluster, 2, 100);
	swim_cluster_add_link(cluster, 2, 1);
	swim_cluster_add_link(cluster, 2, 0);

	struct swim *s0 = swim_cluster_member(cluster, 0);
	const struct swim_member *s0_self = swim_self(s0);
	const char *new_s0_uri = "127.0.0.5:1";
	fail_if(swim_cfg(s0, "127.0.0.5:1", -1, -1, -1, NULL) != 0);
	/*
	 * Since S1 knows about S2 only, one round step is enough.
	 */
	swim_run_for(1);
	struct swim *s1 = swim_cluster_member(cluster, 1);
	const struct swim_member *s0_view =
		swim_member_by_uuid(s1, swim_member_uuid(s0_self));
	is(strcmp(new_s0_uri, swim_member_uri(s0_view)), 0,
	   "S1 updated its URI and S2 sees that");
	/*
	 * S1 should not send the new address to S3 - drop its
	 * packets.
	 */
	swim_cluster_set_drop(cluster, 0, 100);
	/*
	 * S2 should not manage to send the new address to S3, but
	 * should accept S3 packets with the old address and
	 * ignore it.
	 */
	swim_cluster_set_drop_out(cluster, 1, 100);
	/*
	 * Main part of the test - S3 sends the old address to S2.
	 */
	swim_cluster_set_drop(cluster, 2, 0);
	swim_run_for(3);
	is(strcmp(new_s0_uri, swim_member_uri(s0_view)), 0,
	   "S2 still keeps new S1's URI, even received the old one from S3");

	swim_cluster_delete(cluster);
	swim_finish_test();
}
static void
swim_test_broadcast(void)
{
	swim_start_test(6);
	int size = 4;
	struct swim_cluster *cluster = swim_cluster_new(size);
	struct swim *s0 = swim_cluster_member(cluster, 0);
	struct swim *s1 = swim_cluster_member(cluster, 1);
	const char *s1_uri = swim_member_uri(swim_self(s1));
	struct uri u;
	fail_if(uri_parse(&u, s1_uri) != 0 || u.service == NULL);
	int port = atoi(u.service);
	is(swim_broadcast(s0, port), 0, "S1 chooses to broadcast with port %d",
	   port);
	is(swim_cluster_wait_status(cluster, 1, 0, MEMBER_ALIVE, 1), 0,
	   "S2 receives the broadcast from S1");
	swim_run_for(1);
	is(swim_cluster_member_status(cluster, 2, 0), swim_member_status_MAX,
	   "others don't");

	is(swim_broadcast(s0, 0), 0, "S1 broadcasts ping without port");
	is(swim_cluster_wait_status_everywhere(cluster, 0, MEMBER_ALIVE, 0), 0,
	   "now everyone sees S1");
	is(swim_cluster_wait_fullmesh(cluster, size), 0,
	   "fullmesh is reached, and no one link was added explicitly");

	swim_cluster_delete(cluster);
	swim_finish_test();
}

static void
swim_test_payload_basic(void)
{
	swim_start_test(11);
	int size, cluster_size = 3;
	struct swim_cluster *cluster = swim_cluster_new(cluster_size);
	for (int i = 0; i < cluster_size; ++i) {
		for (int j = i + 1; j < cluster_size; ++j)
			swim_cluster_interconnect(cluster, i, j);
	}
	ok(swim_cluster_member_payload(cluster, 0, 0, &size) == NULL &&
	   size == 0, "no payload by default");
	is(swim_cluster_member_set_payload(cluster, 0, NULL, 1300), -1,
	   "can not set too big payload");
	ok(swim_error_check_match("Payload should be <="), "diag says too big");

	const char *s0_payload = "S1 payload";
	int s0_payload_size = strlen(s0_payload) + 1;
	is(swim_cluster_member_set_payload(cluster, 0, s0_payload,
					   s0_payload_size), 0,
	   "payload is set");
	is(swim_cluster_member_incarnation(cluster, 0, 0).version, 1,
	   "version is incremented on each payload update");
	const char *tmp = swim_cluster_member_payload(cluster, 0, 0, &size);
	ok(size == s0_payload_size && memcmp(s0_payload, tmp, size) == 0,
	   "payload is successfully obtained back");

	is(swim_cluster_wait_payload_everywhere(cluster, 0, s0_payload,
						s0_payload_size, cluster_size),
	   0, "payload is disseminated");
	s0_payload = "S1 second version of payload";
	s0_payload_size = strlen(s0_payload) + 1;
	is(swim_cluster_member_set_payload(cluster, 0, s0_payload,
					   s0_payload_size), 0,
	   "payload is changed");
	is(swim_cluster_member_incarnation(cluster, 0, 0).version, 2,
	   "version is incremented on each payload update");
	is(swim_cluster_wait_payload_everywhere(cluster, 0, s0_payload,
						s0_payload_size, cluster_size),
	   0, "second payload is disseminated");
	/*
	 * Test that new incarnations help to rewrite the old
	 * payload from anti-entropy.
	 */
	swim_cluster_set_drop(cluster, 0, 100);
	s0_payload = "S1 third version of payload";
	s0_payload_size = strlen(s0_payload) + 1;
	fail_if(swim_cluster_member_set_payload(cluster, 0, s0_payload,
						s0_payload_size) != 0);
	/* Wait at least one round until payload TTD gets 0. */
	swim_run_for(3);
	swim_cluster_set_drop(cluster, 0, 0);
	is(swim_cluster_wait_payload_everywhere(cluster, 0, s0_payload,
						s0_payload_size, cluster_size),
	   0, "third payload is disseminated via anti-entropy");

	swim_cluster_delete(cluster);
	swim_finish_test();
}

static void
swim_test_encryption(void)
{
	swim_start_test(3);
	struct swim_cluster *cluster = swim_cluster_new(2);
	const char *key = "1234567812345678";
	swim_cluster_set_codec(cluster, CRYPTO_ALGO_AES128, CRYPTO_MODE_CBC,
			       key, CRYPTO_AES128_KEY_SIZE);
	swim_cluster_add_link(cluster, 0, 1);

	is(swim_cluster_wait_fullmesh(cluster, 2), 0,
	   "cluster works with encryption");
	swim_cluster_delete(cluster);
	/*
	 * Test that the instances can not interact with different
	 * encryption keys.
	 */
	cluster = swim_cluster_new(2);
	struct swim *s1 = swim_cluster_member(cluster, 0);
	int rc = swim_set_codec(s1, CRYPTO_ALGO_AES128, CRYPTO_MODE_CBC,
				key, CRYPTO_AES128_KEY_SIZE);
	fail_if(rc != 0);
	struct swim *s2 = swim_cluster_member(cluster, 1);
	key = "8765432187654321";
	rc = swim_set_codec(s2, CRYPTO_ALGO_AES128, CRYPTO_MODE_CBC,
			    key, CRYPTO_AES128_KEY_SIZE);
	fail_if(rc != 0);
	swim_cluster_add_link(cluster, 0, 1);
	swim_run_for(2);
	ok(! swim_cluster_is_fullmesh(cluster),
	   "different encryption keys - can't interact");

	rc = swim_set_codec(s1, CRYPTO_ALGO_NONE, CRYPTO_MODE_ECB, NULL, 0);
	fail_if(rc != 0);
	rc = swim_set_codec(s2, CRYPTO_ALGO_NONE, CRYPTO_MODE_ECB, NULL, 0);
	fail_if(rc != 0);
	is(swim_cluster_wait_fullmesh(cluster, 2), 0,
	   "cluster works after encryption has been disabled");

	swim_cluster_delete(cluster);

	swim_finish_test();
}

static void
swim_test_slow_net(void)
{
	swim_start_test(0);
	struct swim_cluster *cluster = swim_cluster_new(2);
	swim_cluster_interconnect(cluster, 0, 1);
	swim_cluster_block_io(cluster, 0);
	swim_cluster_block_io(cluster, 1);

	note("slow network leads to idle round steps, they should not produce "\
	     "a new message");
	swim_run_for(5);

	swim_cluster_delete(cluster);
	swim_finish_test();
}

struct trigger_ctx {
	int counter;
	bool is_deleted;
	bool need_sleep;
	struct fiber *f;
	struct swim_on_member_event_ctx ctx;
};

static int
swim_on_member_event_save(struct trigger *t, void *event)
{
	struct trigger_ctx *c = (struct trigger_ctx *) t->data;
	++c->counter;
	if (c->ctx.member != NULL)
		swim_member_unref(c->ctx.member);
	c->ctx = *((struct swim_on_member_event_ctx *) event);
	swim_member_ref(c->ctx.member);
	return 0;
}

static int
swim_on_member_event_yield(struct trigger *t, void *event)
{
	struct trigger_ctx *c = (struct trigger_ctx *) t->data;
	++c->counter;
	c->f = fiber();
	while (c->need_sleep)
		fiber_yield();
	return 0;
}

static void
swim_trigger_destroy_cb(struct trigger *t)
{
	((struct trigger_ctx *) t->data)->is_deleted = true;
}

static int
swim_cluster_delete_f(va_list ap)
{
	struct swim_cluster *c = (struct swim_cluster *)
		va_arg(ap, struct swim_cluster *);
	swim_cluster_delete(c);
	return 0;
}

static void
swim_test_triggers(void)
{
	swim_start_test(20);
	struct swim_cluster *cluster = swim_cluster_new(2);
	swim_cluster_set_ack_timeout(cluster, 1);
	struct trigger_ctx tctx, tctx2;
	memset(&tctx, 0, sizeof(tctx));
	memset(&tctx2, 0, sizeof(tctx2));
	struct trigger *t1 = (struct trigger *) malloc(sizeof(*t1));
	assert(t1 != NULL);
	trigger_create(t1, swim_on_member_event_save, (void *) &tctx,
		       swim_trigger_destroy_cb);

	/* Skip 'new self' events. */
	swim_cluster_run_triggers(cluster);

	struct swim *s1 = swim_cluster_member(cluster, 0);
	trigger_add(swim_trigger_list_on_member_event(s1), t1);
	swim_cluster_interconnect(cluster, 0, 1);
	swim_cluster_run_triggers(cluster);

	is(tctx.counter, 1, "trigger is fired");
	ok(! tctx.is_deleted, "is not deleted");
	is(tctx.ctx.member, swim_cluster_member_view(cluster, 0, 1),
	   "ctx.member is set");
	is(tctx.ctx.events, SWIM_EV_NEW, "ctx.events is set");

	swim_cluster_member_set_payload(cluster, 0, "123", 3);
	swim_cluster_run_triggers(cluster);
	is(tctx.counter, 2, "self payload is updated");
	is(tctx.ctx.member, swim_self(s1), "self is set as a member");
	is(tctx.ctx.events, SWIM_EV_NEW_PAYLOAD | SWIM_EV_NEW_VERSION,
	   "both version and payload events are presented");

	swim_cluster_set_drop(cluster, 1, 100);
	fail_if(swim_cluster_wait_status(cluster, 0, 1,
					 MEMBER_SUSPECTED, 3) != 0);
	swim_cluster_run_triggers(cluster);
	is(tctx.counter, 3, "suspicion fired a trigger");
	is(tctx.ctx.events, SWIM_EV_NEW_STATUS, "status suspected");

	fail_if(swim_cluster_wait_status(cluster, 0, 1, MEMBER_DEAD, 3) != 0);
	swim_cluster_run_triggers(cluster);
	is(tctx.counter, 4, "death fired a trigger");
	is(tctx.ctx.events, SWIM_EV_NEW_STATUS, "status dead");

	fail_if(swim_cluster_wait_status(cluster, 0, 1,
					 swim_member_status_MAX, 2) != 0);
	swim_cluster_run_triggers(cluster);
	is(tctx.counter, 5, "drop fired a trigger");
	is(tctx.ctx.events, SWIM_EV_DROP, "status dropped");
	is(swim_cluster_member_view(cluster, 0, 1), NULL,
	   "dropped member is not presented in the member table");
	isnt(tctx.ctx.member, NULL, "but is in the event context");
	/*
	 * There is a complication about yields. If a trigger
	 * yields, other triggers wait for its finish. And all
	 * the triggers should be ready to SWIM deletion in the
	 * middle of an event processing. SWIM object should not
	 * be deleted, until all the triggers are done.
	 */
	struct trigger *t2 = (struct trigger *) malloc(sizeof(*t2));
	assert(t2 != NULL);
	tctx2.need_sleep = true;
	trigger_create(t2, swim_on_member_event_yield, (void *) &tctx2, NULL);
	trigger_add(swim_trigger_list_on_member_event(s1), t2);
	swim_cluster_add_link(cluster, 0, 1);
	swim_cluster_run_triggers(cluster);
	is(tctx2.counter, 1, "yielding trigger is fired");
	is(tctx.counter, 5, "non-yielding still is not");

	struct fiber *async_delete_fiber =
		fiber_new("async delete", swim_cluster_delete_f);
	fiber_start(async_delete_fiber, cluster);
	ok(! tctx.is_deleted, "trigger is not deleted until all currently "\
	   "sleeping triggers are finished");
	tctx2.need_sleep = false;
	fiber_wakeup(tctx2.f);
	while (! tctx.is_deleted)
		fiber_sleep(0);
	note("now all the triggers are done and deleted");

	free(t2);
	if (tctx.ctx.member != NULL)
		swim_member_unref(tctx.ctx.member);

	/* Check that recfg fires version update trigger. */
	s1 = swim_new(0);
	struct tt_uuid uuid = uuid_nil;
	uuid.time_low = 1;
	fail_if(swim_cfg(s1, "127.0.0.1:1", -1, -1, -1, &uuid) != 0);

	memset(&tctx, 0, sizeof(tctx));
	trigger_add(swim_trigger_list_on_member_event(s1), t1);
	fail_if(swim_cfg(s1, "127.0.0.1:2", -1, -1, -1, NULL) != 0);
	while (tctx.ctx.events == 0)
		fiber_sleep(0);
	is(tctx.ctx.events, SWIM_EV_NEW_URI | SWIM_EV_NEW_VERSION,
	   "local URI update warns about version update");
	ok((tctx.ctx.events & SWIM_EV_NEW_INCARNATION) != 0,
	   "version is a part of incarnation, so the latter is updated too");
	swim_delete(s1);

	if (tctx.ctx.member != NULL)
		swim_member_unref(tctx.ctx.member);
	free(t1);

	swim_finish_test();
}

static void
swim_test_generation(void)
{
	swim_start_test(3);

	struct swim_cluster *cluster = swim_cluster_new(2);
	swim_cluster_interconnect(cluster, 0, 1);

	const char *p1 = "payload 1";
	int p1_size = strlen(p1);
	swim_cluster_member_set_payload(cluster, 0, p1, p1_size);
	is(swim_cluster_wait_payload_everywhere(cluster, 0, p1, p1_size, 1), 0,
	   "S1 disseminated its payload to S2");

	swim_cluster_restart_node(cluster, 0);
	const char *p2 = "payload 2";
	int p2_size = strlen(p2);
	swim_cluster_member_set_payload(cluster, 0, p2, p2_size);
	is(swim_cluster_wait_payload_everywhere(cluster, 0, p2, p2_size, 2), 0,
	   "S1 restarted and set another payload. Without generation it could "\
	   "lead to never disseminated new payload.");
	is(swim_cluster_member_incarnation(cluster, 1, 0).generation, 1,
	   "S2 sees new generation of S1");

	swim_cluster_delete(cluster);

	swim_finish_test();
}

static void
swim_test_dissemination_speed(void)
{
	swim_start_test(2);

	int size = 100;
	double ack_timeout = 0.1;
	struct swim_cluster *cluster = swim_cluster_new(size);
	swim_cluster_set_ack_timeout(cluster, ack_timeout);
	swim_cluster_set_gc(cluster, SWIM_GC_OFF);
	for (int i = 0; i < size; ++i) {
		for (int j = i + 1; j < size; ++j)
			swim_cluster_interconnect(cluster, i, j);
	}
	swim_cluster_set_drop(cluster, 0, 100);
	fail_if(swim_cluster_wait_status_anywhere(cluster, 0,
						  MEMBER_DEAD, size) != 0);
	/*
	 * Not a trivial problem - at start of a cluster there are
	 * so many events, that they occupy a UDP packet fully.
	 * All these events are 'a new member is added'. And
	 * because of that other much more important events
	 * starve. In this concrete test a new event 'member is
	 * dead' starves. To beat that problem SWIM says that
	 * events should be disseminated not longer than for a
	 * O(log) round steps. In such a case all the events are
	 * expired quite fast, and anti-entropy swiftly finishes
	 * the job. Usually this test works in log * 2, log * 3
	 * steps. Here it is log * 6 to avoid flakiness in some
	 * extra rare and slow random cases, but to still check
	 * for O(log) speed.
	 */
	is(swim_cluster_wait_status_everywhere(cluster, 0, MEMBER_DEAD,
					       log2(size) * 6), 0,
	   "dissemination work in log time even at the very start of a cluster");
	swim_cluster_set_drop(cluster, 0, 0);
	fail_if(swim_cluster_wait_status_everywhere(cluster, 0,
						    MEMBER_ALIVE, size) != 0);
	/*
	 * Another big-cluster case. Assume, that something
	 * happened and all the members generated an event. For
	 * example, changed their payload. It creates a storm of
	 * events, among which some important ones can be lost.
	 * Such as a failure detection. The only solution again -
	 * make the events as short living as possible in order to
	 * faster free space in a UDP packet for other events and
	 * for anti-entropy. The test below proves that even when
	 * there is an event storm, failure dissemination still
	 * works for O(log) time.
	 */
	swim_cluster_set_drop(cluster, 0, 100);
	fail_if(swim_cluster_wait_status_anywhere(cluster, 0,
						  MEMBER_DEAD, size) != 0);
	for (int i = 0; i < size; ++i)
		swim_cluster_member_set_payload(cluster, i, "", 0);
	is(swim_cluster_wait_status_everywhere(cluster, 0, MEMBER_DEAD,
					       log2(size) * 6), 0,
	   "dissemination can withstand an event storm");

	swim_cluster_delete(cluster);

	swim_finish_test();
}

static void
swim_test_suspect_new_members(void)
{
	swim_start_test(2);

	struct swim_cluster *cluster = swim_cluster_new(3);
	swim_cluster_set_ack_timeout(cluster, 1);
	swim_cluster_interconnect(cluster, 0, 1);
	swim_cluster_interconnect(cluster, 1, 2);

	swim_cluster_set_drop(cluster, 0, 100);
	swim_cluster_block_io(cluster, 2);
	is(swim_cluster_wait_status(cluster, 1, 0, swim_member_status_MAX, 15),
	   0, "S2 dropped S1 as dead");
	swim_cluster_unblock_io(cluster, 2);
	swim_run_for(1);
	is(swim_cluster_member_status(cluster, 2, 0), swim_member_status_MAX,
	   "S3 didn't add S1 from S2's messages, because S1 didn't answer "\
	   "on a ping");

	swim_cluster_delete(cluster);

	swim_finish_test();
}

static void
swim_test_member_by_uuid(void)
{
	swim_start_test(3);
	struct swim_cluster *cluster = swim_cluster_new(1);

	struct swim *s1 = swim_cluster_member(cluster, 0);
	const struct swim_member *s1_self = swim_self(s1);
	is(swim_member_by_uuid(s1, swim_member_uuid(s1_self)), s1_self,
	   "found by UUID")

	struct tt_uuid uuid = uuid_nil;
	uuid.time_low = 1000;
	is(swim_member_by_uuid(s1, &uuid), NULL, "not found by valid UUID");
	is(swim_member_by_uuid(s1, NULL), NULL, "not found by NULL UUID");

	swim_cluster_delete(cluster);
	swim_finish_test();
}

static int
main_f(va_list ap)
{
	swim_start_test(23);

	(void) ap;
	fakeev_init();
	fakenet_init();

	swim_test_one_link();
	swim_test_sequence();
	swim_test_uuid_update();
	swim_test_cfg();
	swim_test_add_remove();
	swim_test_basic_failure_detection();
	swim_test_probe();
	swim_test_refute();
	swim_test_basic_gossip();
	swim_test_too_big_packet();
	swim_test_undead();
	swim_test_packet_loss();
	swim_test_quit();
	swim_test_uri_update();
	swim_test_broadcast();
	swim_test_payload_basic();
	swim_test_encryption();
	swim_test_slow_net();
	swim_test_triggers();
	swim_test_generation();
	swim_test_dissemination_speed();
	swim_test_suspect_new_members();
	swim_test_member_by_uuid();

	fakenet_free();
	fakeev_free();

	test_result = check_plan();
	footer();
	return 0;
}

int
main()
{
	swim_run_test("swim.txt", main_f);
	return test_result;
}